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


# TEST DESCRIPTION
# Redirect source interface, port 8080 (Web Services) to destination IP, port 80 (Web Services), protocol TCP.
#
# TEST PROCEDURE
# Set port forwarding.
# Delete port forwarding.
#
# EXPECTED RESULTS
# Test is passed:
# - if port forwarding can be configured
# - if port forwarding can be deleted
#
# Test is failed:
# - otherwise

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1 \$2 \$3 \$4 \$5

where options are:
    -h  show this help message

where arguments are:
    src_ifname=\$1 -- used as src_ifname in IP_Port_Forward table - (string)(required)
    src_port=\$2 -- used as src_port in IP_Port_Forward table - (string)(required)
    dst_ipaddr=\$3 -- used as dst_ipaddr in IP_Port_Forward table - (string)(required)
    dst_port=\$4 -- used as dst_port in IP_Port_Forward table - (string)(required)
    protocol=\$5 -- used as protocol in IP_Port_Forward table - (string)(required)

this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_ovsdb_ip_port_forward.sh wifi0 8080 10.10.10.200 80 tcp
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Provide all 5 arguments.
if [ $# -lt 5 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

trap 'run_setup_if_crashed nm || true' EXIT SIGINT SIGTERM

# No default values.
src_ifname=$1
src_port=$2
dst_ipaddr=$3
dst_port=$4
protocol=$5

tc_name="nm2/$(basename "$0")"

log "$tc_name: Set IP FORWARD in OVSDB"
set_ip_forward "$src_ifname" "$src_port" "$dst_ipaddr" "$dst_port" "$protocol" &&
    log "$tc_name: Failed to set ip port forward - $src_ifname" ||
    raise "Failed to set ip port forward - $src_ifname" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check for IP FORWARD record in iptables"

wait_for_function_response 0 "ip_port_forward $dst_ipaddr:$dst_port" &&
    log "$tc_name: LEVEL2: Ip port forward propagated to iptables" ||
    raise "LEVEL2: Failed to propagate record into iptables" -l "$tc_name" -tc

log "$tc_name: Delete IP FORWARD from OVSDB"
${OVSH} d IP_Port_Forward -w dst_ipaddr=="$dst_ipaddr" -w src_ifname=="$src_ifname" &&
    log "$tc_name: Success to delete IP Port forward - $src_ifname" ||
    raise "Failed to delete IP Port forward - $src_ifname" -l "$tc_name" -tc

wait_ovsdb_entry_remove IP_Port_Forward -w dst_ipaddr "$dst_ipaddr" -w src_ifname "$src_ifname" &&
    log "$tc_name: Success to remove entry - $src_ifname" ||
    raise "Failed to remove entry - $src_ifname" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check is IP FORWARD record deleted from iptables"
wait_for_function_response 1 "ip_port_forward $dst_ipaddr:$dst_port" &&
    log "$tc_name: LEVEL2: Ip port forward deleted from iptables" ||
    force_delete_ip_port_forward_die "$src_ifname" "NM_PORT_FORWARD" "$dst_ipaddr:$dst_port"

pass
