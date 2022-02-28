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
fut_topdir="$(realpath "$current_dir"/../../..)"

# FUT environment loading
source "${fut_topdir}"/config/default_shell.sh
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "$fut_topdir/lib/rpi_lib.sh"
def_port=5201
protocol="TCP"

usage() {
    cat << usage_string
tools/client/rpi/run_upnp_client.sh [-h] arguments
Description:
    - Run UPnP Client on the RPI client and run iperf3 server for traffic check.
Arguments:
    -h                        : Show this help message
    - \$1 (wlan_name)         : Interface name                                        : (string)(required)
    - \$2 (wlan_namespace)    : Interface namespace name                              : (string)(required)
    - \$3 (client_ip_address) : IP address to be assigned for the client interface    : (string)(required)
    - \$4 (dut_ip_address)    : IP address of the DUT                                 : (string)(required)
    - \$5 (port)              : Port number on which upnpc is run                     : (int)(optional)(default=${def_port})

Script usage example:
   ./tools/client/rpi/run_upnp_client.sh wlan0 nswifi1 10.10.10.20 10.10.10.30 5201
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

NARGS=4
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/client/rpi/run_upnp_client.sh" -arg

wlan_name=${1}
wlan_namespace=${2}
client_ip_address=${3}
dut_ip_address=${4}
port=${5:-$def_port}
wlan_namespace_cmd="ip netns exec ${wlan_namespace} bash"

log_title "tools/client/rpi/run_upnp_client.sh: Run UPnP client on the device"

if [[ "$EUID" -ne 0 ]]; then
    raise "FAIL: Please run this function as root - sudo" -l "tools/client/rpi/run_upnp_client.sh"
fi

log "tools/client/rpi/run_upnp_client.sh: Assigning IP address to ${wlan_name} interface"
${wlan_namespace_cmd} -c "ifconfig ${wlan_name} ${client_ip_address} netmask 255.255.255.0 up"
if [ $? -eq 0 ]; then
    log -deb "tools/client/rpi/run_upnp_client.sh: IP address ${client_ip_address} assigned to the interface: ${wlan_name} - Success"
else
    raise "FAIL: IP address ${client_ip_address} not assigned on the interface: ${wlan_name}" -l "tools/client/rpi/run_upnp_client.sh" -tc
fi

log "tools/client/rpi/run_upnp_client.sh: Adding default route on client"
${wlan_namespace_cmd} -c "ip route add default via ${dut_ip_address}"

log "tools/client/rpi/run_upnp_client.sh: Starting UPnPC on client host"
${wlan_namespace_cmd} -c "/usr/bin/upnpc -a ${client_ip_address} ${port} ${port} ${protocol}"
if [ $? -eq 0 ]; then
    log -deb "tools/client/rpi/run_upnp_client.sh: UPnP client started successfully on the device - Success"
else
    raise "FAIL: UPnP client failed to start on the device!" -l "tools/client/rpi/run_upnp_client.sh" -tc
fi

log "tools/client/rpi/run_upnp_client.sh: Running iperf server to check traffic"
${wlan_namespace_cmd} -c "nohup iperf3 -s -1 -D"

pass
