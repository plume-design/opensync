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

# FUT environment loading
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures interfaces mtu through Wifi_inet_Config 'mtu' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name) : used as if_name in Wifi_Inet_Config table : (string)(required)
    \$2 (if_type) : used as if_type in Wifi_Inet_Config table : (string)(required)
    \$3 (mtu)     : used as mtu in Wifi_Inet_Config table     : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <IF-TYPE> <MTU>
Script usage example:
   ./${tc_name} eth0 eth 1500
usage_string
}
while getopts h option; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            echo "Unknown argument" && exit 1
            ;;
    esac
done

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
# Fill variables with provided arguments or defaults.
if_name=$1
if_type=$2
mtu=$3

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    fut_info_dump_line
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: NM2 test - Testing table Wifi_Inet_Config field mtu"

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
wait_for_function_response 0 "get_interface_mtu_from_system $if_name | grep -q \"$mtu\"" &&
    log "$tc_name: MTU applied to ifconfig - interface $if_name" ||
    raise "Failed to apply MTU to ifconfig - interface $if_name" -l "$tc_name" -tc

pass
