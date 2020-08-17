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


if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1 \$2

where options are:
    -h  show this help message

where arguments are:
    firmware_version=\$1 -- used as FW version to verify running correct FW - (string)(required)
    only_check_nonempty=\$2 -- used to override exact FW version match and only check if firmware_version is populated - (string)(optional)

this script is dependent on following:
    - running DM manager

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0") 2.4.3-72-g65b961c-dev-debug
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -ne 1 ]; then
    echo 1>&2 "$0: incorrect number of input arguments"
    echo "$usage"
    exit 2
fi

search_rule=${1:-"non_empty"}

tc_name="onbrd/$(basename "$0")"

if [ "$search_rule" = "non_empty" ]; then
    log "$tc_name: ONBRD Verify FW version, waiting for $search_rule string"
    wait_for_function_response 'notempty' "get_ovsdb_entry_value AWLAN_Node firmware_version" &&
        log "$tc_name: check_firmware_version_populated - firmware_version populated" ||
        raise "check_firmware_version_populated - firmware_version un-populated" -l "$tc_name" -tc
elif [ "$search_rule" = "pattern_match" ]; then
    log "$tc_name: ONBRD Verify FW version, waiting for $search_rule"
    wait_for_function_response 0 "onbrd_check_fw_pattern_match" &&
        log "$tc_name: check_fw_pattern_match - firmware_version patter match" ||
        raise "check_fw_pattern_match - firmware_version patter didn't match" -l "$tc_name" -tc
else
    log "$tc_name: ONBRD Verify FW version, waiting for exactly '$search_rule'"
    wait_ovsdb_entry AWLAN_Node -is firmware_version "$search_rule" &&
        log "$tc_name: wait_ovsdb_entry - AWLAN_Node firmware_version equal to '$1'" ||
        raise "wait_ovsdb_entry - AWLAN_Node firmware_version NOT equal to '$1'" -l "$tc_name" -tc
fi

pass
