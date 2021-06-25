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
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="wm2/$(basename "$0")"
manager_setup_file="wm2/wm2_setup.sh"

usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script to verify Wifi_Associated_Clients table is populated with correct values when client is connected to DUT.
Arguments:
    -h  show this help message
    \$1  (client_mac)     : MAC address of client connected to ap: (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <CLIENT MAC>
Script usage example:
    ./${tc_name} a1:b2:c3:d4:e5:f6

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

trap '
    fut_info_dump_line
    print_tables Wifi_Associated_Clients
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly ${NARGS} input argument" -l "${tc_name}" -arg

client_mac=${1}
log_title "$tc_name: WM2 test - Verify Wifi_Associated_Clients table is populated with client MAC"

check_ovsdb_entry Wifi_Associated_Clients -w mac "$client_mac" &&
    log "$tc_name: Valid client mac $client_mac is populated in the Wifi_Associated_Clients table." ||
    raise "FAIL: Client mac address is not present in the Wifi_Associated_Clients table." -l "$tc_name" -tc

# Make sure state in Wifi_Associated_Clients is 'active' for connected client
get_state=$(get_ovsdb_entry_value Wifi_Associated_Clients state -w mac "$client_mac" -r)
[ "$get_state" == "active" ] &&
    log "$tc_name: Wifi_Associated_Clients::state is '$get_state' for $client_mac" ||
    raise "FAIL: Wifi_Associated_Clients::state is NOT 'active' for $client_mac" -l "$tc_name" -tc

pass
