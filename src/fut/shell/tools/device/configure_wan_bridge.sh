#!/bin/sh

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


# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${FUT_TOPDIR}/shell/lib/wm2_lib.sh
source ${FUT_TOPDIR}/shell/lib/nm2_lib.sh
source ${LIB_OVERRIDE_FILE}

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM
tc_name="tools/device/$(basename "$0")"
upstream_ip_default="192.168.200.1"
internet_ip_default="1.1.1.1"
wan_mtu_default=1500
n_ping_default=5
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures WAN bridge and checks internet connectivity on device
Arguments:
    -h  show this help message
    - \$1 (eth)         : Physical ethernet interface name to create bridges: (string)(required)
    - \$2 (wan)         : WAN interface name                                  : (string)(required)
    - \$3 (lan)         : LAN interface name                                  : (string)(required)
    - \$4 (wan_mtu)     : WAN MTU : (int)(optional)                           : (int)(optional)    : (default:${wan_mtu_default})
    - \$5 (upstream_ip) : IP address to check connection to WAN               : (string)(optional) : (default:${upstream_ip_default})
    - \$6 (internet_ip) : IP address to check internet connection             : (string)(optional) : (default:${internet_ip_default})
    - \$7 (n_ping)      : Number of packets to send while checking connection : (int)(optional)    : (default:${n_ping_default})
Script usage example:
   ./${tc_name} eth0 br-wan br-home 2000
usage_string
}
while getopts h option > /dev/null 2>&1; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
done

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

eth=${1}
wan=${2}
lan=${3}
wan_mtu=${4:-"$wan_mtu_default"}
upstream_ip=${5:-"$upstream_ip_default"}
internet_ip=${6:-"$internet_ip_default"}
n_ping=${7:-"$n_ping_default"}

check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    is_wano=true ||
    is_wano=false
log "${tc_name}: Is WANO enabled: ${is_wano}"

log "$tc_name: Configure uplink"
mac_eth0=$(mac_get "${eth}" | tr [A-Z] [a-z])
[ -z "${mac_eth0}" ] && raise "Ethernet MAC 0 empty" -arg
mac_eth1=$(printf "%02x:%s" $((0x${mac_eth0%%:*} | 0x2)) "${mac_eth0#*:}")
[ -z "${mac_eth1}" ] && raise "Ethernet MAC 1 empty" -arg

if [ "${is_wano}" == false ];then
    add_ovs_bridge "${wan}" "${mac_eth0}" "${wan_mtu}"
    add_bridge_port "${wan}" "${eth}"
    add_ovs_bridge "${lan}" "${mac_eth1}"
else
    add_ovs_bridge "${lan}" "${mac_eth0}" "${wan_mtu}"
    add_bridge_port "${lan}" "${eth}"
fi

remove_sta_interfaces &&
    log -deb "$tc_name: STA interfaces removed" ||
    raise "Failed to remove STA interfaces" -l "$tc_name" -ds

if [ "${is_wano}" == false ];then
    create_inet_entry2 \
        -if_name "${wan}" \
        -if_type "bridge" \
        -ip_assign_scheme "dhcp" \
        -upnp_mode "external" \
        -NAT true \
        -network true \
        -enabled true &&
        log -deb "$tc_name: Interface ${wan} successfully created" ||
        raise "Failed to create interface ${wan}" -l "$tc_name" -ds
    add_bridge_port "${wan}" "patch-w2h"
    set_interface_patch "${wan}" "patch-w2h" "patch-h2w"
    add_bridge_port "${lan}" "patch-h2w"
    set_interface_patch "${lan}" "patch-h2w" "patch-w2h"
else
    create_inet_entry2 \
        -if_name "${lan}" \
        -if_type "bridge" \
        -ip_assign_scheme "dhcp" \
        -upnp_mode "external" \
        -NAT true \
        -network true \
        -enabled true &&
        log -deb "$tc_name: Interface ${lan} successfully created" ||
        raise "Failed to create interface ${lan}" -l "$tc_name" -ds
fi

[ "${is_wano}" == true ] && udhcpc_if=${lan} || udhcpc_if=${wan}
wait_for_function_response 0 "check_pid_udhcp ${udhcpc_if}" 30 &&
    log -deb "UDHCPC running on ${udhcpc_if}" ||
    raise "UDHCPC not running on ${udhcpc_if}" -ds

wait_for_function_response 0 "ping -c${n_ping} ${upstream_ip}" 2 &&
    log -deb "Can ping router" ||
    raise "Can not ping router" -ds
wait_for_function_response 0 "ping -c${n_ping} ${internet_ip}" 2 &&
    log -deb "Can ping internet" ||
    log -deb "Can not ping internet"
