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


# FUT environment loading
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Validate home vaps on home bridge exist
Arguments:
    -h  show this help message
    \$1 (interface_name)        : used as interface name to check        : (string)(required)
    \$2 (bridge_home_interface) : used as bridge interface name to check : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <INTERFACE-NAME> <BRIDGE-HOME-INTERFACE>
Script usage example:
   ./${tc_name} wl1.2 br-home
   ./${tc_name} home-ap-l50 br-home
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
NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap '
fut_info_dump_line
print_tables Wifi_VIF_Config Wifi_VIF_State
fut_info_dump_line
' EXIT SIGINT SIGTERM

interface_name=$1
bridge_home_interface=$2

log_title "$tc_name: ONBRD test - Verify home VAPs on home bridge, check if interface '${interface_name}' in '${bridge_home_interface}'"

wait_for_function_response 0 "check_ovsdb_entry Wifi_VIF_State -w if_name $interface_name" &&
    log "$tc_name: SUCCESS: interface $interface_name exists" ||
    raise "FAIL: interface $interface_name does not exist" -l "$tc_name" -tc

log "$tc_name: Setting Wifi_VIF_Config bridge to $bridge_home_interface"
update_ovsdb_entry Wifi_VIF_Config -u bridge "$bridge_home_interface" &&
    log "$tc_name: update_ovsdb_entry - Wifi_VIF_Config table updated - bridge $bridge_home_interface" ||
    raise "update_ovsdb_entry - Failed to update Wifi_VIF_Config table - bridge $bridge_home_interface" -l "$tc_name" -tc

log "$tc_name: Verify bridge, waiting for Wifi_VIF_State bridge is $bridge_home_interface"
wait_ovsdb_entry Wifi_VIF_State -w if_name "$interface_name" -is bridge "$bridge_home_interface" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_VIF_State bridge is $bridge_home_interface" ||
    raise "wait_ovsdb_entry - Wifi_VIF_State bridge is NOT $bridge_home_interface" -l "$tc_name" -tc

log "$tc_name: Clean created interfaces after test"
vif_clean

pass
