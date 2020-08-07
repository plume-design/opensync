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
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

this script is dependent on following:
    - running DM manager
    - both DUT and cloud controller (simulation service on RPI) must have correct certificates and CA files
    - device time synched to real time

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0")
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

tc_name="onbrd/$(basename "$0")"

log "$tc_name: ONBRD Verify client TLS connection"

connect_to_fut_cloud &&
    log "$tc_name: Device connected to FUT cloud. Start test case execution" ||
    raise "Failed to connect device to FUT cloud. Terminate test" -l "$tc_name" -tc

# Check if connection is maintained for 60s
log "$tc_name: Checking if connection is maintained and stable"
for interval in $(seq 1 3); do
    log "$tc_name: Sleeping for 20 seconds"
    sleep 20

    log "$tc_name: Wait for connection status in Manager table is ACTIVE, check num: $interval"
    wait_for_function_response 0 "wait_cloud_state ACTIVE" &&
        log "$tc_name: wait_cloud_state - Connection state is ACTIVE, check num: $interval" ||
        raise "wait_cloud_state - FAILED: Connection state is NOT ACTIVE, check num: $interval" -l "$tc_name" -tc
done

pass
