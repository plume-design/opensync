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

manager_setup_file="nm2/nm2_setup.sh"

usage()
{
cat << usage_string
nm2/nm2_set_upnp_mode.sh [-h] arguments
Description:
    - Script configures interfaces upnp through Wifi_inet_Config 'upnp_mode' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise

Arguments:
    -h  show this help message
    \$1 (wan_iface)       : Interface used for WAN uplink (WAN bridge or eth WAN) : (string)(required)
    \$2 (lan_bridge)      : Interface name of LAN bridge                          : (string)(required)
    \$3 (lan_ip_addr)     : IP address to be assigned on LAN interface            : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_set_upnp_mode.sh <WAN_IFACE> <BR-HOME> <IP-ADDR>
Script usage example:
   ./nm2/nm2_set_upnp_mode.sh eth0 br-home 10.10.10.30
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


NARGS=3
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "nm2/nm2_set_upnp_mode.sh" -arg
wan_iface=${1}
lan_bridge=${2}
lan_ip_addr=${3}


trap '
fut_info_dump_line
check_pid_udhcp $wan_iface
print_tables Wifi_Inet_State
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_set_upnp_mode.sh: NM2 test - Setting UPnP mode"

# WAN bridge section
log "nm2/nm2_set_upnp_mode.sh: Setting Wifi_Inet_Config::ip_assign_scheme to dhcp on '$wan_iface'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_iface" -u ip_assign_scheme dhcp &&
    log "nm2/nm2_set_upnp_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'dhcp' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme is not 'dhcp'" -l "nm2/nm2_set_upnp_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_iface" -is ip_assign_scheme dhcp &&
    log "nm2/nm2_set_upnp_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'dhcp' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'dhcp'" -l "nm2/nm2_set_upnp_mode.sh" -tc

log "nm2/nm2_set_upnp_mode.sh: Setting Wifi_Inet_Config::upnp_mode to external on '$wan_iface'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_iface" -u upnp_mode "external" &&
    log "nm2/nm2_set_upnp_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is 'external' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not 'external'" -l "nm2/nm2_set_upnp_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_iface" -is upnp_mode external &&
    log "nm2/nm2_set_upnp_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'external' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'external'" -l "nm2/nm2_set_upnp_mode.sh" -tc

# LAN bridge section
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u inet_addr ${lan_ip_addr} &&
    log "nm2/nm2_set_upnp_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::inet_addr is '${lan_ip_addr}' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::inet_addr is not '${lan_ip_addr}'" -l "nm2/nm2_set_upnp_mode.sh" -oe

update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u netmask 255.255.255.0 &&
    log "nm2/nm2_set_upnp_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::netmask is '255.255.255.0' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::netmask is not '255.255.255.0'" -l "nm2/nm2_set_upnp_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is inet_addr ${lan_ip_addr} &&
    log "nm2/nm2_set_upnp_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::inet_addr is ${lan_ip_addr}  - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::inet_addr is not ${lan_ip_addr}" -l "nm2/nm2_set_upnp_mode.sh" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is netmask 255.255.255.0 &&
    log "nm2/nm2_set_upnp_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::netmask is 255.255.255.0  - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::netmask is not 255.255.255.0" -l "nm2/nm2_set_upnp_mode.sh" -tc

log "nm2/nm2_set_upnp_mode.sh: Setting Wifi_Inet_Config::upnp_mode to internal on '$lan_bridge'"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$lan_bridge" -u upnp_mode "internal" &&
    log "nm2/nm2_set_upnp_mode.sh: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is 'internal' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not 'internal'" -l "nm2/nm2_set_upnp_mode.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lan_bridge" -is upnp_mode internal &&
    log "nm2/nm2_set_upnp_mode.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'internal' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'internal'" -l "nm2/nm2_set_upnp_mode.sh" -tc

print_tables Wifi_Inet_Config Wifi_Inet_State

pass
