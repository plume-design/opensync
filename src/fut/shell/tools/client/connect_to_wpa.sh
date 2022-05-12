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


current_dir=$(dirname "$(realpath "$BASH_SOURCE")")
fut_topdir="$(realpath "$current_dir"/../..)"

# shellcheck disable=SC1091
# FUT environment loading
source "${fut_topdir}"/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${fut_topdir}"/lib/unit_lib.sh

usage() {
    cat << usage_string
tools/client/connect_to_wpa.sh [-h] arguments
Description:
    - Connect Client to Wireless network
Arguments:
    -h  show this help message
    \$1 (client_model)        : Type of client (rpi/brix)                               : (string)(required)
    \$2 (wpa_type)            : Type of wifi security (wpa2/wpa3)                       : (string)(required)
    \$3 (wlan_namespace)      : Interface namespace name                                : (string)(required)
    \$4 (wlan_name)           : Wireless interface name                                 : (string)(required)
    \$5 (wpa_supp_cfg_path)   : wpa_supplicant.conf file path                           : (string)(required)
    \$6 (ssid)                : Wireless ssid name                                      : (string)(required)
    \$7 (psk)                 : Wireless psk key                                        : (string)(required)
    \$8 (enable_dhcp)         : Wait for IP address from DHCP after connect             : (string)(option)   : (default:true)
    \$9 (check_internet_ip)   : Check internet access on specific IP, ('false' to skip) : (string)(option)   : (default:8.8.8.8)
    \$10 (dns_ip)             : Default DNS nameserver                                  : (string)(option)   : (default:8.8.8.8)
    \$11 (gateway_ip)         : Default gateway IP address                              : (string)(option)   : (default:192.168.200.1)
Dependency:
  - route -n and iwconfig tool
Script usage example:
   ./tools/client/connect_to_wpa.sh rpi wpa2 nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name network_psk_key
   ./tools/client/connect_to_wpa.sh brix wpa3 nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name network_psk_key false
usage_string
}
if [ -n "${1}" ]; then
    case "${1}" in
    help | \
    --help | \
    -h)
        usage && exit 1
        ;;
    *) ;;

    esac
fi
NARGS=7
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/connect_to_wpa.sh" -arg

client_model=${1}
wpa_type=${2}
wlan_namespace=${3}
wlan_name=${4}
wpa_supp_cfg_path=${5}
ssid=${6}
psk=${7}
enable_dhcp=${8:-true}
check_internet_ip=${9:-"8.8.8.8"}
dns_ip=${10:-"8.8.8.8"}
gateway_ip=${11:-"192.168.200.1"}
wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"
wpa_supp_base_name=${wpa_supp_cfg_path%%".conf"}

if [[ "$EUID" -ne 0 ]]; then
    raise "FAIL: Please run this function as root - sudo" -l "tools/client/connect_to_wpa.sh"
fi

if [ "${client_model}" != "rpi" ] && [ "${client_model}" != "brix" ]; then
    usage && raise "Wrong client model. Supported models: 'rpi', 'brix'" -l "tools/client/connect_to_wpa.sh" -arg
fi

if [ "${wpa_type}" != "wpa2" ] && [ "${wpa_type}" != "wpa3" ]; then
    usage && raise "Wrong WPA security type used. Supported types: 'wpa2', 'wpa3'" -l "tools/client/connect_to_wpa.sh" -arg
fi

###############################################################################
# DESCRIPTION:
#   Function restarts dhclient on RPI Server by removing old dhclient pid,
#   config files, flushing ip addresses for wireless interface and starting
#   new dhclient
# INPUT PARAMETER(S):
# RETURNS:
#   0     On success.
#   not 0 On failure.
# USAGE EXAMPLE(S):
#   restart_dhclient
###############################################################################
restart_dhclient()
{
    log "tools/client/connect_to_wpa.sh: Restarting dhclient"
    cmd="dhclient -4 -r ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - FAIL"
    cmd="dhclient -6 -r ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - FAIL"
    cmd="ip addr flush ${wlan_name} scope global"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - FAIL"
    # Put default $dns_ip dns into resolv.conf
    cmd="sh -c \"echo 'nameserver $dns_ip' > /etc/resolv.conf\""
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - FAIL"
    dhclient_4_pids=$(ps aux | grep "dhclient.${wlan_name}" | grep -v grep | awk '{print $2}')
    if [ -n "${dhclient_4_pids}" ]; then
        log "tools/client/connect_to_wpa.sh: Killing dhclient.${wlan_name} | ${dhclient_4_pids}"
        # shellcheck disable=SC2086
        kill $dhclient_4_pids && echo 'OK' || echo 'FAIL'
    fi
    dhclient_6_pids=$(ps aux | grep "dhclient6.${wlan_name}" | grep -v grep | awk '{print $2}')
    if [ -n "${dhclient_6_pids}" ]; then
        log "tools/client/connect_to_wpa.sh: Killing dhclient6.${wlan_name} | ${dhclient_6_pids}"
        # shellcheck disable=SC2086
        kill $dhclient_6_pids && echo 'OK' || echo 'FAIL'
    fi
    cmd="rm -f /var/lib/dhcp/dhclient*${wlan_name}* /var/run/dhclient*${wlan_name}*"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - FAIL"
    cmd="timeout 20 dhclient -4 -pf /var/run/dhclient.${wlan_name}.pid -lf /var/lib/dhcp/dhclient.${wlan_name}.leases -I -df /var/lib/dhcp/dhclient6.${wlan_name}.leases ${wlan_name}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    return $?
}

###############################################################################
# DESCRIPTION:
#   Function manually manipulates route table for wlan0 to bypass bug in
#   dhclient restart procedure where dhclient does not populate route table
#   which results in network being unreachable
# INPUT PARAMETER(S):
#   $1 Gateway IP address of connected network on wlan0
# RETURNS:
#   0     On success.
#   not 0 On failure.
# USAGE EXAMPLE(S):
#   manual_route_set 192.168.200.1
###############################################################################
manual_route_set()
{
    gateway_ip_r=${1}
    # Remove any old route
    cmd="ip route del default"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    cmd="ip route del ${gateway_ip_r}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    # Not required to be ${gateway_ip_r}
    cmd="ip route del 192.168.200.0/24"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    sleep 1
    # Add default route ${gateway_ip_r}
    cmd="ip route add ${gateway_ip_r} dev wlan0"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    cmd="ip route add default via ${gateway_ip_r}"
    ${wlan_namespace_cmd} -c "${cmd}" &&
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - OK" ||
        log "tools/client/connect_to_wpa.sh: CMD: ${cmd} - #1 - FAIL"
    sleep 1
    ${wlan_namespace_cmd} -c "route"
}

connect_to_wpa()
{
    log "tools/client/connect_to_wpa.sh Removing old /tmp/wpa_supplicant_${wlan_name}* files"
    ${wlan_namespace_cmd} -c "rm -rf \"/tmp/wpa_supplicant_${wlan_name}*\"" || true

    log "tools/client/connect_to_wpa.sh Bringing $wlan_name down: ifdown ${wlan_name} --force"
    ${wlan_namespace_cmd} -c "ifdown ${wlan_name} --force"
    ret=$?
    sleep 1
    if [ "$ret" -ne 0 ]; then
        log "tools/client/connect_to_wpa.sh Failed while bringing interface ${wlan_name} down - maybe already down?"
    fi

    # Check if wpa_supplicant is running - force kill
    wpa_supp_pids=$(${wlan_namespace_cmd} -c "pidof wpa_supplicant")
    if [[ -n "${wpa_supp_pids}" ]]; then
        log "tools/client/connect_to_wpa.sh Killing wpa_supplicant_pids: kill ${wpa_supp_pids}"
        ${wlan_namespace_cmd} -c "kill ${wpa_supp_pids}"
    fi

    log "tools/client/connect_to_wpa.sh Bringing $wlan_name down: ifconfig down"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} down"
    sleep 3
    log "tools/client/connect_to_wpa.sh Bringing $wlan_name up: ifconfig up"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} up"
    sleep 3
    wpa_cmd="wpa_supplicant -D nl80211,wext -i ${wlan_name} -c ${wpa_supp_cfg_path} -P ${wpa_supp_base_name}.pid -f /tmp/wpa_supplicant_${wlan_name}.log -t -B -d"
    log "tools/client/connect_to_wpa.sh: Starting ${wpa_cmd}"
    ${wlan_namespace_cmd} -c "${wpa_cmd}"
    sleep 3
    if [[ "$enable_dhcp" == true ]]; then
        restart_dhclient
        sleep 1
    fi
}

create_wpa_supplicant_config()
{
    rm "${wpa_supp_cfg_path}"
    touch "${wpa_supp_cfg_path}"
    log "tools/client/connect_to_wpa.sh: Creating ${wpa_supp_cfg_path} file"
    echo 'ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' > "${wpa_supp_cfg_path}"
    echo 'update_config=1' >> "${wpa_supp_cfg_path}"
    echo 'country=US' >> "${wpa_supp_cfg_path}"
    if [ "${wpa_type}" == "wpa2" ]; then
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
    log "tools/client/connect_to_wpa.sh - ${wpa_supp_cfg_path} file:"
    cat "${wpa_supp_cfg_path}"
}

connected=false
connect_retry_max=3
connect_retry_count=1
connect_check_timeout=10

while [ "${connected}" == false ] && [ "${connect_retry_count}" -le "${connect_retry_max}" ]; do
    log "tools/client/connect_to_wpa.sh: Starting connection to network - #${connect_retry_count}"
    ${wlan_namespace_cmd} -c "wpa_cli -i $wlan_name reconfigure"
    sleep 1
    ${wlan_namespace_cmd} -c "iw reg set US"
    sleep 1
    create_wpa_supplicant_config
    sleep 1
    connect_to_wpa
    sleep 1
    log "tools/client/connect_to_wpa.sh: Checking if $wlan_name is connected to network: $ssid"
    wait_for_function_response 0 "${wlan_namespace_cmd} -c \"iwconfig ${wlan_name} | grep $ssid\"" "${connect_check_timeout}"
    if [ "$?" -ne 0 ]; then
        log -wrn "tools/client/connect_to_wpa.sh: Interface $wlan_name not connected to $ssid"
        ${wlan_namespace_cmd} -c "iwconfig"
    else
        if [ "${check_internet_ip}" != false ]; then
            sleep 2
            wait_for_function_response 0 "${wlan_namespace_cmd} -c \"ping -c 3 ${check_internet_ip}\"" "${connect_check_timeout}"
            internet_check="$?"
            if [ "$internet_check" != 0 ]; then
                log "tools/client/connect_to_wpa.sh: Could not ping internet ${check_internet_ip}"
                log "tools/client/connect_to_wpa.sh: Trying manually setting routes"
                restart_dhclient
                manual_route_set "${gateway_ip}"
                wait_for_function_response 0 "${wlan_namespace_cmd} -c \"ping -c 3 ${check_internet_ip}\"" "${connect_check_timeout}"
                internet_check="$?"
                if [ "$internet_check" != 0 ]; then
                    log "tools/client/connect_to_wpa.sh: Dumping routes"
                    ${wlan_namespace_cmd} -c "route -n" || true
                    log "tools/client/connect_to_wpa.sh: Dumping iwconfig ${wlan_name}"
                    ${wlan_namespace_cmd} -c "iwconfig ${wlan_name}" || true
                else
                    log "tools/client/connect_to_wpa.sh: Internet access available"
                    connected=true
                fi
            else
                log "tools/client/connect_to_wpa.sh: Internet access available"
                connected=true
            fi
        else
            connected=true
        fi
    fi
    connect_retry_count=$((connect_retry_count+1))
done

[ "${connected}" == true ] &&
    pass "tools/client/connect_to_wpa.sh: Interface $wlan_name is connected to $ssid" ||
    log -err "tools/client/connect_to_wpa.sh: Could not connect $wlan_name to network: $ssid"
