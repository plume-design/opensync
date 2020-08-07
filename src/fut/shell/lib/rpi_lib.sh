#!/usr/bin/env bash

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


lib_dir=$(dirname "$(realpath "$BASH_SOURCE")")
FUT_TOPDIR="$(realpath "$lib_dir"/../..)"

source "$lib_dir/unit_lib.sh"

file_name="$(basename "$0")"

############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common OpenSync functions used globally
#
############################################ INFORMATION SECTION - STOP ################################################

# RPI server "iptables" defaults to "xtables-nft-multi"
iptables_cmd="iptables-legacy"
iptables_chain="FORWARD"

############################################ UTILITY SECTION - START ###################################################

dpkg_is_package_installed()
{
    package=$1
    dpkg -l | grep -w ${package} | egrep -q ^ii
    return $?
}

start_cloud_simulation()
{
    local fn_name="${file_name}:brv_setup_env"
    log "Check if haproxy package is installed"
    dpkg_is_package_installed "haproxy" ||
        raise "haproxy not installed" -l "$fn_name" -ds

    sudo mkdir -p /etc/haproxy/certs/

    log "Copy haproxy configuration file and certificates"
    sudo cp "${FUT_TOPDIR}/shell/tools/rpi/files"/haproxy.cfg /etc/haproxy/haproxy.cfg ||
        raise "Config file not present!" -l "$fn_name" -ds
    sudo cp "${FUT_TOPDIR}/shell/tools/rpi/files"/{fut_controller.pem,plume_ca_chain.pem} /etc/haproxy/certs/ ||
        raise "Certificates not present!" -l "$fn_name" -ds

    log "Restart haproxy service"
    sudo service haproxy restart ||
        raise "haproxy not started" -l "$fn_name" -ds
    log "haproxy service running"

    log "Starting Cloud listener - logging path /tmp/cloud_listener.log"
    "${FUT_TOPDIR}"/framework/tools/cloud_listener.py -v > /dev/null 2>&1 &
    log "Cloud listener started" && exit 0
}

stop_cloud_simulation()
{
    log "Stop haproxy service"
    sudo service haproxy stop
    sudo systemctl is-active --quiet haproxy && die "haproxy not stopped"
    log "haproxy service stopped"
    log "Stop Cloud listener"
    kill $(pgrep cloud_listener) > /dev/null 2>&1 &
    log "Cloud listener stopped"
}

############################################ UTILITY SECTION - STOP ####################################################


############################################ NETWORK SECTION - START ###################################################

network_connect_to_wpa2()
{
    interface=$1
    network_ssid=$2
    network_bssid=$3
    network_pass=$4
    network_key_mgmt=$5
    enable_dhcp=$6
    msg_prefix=${7:-"network_connect_to_wpa2 -"}
    network_bssid_upper=$(echo "$network_bssid" | tr a-z A-Z)

    if [[ "$EUID" -ne 0 ]]; then
        die "$msg_prefix Please run this function as root - sudo"
    fi

    if [[ -z "$network_ssid" ]]; then
        die "$msg_prefix Empty network_ssid"
    elif [[ -z "$network_pass" ]]; then
        die "$msg_prefix Empty network_pass"
    fi

    log "$msg_prefix Bringing $interface down"
    ifdown $interface

    if [ "$?" -ne 0 ]; then
        die "$msg_prefix Failed while bringing interface $interface down"
    fi

    sleep 3

    rm /etc/wpa_supplicant/wpa_supplicant.conf
    touch /etc/wpa_supplicant/wpa_supplicant.conf

    log "$msg_prefix Creating wpa_supplicant.conf file"

    echo 'ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' >> /etc/wpa_supplicant/wpa_supplicant.conf
    echo 'update_config=1' >> /etc/wpa_supplicant/wpa_supplicant.conf

    echo "
network={
    ssid=\"$network_ssid\"
    proto=RSN
    key_mgmt=$network_key_mgmt
    scan_ssid=1
    psk=\"$network_pass\"
    priority=1
    bssid=$network_bssid
}" >> /etc/wpa_supplicant/wpa_supplicant.conf

    log "$msg_prefix wpa_supplicant.conf file:"

    cat /etc/wpa_supplicant/wpa_supplicant.conf

    log "$msg_prefix Bringing $interface up"

    if [[ "$enable_dhcp" == "on" ]]; then
        log "Using DHCP"
        ifup $interface
    else
        log "Not using dhcp - killing dhclient after 10 seconds - workaround... "
        timeout 10 ifup $interface
        kill $(pidof dhclient)
    fi

    log "$msg_prefix Checking if connected to network - $network_ssid"

    ssid_connected=$(iwconfig $interface | grep "$network_ssid")

    if [ $? -ne 0 ]; then
        iwconfig && die "$msg_prefix Interface $interface not connected to $network_ssid"
    fi

    bssid_connected=$(iwconfig $interface | grep "$network_bssid_upper")

    if [[ $? -ne 0 ]]; then
        iwconfig && die "$msg_prefix Interface $interface not connected to $network_bssid_upper"
    fi

    pass "$msg_prefix Interface $interface is connected to the network $network_ssid"
}

############################################ NETWORK SECTION - STOP ####################################################

########################################## FW IMAGE SECTION - START ####################################################

um_create_fw_key_file()
{
    um_fw_path=$1
    um_fw_key=$2

    log "Creating fw key $um_fw_key at $um_fw_path"
    echo "$um_fw_key" > "$um_fw_path.key" &&
        log "Created $um_fw_path.key" ||
        die "Failed to create $um_fw_path.key"
}

um_encrypt_image()
{
    um_fw_unc_path=$1
    um_fw_key_path=$2
    um_fw_name=${um_fw_unc_path##*/}
    um_fw_pure_name=${um_fw_name//".img"/""}
    um_file_cd_path=${um_fw_unc_path//"$um_fw_name"/""}
    um_fw_enc_path="$um_file_cd_path/${um_fw_pure_name}.eim"

    [ -f "$um_fw_enc_path" ] && rm "$um_fw_enc_path"

    log "Encypring image $um_fw_path with key $um_fw_key_path"
    openssl enc -aes-256-cbc -pass pass:"$(cat "$um_fw_key_path")" -md sha256 -nosalt -in "$um_fw_unc_path" -out "$um_fw_enc_path" &&
        log "Image encrypted" ||
        die "Faield to encrypt image"
}

um_create_md5_file()
{
    um_file_path=$1
    um_fw_name=${um_file_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}

    log "Creating md5 sum file of file $um_file_path"
    cd "$um_file_cd_path" && md5sum "$um_fw_name" > "$um_fw_name.md5" &&
        log "md5 sum file created" ||
        die "Failed to create md5 sum file"
}

um_create_corrupt_md5_file()
{
    um_fw_path=$1
    um_fw_name=${um_fw_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}
    um_md5_name="$um_file_cd_path/${um_fw_name}.md5"
    um_hash_only="$(cd "$um_file_cd_path" && md5sum "$um_fw_name" | cut -d' ' -f1)"

    log "Creating $um_fw_path.md5"
    echo "${um_hash_only:16:16}${um_hash_only:0:16}  ${um_fw_name}" > "$um_md5_name" &&
        log "Created $um_md5_name" ||
        die "Failed to create $um_md5_name"
}

um_create_corrupt_image()
{
    um_fw_path=$1
    um_fw_name=${um_fw_path##*/}
    um_file_cd_path=${um_fw_path//"$um_fw_name"/""}
    um_corrupt_fw_path="$um_file_cd_path/corrupt_${um_fw_name}"
    um_corrupt_size=$(("$(stat --printf="%s" "$um_fw_path")" - "1"))

    [ -f "$um_corrupt_fw_path" ] && rm "$um_corrupt_fw_path"

    log "Corrupting image $um_fw_path"
    dd if="$um_fw_path" of="$um_corrupt_fw_path" bs=1 count="$um_corrupt_size" &&
        log "Image corrupted Step #1" ||
        die "Failed to corruput image Step #1"

    dd if=/dev/urandom of="$um_corrupt_fw_path" bs=1 count=100 seek=1000 conv=notrunc &&
        log "Image corrupted Step #2" ||
        die "Failed to corruput image Step #2"

}

########################################## FW IMAGE SECTION - STOP #####################################################


############################################ CM SECTION - START ########################################################

# Manipulates internet access for selected IP.
# Internet can be blocked or unblocked by adding or removing DROP rule.
address_internet_manipulation()
{
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    # Select command and exit code according to options.
    [[ $type == "block" ]] && type_arg="-I" || type_arg="-D"
    [[ $type == "block" ]] && type_ec=0 || type_ec=1

    log -deb "Manipulating internet for ip address $ip_address"
    address_internet_check "$ip_address" "$type"

    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg FORWARD -s $ip_address -o eth0 -j DROP" &&
        log -deb "Internet ${type}ed for address $ip_address" ||
        die "Failed to $type internet for address $ip_address"

    return 0
}

address_internet_check()
{
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    if [[ "$type" == 'block' ]]; then
        exit_code=0
    else
        exit_code=1
    fi

    check_ec=$(${sudo_cmd} ${iptables_cmd} -C FORWARD -s "$ip_address" -o eth0 -j DROP)

    if [ "$?" -eq "$exit_code" ]; then
        die_with_code 0 "Internet already $type for address $ip_address"
    else
        return 1
    fi
}

interface_internet_check()
{
    local if_name=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}
    local fn_sleep=3

    if [[ "$type" == 'block' ]]; then
        exit_code=0
    else
        exit_code=1
    fi

    wait_for_function_response "$exit_code" "${sudo_cmd} ${iptables_cmd} -C INPUT -i $if_name -j DROP" "${fn_sleep}" &&
        die_with_code 0 "Internet already ${type}ed for interface $if_name" ||
        return 1

    return 0
}

# Manipulates internet access for selected IP.
# Internet can be blocked or unblocked by adding or removing DROP rule.
address_dns_manipulation()
{
    ip_address=${1}
    type=${2}
    fn_name="rpi_lib:address_dns_manipulation"
    log -deb "[DEPRECATED] - Function ${fn_name} is deprecated in favor of address_dns_manipulation2"
    # Select command and exit code according to options.
    [ "$type" == "block" ] && type_arg="-I" || type_arg="-D"
    [ "$type" == "block" ] && type_ec=0 || type_ec=1

    log -deb "$fn_name - Manipulating DNS traffic for ip address $ip_address"
    address_dns_check "$ip_address" "$type"

    wait_for_function_response "$type_ec" "iptables $type_arg OUTPUT -s $ip_address --dport 53 -j DROP" &&
        log -deb "$fn_name - DNS traffic ${type}ed for address $ip_address" ||
        raise "Failed to $type DNS traffic for address $ip_address" -l "$fn_name" -ds

    return 0
}

# Manipulates DNS traffic for source IP by adding or removing DROP rule
address_dns_manipulation2()
{
    local fn_name="rpi_lib:address_dns_manipulation2"
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}
    local retry_cnt=3
    [ $# -ne 2 ] && raise "Requires 2 input arguments" -l "${fn_name}" -arg
    [ -z $1 -o -z $2 ] && raise "Empty input argument(s)" -l "${fn_name}" -arg
    log -deb "${fn_name} Manipulate DNS traffic: ${type} ${ip_address}"

    local iptables_args="${iptables_chain} -p udp -s ${ip_address} --dport 53 -j DROP"
    if [ "${type}" == "block" ]; then
        address_dns_check "${ip_address}" "${type}" && return 0
        local cmd="${sudo_cmd} ${iptables_cmd} -I ${iptables_args}"
    elif [ "${type}" == "unblock" ]; then
        address_dns_check "${ip_address}" "${type}" && return 0
        local cmd="${sudo_cmd} ${iptables_cmd} -D ${iptables_args}"
    else
        raise "Invalid input argument type ${type}" -l "${fn_name}" -arg
    fi

    wait_for_function_exitcode "0" "${cmd}" "${retry_cnt}" &&
        log -deb "${fn_name} - DNS traffic ${type}ed for ${ip_address}" ||
        raise "Failed to ${type} DNS traffic for ${ip_address}" -l "${fn_name}" -ds

    address_dns_check "${ip_address}" "${type}" &&
        log -deb "${fn_name} - Command ${cmd} success" ||
        raise "Command ${cmd} incorrectly reported success, check system" -l "${fn_name}" -ds
}

address_dns_check()
{
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}
    fn_name="rpi_lib:address_dns_check"

    if [ "$type" == 'block' ]; then
        exit_code=0
    else
        exit_code=1
    fi

    check_ec=$(${sudo_cmd} ${iptables_cmd} -C ${iptables_chain} -p udp -s "$ip_address" --dport 53 -j DROP)

    if [ "$?" -eq "$exit_code" ]; then
        log -deb "$fn_name - DNS traffic already ${type}ed for address $ip_address"
        return 0
    else
        return 1
    fi
}

############################################ CM SECTION - STOP #########################################################
