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
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2 \$3

where options are:
    -h  show this help message

where arguments are:
    if_name=\$1 -- used as if_name in Wifi_Inet_Config table - (string)(required)
    if_type=\$2 -- used as if_type in Wifi_Inet_Config table - default 'vi' - (string)(optional)
    inet_addr=\$3 -- used as inet_addr in Wifi_Inet_Config table - default '10.10.10.20' - (integer)(optional)

this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_set_inet_addr.sh eth0 eth 10.10.10.15
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 2 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1
if_type=$2
inet_addr=$3

tc_name="nm2/$(basename "$0")"

log "$tc_name: Creating Wifi_Inet_Config entries for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -netmask "255.255.255.0" \
    -ip_assign_scheme static \
    -if_type "$if_type" \
    -inet_addr "$inet_addr" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Setting INET_ADDR to $inet_addr"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u inet_addr "$inet_addr" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - inet_addr $inet_addr" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - inet_addr $inet_addr" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is inet_addr "$inet_addr" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - inet_addr $inet_addr" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - inet_addr $inet_addr" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if INET_ADDR was properly applied to $if_name"
wait_for_function_response 0 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: INET_ADDR applied to ifconfig - interface $if_name" ||
    raise "Failed to apply INET_ADDR to ifconfig - interface $if_name" -l "$tc_name" -tc

log "$tc_name: Removing INET_ADDR to $inet_addr"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u inet_addr "[\"set\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - inet_addr [\"set\",[]]" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - inet_addr [\"set\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is inet_addr "0.0.0.0" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - inet_addr 0.0.0.0" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - inet_addr 0.0.0.0" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if INET_ADDR was properly removed from $if_name"
wait_for_function_response 1 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
    log "$tc_name: INET_ADDR removed from ifconfig - interface $if_name" ||
    raise "Failed to removed INET_ADDR from ifconfig - interface $if_name" -l "$tc_name" -tc

pass
