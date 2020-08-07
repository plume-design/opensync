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
    reset_inet_entry $internal_if
    reset_inet_entry $external_if
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2

where options are:
    -h  show this help message

where arguments are:
    internal_if=\$1 -- used to set internal_if in Wifi_Inet_Config table as internal UPnP interface - (string)(required)
    external_if=\$2 -- used to set external_if in Wifi_Inet_Config table as external UPnP interface - (string)(required)


this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_set_upnp_mode.sh eth0 br-wan
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

internal_if=$1
external_if=$2

tc_name="nm2/$(basename "$0")"

log "$tc_name: Creating Wifi_Inet_Config entries for $internal_if"
create_inet_entry \
    -if_name "$internal_if" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -netmask 255.255.255.0 \
    -inet_addr 10.10.10.30 &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Setting UPNP_MODE internal on $internal_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$internal_if" -u upnp_mode internal &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - upnp_mode=internal" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - upnp_mode=internal" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$internal_if" -is upnp_mode internal &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - upnp_mode=internal" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - upnp_mode=internal" -l "$tc_name" -tc

log "$tc_name: Setting UPNP_MODE external on $external_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$external_if" -u upnp_mode external &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - upnp_mode=external" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - upnp_mode=external" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$external_if" -is upnp_mode external &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - upnp_mode=external" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - upnp_mode=external" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 checks"
wait_for_function_response 0 "check_upnp_conf $internal_if $external_if" &&
    log "$tc_name: LEVEL 2: UPNP applied to OS" ||
    raise "LEVEL 2: Failed to apply UPNP to OS" -l "$tc_name" -tc

log "$tc_name: Disabling UPNP_MODE on $internal_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$internal_if" -u upnp_mode "[\"set\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - upnp_mode=[\"set\",[]]" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - upnp_mode=[\"set\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$internal_if" -is upnp_mode "disabled" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - upnp_mode=disabled" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - upnp_mode=disabled" -l "$tc_name" -tc

log "$tc_name: Disabling UPNP_MODE on $external_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$external_if" -u upnp_mode "[\"set\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - upnp_mode=[\"set\",[]]" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - upnp_mode=[\"set\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$external_if" -is upnp_mode "disabled" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - upnp_mode=disabled" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - upnp_mode=disabled" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 checks"
wait_for_function_response 1 "check_upnp_conf $internal_if $external_if" &&
    log "$tc_name: LEVEL 2: UPNP removed from OS" ||
    raise "LEVEL 2: Failed to remove UPNP remove OS" -l "$tc_name" -tc

pass
