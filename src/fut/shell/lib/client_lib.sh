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
export FUT_CLIENT_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/client_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Library of common functions to be executed on client devices
#
####################### INFORMATION SECTION - STOP ############################

####################### CLIENT SETUP SECTION - START ############################

###############################################################################
# DESCRIPTION:
#   Function restarts dhclient on RPI Server by removing old dhclient pid,
#   config files, flushing ip addresses for wireless interface and starting
#   new dhclient
# INPUT PARAMETER(S):
#   $1   Wireless interface name (string, required)
#   $2   Interface namespace name (string, required)
#   $3   Default DNS server IP (string, optional)
# RETURNS:
#   0    On success.
# USAGE EXAMPLE(S):
#   restart_dhclient wlan0 nswifi1 8.8.8.8
###############################################################################
restart_dhclient()
{
    local NARGS=2
    [ $# -lt ${NARGS} ] &&
        raise "client_lib:restart_dhclient requires ${NARGS} input argument(s), $# given" -arg

    wlan_name=${1}
    wlan_namespace=${2}
    dns_ip=${3:-"8.8.8.8"}

    wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"
    log "client_lib:restart_dhclient - Restarting dhclient"
    cmd="dhclient -4 -r ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - FAIL"
    cmd="dhclient -6 -r ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - FAIL"
    cmd="ip addr flush ${wlan_name} scope global"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - FAIL"
    # Put default $dns_ip dns into resolv.conf
    cmd="sh -c \"echo 'nameserver $dns_ip' > /etc/resolv.conf\""
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - FAIL"
    dhclient_4_pids=$(ps aux | grep "dhclient.${wlan_name}" | grep -v grep | awk '{print $2}')
    if [ -n "${dhclient_4_pids}" ]; then
        log "client_lib:restart_dhclient - Killing dhclient.${wlan_name} | ${dhclient_4_pids}"
        # shellcheck disable=SC2086
        kill $dhclient_4_pids && echo 'OK' || echo 'FAIL'
    fi
    dhclient_6_pids=$(ps aux | grep "dhclient6.${wlan_name}" | grep -v grep | awk '{print $2}')
    if [ -n "${dhclient_6_pids}" ]; then
        log "client_lib:restart_dhclient - Killing dhclient6.${wlan_name} | ${dhclient_6_pids}"
        # shellcheck disable=SC2086
        kill $dhclient_6_pids && echo 'OK' || echo 'FAIL'
    fi
    cmd="rm -f /var/lib/dhcp/dhclient*${wlan_name}* /var/run/dhclient*${wlan_name}*"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - FAIL"
    cmd="timeout 30 dhclient -4 -pf /var/run/dhclient.${wlan_name}.pid -lf /var/lib/dhcp/dhclient.${wlan_name}.leases -I -df /var/lib/dhcp/dhclient6.${wlan_name}.leases ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "client_lib:restart_dhclient - CMD: ${cmd} - #1 - OK" ||
        log "client_lib:restart_dhclient - CMD: ${cmd} - #1 - FAIL"
    return $?
}

#######################################################################################
# DESCRIPTION:
#   Function invokes another function 'create_wpa_supplicant_config' that creates
#   wpa_supplicant config and kills wpa_supplicant, if aleardy running before running
#   new one. It also tries to restart dhclient if requested. Returns the connection
#   status to the caller.
#   Raises exception if wireless interface fails to turn UP.
# INPUT PARAMETER(S):
#   $1 Interface namespace (string, required)
#   $2 Wireless interface name (string, required)
#   $3 Path to wpa_supplicant.conf (string, required)
#   $4 wpa_type to be used: open/WPA2/WPA3 (string, required)
#   $5 Wireless SSID to connect to (string, required)
#   $6 DHCP enabled or not (string, required)
#   $7 Network PSK key, required for WPA2/WPA3 security modes (string, optional)
#   $8 Default DNS server IP (string, optional)
#   $9 timeout to check connection (integer, optional)
# RETURNS:
#   0     On success.
#   not 0 On failure.
# USAGE EXAMPLE(S):
#  connect_to_wpa nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf \
#  wpa2 fut_ssid true network_key 8.8.8.8 30
########################################################################################
connect_to_wpa()
{
    local NARGS=6
    [ $# -lt ${NARGS} ] &&
        raise "client_lib:connect_to_wpa requires ${NARGS} input argument(s), $# given" -arg

    wlan_namespace=${1}
    wlan_name=${2}
    wpa_supp_cfg_path=${3}
    wpa_type=${4}
    ssid=${5}
    enable_dhcp=${6}
    psk=${7}
    dns_ip=${8:-"8.8.8.8"}
    connect_check_timeout=${9:-30}

    wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"

    create_wpa_supplicant_config ${wpa_supp_cfg_path} ${wpa_type} ${ssid} ${psk}
    sleep 1

    wpa_supp_base_name=${wpa_supp_cfg_path%%".conf"}
    # Check if wpa_supplicant is running - force kill
    wpa_supp_pids=$(${wlan_namespace_cmd} -c "pidof wpa_supplicant")
    if [[ -n "${wpa_supp_pids}" ]]; then
        log "client_lib:connect_to_wpa - Killing wpa_supplicant_pids: kill ${wpa_supp_pids}"
        ${wlan_namespace_cmd} -c "kill ${wpa_supp_pids}"
    fi

    log "client_lib:connect_to_wpa - Removing old /tmp/wpa_supplicant_${wlan_name}* files"
    ${wlan_namespace_cmd} -c "rm -rf \"/tmp/wpa_supplicant_${wlan_name}*\"" || true

    log "client_lib:connect_to_wpa - Bringing $wlan_name down: ifdown ${wlan_name} --force"
    ${wlan_namespace_cmd} -c "ifdown ${wlan_name} --force"
    ret=$?
    sleep 1
    if [ "$ret" -ne 0 ]; then
        log "client_lib:connect_to_wpa -Failed while bringing interface ${wlan_name} down - maybe already down?"
    fi

    log "client_lib:connect_to_wpa - Bringing $wlan_name down: ifconfig down"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} down"
    sleep 3
    log "client_lib:connect_to_wpa - Bringing $wlan_name up: ifconfig up"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} up"
    sleep 3

    wait_for_function_response 0 "${wlan_namespace_cmd} -c \"ifconfig ${wlan_name} 2>/dev/null | grep \"flags=\" | grep UP\"" &&
        log "client_lib:connect_to_wpa - Interface '${wlan_name}' is UP - Success" ||
        raise "FAIL: Interface '${wlan_name}' is DOWN, should be UP" -l "client_lib:connect_to_wpa" -ds

    wpa_cmd="wpa_supplicant -D nl80211,wext -i ${wlan_name} -c ${wpa_supp_cfg_path} -P ${wpa_supp_base_name}.pid -f /tmp/wpa_supplicant_${wlan_name}.log -t -B -d"
    log "client_lib:connect_to_wpa - Starting ${wpa_cmd}"
    ${wlan_namespace_cmd} -c "${wpa_cmd}"
    sleep 3
    wait_for_function_response 0 "${wlan_namespace_cmd} -c \"iwconfig ${wlan_name} | grep $ssid\"" "${connect_check_timeout}"
    is_connected="$?"
    if [ "$enable_dhcp" == true ] && [ "$is_connected" -eq 0 ]; then
        restart_dhclient $wlan_name $wlan_namespace $dns_ip
        sleep 1
    fi

    return $is_connected
}

###################################################################################################################
# DESCRIPTION:
#   Function creates the wpa_supplicant.conf file in the path provided.
# INPUT PARAMETER(S):
#   $1  Path to the wpa_supplicant config file (string, required)
#   $2  wpa_type to be used: open/WPA2/WPA3 (string, required)
#   $3  SSID of the network (string, required)
#   $4  Network PSK key, required for WPA2/WPA3 security modes (string, optional)
# RETURNS:
# USAGE EXAMPLE(S):
#  create_wpa_supplicant_config /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf open fut_ssid
#  create_wpa_supplicant_config /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf wpa2 fut_ssid network_key
###################################################################################################################
create_wpa_supplicant_config()
{
    local NARGS=3
    [ $# -lt ${NARGS} ] &&
        raise "client_lib:create_wpa_supplicant_config requires ${NARGS} input argument(s), $# given" -arg

    wpa_supp_cfg_path=${1}
    wpa_type=${2}
    ssid=${3}
    psk=${4}

    rm "${wpa_supp_cfg_path}"
    touch "${wpa_supp_cfg_path}"
    log "client_lib:create_wpa_supplicant_config - Creating ${wpa_supp_cfg_path} file"
    echo 'ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' > "${wpa_supp_cfg_path}"
    echo 'update_config=1' >> "${wpa_supp_cfg_path}"
    echo 'country=US' >> "${wpa_supp_cfg_path}"
    if [ "${wpa_type}" == "open" ]; then
(cat <<EOF
network={
    ssid="${ssid}"
    scan_ssid=1
    key_mgmt=NONE
    priority=1
}
EOF
) >> $wpa_supp_cfg_path
    elif [ "${wpa_type}" == "wpa2" ]; then
(cat <<EOF
network={
    ssid="${ssid}"
    scan_ssid=1
    psk="${psk}"
    key_mgmt=WPA-PSK
    priority=1
}
EOF
) >> $wpa_supp_cfg_path
    elif [ "${wpa_type}" == "wpa3" ]; then
(cat <<EOF
network={
    ssid="${ssid}"
    scan_ssid=1
    psk="${psk}"
    key_mgmt=SAE
    ieee80211w=2
    priority=1
}
EOF
) >> $wpa_supp_cfg_path
    fi
    log "client_lib:create_wpa_supplicant_config - ${wpa_supp_cfg_path} file:"
    cat "${wpa_supp_cfg_path}"
}

####################### CLIENT SETUP SECTION - STOP ############################
