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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    reset_inet_entry $if_name || true
    [ "$if_type" == "vif" ] && vif_clean
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

tc_name="nm2/$(basename "$0")"
usage()
{
cat << EOF
${tc_name} [-h] if_name [if_type] [start_pool] [end_pool]
Options:
    -h  show this help message
Arguments:
    if_name=$1 -- field if_name in Wifi_Inet_Config table - (string)(required)
    if_type=$2 -- field if_type in Wifi_Inet_Config table - (string)(optional, default: vif)
    start_pool=$3 -- value start_pool in field dhcpd in Wifi_Inet_Config - (string)(optional, default: 10.10.10.20)
    end_pool=$4 -- value end_pool in field dhcpd in Wifi_Inet_Config - (string)(optional, default: 10.10.10.50)
Dependencies:
    NM manager, WM manager
Example:
    ${tc_name} wifi0 vif 10.10.10.16 10.10.10.32
EOF
exit 1
}

while getopts h option; do
    case "$option" in
        h)
            usage
            ;;
    esac
done

NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1
if_type=${2:-"vif"}
start_pool=${3:-"10.10.10.20"}
end_pool=${4:-"10.10.10.50"}

log_title "${tc_name}: Testing table Wifi_Inet_Config field dhcpd"

log "${tc_name}: Creating Wifi_Inet_Config entries for $if_name"
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
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Check if interface is UP - $if_name"
wait_for_function_response 0 "interface_is_up $if_name " &&
    log "$tc_name: Interface is UP - $if_name" ||
    raise "FAILED: Interface is DOWN, should be UP - $if_name" -l "$tc_name" -tc

log "$tc_name: Setting DHCP range on $if_name"
enable_disable_dhcp_server "$if_name" "$start_pool" "$end_pool" ||
    raise "Cannot update DHCP settings inside CONFIG $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if settings were applied to the DHCP server #1"
wait_for_function_response 0 "wait_for_dnsmasq $if_name $start_pool $end_pool" &&
    log "$tc_name: DNSMASQ DHCP configuration VALID (present) - $if_name" ||
    raise "DNSMASQ DHCP configuration NOT VALID (not present) - $if_name" -l "$tc_name" -tc

log "$tc_name: Checking if DHCP server configuration is removed when the interface is DOWN - $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - $if_name" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - $if_name" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - $if_name" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking DHCP configuration"
wait_for_function_response 1 "wait_for_dnsmasq $if_name $start_pool $end_pool" &&
    log "$tc_name: DNSMASQ DHCP configuration VALID (not present) - $if_name" ||
    raise "DNSMASQ DHCP configuration NOT VALID (still present) - $if_name" -l "$tc_name" -tc

log "$tc_name: Checking if DHCP server configuration is re-applied when the interface is UP - $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u enabled true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - $if_name" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - $if_name" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is enabled true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - $if_name" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking DHCP configuration"
wait_for_function_response 0 "wait_for_dnsmasq $if_name $start_pool $end_pool" &&
    log "$tc_name: DNSMASQ DHCP configuration VALID (present) - $if_name" ||
    raise "DNSMASQ DHCP configuration NOT VALID (not present) - $if_name" -l "$tc_name" -tc

log "$tc_name: Switching interface to DHCP client"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u "ip_assign_scheme" "dhcp" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - $if_name" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - $if_name" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is "ip_assign_scheme" "dhcp" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - $if_name" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if DHCP client is alive - $if_name"
wait_for_function_response 0 "check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"" &&
    log "$tc_name: DHCP client process ACTIVE - $if_name" ||
    raise "DHCP client process NOT ACTIVE - $if_name" -l "$tc_name" -tc

log "$tc_name: Disabling DHCP client - $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
    -u "dhcpd" "[\"map\",[]]" \
    -u "ip_assign_scheme" "static" &&
        log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - $if_name" ||
        raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - $if_name" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
    -is "dhcpd" "[\"map\",[]]" \
    -is "ip_assign_scheme" "static" &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - $if_name" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if settings were removed from the DHCP server - $if_name"
wait_for_function_response 1 "wait_for_dnsmasq $if_name $start_pool $end_pool" &&
    log "$tc_name: DNSMASQ DHCP configuration VALID (not present) #2 - $if_name" ||
    raise "DNSMASQ DHCP configuration NOT VALID (still present) #2 - $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if DHCP client is dead - $if_name"
wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
    log "$tc_name: DHCP client process NOT ACTIVE - $if_name" ||
    raise "DHCP client process ACTIVE - $if_name" -l "$tc_name" -tc

pass

