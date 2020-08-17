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


if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

this script is dependent on following:
    - running DM manager

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0") br-wan br-home 10.10.10.20 10.10.10.50
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

br_wan=${1:-br-wan}
br_home=${2:-br-home}
start_pool=${3:-10.10.10.20}
end_pool=${4:-10.10.10.50}

tc_name="onbrd/$(basename "$0")"

log "$tc_name: ONBRD Verify router mode settings applied"

# br-wan section
log "$tc_name: Check if interface is UP - $br_wan"
wait_for_function_response 0 "interface_is_up $br_wan" &&
    log "$tc_name: Interface is UP - $br_wan" ||
    raise "FAILED: Interface is DOWN, should be UP - $br_wan" -l "$tc_name" -tc

# Check if DHCP client is running on br-wan (wan bridge)
wait_for_function_response 0 "check_pid_udhcp $br_wan" &&
    log "lib/unit_lib: check_pid_udhcp - PID found, DHCP client running" ||
    raise "nit_lib: check_pid_udhcp - PID not found, , DHCP client NOT running" -l "$tc_name" -tc

log "$tc_name: Setting NAT to true"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$br_wan" -u NAT true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - NAT true" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - - NAT true" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$br_wan" -is NAT true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - NAT=true" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - NAT=true" -l "$tc_name" -tc

# br-home section
log "$tc_name: Setting DHCP range on $br_home"
enable_disable_dhcp_server "$br_home" "$start_pool" "$end_pool" ||
    raise "Cannot update DHCP settings inside CONFIG $br_wan" -l "$tc_name" -tc

log "$tc_name: Setting NAT to false"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$br_home" -u NAT false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - NAT false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - - NAT false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$br_home" -is NAT false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - NAT=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - NAT=false" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$br_home" -u ip_assign_scheme static &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=static" ||
    raise "update_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=static" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$br_home" -is ip_assign_scheme static &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=static" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=static" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$br_home" -is "dhcpc" "[\"map\",[]]" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - dhcpc [\"map\",[]]" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - dhcpc [\"map\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$br_home" -is "netmask" 0.0.0.0 &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - netmask 0.0.0.0" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - netmask 0.0.0.0" -l "$tc_name" -tc

# Restart managers to tidy up config
restart_managers

pass
