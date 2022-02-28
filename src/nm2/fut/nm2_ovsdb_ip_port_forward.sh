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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
nm2/nm2_ovsdb_ip_port_forward.sh [-h] arguments
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
                 Run: ./nm2/nm2_ovsdb_ip_port_forward.sh <SRC-IFNAME> <SRC-PORT> <DST-IPADDR> <DST-PORT> <PROTOCOL>
Script usage example:
   ./nm2/nm2_ovsdb_ip_port_forward.sh wifi0 8080 10.10.10.200 80 tcp
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

NARGS=5
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_ovsdb_ip_port_forward.sh" -arg
# No default values.
src_ifname=$1
src_port=$2
dst_ipaddr=$3
dst_port=$4
protocol=$5

trap '
    fut_info_dump_line
    print_tables IP_Port_Forward
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_ovsdb_ip_port_forward.sh: NM2 test - Testing IP port forwarding"

log "nm2/nm2_ovsdb_ip_port_forward.sh: Set IP FORWARD in OVSDB"
set_ip_port_forwarding "$src_ifname" "$src_port" "$dst_ipaddr" "$dst_port" "$protocol" &&
    log "nm2/nm2_ovsdb_ip_port_forward.sh: Set IP port forward for $src_ifname - Success" ||
    raise "FAIL: Failed to set IP port forward - $src_ifname" -l "nm2/nm2_ovsdb_ip_port_forward.sh" -tc

log "nm2/nm2_ovsdb_ip_port_forward.sh: Check for IP FORWARD record in iptables - LEVEL2"
wait_for_function_response 0 "check_ip_port_forwarding $dst_ipaddr:$dst_port" &&
    log "nm2/nm2_ovsdb_ip_port_forward.sh: LEVEL2 - IP port forward record propagated to iptables" ||
    raise "FAIL: LEVEL2 - Failed to propagate record into iptables" -l "nm2/nm2_ovsdb_ip_port_forward.sh" -tc

log "nm2/nm2_ovsdb_ip_port_forward.sh: Delete IP FORWARD from OVSDB"
${OVSH} d IP_Port_Forward -w dst_ipaddr=="$dst_ipaddr" -w src_ifname=="$src_ifname" &&
    log "nm2/nm2_ovsdb_ip_port_forward.sh: Deleted IP FORWARD for $src_ifname from IP_Port_Forward" ||
    raise "FAIL: Failed to delete IP FORWARD for $src_ifname from IP_Port_Forward" -l "nm2/nm2_ovsdb_ip_port_forward.sh" -tc

wait_ovsdb_entry_remove IP_Port_Forward -w dst_ipaddr "$dst_ipaddr" -w src_ifname "$src_ifname" &&
    log "nm2/nm2_ovsdb_ip_port_forward.sh: Removed entry from IP_Port_Forward for $src_ifname - Success" ||
    raise "FAIL: Failed to remove entry from IP_Port_Forward for $src_ifname" -l "nm2/nm2_ovsdb_ip_port_forward.sh" -tc

log "nm2/nm2_ovsdb_ip_port_forward.sh: Check is IP FORWARD record is deleted from iptables - LEVEL2"
wait_for_function_response 1 "check_ip_port_forwarding $dst_ipaddr:$dst_port" &&
    log "nm2/nm2_ovsdb_ip_port_forward.sh: LEVEL2 - IP FORWARD record deleted from iptables - Success" ||
    force_delete_ip_port_forward_raise "$src_ifname" "NM_PORT_FORWARD" "$dst_ipaddr:$dst_port"

pass
