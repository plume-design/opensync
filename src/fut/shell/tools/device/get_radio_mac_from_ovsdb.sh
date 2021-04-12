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
# Script echoes single line so we are redirecting source output to /dev/null
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source /tmp/fut-base/shell/config/default_shell.sh &> /dev/null
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh" &> /dev/null
[ -n "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" &> /dev/null

tc_name="tools/device/$(basename "$0")"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - This script gets radio physical (MAC) address from ovsdb
Arguments:
    -h  show this help message
    \$1 (where_clause) : ovsdb "where" clause for Wifi_Radio_State table, that determines how we get the MAC address : (string)(required)
Script usage example:
    ./${tc_name} "if_name==wifi1"
    ./${tc_name} "freq_band==5GL"
    ./${tc_name} "channel==44"
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
NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

where_clause="${1}"
# It is important that no logging is performed for functions that output values
fnc_str="get_radio_mac_from_ovsdb ${where_clause}"
wait_for_function_output "notempty" "${fnc_str}" >/dev/null 2>&1
if [ $? -eq 0 ]; then
    iface_mac_raw=$($fnc_str) || raise "Failure: ${fnc_str}"  -l "$tc_name" -f
else
    raise "Failure: ${fnc_str}" -l "${tc_name}" -f
fi

echo -n "${iface_mac_raw}"
exit 0
