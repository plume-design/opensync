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



# TEST DEFINITION DESCRIPTION
# Try to configure non existing interface.
# Non existing interface should not be created/configured.
# Also NM should not crash if non existing interface tries to be created/configured.
#
# TEST PROCEDURE
# Test inserts interface to Wifi_Inet_Config by calling insert_ovsdb_entry.
# It checks Wifi_Inet_State table for interface with wait_ovsdb_entry.
# Checks if interface is created in system.
# Checks if NM is running.
# If it cannot find NM PID, it crashed. Test fails.
#
# EXPECTED RESULTS
# Test is passed:
# - if interface is not created
# - NM survived, did not crash
# Test fails:
# - if nonexisting interface was created but it shouldn't be
# - NM crashed (PID is no longer available)

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${FUT_TOPDIR}/shell/lib/nm2_lib.sh
source ${LIB_OVERRIDE_FILE}

# Execute on EXIT signal.
trap 'run_setup_if_crashed nm || true' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2 \$3

where options are:
    -h  show this help message

where arguments are:
    if_name=\$1 -- used as if_name in Wifi_Inet_Config table - (string)(required)
    if_type=\$2 -- used as if_type in Wifi_Inet_Config table - (string)(required)
    inet_addr=\$3 -- used as inet_addr in Wifi_Inet_Config table - (string)(required)

this script is dependent on following:
    - running NM manager

example of usage:
   /tmp/fut-base/shell/ob/$(basename "$0") test1 eth 10.10.10.15
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Provide at least 3 argument(s).
if [ $# -lt 3 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

# Fill variables with provided arguments or defaults.
if_name=$1
if_type=$2
ip_address=$3

tc_name="nm2/$(basename "$0")"

log "$tc_name: Creating NONEXISTENT interface $if_name of type $if_type"

insert_ovsdb_entry Wifi_Inet_Config \
    -i if_name "$if_name" \
    -i if_type "$if_type" \
    -i enabled true \
    -i network true \
    -i NAT false \
    -i inet_addr "$ip_address" \
    -i netmask "255.255.255.0" \
    -i broadcast "10.10.10.255" \
    -i ip_assign_scheme static \
    -i parent_ifname eth1 \
    -i mtu 1500 &&
        log "$tc_name: Creating NONEXISTENT interface - Failed to insert_ovsdb_entry" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

log "$tc_name: Checking if NONEXISTENT interface $if_name was CREATED"
# Interface must be present in Wifi_Inet_State table...
wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is if_type "$if_type" &&
    log "$tc_name: NONEXISTENT interface present in Wifi_Inet_State table - if_name $if_name" ||
    raise "Wifi_Inet_State - {if_name:=$if_name}" -l "$tc_name" -ow

# ...but not in system.
wait_for_function_response 1 "check_interface_exists $if_name" &&
    log "$tc_name: SUCCESS: Interface $if_name of type $if_type does NOT exist on system" ||
    raise "FAIL: Interface $if_name of type $if_type exists on system, but should NOT" -l "$tc_name" -tc

# Check if manager survived.
manager_pid_file="${OPENSYNC_ROOTDIR}/bin/nm"
wait_for_function_response 0 "check_manager_alive $manager_pid_file" &&
    log "$tc_name: SUCCESS: NETWORK MANAGER is running" ||
    raise "FAILED: NETWORK MANAGER not running/crashed" -l "$tc_name" -tc

pass
