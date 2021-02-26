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
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"
haproxy_cfg_path="tools/rpi/files/haproxy.cfg"
fut_cloud_start_path="tools/rpi/start_cloud_simulation.sh"
usage()
{
cat << usage_string
${tc_name} [-h]
Description:
    - Validate CM connecting to specific Cloud TLS version
Arguments:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
    - On RPI SERVER:
        - Edit ${haproxy_cfg_path} to change TLS version
            Look for:
              ssl-default-bind-options force-tlsv<TLS-VERSION> ssl-max-ver TLSv<TLS-VERSION> ssl-min-ver TLSv<TLS-VERSION>
              ssl-default-server-options force-tlsv<TLS-VERSION> ssl-max-ver TLSv<TLS-VERSION> ssl-min-ver TLSv<TLS-VERSION>
            Change <TLS-VERSION> to one of following: 1.0, 1.1. 1.2
        - Run ./${fut_cloud_start_path} -r
    - On DEVICE: Run: ./${tc_name}
Script usage example:
   ./${tc_name}
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

log "$tc_name: Setting CM log level to TRACE"
set_manager_log CM TRACE

log_title "$tc_name: ONBRD test - Verify client TLS connection"

connect_to_fut_cloud &&
    log "$tc_name: Device connected to FUT cloud. Start test case execution" ||
    raise "Failed to connect device to FUT cloud. Terminate test" -l "$tc_name" -tc
# Check if connection is maintained for 60s
log "$tc_name: Checking if connection is maintained and stable"
for interval in $(seq 1 3); do
    log "$tc_name: Sleeping for 20 seconds"
    sleep 20
    log "$tc_name: Check connection status in Manager table is ACTIVE, check num: $interval"
    ${OVSH} s Manager status -r | grep "ACTIVE" &&
        log "$tc_name: wait_cloud_state - Connection state is ACTIVE, check num: $interval" ||
        raise "wait_cloud_state - FAILED: Connection state is NOT ACTIVE, check num: $interval, connection should be maintained" -l "$tc_name" -tc
done

pass
