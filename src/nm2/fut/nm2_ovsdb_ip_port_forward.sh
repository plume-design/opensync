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

# FUT environment loading
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script checks if IP port forward rule is created on the system when configured through IP_Port_Forward table
        Script fails if IP port is not forwarded on the system
Arguments:
    -h  show this help message
    \$1 (src_ifname) : used as src_ifname in IP_Port_Forward table : (string)(required)
    \$2 (src_port)   : used as src_port in IP_Port_Forward table   : (string)(required)
    \$3 (dst_ipaddr) : used as dst_ipaddr in IP_Port_Forward table : (string)(required)
    \$4 (dst_port)   : used as dst_port in IP_Port_Forward table   : (string)(required)
    \$5 (protocol)   : used as protocol in IP_Port_Forward table   : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <SRC-IFNAME> <SRC-PORT> <DST-IPADDR> <DST-PORT> <PROTOCOL>
Script usage example:
   ./${tc_name} wifi0 8080 10.10.10.200 80 tcp
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
NARGS=5
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap 'run_setup_if_crashed nm || true' EXIT SIGINT SIGTERM

# No default values.
src_ifname=$1
src_port=$2
dst_ipaddr=$3
dst_port=$4
protocol=$5

log_title "$tc_name: NM2 test - Testing IP port forwarding"

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
