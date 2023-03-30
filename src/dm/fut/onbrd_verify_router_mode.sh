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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="onbrd/onbrd_setup.sh"

usage()
{
cat << usage_string
onbrd/onbrd_verify_router_mode.sh [-h] arguments
Description:
    - Validate device router mode settings
Arguments:
    -h  show this help message
    \$1 (wan_iface)         : Interface used for WAN uplink (WAN bridge or eth WAN) : (string)(required)
    \$2 (lan_bridge)        : Interface name of LAN bridge                          : (string)(required)
    \$3 (dhcp_start_pool)   : Start of DHCP pool in Wifi_Inet_Config                : (string)(required)
    \$4 (dhcp_end_pool)     : End of DHCP pool in Wifi_Inet_Config                  : (string)(required)
    \$5 (internal_inet_addr): IP address for LAN bridge interface                   : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_verify_router_mode.sh <WAN-IFACE> <BR-HOME> <START-POOL> <END-POOL> <INET-ADDR>
Script usage example:
   ./onbrd/onbrd_verify_router_mode.sh eth0 br-home 192.168.40.2 192.168.40.254 192.168.40.1
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


NARGS=5
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "onbrd/onbrd_verify_router_mode.sh" -arg
wan_iface=${1}
lan_bridge=${2}
dhcp_start_pool=${3}
dhcp_end_pool=${4}
internal_inet_addr=${5}
internal_upnp_mode=internal
external_upnp_mode=external
internal_netmask="255.255.255.0"

dhcpd='["map",[["dhcp_option","3,'${internal_inet_addr}';6,'${internal_inet_addr}'"],["force","false"],["lease_time","12h"],["start","'${dhcp_start_pool}'"],["stop","'${dhcp_end_pool}'"]]]'

trap '
fut_info_dump_line
check_pid_udhcp $wan_iface
print_tables Wifi_Inet_State
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "onbrd/onbrd_verify_router_mode.sh: ONBRD test - Verify router mode settings applied"
log "onbrd/onbrd_verify_router_mode.sh: Remove port '$wan_iface' from bridge '${lan_bridge}' if exists"
remove_bridge_port "${lan_bridge}" "${wan_iface}" &&
    log "onbrd/onbrd_verify_router_mode.sh: Port '${wan_iface}' removed from bridge '${lan_bridge}' or port did not exist!"

# WAN bridge section
log "onbrd/onbrd_verify_router_mode.sh: Check if interface '$wan_iface' is UP"
wait_for_function_response 0 "check_eth_interface_state_is_up $wan_iface" &&
    log "onbrd/onbrd_verify_router_mode.sh: Interface '$wan_iface' is UP - Success" ||
    raise "FAIL: Interface '$wan_iface' is DOWN, should be UP" -l "onbrd/onbrd_verify_router_mode.sh" -ds

# Check if DHCP client is running on WAN bridge
log "onbrd/onbrd_verify_router_mode.sh: Check if DHCP client is running on WAN bridge - '$wan_iface'"
wait_for_function_response 0 "check_pid_udhcp $wan_iface" &&
    log "onbrd/onbrd_verify_router_mode.sh: check_pid_udhcp '$wan_iface' - PID found, DHCP client running - Success" ||
    raise "FAIL: check_pid_udhcp '$wan_iface' - PID not found, DHCP client is not running" -l "onbrd/onbrd_verify_router_mode.sh" -tc

log "onbrd/onbrd_verify_router_mode.sh: Setting Wifi_Inet_Config::NAT to true on '$wan_iface'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_iface" -u NAT true &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'true' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'true'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_iface" -is NAT true &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'true'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

log "onbrd/onbrd_verify_router_mode.sh: Setting Wifi_Inet_Config::upnp_mode to '$external_upnp_mode' on '$wan_iface'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_iface" -u upnp_mode $external_upnp_mode &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is '$external_upnp_mode' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not '$external_upnp_mode'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_iface" -is upnp_mode $external_upnp_mode &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is '$external_upnp_mode' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not '$external_upnp_mode'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

# LAN bridge section
log "onbrd/onbrd_verify_router_mode.sh: Setting Wifi_Inet_Config::network and Wifi_Inet_Config::enabled to TRUE on $lan_bridge"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u network true -u enabled true &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::network and Wifi_Inet_Config::enabled updated - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::network and Wifi_Inet_Config::enabled" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is network true -is enabled true &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_State::network and Wifi_Inet_State::enabled set TRUE - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to update Wifi_Inet_State::network and Wifi_Inet_State::enabled" -l "onbrd/onbrd_verify_router_mode.sh" -tc

log "onbrd/onbrd_verify_router_mode.sh: Setting DHCP range on $lan_bridge to '$dhcp_start_pool' '$dhcp_end_pool'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u dhcpd '${dhcpd}' &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::dhcpd is updated - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::dhcpd" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is dhcpd '${dhcpd}' &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::dhcpd is updated - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::dhcpd" -l "onbrd/onbrd_verify_router_mode.sh" -tc

log "onbrd/onbrd_verify_router_mode.sh: Setting Wifi_Inet_Config::NAT to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u NAT false &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::NAT is 'false' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'false'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is NAT false &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'false' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'false'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

log "onbrd/onbrd_verify_router_mode.sh: Setting Wifi_Inet_Config::upnp_mode to '$internal_upnp_mode' on '$lan_bridge'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u upnp_mode $internal_upnp_mode &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is '$internal_upnp_mode' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not '$internal_upnp_mode'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is upnp_mode $internal_upnp_mode &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is '$internal_upnp_mode' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not '$internal_upnp_mode'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u ip_assign_scheme static &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'static' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme is not 'static'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is ip_assign_scheme static &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'static' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'static'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u inet_addr ${internal_inet_addr} -u netmask ${internal_netmask} &&
    log "onbrd/onbrd_verify_router_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::inet_addr and Wifi_Inet_Config::netmask updated - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::inet_addr and Wifi_Inet_Config::netmask'" -l "onbrd/onbrd_verify_router_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is inet_addr ${internal_inet_addr} -is netmask ${internal_netmask} &&
    log "onbrd/onbrd_verify_router_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::inet_addr and Wifi_Inet_State::netmask' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::inet_addr and Wifi_Inet_State::netmask'" -l "onbrd/onbrd_verify_router_mode.sh" -tc

print_tables Wifi_Inet_Config Wifi_Inet_State

# Restart managers to tidy up config
restart_managers

pass
