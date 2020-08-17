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


# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/lib/cm2_lib.sh
source ${LIB_OVERRIDE_FILE}

log "tools/device/$(basename "$0"): Redirecting device to simulated cloud on RPI server"

target=${1:-"192.168.200.1"}
port=${2:-"443"}
cert_dir=${3:-"${FUT_TOPDIR}/shell/tools/device/files"}
ca_fname=${4:-"fut_ca.pem"}

wan_if="br-wan"
wan_port="eth0"

# Ensure startup procedure
stop_wd_man_hc ||
    die "tools/device/$(basename "$0"): stop_wd_man_hc - Failed"

start_openswitch ||
    die "tools/device/$(basename "$0"): start_openswitch - Failed"

cm_disable_fatal_state ||
    die "tools/device/$(basename "$0"): cm_disable_fatal_state - Failed"

# Ensure upstream connectivity
log "tools/device/$(basename "$0"): Create WAN bridge"
add_bridge_interface "$wan_if" "$wan_port" ||
    die "tools/device/$(basename "$0"): add_bridge_interface $wan_if $wan_port - Failed"

log "tools/device/$(basename "$0"): Add interface to WAN bridge"
add_bridge_port "$wan_if" "$wan_port" ||
    die "tools/device/$(basename "$0"): add_bridge_port $wan_if $wan_port - Failed"

log "tools/device/$(basename "$0"): Bring up interface $wan_if"
ifconfig "$wan_if" up ||
    die "tools/device/$(basename "$0"): Failed to bring up interface $wan_if"

log "tools/device/$(basename "$0"): Check for dhcp client and WAN IP"
start_udhcpc "$wan_if" true ||
    die "tools/device/$(basename "$0"): start_udhcpc - Failed"

log "Check for ${target} ping"
ping -c5 "$target" >/dev/null 2>&1 ||
    log "tools/device/$(basename "$0"): $wan_if did not get IP lease - Failed"

start_specific_manager cm -v ||
    die "tools/device/$(basename "$0"): start_specific_manager cm - Failed"

log "tools/device/$(basename "$0"): Configure certificates"
test -f "$cert_dir/$ca_fname" || die "tools/device/$(basename "$0"): file not found - Failed"

update_ovsdb_entry SSL \
    -u ca_cert "$cert_dir/$ca_fname"

log "Configure uplink information"
insert_ovsdb_entry Connection_Manager_Uplink \
    -i if_name "$wan_port" \
    -i if_type eth \
    -i has_L2 true \
    -i has_L3 true \
    -i priority 2

log "Make CM happy"
insert_ovsdb_entry Wifi_Master_State \
    -i if_name "$wan_port" \
    -i if_type eth \
    -i network_state up \
    -i port_state active \
    -i inet_addr 0.0.0.0 \
    -i netmask 0.0.0.0

insert_ovsdb_entry Wifi_Master_State \
    -i if_name "$wan_if" \
    -i if_type bridge \
    -i network_state up \
    -i port_state active \
    -i inet_addr 192.168.200.10 \
    -i netmask 255.255.255.0

# Inactivity probe sets the timing of keepalive packets
update_ovsdb_entry Manager \
    -u inactivity_probe 60000

for i in $(seq 1 3); do
        # Remove redirector, to not interfere with the flow
        update_ovsdb_entry AWLAN_Node \
            -u redirector_addr ''
        # AWLAN_Node::manager_addr is the controller address, provided by redirector
        update_ovsdb_entry AWLAN_Node \
            -u manager_addr "ssl:$target:$port"
        # CM should ideally fill in Manager::target itself
        update_ovsdb_entry Manager \
            -u target "ssl:$target:$port"

        # Ensure ovsdb connection is maintained
        wait_cloud_state ACTIVE

        # If connection is maintained, succeed
        test $? && break
    done

log "tools/device/$(basename "$0"): CM Connected to simulated Cloud"
exit 0
