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
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

this script is dependent on following:
    - running DM manager

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0")
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Fill variables with provided arguments or defaults.
wan_interface=${1:-br-wan}
wan_ip=${2:-192.168.200.10}
home_interface=${3:-br-home}
patch_w2h=${5:-patch-w2h}
patch_h2w=${6:-patch-h2w}

tc_name="onbrd/$(basename "$0")"
log "$tc_name: ONBRD Verify bridge mode settings applied"

# br-wan section
# Check if DHCP client is running on br-wan (wan bridge)
wait_for_function_response 0 "check_pid_udhcp $wan_interface" &&
    log "$tc_name: check_pid_udhcp - PID found, DHCP client running" ||
    raise "check_pid_udhcp - PID not found, DHCP client NOT running" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_interface" -u NAT true &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - NAT=true" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - NAT=true" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is NAT true &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - NAT=true" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - NAT=true" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$wan_interface" -u ip_assign_scheme dhcp &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=dhcp" ||
    raise "update_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=dhcp" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is ip_assign_scheme dhcp &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=dhcp" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=dhcp" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is inet_addr "$wan_ip" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - inet_addr is private" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - inet_addr is NOT private" -l "$tc_name" -tc

# br-home section
update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u NAT false &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - NAT=false" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - NAT=false" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is NAT false &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - NAT=false" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - NAT=false" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u ip_assign_scheme none &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=none" ||
    raise "update_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=none" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is ip_assign_scheme none &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=none" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=none" -l "$tc_name" -tc

update_ovsdb_entry Wifi_Inet_Config -w if_name "$home_interface" -u "dhcpd" "[\"map\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - dhcpd=[\"map\",[]]" ||
    raise "update_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - dhcpd=[\"map\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is "dhcpd" "[\"map\",[]]" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - dhcpd [\"map\",[]]" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - dhcpd [\"map\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is "netmask" "0.0.0.0" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - netmask 0.0.0.0" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - netmask 0.0.0.0" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$home_interface" -is inet_addr "0.0.0.0" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - inet_addr is 0.0.0.0" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - inet_addr is NOT 0.0.0.0" -l "$tc_name" -tc

# Creating patch interface
log "$tc_name: create_patch_interface - creating patch $patch_w2h $patch_h2w"
create_patch_interface "$wan_interface" "$patch_w2h" "$patch_h2w" &&
    log "$tc_name: check_patch_if_exists - patch interface $patch_w2h created" ||
    raise "check_patch_if_exists - Failed to create patch interface $patch_w2h" -l "$tc_name" -tc

wait_for_function_response 0 "check_if_patch_exists $patch_w2h" &&
    log "$tc_name: check_patch_if_exists - patch interface $patch_w2h exists" ||
    raise "check_patch_if_exists - patch interface $patch_w2h does not exists" -l "$tc_name" -tc

wait_for_function_response 0 "check_if_patch_exists $patch_h2w" &&
    log "$tc_name: check_patch_if_exists - patch interface $patch_h2w exists" ||
    raise "check_patch_if_exists - patch interface $patch_h2w does not exists" -l "$tc_name" -tc

# Restart managers to tidy up config
restart_managers

pass
