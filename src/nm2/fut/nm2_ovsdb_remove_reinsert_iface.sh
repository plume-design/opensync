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
inet_addr_default="10.10.10.30"

usage()
{
cat << usage_string
nm2/nm2_ovsdb_remove_reinsert_iface.sh [-h] arguments
Description:
    - Script enables and disables interface through Wifi_Inet_Config table
        Script fails if the interface is not created in Wifi_Inet_State table or it is not present on the system with given
        configuration
Arguments:
    -h  show this help message
    \$1 (if_name)   : used as if_name in Wifi_Inet_Config table   : (string)(required)
    \$2 (if_type)   : used as if_type in Wifi_Inet_Config table   : (string)(required)
    \$3 (inet_addr) : used as inet_addr in Wifi_Inet_Config table : (string)(optional) : (default:${inet_addr_default})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_ovsdb_remove_reinsert_iface.sh <IF-NAME> <IF-TYPE>
Script usage example:
   ./nm2/nm2_ovsdb_remove_reinsert_iface.sh eth0.100 vlan
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

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -arg
if_name=$1
if_type=$2
inet_addr=${3:-${inet_addr_default}}

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    reset_inet_entry $if_name || true
    check_restore_management_access || true
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_ovsdb_remove_reinsert_iface.sh: NM2 test - Remove reinsert interface"

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Creating $if_type interface"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -if_type "$if_type" \
    -netmask "255.255.255.0" \
    -inet_addr "$inet_addr" &&
        log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -ds

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Check if IP ADDRESS: $inet_addr was properly applied to $if_name (ENABLED #1) - LEVEL2"
wait_for_function_response 0 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Settings applied to ifconfig (ENABLED #1) for $if_name - Success" ||
    raise "FAIL: Failed to apply settings to ifconfig (ENABLED #1) for $if_name" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Setting ENABLED to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: update_ovsdb_entry - Wifi_Inet_Config::enabled is 'false' - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Inet_Config::enabled is not 'false'" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::enabled is 'false' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::enabled is not 'false'" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Check if IP ADDRESS: $inet_addr was properly removed from $if_name (DISABLED #1) - LEVEL2"
wait_for_function_response 1 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Settings removed from ifconfig (DISABLED #1) for $if_name - Success" ||
    raise "FAIL: Failed to remove settings from ifconfig (DISABLED #1) for $if_name" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Setting ENABLED to true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=true - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=true" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=true - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=true" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: LEVEL 2 - Check if IP ADDRESS: $inet_addr was properly applied to $if_name (ENABLED #2)"
wait_for_function_response 0 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Settings applied to ifconfig (ENABLED #2) - $if_name - Success" ||
    raise "FAIL: Failed to apply settings to ifconfig (ENABLED #2) - $if_name" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Setting ENABLED to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: update_ovsdb_entry - Wifi_Inet_Config table updated - enabled=false - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config - enabled=false" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - enabled=false - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - enabled=false" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Check if IP address: $inet_addr was properly removed from $if_name (DISABLED #2) - LEVEL2"
wait_for_function_response 1 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Settings removed from ifconfig (DISABLED #2) for $if_name - Success" ||
    raise "FAIL: Failed to remove settings from ifconfig (DISABLED #2) for $if_name" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Setting ENABLED to true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: update_ovsdb_entry - Wifi_Inet_Config::enabled is 'true' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::enabled is not 'true'" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::enabled is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::enabled is not true" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: Check if IP address was properly applied to $if_name (ENABLED #1) - LEVEL2"
wait_for_function_response 0 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
    log "nm2/nm2_ovsdb_remove_reinsert_iface.sh: LEVEL2 - Settings applied to ifconfig (ENABLED #3) - enabled is 'true' - Success" ||
    raise "FAIL: Failed to apply settings to ifconfig (ENABLED #3) - enabled is not 'true'" -l "nm2/nm2_ovsdb_remove_reinsert_iface.sh" -tc

pass
