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
# shellcheck disable=SC1091
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
    - Validate wan ip address
Arguments:
    -h  show this help message
    \$1 (wan_interface)     : used as interface name to check WAN IP if WANO is disabled : (string)(required)
    \$2 (wan_ip)            : used as WAN IP address to be checked                       : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} br-wan 192.168.200.10
Script usage example:
   ./${tc_name} br-wan 192.168.200.10
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
print_tables Wifi_Inet_State
ifconfig "$wan_interface"
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
wan_interface=${1}
inet_addr=${2}

log_title "$tc_name: ONBRD test - Verify WAN_IP in Wifi_Inet_State is correctly applied"

log "$tc_name: Verify WAN IP address '$inet_addr' for interface '$wan_interface'"
wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is inet_addr "$inet_addr" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_State '$wan_interface' inet_addr is equal to '$inet_addr'" ||
    raise "wait_ovsdb_entry - Wifi_Inet_State '$wan_interface' inet_addr is NOT equal to '$inet_addr'" -l "$tc_name" -tc
wait_for_function_response 0 "verify_wan_ip_l2 $wan_interface $inet_addr" &&
    log "$tc_name: LEVEL2 WAN IP for '$wan_interface' is equal to '$inet_addr'" ||
    raise "LEVEL2 WAN IP for '$wan_interface' is NOT equal to '$inet_addr'" -l "$tc_name" -tc

pass
