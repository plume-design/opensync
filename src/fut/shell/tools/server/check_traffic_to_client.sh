#!/bin/bash

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


current_dir=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
export FUT_TOPDIR="$(realpath "$current_dir"/../../..)"

# FUT environment loading
source "${FUT_TOPDIR}/shell/config/default_shell.sh" &> /dev/null
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh" &> /dev/null
source "${FUT_TOPDIR}/shell/lib/rpi_lib.sh"


usage()
{
cat << usage_string
tools/server/check_traffic_to_client.sh [-h]
Description:
    This script checks if traffic is flowing to the IP/Port of the host.
    Note that, this function runs iperf3 client and host must be running
    iperf3 server to check traffic flow successfully.
Options:
    -h  show this help message
Arguments:
    wan_ip=$1   -- IP address of the host                   - (string)(required)
    port_num=$2 -- port number, the traffic is checked on   - (int)(required)
Script usage example:
   ./tools/server/check_traffic_to_client.sh 192.168.200.10 5201
usage_string
exit 1
}

NARGS=2
if [ $# -ne ${NARGS} ]; then
    raise "Requires exactly ${NARGS} input argument(s)." -arg
    usage
fi

wan_ip=${1}
port_num=${2}

log "tools/server/check_traffic_to_client.sh: Checking if traffic is flowing to the IP and port address of the host"

check_traffic_iperf3_client $wan_ip $port_num

pass
