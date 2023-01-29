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
source "${FUT_TOPDIR}/shell/lib/othr_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="dm/othr_setup.sh"
usage()
{
cat << usage_string
othr/othr_verify_iperf3_speedtest.sh [-h] arguments
Description:
    - Script verifies iperf3 speedtest feature works on the DUT.
Arguments:
    -h  show this help message
    \$1 (server_ip_addr)     : IP address of the server                 : (string)(required)
    \$2 (traffic_type)       : Allowed values: forward, reverse, udp    : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./othr/othr_verify_iperf3_speedtest.sh <SERVER_IP_ADDRESS> <TRAFFIC_TYPE>
Script usage example:
    ./othr/othr_verify_iperf3_speedtest.sh 192.168.200.1 forward
    ./othr/othr_verify_iperf3_speedtest.sh 192.168.200.1 udp
usage_string
}
if [ -n "${1}" ]; then
    case "${1}" in
        help | \
        --help | \
        -h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
fi

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "othr/othr_verify_iperf3_speedtest.sh" -arg
server_ip_addr=${1}
traffic_type=${2}

log_title "othr/othr_verify_iperf3_speedtest.sh: OTHR test - Verify if iperf3 speedtest feature works on the DUT"

if [ ${traffic_type} == "forward" ]; then
    # Default traffic is TCP and uplink flow
    iperf3 -c ${server_ip_addr} -t 5 &&
        log "othr/othr_verify_iperf3_speedtest.sh: iperf3 client transferred uplink traffic to server - Success" ||
        raise "FAIL: iperf3 client failed to transfer traffic to server" -l "othr/othr_verify_iperf3_speedtest.sh" -tc
elif [ ${traffic_type} == "reverse" ]; then
    # Default traffic is TCP and -R option for downlink flow
    iperf3 -c ${server_ip_addr} -R -t 5 &&
        log "othr/othr_verify_iperf3_speedtest.sh: iperf3 client received the downlink traffic from the server - Success" ||
        raise "FAIL: iperf3 client failed to receive downlink traffic from the server" -l "othr/othr_verify_iperf3_speedtest.sh" -tc
elif [ ${traffic_type} == "udp" ]; then
    # Add option '-u' to iperf3 command for UDP traffic
    iperf3 -c ${server_ip_addr} -u -t 5 &&
        log "othr/othr_verify_iperf3_speedtest.sh: iperf3 client received the UDP traffic from the server - Success" ||
        raise "FAIL: iperf3 client failed to receive UDP traffic from the server" -l "othr/othr_verify_iperf3_speedtest.sh" -tc
else
    raise "FAIL: Invalid option supplied. Allowed options: 'forward', 'reverse', 'udp'" -l "othr/othr_verify_iperf3_speedtest.sh" -tc
fi

pass
