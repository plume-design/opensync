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

## Internet lost test case for CM

# TEST PROCEDURE
# Step 1 (script is called with parameters 4 and check_counter):
# - Internet connection is blocked for DUT.
# - Internet lost counter is suppose to reach its max. count.
# Step 2 (script is called with parameters 0 and internet_recover):
# - Internet connection is unblocked for DUT.
# - Internet lost counter is suppose to reset to zero.
# - Cloud connection should go to ACTIVE.

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi

source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    check_restore_management_access || true
    run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

where arguments are:
    unreachable_internet_counter=\$@ -- used as value counter must reach - (string)
    test_type=\$@ -- used as test step - (string)

this script is dependent on following:
    - running CM manager
    - running NM manager

example of usage:
    /tmp/fut-base/shell/cm2/$(basename "$0") 4 check_counter
    /tmp/fut-base/shell/cm2/$(basename "$0") 0 internet_recovered
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

unreachable_internet_counter=${1:-4}
test_type=${2:-"check_counter"}

tc_name="cm2/$(basename "$0")"
log "$tc_name: CM2 test - Internet lost - $test_type"

if [ "$test_type" = "check_counter" ]; then
    log "$tc_name: Waiting for unreachable_internet_counter to reach $unreachable_internet_counter"
    wait_ovsdb_entry Connection_Manager_Uplink -w if_name eth0 -is unreachable_internet_counter "$unreachable_internet_counter" &&
        log "$tc_name: Connection_Manager_Uplink unreachable_internet_counter reached $unreachable_internet_counter" ||
        raise "Connection_Manager_Uplink - {unreachable_internet_counter:=$unreachable_internet_counter}" -l "$tc_name" -ow
elif [ "$test_type" = "internet_recovered" ]; then
    log "$tc_name: Waiting for unreachable_internet_counter to reset to 0"
    wait_ovsdb_entry Connection_Manager_Uplink -w if_name eth0 -is unreachable_internet_counter "0" &&
        log "$tc_name: Connection_Manager_Uplink unreachable_internet_counter reset to 0" ||
        raise "Connection_Manager_Uplink - {unreachable_internet_counter:=0}" -l "$tc_name" -ow
else
    raise "Wrong test type option" -l "$tc_name" -tc
fi

pass
