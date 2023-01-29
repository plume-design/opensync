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
source "${fut_topdir}"/lib/client_lib.sh

enable_dhcp_default=true
ip_address_default="8.8.8.8"
connect_retry_max_default=2
connect_check_timeout_default=30
psk=""
usage() {
    cat << usage_string
tools/client/connect_to_wpa.sh [-h] arguments
Description:
    - Connect Client to Wireless network
Fixed positional arguments:
    -h  show this help message
    \$1 (wpa_type)            : Type of wifi security (open/wpa2/wpa3)                  : (string)(required)
    \$2 (wlan_namespace)      : Interface namespace name                                : (string)(required)
    \$3 (wlan_name)           : Wireless interface name                                 : (string)(required)
    \$4 (wpa_supp_cfg_path)   : wpa_supplicant.conf file path                           : (string)(required)
    \$5 (ssid)                : Wireless ssid name                                      : (string)(required)
Variable optional arguments:
    (psk)                     : Wireless psk key, required only for WPA2/WPA3 wpa_type  : (string)(option)
    (enable_dhcp)             : Wait for IP address from DHCP after connect             : (string)(option)   : (default:true)
    (check_internet_ip)       : Check internet access on specific IP, ('false' to skip) : (string)(option)   : (default:8.8.8.8)
    (dns_ip)                  : Default DNS nameserver                                  : (string)(option)   : (default:8.8.8.8)
    (connect_retry_max)       : Number of connection retries allowed                    : (integer)(option)  : (default:2)
    (connect_check_timeout)   : Timeout in seconds for checking client connection       : (integer)(option)  : (default:30)
Dependency:
  - route -n and iwconfig tool
Script usage example:
   ./tools/client/connect_to_wpa.sh open nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name
   ./tools/client/connect_to_wpa.sh wpa2 nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name -psk network_psk_key
   ./tools/client/connect_to_wpa.sh wpa3 nswifi1 wlan0 /etc/netns/nswifi1/wpa_supplicant/wpa_supplicant.conf network_ssid_name -psk network_psk_key -dhcp false -internet_ip false -dns_ip 8.8.8.8 -retry 3 -connect_timeout 60
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
NARGS=5
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/connect_to_wpa.sh" -arg

wpa_type=${1}
wlan_namespace=${2}
wlan_name=${3}
wpa_supp_cfg_path=${4}
ssid=${5}
enable_dhcp=${enable_dhcp_default}
check_internet_ip=${ip_address_default}
dns_ip=${ip_address_default}
connect_retry_max=${connect_retry_max_default}
connect_check_timeout=${connect_check_timeout_default}
wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"
wpa_supp_base_name=${wpa_supp_cfg_path%%".conf"}

shift $NARGS
# Parsing optional arguments with flags, if passed.
while [ -n "${1}" ]; do
    option=${1}
    shift
    case "${option}" in
        -psk)
            psk=${1}
            shift
            ;;
        -dhcp)
            enable_dhcp=${1}
            shift
            ;;
        -internet_ip)
            check_internet_ip=${1}
            shift
            ;;
        -dns_ip)
            dns_ip=${1}
            shift
            ;;
        -retry)
            connect_retry_max=${1}
            shift
            ;;
        -connect_timeout)
            connect_check_timeout=${1}
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "tools/client/connect_to_wpa.sh" -arg
            ;;
    esac
done

if [[ "$EUID" -ne 0 ]]; then
    raise "FAIL: Please run this function as root - sudo" -l "tools/client/connect_to_wpa.sh"
fi

if [ "${wpa_type}" != "open" ] && [ "${wpa_type}" != "wpa2" ] && [ "${wpa_type}" != "wpa3" ]; then
    usage && raise "Wrong WPA security type used. Supported types: 'open', 'wpa2', 'wpa3'" -l "tools/client/connect_to_wpa.sh" -arg
fi

if [ "${wpa_type}" != "open" ]; then
    [ $psk == "" ] && raise "PSK key is mandatory for ${wpa_type} security mode" -l "tools/client/connect_to_wpa.sh" -arg
fi

connected=false
connect_retry_count=1
ping_timeout=15

while [ "${connected}" == false ] && [ "${connect_retry_count}" -le "${connect_retry_max}" ]; do
    log "tools/client/connect_to_wpa.sh: Starting connection to network - #${connect_retry_count}"
    ${wlan_namespace_cmd} -c "wpa_cli -i $wlan_name reconfigure"
    sleep 1
    ${wlan_namespace_cmd} -c "iw reg set US"
    sleep 1
    connect_to_wpa ${wlan_namespace} ${wlan_name} ${wpa_supp_cfg_path} ${wpa_type} ${ssid} ${enable_dhcp} ${psk} ${dns_ip} ${connect_check_timeout}
    if [ "$?" -ne 0 ]; then
        log -wrn "tools/client/connect_to_wpa.sh: Interface $wlan_name not connected to $ssid"
        ${wlan_namespace_cmd} -c "iwconfig"
    else
        if [ "${check_internet_ip}" != false ]; then
            sleep 2
            wait_for_function_response 0 "${wlan_namespace_cmd} -c \"ping -c 3 ${check_internet_ip}\"" "${ping_timeout}"
            internet_check="$?"
            if [ "$internet_check" != 0 ]; then
                log "tools/client/connect_to_wpa.sh: Could not ping internet ${check_internet_ip}"
                log "tools/client/connect_to_wpa.sh: Restarting dhclient"
                restart_dhclient ${wlan_name} ${wlan_namespace} ${dns_ip}
                wait_for_function_response 0 "${wlan_namespace_cmd} -c \"ping -c 3 ${check_internet_ip}\"" "${ping_timeout}"
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
