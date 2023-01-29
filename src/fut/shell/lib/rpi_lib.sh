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
#   $1  package to be checked (string, required)
# RETURNS:
#   Last exit code.
# USAGE EXAMPLE(S):
#   dpkg_is_package_installed "haproxy"
###############################################################################
dpkg_is_package_installed()
{
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
#       - if haproxy file is not present,
#       - if certificate files are not present,
#       - if haproxy did not start.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_cloud_simulation
###############################################################################
start_cloud_simulation()
{
    local cert_dir="/etc/haproxy/certs/fut/"
    local ca_certificate_path="${FUT_TOPDIR}/shell/tools/server/files/ca_chain.pem"
    log -deb "rpi_lib:start_cloud_simulation - Check if haproxy package is installed"
    dpkg_is_package_installed "haproxy" ||
        raise "FAIL: haproxy not installed" -l "rpi_lib:start_cloud_simulation" -ds

    log -deb "rpi_lib:start_cloud_simulation - Creating cert dir: ${cert_dir}"
    sudo mkdir -p "${cert_dir}" ||
        raise "FAIL: Could not create cert dir!" -l "rpi_lib:start_cloud_simulation" -ds
    log -deb "rpi_lib:start_cloud_simulation - Copy haproxy configuration file and certificates"
    sudo cp "${FUT_TOPDIR}/shell/tools/server/files"/haproxy.cfg /etc/haproxy/haproxy.cfg ||
        raise "FAIL: Config file not present!" -l "rpi_lib:start_cloud_simulation" -ds
    sudo cp "${FUT_TOPDIR}/shell/tools/server/certs"/{server.pem,server.key,ca.pem} "${cert_dir}" ||
        raise "FAIL: Certificates not present!" -l "rpi_lib:start_cloud_simulation" -ds
    # Combine FUT ca.pem with ca_chain.pem
    sudo bash -c "cat ${ca_certificate_path} >> ${cert_dir}/ca.pem" ||
        raise "FAIL: Failed to combine ca.pem with ca_chain.pem" -l "rpi_lib:start_cloud_simulation" -ds
    log -deb "rpi_lib:start_cloud_simulation - Inserting server.key into server.pem since haproxy requires it"
    sudo bash -c "cat ${cert_dir}/server.key >> ${cert_dir}/server.pem" ||
        raise "FAIL: Failed to insert server.key into server.pem" -l "rpi_lib:start_cloud_simulation" -ds
    cur_user="$(id -u):$(id -g)"
    sudo chown -R ${cur_user} "${cert_dir}" ||
        raise "FAIL: Could not set certificate ownership to ${cur_user}!" -l "rpi_lib:start_cloud_simulation" -ds
    log -deb "rpi_lib:start_cloud_simulation - Restart haproxy service"
    sudo service haproxy restart ||
        raise "FAIL: haproxy not started" -l "rpi_lib:start_cloud_simulation" -ds
    log -deb "rpi_lib:start_cloud_simulation - haproxy service running"

    log -deb "rpi_lib:start_cloud_simulation - Starting Cloud listener - logging path /tmp/cloud_listener.log"
    "${FUT_TOPDIR}"/framework/tools/cloud_listener.py --verbose &> /tmp/cloud_listener.log &
    log -deb "rpi_lib:start_cloud_simulation - Cloud listener started" && exit 0
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
    log "rpi_lib:stop_cloud_simulation - Stop haproxy service"

    sudo service haproxy stop
    sudo systemctl is-active --quiet haproxy &&
        raise "FAIL: haproxy not stopped" -l "rpi_lib:stop_cloud_simulation" -ds
    kill $(ps aux | grep "haproxy" | grep -v "grep" | awk '{print $2}') > /dev/null 2>&1 &
    log -deb "rpi_lib:stop_cloud_simulation - haproxy service stopped"
    log -deb "rpi_lib:stop_cloud_simulation - Stop Cloud listener"
    kill $(ps aux | grep "cloud_listener" | grep -v "grep" | awk '{print $2}') > /dev/null 2>&1 &
    log -deb "rpi_lib:stop_cloud_simulation - Cloud listener stopped"
}

###############################################################################
# DESCRIPTION:
#   Function starts MQTT Broker on RPI server.
#   Uses haproxy package.
#   Raises exception:
#       - if package is not installed,
#       - if mosquitto file is not present,
#       - if certificate files are not present,
#       - if mosquitto did not start.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_fut_mqtt
###############################################################################
start_fut_mqtt()
{
    local cert_dir="/etc/mosquitto/certs/fut/"
    local mqtt_conf_file="${FUT_TOPDIR}/shell/tools/server/files/fut_mqtt.conf"

    log -deb "rpi_lib:start_fut_mqtt - Creating cert dir: ${cert_dir}"
    sudo mkdir -p "${cert_dir}" ||
        raise "FAIL: Failed to create cert dir!" -l "rpi_lib:start_fut_mqtt" -ds

    log -deb "rpi_lib:start_fut_mqtt - Copy mosquitto certificates"
    sudo cp "${FUT_TOPDIR}/shell/tools/server/certs"/{ca.pem,server.pem,server.key} "${cert_dir}" ||
        raise "FAIL: Certificates not present!" -l "rpi_lib:start_fut_mqtt" -ds

    cur_user="$(id -u):$(id -g)"
    sudo chown -R ${cur_user} "${cert_dir}" ||
        raise "FAIL: Failed to set permission for certificates!" -l "rpi_lib:start_fut_mqtt" -ds

    log -deb "rpi_lib:start_fut_mqtt - Start mosquitto service"
    /usr/sbin/mosquitto -c "${mqtt_conf_file}" -d ||
        raise "FAIL: mosquitto not started" -l "rpi_lib:start_fut_mqtt" -ds

    log -deb "rpi_lib:start_fut_mqtt - mosquitto service running"
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
    log -deb "rpi_lib:stop_fut_mqtt - Stopping MQTT daemon"
    # shellcheck disable=SC2046
    sudo kill $(ps aux | grep "mosquitto" | grep -v "grep" | awk '{print $2}') &&
        log -deb "rpi_lib:stop_fut_mqtt - mosquitto service stopped" ||
        log -deb "rpi_lib:stop_fut_mqtt - mosquitto service not running"
}

###############################################################################
# DESCRIPTION:
#   Function prints all the details of certificate in PEM format.
# INPUT PARAMETER(S):
#   $1 certificate file (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   print_certificate_details "cert.pem"
###############################################################################
print_certificate_details()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:print_certificate_details requires ${NARGS} input argument(s), $# given" -arg
    cert_file=$1

    log "rpi_lib: print_certificate_details - Printing details of certificate: $cert_file"
    openssl x509 -in $cert_file -noout -text
}
####################### UTILITY SECTION - STOP ################################

####################### NETWORK SECTION - START ###############################

####################### NETWORK SECTION - STOP ################################

####################### FW IMAGE SECTION - START ##############################

###############################################################################
# DESCRIPTION:
#   Function encrypts unencrypted image file with the provided encryption key.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to unencrypted image file (string, required)
#   $2  encryption key (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   um_encrypt_image <PATH_TO_UNENCRYPTED_IMAGE> <ENCRYPTION_KEY>
###############################################################################
um_encrypt_image()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:um_encrypt_image requires ${NARGS} input argument(s), $# given" -arg
    um_fw_unc_path=$1
    um_fw_key_path=$2
    um_fw_name=${um_fw_unc_path##*/}
    um_fw_pure_name=${um_fw_name//".img"/""}
    um_file_cd_path=${um_fw_unc_path//"$um_fw_name"/""}
    um_fw_enc_path="$um_file_cd_path/${um_fw_pure_name}.eim"

    [ -f "$um_fw_enc_path" ] && rm "$um_fw_enc_path"

    log "rpi_lib:um_encrypt_image - Encrypting image $um_fw_path with key $um_fw_key_path"
    openssl enc -aes-256-cbc -pass pass:"$(cat "$um_fw_key_path")" -md sha256 -nosalt -in "$um_fw_unc_path" -out "$um_fw_enc_path" &&
        log -deb "rpi_lib:um_encrypt_image - Image encrypted - Success" ||
        raise "FAIL: Failed to encrypt image" -l "rpi_lib:um_encrypt_image" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates md5 file to corresponding FW image file.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to image file (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   create_md5_file /tmp/clean_device_fw.img
###############################################################################
create_md5_file()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:create_md5_file requires ${NARGS} input argument(s), $# given" -arg
    um_file_path=$1

    um_fw_name=${um_file_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}

    log "rpi_lib:create_md5_file - Creating md5 sum file of file $um_file_path"
    cd "$um_file_cd_path" && md5sum "$um_fw_name" > "$um_fw_name.md5" &&
        log -deb "rpi_lib:create_md5_file - md5 sum file created - Success" ||
        raise "FAIL: Could not create md5 sum file" -l "rpi_lib:create_md5_file" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates corrupted md5 file to corresponding FW image file.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to image file (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   create_corrupt_md5_file /tmp/clean_device_fw.img
###############################################################################
create_corrupt_md5_file()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:create_corrupt_md5_file requires ${NARGS} input argument(s), $# given" -arg
    um_file_path=$1

    um_fw_name=${um_file_path##*/}
    um_file_cd_path=${um_file_path//"$um_fw_name"/""}
    um_md5_name="$um_file_cd_path/${um_fw_name}.md5"
    um_hash_only="$(cd "$um_file_cd_path" && md5sum "$um_fw_name" | cut -d' ' -f1)"

    log "rpi_lib:create_corrupt_md5_file - Creating $um_file_path.md5"
    echo "${um_hash_only:16:16}${um_hash_only:0:16}  ${um_fw_name}" > "$um_md5_name" &&
        log -deb "rpi_lib:create_corrupt_md5_file - Created '$um_md5_name' - Success" ||
        raise "FAIL: Could not create '$um_md5_name'" -l "rpi_lib:create_corrupt_md5_file" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates corrupted FW image file.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  path to image file (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   um_create_corrupt_image /tmp/clean_device_fw.img
###############################################################################
um_create_corrupt_image()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:um_create_corrupt_image requires ${NARGS} input argument(s), $# given" -arg
    um_fw_path=$1

    um_fw_name=${um_fw_path##*/}
    um_file_cd_path=${um_fw_path//"$um_fw_name"/""}
    um_corrupt_fw_path="$um_file_cd_path/corrupt_${um_fw_name}"
    # shellcheck disable=SC2034
    um_corrupt_size=$(("$(stat --printf="%s" "$um_fw_path")" - "1"))

    [ -f "$um_corrupt_fw_path" ] && rm "$um_corrupt_fw_path"

    log "rpi_lib:um_create_corrupt_image - Corrupting image '$um_fw_path'"
    cp "$um_fw_path" "$um_corrupt_fw_path" &&
        log -deb "rpi_lib:um_create_corrupt_image - Image copied Step #1" ||
        raise "FAIL: Could not copy image Step #1" -l "rpi_lib:um_create_corrupt_image" -ds

    dd if=/dev/urandom of="$um_corrupt_fw_path" bs=1 count=100 seek=1000 conv=notrunc &&
        log -deb "rpi_lib:um_create_corrupt_image - Image corrupted Step #2" ||
        raise "FAIL: Could not corrupt image Step #2" -l "rpi_lib:um_create_corrupt_image" -ds

    return 0
}

####################### FW IMAGE SECTION - STOP ###############################

####################### CM SECTION - START ####################################

###############################################################################
# DESCRIPTION:
#   Function manipulates internet access for source IP by adding or removing
#   DROP rule. Traffic can be blocked or unblocked.
#   Dies if cannot manipulate traffic.
# INPUT PARAMETER(S):
#   $1  ip address to block or unblock (string, required)
#   $2  type of rule to add, supported block, unblock (string, required)
#   $3  sudo command (defaults to sudo) (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   address_internet_manipulation 192.168.200.10 block
#   address_internet_manipulation 192.168.200.10 unblock
###############################################################################
address_internet_manipulation()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "rpi_lib:address_internet_manipulation requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    # Select command and exit code according to options.
    [[ $type == "block" ]] && type_arg="-I" || type_arg="-D"
    [[ $type == "block" ]] && type_ec=0 || type_ec=1

    log "rpi_lib:address_internet_manipulation - Manipulating internet for ip address '$ip_address'"
    address_internet_check "$ip_address" "$type"

    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg FORWARD -s $ip_address -o eth0 -j DROP" &&
        log -deb "rpi_lib:address_internet_manipulation - Internet ${type}ed for address '$ip_address' - Success" ||
        raise "FAIL: Could not $type internet for address '$ip_address'" -l "rpi_lib:address_internet_manipulation" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function manipulates (blocks/unblocks) the traffic for the
#   cloud controller IP.
# INPUT PARAMETER(S):
#   $1  ip address to be blocked or unblocked (string, required)
#   $2  type of manipulation, supported block, unblock (string, required)
#   $3  sudo command (defaults to sudo) (string, optional)
# RETURNS:
#   0   Traffic blocked or unblocked.
#   1   Traffic manipulation failed.
# USAGE EXAMPLE(S):
#   manipulate_cloud_controller_traffic 12.34.45.56 block
###############################################################################
manipulate_cloud_controller_traffic()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "rpi_lib:manipulate_cloud_controller_traffic requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}

    # Select command and exit code according to options.
    [[ $type == "block" ]] && type_arg="-I" || type_arg="-D"
    [[ $type == "block" ]] && type_ec=0 || type_ec=1

    log "rpi_lib:manipulate_cloud_controller_traffic - Manipulating traffic for ip address $ip_address"

    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg FORWARD -s $ip_address -i eth0 -j DROP" &&
    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg FORWARD -d $ip_address -i eth0 -j DROP" &&
    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg INPUT -s $ip_address -i eth0 -j DROP" &&
    wait_for_function_response "$type_ec" "${sudo_cmd} ${iptables_cmd} $type_arg OUTPUT -d $ip_address -j DROP" &&
        log -deb "rpi_lib:manipulate_cloud_controller_traffic - Traffic ${type}ed for address '$ip_address' - Success" ||
        raise "FAIL: Could not $type traffic for address '$ip_address'" -l "rpi_lib:manipulate_cloud_controller_traffic" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if internet access is already blocked or unblocked
#   for source IP.
#   Raises exception if internet access is already blocked or unblocked.
# INPUT PARAMETER(S):
#   $1  ip address to be blocked or unblocked (string, required)
#   $2  type of manipulation, supported block, unblock (string, required)
#   $3  sudo command (string, optional, defaults to sudo)
# RETURNS:
#   0   Traffic already blocked or unblocked.
#   1   Traffic not yet manipulated.
# USAGE EXAMPLE(S):
#   address_internet_check 192.168.200.10 block
###############################################################################
address_internet_check()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "rpi_lib:address_internet_check requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
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
        raise "FAIL: Internet already ${type}ed for address '$ip_address'" -l "rpi_lib:address_internet_check" -ec 0 -ds
    else
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function manipulates DNS traffic for source IP by adding or removing
#   DROP rule. Traffic can be blocked or unblocked.
#   Raises exception if there are not enough arguments, invalid arguments or
#   traffic cannot be manipulated.
# INPUT PARAMETER(S):
#   $1  ip address to block or unblock (string, required)
#   $2  type of rule to add, supported block, unblock (string, required)
#   $3  sudo command (string, optional, defaults to sudo)
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
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "rpi_lib:address_dns_manipulation requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
    local ip_address=${1}
    local type=${2}
    local sudo_cmd=${3:-"sudo"}
    local retry_cnt=3

    [ -z "${1}" ] || [ -z "${2}" ] &&
        raise "Empty input argument(s)" -l "rpi_lib:address_dns_manipulation" -arg

    log -deb "rpi_lib:address_dns_manipulation - Manipulate DNS traffic: ${type} ${ip_address}"

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
        # It will delete all the rules,
        # iptables will return exit code 1 in case non existing rule tries to be deleted
        local wait_exit_code=1
    else
        raise "FAIL: Invalid input argument type, given: ${type}, supported: block, unblock" -l "rpi_lib:address_dns_manipulation" -arg
    fi

    local cmd_udp="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_udp}"
    local cmd_udp_ssl="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_udp_ssl}"
    local cmd_tcp="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_tcp}"
    local cmd_tcp_ssl="${sudo_cmd} ${iptables_cmd} ${action_type} ${iptables_args_tcp_ssl}"

    wait_for_function_exit_code "${wait_exit_code}" "${cmd_udp}" "${retry_cnt}" &&
        log -deb "rpi_lib:address_dns_manipulation - DNS traffic ${type}ed for '${ip_address}'" ||
        raise "FAIL: Could not ${type} DNS traffic for '${ip_address}'" -l "rpi_lib:address_dns_manipulation" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_tcp}" "${retry_cnt}" &&
        log -deb "rpi_lib:address_dns_manipulation - DNS traffic ${type}ed for '${ip_address}'" ||
        raise "FAIL: Could not ${type} DNS traffic for '${ip_address}'" -l "rpi_lib:address_dns_manipulation" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_udp_ssl}" "${retry_cnt}" &&
        log -deb "rpi_lib:address_dns_manipulation - DNS traffic ${type}ed for '${ip_address}'" ||
        raise "FAIL: Could not ${type} DNS traffic for '${ip_address}'" -l "rpi_lib:address_dns_manipulation" -ds
    wait_for_function_exit_code "${wait_exit_code}" "${cmd_tcp_ssl}" "${retry_cnt}" &&
        log -deb "rpi_lib:address_dns_manipulation - DNS traffic ${type}ed for '${ip_address}'" ||
        raise "FAIL: Could not ${type} DNS traffic for '${ip_address}'" -l "rpi_lib:address_dns_manipulation" -ds
    address_dns_check "${ip_address}" "${type}" &&
        log -deb "rpi_lib:address_dns_manipulation - Command '${cmd_udp}' success" ||
        raise "FAIL: Command manipulating iptables incorrectly reported success, check system" -l "rpi_lib:address_dns_manipulation" -ds
}

###############################################################################
# DESCRIPTION:
#   Function checks if traffic is already blocked or unblocked for source IP.
# INPUT PARAMETER(S):
#   $1  IP address to be blocked or unblocked (string, required)
#   $2  type of rule to add, supported block, unblock (string, required)
#   $3  sudo command (string, optional, defaults to sudo)
# RETURNS:
#   0   Traffic already blocked or unblocked.
#   1   Traffic not yet manipulated.
# USAGE EXAMPLE(S):
#   address_dns_check
###############################################################################
address_dns_check()
{
    NARGS_MIN=2
    NARGS_MAX=3
    [ $# -ge ${NARGS_MIN} ] && [ $# -le ${NARGS_MAX} ] ||
        raise "rpi_lib:address_dns_check requires ${NARGS_MIN}-${NARGS_MAX} input arguments, $# given" -arg
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
        log -deb "rpi_lib:address_dns_check - DNS traffic already ${type}ed for address '$ip_address'"
        return 0
    else
        return 1
    fi
}

#################################################################################
# DESCRIPTION:
#   Function checks if traffic is flowing to the WAN IP/Port of host.
#   This function depends on usage of iperf3 tool.
# INPUT PARAMETER(S):
#   $1  IP address to check traffic (string, required)
#   $2  port number on DUT (interger, required)
# RETURNS:
#   0   Traffic flow is successful.
# USAGE EXAMPLE(S):
#  check_traffic_iperf3_client 192.168.200.10 8001
# NOTE: This function runs iperf3 client and the host (bearing IP/Port)
#       on the other side must be running iperf3 server to check traffic flow.
##################################################################################
check_traffic_iperf3_client()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:check_traffic_iperf3_client requires ${NARGS} input argument(s), $# given" -arg
    local ip_address=${1}
    local port=${2}

    check_ec=$(iperf3 -c ${ip_address} -p ${port} -t 5)
    echo "$check_ec" | grep -i "connected.*${ip_address}.*${port}"
    if [ "$?" -eq 0 ]; then
        log -deb "rpi_lib:check_traffic_iperf3_client: Traffic is reachable to WAN IP of the DUT: ${ip_address}:${port} - Success"
    else
        raise "FAIL: Traffic failed to reach WAN IP of the DUT: ${ip_address}:${port}" -l "rpi_lib:check_traffic_iperf3_client" -tc
    fi

    return 0
}

#################################################################################
# DESCRIPTION:
#   Function runs iperf3 server on the host. Server exits after serving one
#   client at a time('-1' option). This function depends on usage of iperf3 tool.
#   Raises exception if iperf3 fails to start.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Returns 0 on success.
# USAGE EXAMPLE(S):
#  run_iperf3_server
##################################################################################
run_iperf3_server()
{
    iperf3 -s -1 -D &&
        log -deb "rpi_lib:run_iperf3_server: Running iperf3 server on the device - Success" ||
        raise "FAIL: iperf3 failed to run on the device" -l "rpi_lib:run_iperf3_server" -tc

    return 0
}

#################################################################################
# DESCRIPTION:
#   Function generates self-signed certificate files which will be used for
#       simulated FUT Cloud connection and MQTT broker connection
# INPUT PARAMETER(S):
#   - None
# RETURNS:
#   0   Certificates generated successfully
# USAGE EXAMPLE(S):
#  generate_fut_self_signed_certificates
##################################################################################
generate_fut_self_signed_certificates()
{
    local NARGS=0
    [ $# -ne ${NARGS} ] &&
        raise "rpi_lib:generate_fut_self_signed_certificates does not require any input argument(s), $# given" -arg
    local certificate_path="${FUT_TOPDIR}/shell/tools/server/certs"
    local certificate_subjects_ca="/C=US/ST=FUT/L=FUT/O=FUT/CN=*.opensync.io:ca"
    local certificate_subjects_server="/C=US/ST=FUT/L=FUT/O=FUT/CN=*.opensync.io"
    log -deb "rpi_lib:generate_fut_self_signed_certificates - Removing any existing certificates in ${certificate_path}"
    if [ -d "${certificate_path}" ]; then
        rm ${certificate_path}/* &&
            log -deb "rpi_lib:generate_fut_self_signed_certificates - Certificate removed successfully"
    else
        log -deb "rpi_lib:generate_fut_self_signed_certificates - ${certificate_path} does not exists, creating folder"
        mkdir -p "${certificate_path}" &&
            log -deb "rpi_lib:generate_fut_self_signed_certificates - ${certificate_path} folder created" ||
            raise "FAIL: Failed to create folder ${certificate_path}" -l "rpi_lib:generate_fut_self_signed_certificates"
    fi
    cmd="openssl genrsa -out ${certificate_path}/ca.key 2048"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Executing ${cmd}"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    cmd="openssl req -new -x509 -days 2 -key ${certificate_path}/ca.key -out ${certificate_path}/ca.pem -subj ${certificate_subjects_ca}"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Executing ${cmd}"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    cmd="openssl genrsa -out ${certificate_path}/server.key 2048"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Executing ${cmd}"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    cmd="openssl req -new -out ${certificate_path}/server.csr -key ${certificate_path}/server.key -subj ${certificate_subjects_server}"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Executing ${cmd}"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    cmd="openssl x509 -req -in ${certificate_path}/server.csr -CA ${certificate_path}/ca.pem -CAkey ${certificate_path}/ca.key -CAcreateserial -out ${certificate_path}/server.pem -days 2"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Executing ${cmd}"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Verifying certificates ${certificate_path}"
    cmd="openssl verify -verbose -CAfile ${certificate_path}/ca.pem ${certificate_path}/server.pem"
    ${cmd} ||
        raise "FAIL: Failed to execute: ${cmd}" -l "rpi_lib:generate_fut_self_signed_certificates"
    log -deb "rpi_lib:generate_fut_self_signed_certificates: Printing contents of ${certificate_path}"
    tail ${certificate_path}/* || true
    return 0
}

####################### CM SECTION - STOP #####################################
