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


# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
onbrd/onbrd_verify_wan_iface_mac_addr.sh [-h] arguments
Description:
    - Verify if WAN interface in Wifi_Inet_State has MAC address matching the system.
      If script is used without parameter it is for for WANO enabled devices and would
      determine the WAN interface name by itself by looking into Connection_Manager_Uplink table.
      If used with the parameter it is for no WANO enabled devices and it uses the
      provided parameter.
Arguments:
    -h  show this help message
    \$1 (if_name) : used as WAN interface name to check : (string)(optional)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_verify_wan_iface_mac_addr.sh
    or (no WANO)
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_verify_wan_iface_mac_addr.sh [<WAN_INTERFACE>]
Script usage example:
   ./onbrd/onbrd_verify_wan_iface_mac_addr.sh
   ./onbrd/onbrd_verify_wan_iface_mac_addr.sh eth0
usage_string
}
if [ -n "${1}" ]; then
    case "${1}" in
        help | \
        --help | \
        -h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
fi

trap '
fut_info_dump_line
get_radio_mac_from_system "$wan_interface"
print_tables Connection_Manager_Uplink Wifi_Inet_State
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "onbrd/onbrd_verify_wan_iface_mac_addr.sh: ONBRD test - Verify if WAN interface in Wifi_Inet_State has MAC address matching the system"

NARGS=1
if [ $# -eq 0 ]; then
    print_tables Connection_Manager_Uplink
    wan_interface=$(get_wan_uplink_interface_name)
    if [ -z "$wan_interface" ]; then
       raise "FAIL: Could not auto-determine WAN interface from Connection_Manager_Uplink" -l "onbrd/onbrd_verify_wan_iface_mac_addr.sh" -tc
    fi
elif [ $# -eq ${NARGS} ]; then
    wan_interface=${1}
elif [ $# -gt ${NARGS} ]; then
    usage
    raise "Requires at most '${NARGS}' input argument(s)" -l "onbrd/onbrd_verify_wan_iface_mac_addr.sh" -arg
fi

# shellcheck disable=SC2060
mac_address=$(get_radio_mac_from_system "$wan_interface" | tr [A-Z] [a-z])
if [ -z "$mac_address" ]; then
    raise "FAIL: Could not determine MAC for WAN interface '$wan_interface' from system" -l "onbrd/onbrd_verify_wan_iface_mac_addr.sh" -tc
fi

log "onbrd/onbrd_verify_wan_iface_mac_addr.sh: Verify used WAN interface '$wan_interface' MAC address equals '$mac_address'"
wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is hwaddr "$mac_address" &&
    log "onbrd/onbrd_verify_wan_iface_mac_addr.sh: wait_ovsdb_entry - Wifi_Inet_State '$wan_interface' hwaddr is equal to '$mac_address' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Wifi_Inet_State '$wan_interface' hwaddr is NOT equal to '$mac_address'" -l "onbrd/onbrd_verify_wan_iface_mac_addr.sh" -tc

pass
