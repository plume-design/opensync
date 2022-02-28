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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage() {
    cat <<usage_string
tools/device/validate_port_forward_entry_in_iptables.sh [-h] arguments
Description:
    - Script checks port forwarding rule in the iptable on DUT.
Arguments:
    -h  show this help message
    - \$1 (client_ip_addr)      : IP address to validate entry in the iptable   : (string)(required)
    - \$2 (port_num)            : Port number to validate entry in the iptable  : (interger)(required)

Script usage example:
    ./tools/device/validate_port_forward_entry_in_iptables.sh 10.10.10.20 5201
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

# INPUT ARGUMENTS:
NARGS=2
[ $# -ne ${NARGS} ] && raise "Requires '${NARGS}' input argument" -arg

client_ip_addr=$1
port_num=$2

log_title "tools/device/validate_port_forward_entry_in_iptables.sh: Verify port forwarding in the iptable rules"

log "tools/device/validate_port_forward_entry_in_iptables.sh: Checking the iptables rules"
iptable_rules=$(iptables -t nat -vnL)

echo "$iptable_rules" | grep -i "dpt:${port_num} to:${client_ip_addr}:${port_num}"
if [ $? -eq 0 ]; then
    log -deb "tools/device/validate_port_forward_entry_in_iptables.sh: Port number ${port_num} is successfully forwarded - Success"
else
    raise "FAIL: Port number ${port_num} failed to forward!" -l "tools/device/validate_port_forward_entry_in_iptables.sh" -tc
fi
