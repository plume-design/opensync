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

manager_setup_file="nm2/nm2_setup.sh"
create_radio_vif_file="tools/device/create_radio_vif_interface.sh"
if_type_default="vif"
start_pool_default="10.10.10.20"
end_pool_default="10.10.10.50"
usage()
{
cat << usage_string
nm2/nm2_ovsdb_configure_interface_dhcpd.sh [-h] arguments
Description:
    - Script configures dhcpd on the interface through Wifi_Inet_Config table
        Script passes if Wifi_Inet_State table properly reflects changes from Wifi_Inet_Config table and dhcpd process on
        the system starts and stop when configured from Wifi_Inet_Config table
Arguments:
    -h  show this help message
    \$1 (if_name)    : field if_name in Wifi_Inet_Config table             : (string)(required)
    \$2 (if_type)    : field if_type in Wifi_Inet_Config table             : (string)(optional) : (default:${if_type_default})
    \$3 (start_pool) : value start_pool in field dhcpd in Wifi_Inet_Config : (string)(optional) : (default:${start_pool_default})
    \$4 (end_pool)   : value end_pool in field dhcpd in Wifi_Inet_Config   : (string)(optional) : (default:${end_pool_default})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
          In case of if_type==eth:
              Run: ./nm2/nm2_ovsdb_configure_interface_dhcpd.sh <IF-NAME> <IF-TYPE> <START-POOL> <END-POOL>
          In case of if_type==vif:
               Create radio-vif interface (see ${create_radio_vif_file} -h)
              Run: ./nm2/nm2_ovsdb_configure_interface_dhcpd.sh <IF-NAME> <IF-TYPE> <START-POOL> <END-POOL>
Script usage example:
   ./nm2/nm2_ovsdb_configure_interface_dhcpd.sh wifi0 ${if_type_default} ${start_pool_default} ${end_pool_default}
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

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -arg
if_name=$1
if_type=${2:-${if_type_default}}
start_pool=${3:-${start_pool_default}}
end_pool=${4:-${end_pool_default}}

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    reset_inet_entry $if_name || true
    [ "$if_type" == "vif" ] && vif_reset
    check_restore_management_access || true
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: NM2 test - Testing table Wifi_Inet_Config field dhcpd"

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -if_type "$if_type" \
    -inet_addr 10.10.10.10 \
    -netmask 255.255.255.0 \
    -dns "[\"map\",[]]" \
    -gateway 10.10.10.254 &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -ds

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Check if interface is UP - $if_name"
get_if_fn_type="check_${if_type}_interface_state_is_up"
wait_for_function_response 0 "$get_if_fn_type $if_name " &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Interface $if_name is UP - Success" ||
    raise "FAIL: Interface $if_name is DOWN, should be UP" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -ds

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Setting DHCP range on $if_name"
configure_dhcp_server_on_interface "$if_name" "$start_pool" "$end_pool" ||
    raise "FAIL: Cannot update DHCP settings on $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: LEVEL2 - Checking if settings were applied to the DHCP server #1"
    wait_for_function_response 0 "check_dhcp_from_dnsmasq_conf $if_name $start_pool $end_pool" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DNSMASQ DHCP configuration VALID (present) for $if_name - Success" ||
        raise "FAIL: DNSMASQ DHCP configuration NOT VALID (not present) for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc
fi

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Checking if DHCP server configuration is removed when the interface is DOWN - $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: update_ovsdb_entry - Wifi_Inet_Config::enabled is false for $if_name - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::enabled is not false for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::enabled is false for $if_name - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::enabled is not false for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: LEVEL2 - Checking DHCP configuration for $if_name"
    wait_for_function_response 1 "check_dhcp_from_dnsmasq_conf $if_name $start_pool $end_pool" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DNSMASQ DHCP configuration VALID (not present) for $if_name - Success" ||
        raise "FAIL: DNSMASQ DHCP configuration NOT VALID (still present) for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc
fi

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Checking if DHCP server configuration is re-applied when the interface is UP for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: update_ovsdb_entry - Wifi_Inet_Config::enabled is true for $if_name - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::enabled is not true for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State for $if_name - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Check if interface is UP - $if_name"
    get_if_fn_type="check_${if_type}_interface_state_is_up"
    wait_for_function_response 0 "$get_if_fn_type $if_name " &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Interface $if_name is UP - Success" ||
        raise "FAIL: Interface $if_name is DOWN, should be UP" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -ds

    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: LEVEL2 - Checking DHCP configuration for $if_name"
    wait_for_function_response 0 "check_dhcp_from_dnsmasq_conf $if_name $start_pool $end_pool" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DNSMASQ DHCP configuration VALID (present) for $if_name - Success" ||
        raise "FAIL: DNSMASQ DHCP configuration NOT VALID (not present) - $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc
fi

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Switching interface to DHCP client for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u "ip_assign_scheme" "dhcp" &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: update_ovsdb_entry - Wifi_Inet_Config table updated for $if_name - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config - $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is "ip_assign_scheme" "dhcp" &&
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State for $if_name - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Checking if DHCP client is alive on $if_name - LEVEL2"
    wait_for_function_response 0 "check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DHCP client process ACTIVE for $if_name - Success" ||
        raise "FAIL: DHCP client process NOT ACTIVE for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc
fi

log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Disabling DHCP client for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
    -u "dhcpd" "[\"map\",[]]" \
    -u "ip_assign_scheme" "static" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: update_ovsdb_entry - Wifi_Inet_Config table updated for $if_name - Success" ||
        raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
    -is "dhcpd" "[\"map\",[]]" \
    -is "ip_assign_scheme" "static" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State for $if_name - Success" ||
        raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Checking if settings were removed from the DHCP server for $if_name - LEVEL2"
    wait_for_function_response 1 "check_dhcp_from_dnsmasq_conf $if_name $start_pool $end_pool" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DNSMASQ DHCP configuration VALID (not present) #2 for $if_name - Success" ||
        raise "FAIL: DNSMASQ DHCP configuration NOT VALID (still present) #2 for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc

    log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: Checking if DHCP client is dead on $if_name - LEVEL2"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "nm2/nm2_ovsdb_configure_interface_dhcpd.sh: DHCP client process NOT ACTIVE for $if_name - Success" ||
        raise "FAIL: DHCP client process ACTIVE for $if_name" -l "nm2/nm2_ovsdb_configure_interface_dhcpd.sh" -tc
fi

pass
