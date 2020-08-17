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


if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/nm2_lib.sh
source ${LIB_OVERRIDE_FILE}

if_name=$1
if_role=$2

log "tools/device/dut_ref/$(basename "$0"): $if_role GRE Setup"

if [ "$if_role" == "gw" ]; then
    wait_for_function_response 'notempty' "cat /tmp/dhcp.leases | awk '{print $3}'"
    gre_remote_inet_addr=$(cat /tmp/dhcp.leases | awk '{print $3}')
    gre_if_name="pgd$(echo ${gre_remote_inet_addr//./-}| cut -d'-' -f3-4)"
    gre_local_inet_addr=$(ovsh s Wifi_Inet_Config -w if_name=="$if_name" inet_addr -r)

    add_bridge_port br-home "$gre_if_name"
elif [ "$if_role" == "leaf" ]; then
    gre_if_name="g-${if_name}"
    gre_local_inet_addr=$(ifconfig "${if_name}" | grep "inet addr:" | awk '{print $2}' | cut -d':' -f2)
    gw_ap_subnet=$(${OVSH} s Wifi_Route_State -w if_name=="$if_name" dest_addr -r)
    gre_remote_inet_addr="$(echo "$gw_ap_subnet" | cut -d'.' -f1-3).$(( $(echo "$gw_ap_subnet" | cut -d'.' -f4) +1 ))"

    add_bridge_port br-wan "$gre_if_name"
else
    die "tools/device/dut_ref/$(basename "$0"): Wrong if_role provided"
fi

create_inet_entry \
    -if_name "$gre_if_name" \
    -network true \
    -enabled true \
    -if_type gre \
    -ip_assign_scheme none \
    -mtu 1500 \
    -gre_ifname "$if_name" \
    -gre_remote_inet_addr "$gre_remote_inet_addr" \
    -gre_local_inet_addr "$gre_local_inet_addr" &&
        log "tools/device/dut_ref/$(basename "$0"): Gre interface $gre_if_name created" ||
        die "tools/device/dut_ref/$(basename "$0"): Failed to create Gre interface $gre_if_name"

add_bridge_port br-wan patch-w2h &&
    log "tools/device/dut_ref/$(basename "$0"): Success - add_bridge_port br-wan patch-w2h" ||
    die "tools/device/dut_ref/$(basename "$0"): Failed - add_bridge_port br-wan patch-w2h"

add_bridge_port br-home patch-h2w &&
    log "tools/device/dut_ref/$(basename "$0"): Success - add_bridge_port br-wan patch-h2w" ||
    die "tools/device/dut_ref/$(basename "$0"): Failed - add_bridge_port br-wan patch-h2w"

set_interface_patch patch-w2h patch-h2w patch-w2h &&
    log "tools/device/dut_ref/$(basename "$0"): Success - set_interface_patch patch-w2h patch-h2w" ||
    die "tools/device/dut_ref/$(basename "$0"): Failed - set_interface_patch patch-w2h patch-h2w"

set_interface_patch patch-h2w patch-w2h patch-h2w &&
    log "tools/device/dut_ref/$(basename "$0"): Success - set_interface_patch patch-h2w patch-w2h" ||
    die "tools/device/dut_ref/$(basename "$0"): Failed - set_interface_patch patch-h2w patch-w2h"

exit 0
