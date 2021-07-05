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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"

usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Validate device router mode settings
Arguments:
    -h  show this help message
    \$1 (wan_iface)       : Interface used for WAN uplink (WAN bridge or eth WAN) : (string)(required)
    \$2 (lan_bridge)      : Interface name of LAN bridge                          : (string)(required)
    \$3 (dhcp_start_pool) : Start of DHCP pool in Wifi_Inet_Config                : (string)(required)
    \$4 (dhcp_end_pool)   : End of DHCP pool in Wifi_Inet_Config                  : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <BR-WAN> <BR-HOME> <START-POOL> <END-POOL>
Script usage example:
   ./${tc_name} eth0 br-home 10.10.10.20 10.10.10.50
   ./${tc_name} br-wan br-home 10.10.10.20 10.10.10.50
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


check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "${tc_name}" -s

NARGS=4
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
wan_iface=${1}
lan_bridge=${2}
dhcp_start_pool=${3}
dhcp_end_pool=${4}

trap '
fut_info_dump_line
check_pid_udhcp $wan_iface
print_tables Wifi_Inet_State
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "$tc_name: ONBRD test - Verify router mode settings applied"

# WAN bridge section
log "$tc_name: Check if interface '$wan_iface' is UP"
wait_for_function_response 0 "get_eth_interface_is_up $wan_iface" &&
    log "$tc_name: Interface '$wan_iface' is UP - Success" ||
    raise "FAIL: Interface '$wan_iface' is DOWN, should be UP" -l "$tc_name" -ds

# Check if DHCP client is running on WAN bridge
log "$tc_name: Check if DHCP client is running on WAN bridge - '$wan_iface'"
wait_for_function_response 0 "check_pid_udhcp $wan_iface" &&
    log "$tc_name: check_pid_udhcp '$wan_iface' - PID found, DHCP client running - Success" ||
    raise "FAIL: check_pid_udhcp '$wan_iface' - PID not found, DHCP client is not running" -l "$tc_name" -tc

log "$tc_name: Setting Wifi_Inet_Config::NAT to true on '$wan_iface'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_iface" -u NAT true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'true' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'true'" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_iface" -is NAT true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'true'" -l "$tc_name" -tc

# LAN bridge section
log "$tc_name: Setting DHCP range on $lan_bridge to '$dhcp_start_pool' '$dhcp_end_pool'"
configure_dhcp_server_on_interface "$lan_bridge" "$dhcp_start_pool" "$dhcp_end_pool" &&
    log "$tc_name: configure_dhcp_server_on_interface - DHCP settings updated - Success" ||
    raise "FAIL: Cannot update DHCP settings inside CONFIG $wan_iface" -l "$tc_name" -tc

log "$tc_name: Setting Wifi_Inet_Config::NAT to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u NAT false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'false' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'false'" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is NAT false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'false' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'false'" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u ip_assign_scheme static &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'static' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme is not 'static'" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is ip_assign_scheme static &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'static' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'static'" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is "netmask" 0.0.0.0 &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::netmask is 0.0.0.0  - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::netmask is not 0.0.0.0" -l "$tc_name" -tc

print_tables Wifi_Inet_Config Wifi_Inet_State

# Restart managers to tidy up config
restart_managers

pass
