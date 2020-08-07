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
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

this script is dependent on following:
    - running DM manager

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0") eth0
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
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1

tc_name="onbrd/$(basename "$0")"

log "$tc_name: ONBRD Verify DHCP dry run success"

update_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -u has_L2 false &&
    log "$tc_name: update_ovsdb_entry - Connection_Manager_Uplink table updated - has_L2 false" ||
    raise "update_ovsdb_entry - Failed to update WifConnection_Manager_Uplinki_Inet_Config - has_L2 false" -l "$tc_name" -tc

wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 false &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink - has_L2 is false" ||
    raise "wait_ovsdb_entry - Failed to reflect has_L2 to Connection_Manager_Uplink has_L2 is not false" -l "$tc_name" -tc

wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L3 false &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink - has_L3 is false" ||
    raise "wait_ovsdb_entry - Failed to reflect has_L3 to Connection_Manager_Uplink has_L3 is not false" -l "$tc_name" -tc


update_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -u has_L2 true &&
    log "$tc_name: update_ovsdb_entry - Connection_Manager_Uplink table updated - has_L2 false" ||
    raise "update_ovsdb_entry - Failed to update WifConnection_Manager_Uplinki_Inet_Config - has_L2 false" -l "$tc_name" -tc

wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 true &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink - has_L2 is true" ||
    raise "wait_ovsdb_entry - Failed to reflect has_L2 to Connection_Manager_Uplink has_L2 is not true" -l "$tc_name" -tc

wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L3 true &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink - has_L3 is true" ||
    raise "wait_ovsdb_entry - Failed to reflect has_L3 to Connection_Manager_Uplink has_L3 is not true" -l "$tc_name" -tc

pass
