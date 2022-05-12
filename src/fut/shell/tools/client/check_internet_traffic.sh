#!/usr/bin/env bash

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


current_dir=$(dirname "$(realpath "$BASH_SOURCE")")
fut_topdir="$(realpath "$current_dir"/../..)"

# FUT environment loading
# shellcheck disable=SC1091
source "${fut_topdir}"/config/default_shell.sh &> /dev/null
# Ignore errors for fut_set_env.sh sourcing
# shellcheck disable=SC1091
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "${fut_topdir}"/lib/unit_lib.sh &> /dev/null

def_n_ping=5
def_ip="1.1.1.1"
usage()
{
cat << usage_string
tools/client/check_internet_traffic.sh [-h] arguments
Description:
    - Script checks device internet connectivity is blocked/unblocked with ping tool
Dependency:
    - "ping" tool with "-c" option to specify number of packets sent
Arguments:
    -h                        : Show this help message
    - \$1 (wlan_namespace)    : Interface namespace name                                    : (string)(required)
    - \$2 (traffic_state)     : 'block' to block internet, 'unblock' to unblock internet    : (string)(required)
    - \$3 (n_ping)            : How many packets are sent                                   : (int)(optional)(default=${def_n_ping})
    - \$4 (internet_check_ip) : IP address to validate internet connectivity                : (string)(optional)(default=${def_ip})
Script usage example:
   ./tools/client/check_internet_traffic.sh nswifi1 block
   ./tools/client/check_internet_traffic.sh nswifi1 unblock 10 1.1.1.1
usage_string
}
if [ -n "${1}" ] > /dev/null 2>&1; then
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
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/check_internet_traffic.sh" -arg
wlan_namespace=${1}
traffic_state=${2}
n_ping=${3:-$def_n_ping}
internet_check_ip=${4:-$def_ip}

wlan_namespace_cmd="sudo ip netns exec ${wlan_namespace} bash"

log "tools/client/check_internet_traffic.sh: Verify if internet traffic is ${traffic_state}ed"

res=$(${wlan_namespace_cmd} -c "ping -c${n_ping} ${internet_check_ip}")
if [ $? -eq 0 ]; then
    if [ "$traffic_state" == "block" ]; then
        raise "FAIL: Internet traffic is not blocked" -l "tools/client/check_internet_traffic.sh" -tc
    fi
else
    if [ "$traffic_state" == "unblock" ]; then
        raise "FAIL: Internet traffic is not unblocked" -l "tools/client/check_internet_traffic.sh" -tc
    fi
fi

log "tools/client/check_internet_traffic.sh: Internet traffic is ${traffic_state}ed"

pass
