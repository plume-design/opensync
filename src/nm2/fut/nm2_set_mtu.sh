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
# Try to configure MTU to existing interface.
#
# TEST PROCEDURE
# - Configure interface with selected parameters
#
# EXPECTED RESULTS
# Test is passed:
# - if interface is properly configured AND
# - if mtu is applied to State table
# Test fails:
# - if inteface cannot be configured OR
# - if mtu is not applied to State table

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
    if_type=\$2 -- used as if_type in Wifi_Inet_Config table - default 'vif' - (string)(optional)
    mtu=\$3 -- used as mtu in Wifi_Inet_Config table - default '1600' - (integer)(optional)

this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_set_mtu.sh eth0 eth 1500
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Provide at least 2 argument(s).
if [ $# -lt 2 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

# Fill variables with provided arguments or defaults.
if_name=$1
if_type=$2
mtu=$3

tc_name="nm2/$(basename "$0")"

log "$tc_name: Creating Wifi_Inet_Config entries for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -if_type "$if_type" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Setting MTU to $mtu"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u mtu "$mtu" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - mtu $mtu" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - mtu $mtu" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is mtu "$mtu" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - mtu $mtu" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - mtu $mtu" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Checking if MTU was properly applied to $if_name"
wait_for_function_response 0 "interface_mtu $if_name | grep -q \"$mtu\"" &&
    log "$tc_name: MTU applied to ifconfig - interface $if_name" ||
    raise "Failed to apply MTU to ifconfig - interface $if_name" -l "$tc_name" -tc

pass
