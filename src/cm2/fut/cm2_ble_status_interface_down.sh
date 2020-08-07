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
    if_name=\$@ -- used as connection interface - (string)

this script is dependent on following:
    - running CM manager
    - running NM manager

example of usage:
    /tmp/fut-base/shell/cm2/$(basename "$0") eth0
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if_name=${1:-eth0}
bit_process="01 05 35 75"

tc_name="cm2/$(basename "$0")"
log "$tc_name: CM2 test - CM OBSERVE BLE STATUS  - interface down/up"
log "$tc_name: Simulating CM full reconnection by dropping interface"

log "$tc_name: Dropping interface $if_name"
ifconfig "$if_name" down &&
    log "$tc_name: Interface $if_name is down" ||
    raise "Couldn't bring down interface $if_name" -l "$tc_name" -ds

log "$tc_name: Sleeping for 5 seconds"
sleep 5

log "$tc_name: Bringing back interface $if_name"
ifconfig "$if_name" up &&
    log "$tc_name: Interface $if_name is up" ||
    raise "Couldn't bring up interface $if_name" -l "$tc_name" -ds

for bit in $bit_process; do
    log "$tc_name: Checking AW_Bluetooth_Config payload for $bit:00:00:00:00:00"
    wait_ovsdb_entry AW_Bluetooth_Config -is payload "$bit:00:00:00:00:00" &&
        log "$tc_name: wait_ovsdb_entry - AW_Bluetooth_Config payload changed to $bit:00:00:00:00:00" ||
        raise "AW_Bluetooth_Config payload failed to change $bit:00:00:00:00:00" -l "$tc_name" -tc
done

pass
