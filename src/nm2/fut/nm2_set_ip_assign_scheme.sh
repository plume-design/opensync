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
$(basename "$0") [-h] \$1 \$2 \$3 \$4

where options are:
    -h  show this help message

where arguments are:
    if_name=\$1 -- used as if_name in Wifi_Inet_Config table - (string)(required)
    if_type=\$2 -- used as if_type in Wifi_Inet_Config table - default 'vif'- (string)(optional)
    ip_assign_scheme=\$3 -- used as ip_assign_scheme in Wifi_Inet_Config table - default 'static' - (string)(optional)
    inet_addr=\$3 -- used as inet_addr column in Wifi_Inet_Config/State table - (string IP address)(optional)

this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_set_ip_assign_scheme.sh wifi0 vif static 10.10.10.100
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 1 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1
if_type=$2
ip_assign_scheme=$3
inet_addr=${4:-10.10.10.30}

tc_name="nm2/$(basename "$0")"

log "$tc_name: Remove running clients first"
rm "/var/run/udhcpc-$if_name.pid" || log "Nothing to remove"
rm "/var/run/udhcpc_$if_name.opts" || log "Nothing to remove"

log "$tc_name: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -netmask "255.255.255.0" \
    -inet_addr "$inet_addr" \
    -ip_assign_scheme static \
    -if_type "$if_type" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

if [ "$ip_assign_scheme" = "dhcp" ]; then
    log "$tc_name: Setting dhcp for $if_name to dhcp"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme dhcp &&
        log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=dhcp" ||
        raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=dhcp" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme dhcp &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=dhcp" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=dhcp" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is alive"
    wait_for_function_response 0 "check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process ACTIVE - interface $if_name" ||
        raise "DHCP client process NOT ACTIVE - interface $if_name" -l "$tc_name" -tc

    log "$tc_name: Setting dhcp for $if_name to none"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme none &&
        log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=none" ||
        raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=none" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme none &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is dead"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process NOT ACTIVE" ||
        raise "DHCP client process ACTIVE" -l "$tc_name" -tc

elif [ "$ip_assign_scheme" = "static" ]; then
    log "$tc_name: Setting ip_assign_scheme for $if_name to static"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u ip_assign_scheme static \
        -u inet_addr "$inet_addr" &&
            log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=static" ||
            raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=static" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
        -is ip_assign_scheme static \
        -is inet_addr "$inet_addr" &&
            log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=static" ||
            raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=static" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2: Checking if settings are applied to ifconfig"
    wait_for_function_response 0 "interface_ip_address $if_name | grep -q \"$inet_addr\"" &&
        log "$tc_name: Settings applied to ifconfig - interface $if_name" ||
        raise "Failed to apply settings to ifconfig - interface $if_name" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is DEAD"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process is DEAD - interface $if_name" ||
        raise "DHCP client process is NOT DEAD - interface $if_name" -l "$tc_name" -tc
else
    raise "Wrong IP_ASSIGN_SCHEME parameter - $ip_assign_scheme" -l "$tc_name" -tc
fi

pass
