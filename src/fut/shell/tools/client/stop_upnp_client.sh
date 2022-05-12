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
source "${fut_topdir}"/config/default_shell.sh
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${fut_topdir}"/lib/unit_lib.sh
protocol="TCP"

usage() {
    cat << usage_string
tools/client/stop_upnp_client.sh [-h] arguments
Description:
    - Stop UPnP Client on the client device and also kill iperf3 server if running on the client
Arguments:
    -h                        : Show this help message
    - \$1 (wlan_namespace)    : Interface namespace name            : (string)(required)
    - \$2 (port)              : Port number on which upnpc is run   : (int)(required)

Script usage example:
   ./tools/client/stop_upnp_client.sh nswifi1 5201
usage_string
}

if [ -n "${1}" ]; then
    case "${1}" in
    help | \
    --help | \
    -h)
        usage && exit 1
        ;;
    *) ;;

    esac
fi

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/stop_upnp_client.sh" -arg

wlan_namespace=${1}
port=${2}
wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"

log_title "tools/client/stop_upnp_client.sh: Run UPnP client on the device"

if [[ "$EUID" -ne 0 ]]; then
    raise "FAIL: Please run this function as root - sudo" -l "tools/client/stop_upnp_client.sh"
fi

log "tools/client/stop_upnp_client.sh: Stoping UPnPC on client host"
${wlan_namespace_cmd} -c "/usr/bin/upnpc -d ${port} ${protocol}" &&
    log -deb "tools/client/stop_upnp_client.sh: UPnP client stopped successfully on the device - Success" ||
    log -deb "FAIL: UPnP client could not be stopped on the device"

log "tools/client/stop_upnp_client.sh: Killing the iperf3 server if alive"
${wlan_namespace_cmd} -c "PID=$(ps -aux | grep "iperf3 -s -1 -D" | head -n1 |awk '{print $2}')"
${wlan_namespace_cmd} -c "kill -9 $PID"
