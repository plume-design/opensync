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

# FUT environment loading
source "${fut_topdir}"/config/default_shell.sh
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "$fut_topdir/lib/rpi_lib.sh"

tc_name="tools/device/$(basename "$0")"
usage() {
    cat << usage_string
${tc_name} [-h] arguments
Description:
    - Connect RPI client to Wireless network
Arguments:
    -h  show this help message
    \$1 (wlan_namespace)      : Interface namespace name                                : (string)(required)
    \$2 (wlan_name)           : Wireless interface name                                 : (string)(required)
    \$3 (wpa_supp_cfg_path)   : wpa_supplicant.conf file path                           : (string)(required)
    \$4 (ssid)                : Wireless ssid name                                      : (string)(required)
    \$5 (psk)                 : Wireless psk key                                        : (string)(required)
    \$6 (enable_dhcp)         : Wait for IP address from DHCP after connect             : (string)(option)   : (default:true)
    \$7 (check_internet_ip)   : Check internet access on specific IP, ('false' to skip) : (string)(option)   : (default:8.8.8.8)
Script usage example:
   ./${tc_name} nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name network_psk_key
   ./${tc_name} nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name network_psk_key false
usage_string
}
while getopts h option; do
    case "$option" in
    h)
        usage && exit 1
        ;;
    *) ;;

    esac
done
NARGS=5
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

wlan_namespace=${1}
wlan_name=${2}
wpa_supp_cfg_path=${3}
ssid=${4}
psk=${5}
enable_dhcp=${6:-true}
check_internet_ip=${7:-"8.8.8.8"}
wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"

if [[ "$EUID" -ne 0 ]]; then
    raise "FAIL: Please run this function as root - sudo" -l "${tc_name}"
fi

connect_to_wpa2()
{
    log "$tc_name Bringing $wlan_name down: ifdown"
    ${wlan_namespace_cmd} -c "ifdown ${wlan_name} --force"
    sleep 1
    if [ "$?" -ne 0 ]; then
        log "$tc_name Failed while bringing interface ${wlan_name} down - maybe already down?"
    fi
    log "$tc_name Bringing $wlan_name down: ifconfig down"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} down"
    sleep 1
    # Check if wpa_supplicant is running - force kill
    wpa_supp_pids=$(${wlan_namespace_cmd} -c "pidof wpa_supplicant")
    if [[ -n "${wpa_supp_pids}" ]]; then
        ${wlan_namespace_cmd} -c "kill ${wpa_supp_pids}"
    fi
    log "$tc_name Bringing $wlan_name down: ifconfig up"
    ${wlan_namespace_cmd} -c "ifconfig ${wlan_name} up"
    sleep 1
    log "$tc_name Bringing $wlan_name up: ifup"
    if [[ "$enable_dhcp" == true ]]; then
        log "$tc_name: Using DHCP"
        ${wlan_namespace_cmd} -c "ifup ${wlan_name}"
    else
        log "$tc_name: Not using dhcp - killing dhclient after 10 seconds - workaround... "
        ${wlan_namespace_cmd} -c "timeout 10 ifup ${wlan_name} && kill $(pidof dhclient)"
    fi
}

rm "${wpa_supp_cfg_path}"
touch "${wpa_supp_cfg_path}"
log "$tc_name: Creating ${wpa_supp_cfg_path} file"
echo 'ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev' > "${wpa_supp_cfg_path}"
echo 'update_config=1' >> "${wpa_supp_cfg_path}"
echo "
network={
    ssid=\"${ssid}\"
    psk=\"${psk}\"
}" >> "${wpa_supp_cfg_path}"
log "$tc_name - ${wpa_supp_cfg_path} file:"
cat "${wpa_supp_cfg_path}"

connected=false
connect_retry_max=3
connect_retry_count=1
while [ "${connected}" == false ] && [ "${connect_retry_count}" -le "${connect_retry_max}" ]; do
    log "$tc_name: Starting connection to network - #${connect_retry_count}"
    connect_to_wpa2
    log "$tc_name: Checking if connected to network - $ssid"
    ssid_connected=$(${wlan_namespace_cmd} -c "iwconfig ${wlan_name} | grep $ssid")
    if [ "$?" -ne 0 ]; then
        log "$tc_name: Interface $wlan_name not connected to $ssid" -l "${tc_name}" -tc
        ${wlan_namespace_cmd} -c "iwconfig"
    else
        if [ "${check_internet_ip}" != false ]; then
            ${wlan_namespace_cmd} -c "ping -c 3 ${check_internet_ip}"
            internet_check="$?"
            if [ "$internet_check" != 0 ]; then
                log "$tc_name: Could not ping internet ${check_internet_ip}"
            else
                log "$tc_name: Internet access available"
                connected=true
            fi
        else
            connected=true
        fi
    fi
    connect_retry_count=$((connect_retry_count+1))
done
[ "${connected}" == true ] &&
    pass "$tc_name Interface $wlan_name is connected to the network $ssid"
    raise -l "${tc_name}" "Failed to connect to network" -tc
