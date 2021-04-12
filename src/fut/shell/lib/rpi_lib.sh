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
export FUT_TOPDIR="$(realpath "$lib_dir"/../..)"
export FUT_RPI_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/rpi_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Library of common functions to be executed on RPI
#
####################### INFORMATION SECTION - STOP ############################

# RPI server "iptables" defaults to "xtables-nft-multi"
iptables_cmd="iptables-legacy"
iptables_chain="FORWARD"

####################### UTILITY SECTION - START ###############################

###############################################################################
# DESCRIPTION:
#   Function checks if package is installed.
# INPUT PARAMETER(S):
#   $1  package to be checked
# RETURNS:
#   Last exit code.
# USAGE EXAMPLE(S):
#   dpkg_is_package_installed "haproxy"
###############################################################################
dpkg_is_package_installed()
{
    local fn_name="rpi_lib:dpkg_is_package_installed"
    package=$1
    dpkg -l | grep -w "${package}" | grep -E -q ^ii
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function starts cloud simulation on RPI server.
#   Uses haproxy package.
#   Raises exception:
#       - if package is not installed,
#       - haproxy file is not present,
#       - certificate files are not present,
#       - haproxy did not start.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_cloud_simulation
###############################################################################
start_cloud_simulation()
{
    local fn_name="rpi_lib:start_cloud_simulation"
    local cert_dir="/etc/haproxy/certs/fut/"
    log "$fn_name - Check if haproxy package is installed"
    dpkg_is_package_installed "haproxy" ||
        raise "FAIL: haproxy not installed" -l "$fn_name" -ds

    log "$fn_name - Creating cert dir: ${cert_dir}"
    sudo mkdir -p "${cert_dir}" ||
        raise "FAIL: Could not create cert dir!" -l "$fn_name" -ds
    log "$fn_name - Copy haproxy configuration file and certificates"
    sudo cp "${FUT_TOPDIR}/shell/tools/rpi/files"/haproxy.cfg /etc/haproxy/haproxy.cfg ||
        raise "FAIL: Config file not present!" -l "$fn_name" -ds
    sudo cp "${FUT_TOPDIR}/shell/tools/rpi/files"/{fut_controller.pem,plume_ca_chain.pem} "${cert_dir}" ||
        raise "FAIL: Certificates not present!" -l "$fn_name" -ds

    log "$fn_name - Restart haproxy service"
    sudo service haproxy restart ||
        raise "FAIL: haproxy not started" -l "$fn_name" -ds
    log "$fn_name - haproxy service running"

    log "$fn_name - Starting Cloud listener - logging path /tmp/cloud_listener.log"
    "${FUT_TOPDIR}"/framework/tools/cloud_listener.py -v > /dev/null 2>&1 &
    log "$fn_name - Cloud listener started" && exit 0
}

###############################################################################
# DESCRIPTION:
#   Function stops cloud simulation on RPI server.
#   Uses haproxy package.
#   Dies if haproxy did not stop.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   stop_cloud_simulation
###############################################################################
stop_cloud_simulation()
{
    fn_name="rpi_lib:stop_cloud_simulation"
    log "$fn_name - Stop haproxy service"

    sudo service haproxy stop
    sudo systemctl is-active --quiet haproxy &&
        raise "FAIL: haproxy not stopped" -l "$fn_name" -ds
    log "$fn_name - haproxy service stopped"
    log "$fn_name - Stop Cloud listener"
    kill $(pgrep cloud_listener) > /dev/null 2>&1 &
    log "$fn_name - Cloud listener stopped"
}

###############################################################################
# DESCRIPTION:
#   Function starts MQTT Broker on RPI server.
#   Uses haproxy package.
#   Raises exception:
#       - if package is not installed,
#       - mosquitto file is not present,
#       - certificate files are not present,
#       - mosquitto did not start.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_fut_mqtt
###############################################################################
start_fut_mqtt()
{
    local fn_name="rpi_lib:start_fut_mqtt"
    local cert_dir="/etc/mosquitto/certs/fut/"
    local mqtt_conf_file="${FUT_TOPDIR}/shell/tools/rpi/files/fut_mqtt.conf"

    log "$fn_name - Creating cert dir: ${cert_dir}"
    sudo mkdir -p "${cert_dir}" ||
        raise "Failed to create cert dir!" -l "$fn_name" -ds

    log "Copy mosquitto certificates"
    sudo cp "${FUT_TOPDIR}/shell/tools/rpi/files"/{fut_controller.pem,plume_ca_chain.pem} "${cert_dir}" ||
        raise "Certificates not present!" -l "$fn_name" -ds

    log "Start mosquitto service"
    /usr/sbin/mosquitto -c "${mqtt_conf_file}" -d ||
        raise "mosquitto not started" -l "$fn_name" -ds
    log "mosquitto service running"
}

###############################################################################
# DESCRIPTION:
#   Function stops MQTT Broker on RPI server.
#   Uses mosquitto package.
#   Dies if mosquitto did not stop.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   stop_fut_mqtt
###############################################################################
stop_fut_mqtt()
{
    fn_name="rpi_lib:stop_fut_mqtt"
    log "${fn_name} - Stopping MQTT daemon"
    # shellcheck disable=SC2046
    sudo kill $(pgrep mosquitto) &&
        log "mosquitto service stopped" ||
        log "mosquitto service not running"
}
####################### UTILITY SECTION - STOP ################################

####################### NETWORK SECTION - START ###############################

####################### NETWORK SECTION - STOP ################################

####################### FW IMAGE SECTION - START ##############################

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
um_encrypt_image()
{
    fn_name="rpi_lib:um_encrypt_image"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
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
        raise "Failed to encrypt image" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
um_create_md5_file()
{
    fn_name="rpi_lib:um_create_md5_file"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    um_file_path=$1

    um_fw_name=${um_file_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}

    log "Creating md5 sum file of file $um_file_path"
    cd "$um_file_cd_path" && md5sum "$um_fw_name" > "$um_fw_name.md5" &&
        log "$fn_name - md5 sum file created" ||
        raise "FAIL: Could not create md5 sum file" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
um_create_corrupt_md5_file()
{
    fn_name="rpi_lib:um_create_corrupt_md5_file"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    um_file_path=$1

    um_fw_name=${um_file_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}
    um_md5_name="$um_file_cd_path/${um_fw_name}.md5"
    um_hash_only="$(cd "$um_file_cd_path" && md5sum "$um_fw_name" | cut -d' ' -f1)"

    log "$fn_name - Creating $um_file_path.md5"
    echo "${um_hash_only:16:16}${um_hash_only:0:16}  ${um_fw_name}" > "$um_md5_name" &&
        log "$fn_name - Created $um_md5_name" ||
        raise "FAIL: Could not create $um_md5_name" -l "$fn_name" -ds
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
um_create_corrupt_image()
{
    fn_name="rpi_lib:um_create_corrupt_image"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    um_fw_path=$1

    um_fw_name=${um_fw_path##*/}
    um_file_cd_path=${um_fw_path//"$um_fw_name"/""}
    um_corrupt_fw_path="$um_file_cd_path/corrupt_${um_fw_name}"
    um_corrupt_size=$(("$(stat --printf="%s" "$um_fw_path")" - "1"))

    [ -f "$um_corrupt_fw_path" ] && rm "$um_corrupt_fw_path"

    log "$fn_name - Corrupting image $um_fw_path"
    dd if="$um_fw_path" of="$um_corrupt_fw_path" bs=1 count="$um_corrupt_size" &&
        log "$fn_name - Image corrupted Step #1" ||
        raise "FAIL: Could not corrupt image Step #1" -l "$fn_name" -ds

    dd if=/dev/urandom of="$um_corrupt_fw_path" bs=1 count=100 seek=1000 conv=notrunc &&
        log "$fn_name - Image corrupted Step #2" ||
        raise "FAIL: Could not corrupt image Step #2" -l "$fn_name" -ds
}

####################### FW IMAGE SECTION - STOP ###############################

####################### CM SECTION - START ####################################

###############################################################################
# DESCRIPTION:
#   Function manipulates internet access for source IP by adding or removing
#   DROP rule. Traffic can be blocked or unblocked.
#   Dies if cannot manipulate traffic.
# INPUT PARAMETER(S):
#   $1  ip address to block or unblock (required)
#   $2  type of rule to add, supported block, unblock (required)
#   $3  sudo command (defaults to sudo) (optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   address_internet_manipulation 192.168.200.10 block
#   address_internet_manipulation 192.168.200.10 unblock
###############################################################################
address_internet_manipulation()
{
    local fn_name="rpi_lib:address_internet_manipulation"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    # Select command and exit code according to options.
    [[ $type == "block" ]] && type_arg="-I" || type_arg="-D"
    [[ $type == "block" ]] && type_ec=0 || type_ec=1

    log -deb "$fn_name - Manipulating internet for ip address $ip_address"
    address_internet_check "$ip_address" "$type"

    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg FORWARD -s $ip_address -o eth0 -j DROP" &&
        log -deb "$fn_name - Internet ${type}ed for address $ip_address" ||
        raise "FAIL: Could not $type internet for address $ip_address" -l "$fn_name" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if internet access is already blocked or unblocked
#   for source IP.
#   Raises exception if internet access is already blocked or unblocked.
# INPUT PARAMETER(S):
#   $1  ip address to be blocked or unblocked (required)
#   $2  type of manipulation, supported block, unblock (required)
#   $3  sudo command (defaults to sudo) (optional)
# RETURNS:
#   0   Traffic already blocked or unblocked.
#   1   Traffic not yet manipulated.
# USAGE EXAMPLE(S):
#   address_internet_check 192.168.200.10 block
###############################################################################
address_internet_check()
{
    local fn_name="rpi_lib:address_internet_check"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
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
        raise "FAIL: Internet already ${type}ed for address $ip_address" -l "$fn_name" -ec 0 -ds
    else
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function manipulates DNS traffic for source IP by adding or removing
#   DROP rule. Traffic can be blocked or unblocked.
#   Raises exception if there are not enough arguments, invalid arguments,
#   traffic cannot be manipulated.
# INPUT PARAMETER(S):
#   $1  ip address to block or unblock (required)
#   $2  type of rule to add, supported block, unblock (required)
#   $3  sudo command (defaults to sudo) (optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION
# USAGE EXAMPLE(S):
#   address_dns_manipulation 192.168.200.10 block
#   address_dns_manipulation 192.168.200.10 unblock
###############################################################################
address_dns_manipulation()
{
    local iptables_chain="INPUT"
    local fn_name="rpi_lib:address_dns_manipulation"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}
    local retry_cnt=3

    [ -z "${1}" ] || [ -z "${2}" ] && raise "Empty input argument(s)" -l "${fn_name}" -arg

    log -deb "${fn_name} - Manipulate DNS traffic: ${type} ${ip_address}"

    local iptables_args_udp="${iptables_chain} -p udp -s ${ip_address} --dport 53 -j DROP"
    local iptables_args_udp_ssl="${iptables_chain} -p udp -s ${ip_address} --dport 853 -j DROP"
    local iptables_args_tcp="${iptables_chain} -p tcp -s ${ip_address} --dport 53 -j DROP"
    local iptables_args_tcp_ssl="${iptables_chain} -p tcp -s ${ip_address} --dport 853 -j DROP"

    address_dns_check "${ip_address}" "${type}" && return 0
    if [ "${type}" == "block" ]; then
        local action_type='-I'
        local wait_exit_code=0
    elif [ "${type}" == "unblock" ]; then
        local action_type='-D'
        # Waiting exit code 1 for unblock in case there is more than one rules of block in iptables
        # It will delete all the rules, iptables will return exit code 1 in case non existing rule tries to be deleted
        local wait_exit_code=1
    else
        raise "FAIL: Invalid input argument type, given: ${type}, supported: block, unblock" -l "${fn_name}" -arg
    fi

    local cmd_udp="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_udp}"
    local cmd_udp_ssl="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_udp_ssl}"
    local cmd_tcp="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_tcp}"
    local cmd_tcp_ssl="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_tcp_ssl}"

    wait_for_function_exit_code "${wait_exit_code}" "${cmd_udp}" "${retry_cnt}" &&
        log -deb "${fn_name} - DNS traffic ${type}ed for ${ip_address}" ||
        raise "FAIL: Could not ${type} DNS traffic for ${ip_address}" -l "${fn_name}" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_tcp}" "${retry_cnt}" &&
        log -deb "${fn_name} - DNS traffic ${type}ed for ${ip_address}" ||
        raise "FAIL: Could not ${type} DNS traffic for ${ip_address}" -l "${fn_name}" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_udp_ssl}" "${retry_cnt}" &&
        log -deb "${fn_name} - DNS traffic ${type}ed for ${ip_address}" ||
        raise "FAIL: Could not ${type} DNS traffic for ${ip_address}" -l "${fn_name}" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_tcp_ssl}" "${retry_cnt}" &&
        log -deb "${fn_name} - DNS traffic ${type}ed for ${ip_address}" ||
        raise "FAIL: Could not ${type} DNS traffic for ${ip_address}" -l "${fn_name}" -ds
    address_dns_check "${ip_address}" "${type}" &&
        log -deb "${fn_name} - Command ${cmd_udp} success" ||
        raise "FAIL: Command manipulating iptables incorrectly reported success, check system" -l "${fn_name}" -ds
}

###############################################################################
# DESCRIPTION:
#   Function checks if traffic is already blocked or unblocked for source IP.
# INPUT PARAMETER(S):
#   $1  ip address to be blocked or unblocked (required)
#   $2  type of rule to add, supported block, unblock (required)
#   $3  sudo command (defaults to sudo) (optional)
# RETURNS:
#   0   Traffic already blocked or unblocked.
#   1   Traffic not yet manipulated.
# USAGE EXAMPLE(S):
#   address_dns_check
###############################################################################
address_dns_check()
{
    fn_name="rpi_lib:address_dns_check"
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "${fn_name} requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local iptables_chain="INPUT"
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    if [ "$type" == 'block' ]; then
        exit_code=0
    else
        exit_code=1
    fi

    # shellcheck disable=SC2034
    check_ec=$(${sudo_cmd} ${iptables_cmd} -C ${iptables_chain} -p udp -s "$ip_address" --dport 53 -j DROP)
    if [ "$?" -eq "$exit_code" ]; then
        log -deb "$fn_name - DNS traffic already ${type}ed for address $ip_address"
        return 0
    else
        return 1
    fi
}

####################### CM SECTION - STOP #####################################
