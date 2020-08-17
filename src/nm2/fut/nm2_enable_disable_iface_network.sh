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


# TEST DESCRIPTION
# Try to enable and disable existing interface.
#
# TEST PROCEDURE
# - Configure interface with selected parameters
# - interface IP address is 10.10.10.30, used to check test fail/pass criteria
#
# EXPECTED RESULTS
# Test is passed:
# - if interface is properly configured AND
# - if interface is disabled and if its table is empty (interface_ip_address is checked) AND
# - if interface is re-enabled and if its table is populated (interface_ip_address is checked)
# Test fails:
# - if inteface cannot be disabled OR
# - if disabled interface table is not empty (interface_ip_address is checked)

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

# Fill variables with provided arguments or defaults.
trap '
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2

where options are:
    -h  show this help message

where arguments are:
    if_name=\$1 -- used as if_name in Wifi_Inet_Config table - (string)(required)
    if_type=\$2 -- used as if_type in Wifi_Inet_Config table - default 'vif'- (string)(optional)

this script is dependent on following:
    - running NM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_enable_disable_iface_network.sh eth0 eth
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Provide at least 1 argument(s).
if [ $# -lt 1 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1
if_type=$2
inet_addr=10.10.10.30

tc_name="nm2/$(basename "$0")"


log "$tc_name: Creating Wifi_Inet_Config entries for: $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -if_type "$if_type" \
    -ip_assign_scheme static \
    -netmask "255.255.255.0" \
    -inet_addr "$inet_addr" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS $inet_addr was properly applied to $if_name"
wait_for_function_response 0 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Setting applied to ifconfig - IP: $inet_addr" ||
    raise "Failed to apply settings to ifconfig - IP: $inet_addr" -l "$tc_name" -tc

log "$tc_name: Disabling network, Wifi_Inet_Config network=false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u network false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - network=false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - network=false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is network false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - network=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - network=false" -l "$tc_name" -tc

log "$tc_name: Checking if all network settings on interface are empty"
wait_for_function_response 1 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Setting removed from ifconfig" ||
    raise "Failed to remove settings to ifconfig" -l "$tc_name" -tc

log "$tc_name: Re-enabling network, Wifi_Inet_Config network=true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u network true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - network=false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - network=false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is network true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - network=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - network=false" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS $inet_addr was properly applied to $if_name"
wait_for_function_response 0 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: Setting applied to ifconfig - IP: $inet_addr" ||
    raise "Failed to apply settings to ifconfig - IP: $inet_addr" -l "$tc_name" -tc

pass
