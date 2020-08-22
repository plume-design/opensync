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


source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut_set_env.sh" ] && source /tmp/fut_set_env.sh
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${LIB_OVERRIDE_FILE}

tc_name="tools/device/$(basename $0)"
help()
{
cat << EOF
${tc_name} [-h] where_clause

This script gets radio physical (MAC) address from ovsdb

Arguments:
    where_clause=$1: ovsdb "where" clause for Wifi_Radio_State table, that determines how we get the MAC address
Examples of usage:
    ${tc_name} "if_name==wifi1"
    ${tc_name} "freq_band==5GL"
    ${tc_name} "channel==44"
EOF
raise "Printed help and usage string" -l "$tc_name" -arg
}

while getopts h option; do
    case "$option" in
        h)
            help
            ;;
    esac
done

NARGS=1
[ $# -ne ${NARGS} ] && raise "Failure: requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
where_clause="${1}"

# It is important that no logging is performed for functions that output values
fnc_str="get_radio_mac_from_ovsdb ${where_clause}"
wait_for_function_output "notempty" "${fnc_str}" 2>&1 >/dev/null
if [ $? -eq 0 ]; then
    iface_mac_raw=$($fnc_str) || raise "Failure: ${fnc_str}"  -l "$tc_name" -f
else
    raise "Failure: ${fnc_str}" -l "${tc_name}" -f
fi

echo -n "${iface_mac_raw}"
exit 0
