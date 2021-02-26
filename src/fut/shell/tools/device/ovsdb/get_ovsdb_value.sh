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


tc_name="device/ovsdb/$(basename "$0")"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Echoes the value of a single field in an OVSDB table
Arguments:
    -h  show this help message
    \$1 (ovsdb_table)       : OVSDB table name which to use           : (string)(required)
    \$2 (ovsdb_field)       : OVSDB table field name to acquire       : (string)(required)
    \$3 (ovsdb_where_field) : OVSDB field name for where condition    : (string)(required)
    \$4 (ovsdb_where_value) : Value for where condition field         : (string)(required)
    \$5 (ovsdb_raw)         : Acquire raw result from OVSDB (ovsh -r) : (bool)(optional) : (default:false)
Script usage example:
   ./${tc_name} Wifi_Radio_State channel if_name wifi0
   ./${tc_name} Wifi_Radio_State channel if_name wifi0 true
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
NARGS=4
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

ovsdb_table=${1}
ovsdb_field=${2}
ovsdb_where_field=${3}
ovsdb_where_value=${4}
ovsdb_raw=${5:-false}
ovsdb_opt=''
ovsdb_where=''

if [ "$ovsdb_raw" == 'true' ]; then
    ovsdb_opt='-raw'
fi

if [ -n "$ovsdb_where_field" ] && [ -n "$ovsdb_where_value" ]; then
    ovsdb_where="-w $ovsdb_where_field $ovsdb_where_value"
fi

get_ovsdb_entry_value "$ovsdb_table" "$ovsdb_field" $ovsdb_where ${ovsdb_opt} ||
    raise "Failed to retrive ovsdb value {$ovsdb_table:$ovsdb_field $ovsdb_where}" -l "$(basename "$0")" -oe
