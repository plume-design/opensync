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

usage()
{
    cat <<usage_string
tools/device/set_bridge_mode.sh [-h] arguments
Description:
    - Scripts sets device working mode into Bridge mode
Arguments:
    -h  show this help message
    - \$1 (lan_bridge)  : Name of the LAN bridge interface : (string)(required)
    - \$2 (wan_if_name) : Primary WAN interface name       : (string)(required)
Script usage example:
   ./tools/device/set_bridge_mode.sh br-home eth0
usage_string
}
if [ -n "${1}" ] >/dev/null 2>&1; then
    case "${1}" in
    help | \
        --help | \
        -h)
        usage && exit 1
        ;;
    *) ;;

    esac
fi

# INPUT ARGUMENTS:
NARGS=2
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
bridge_if_name=${1}
wan_if_name=${2}

# Fixed args internal
bridge_NAT=false
bridge_ip_assign_scheme=dhcp
bridge_upnp_mode=disabled
bridge_network=true
bridge_enabled=true
bridge_dhcpd="[\"map\",[]]"

# Fixed args WAN interface
wan_NAT=false
wan_ip_assign_scheme=none
wan_upnp_mode=disabled
wan_network=true
wan_enabled=true
wan_netmask=0.0.0.0
wan_inet_addr=0.0.0.0
wan_dhcpd="[\"map\",[]]"

log_title "tools/device/set_bridge_mode.sh: Putting device into router mode" ||
    log "tools/device/set_bridge_mode.sh: Configuring WAN interface - ${wan_if_name}"
create_inet_entry \
    -if_name "${wan_if_name}" \
    -NAT "${wan_NAT}" \
    -ip_assign_scheme "${wan_ip_assign_scheme}" \
    -upnp_mode "${wan_upnp_mode}" \
    -network "${wan_network}" \
    -enabled "${wan_enabled}" \
    -netmask "${wan_netmask}" \
    -inet_addr "${wan_inet_addr}" \
    -dhcpd "${wan_dhcpd}" &&
    log "tools/device/set_bridge_mode.sh: WAN interface ${wan_if_name} configured - Success" ||
    raise "WAN interface ${wan_if_name} configuration - Failed" -l "tools/device/set_bridge_mode.sh" -tc

log "tools/device/set_bridge_mode.sh: Adding bridge port ${wan_if_name} to ${bridge_if_name}"
add_bridge_port "$bridge_if_name" "$wan_if_name" &&
    log "tools/device/set_bridge_mode.sh: Bridge port ${wan_if_name} added to bridge ${bridge_if_name} - Success" ||
    raise "Adding of bridge port ${wan_if_name} to bridge ${bridge_if_name} - Failed" -l "tools/device/set_bridge_mode.sh" -tc

log "tools/device/set_bridge_mode.sh: Configuring Bridge interface - ${bridge_if_name}"
create_inet_entry \
    -if_name "${bridge_if_name}" \
    -NAT "${bridge_NAT}" \
    -ip_assign_scheme "${bridge_ip_assign_scheme}" \
    -upnp_mode "${bridge_upnp_mode}" \
    -network "${bridge_network}" \
    -dhcpd "${bridge_dhcpd}" \
    -enabled "${bridge_enabled}" &&
    log "tools/device/set_bridge_mode.sh: Bridge interface ${bridge_if_name} configured - Success" ||
    raise "Bridge interface ${bridge_if_name} configuration - Failed" -l "tools/device/set_bridge_mode.sh" -tc

sleep 5

log "tools/device/set_bridge_mode.sh: Disable DHCP on - ${wan_if_name}"
create_inet_entry \
    -if_name "${wan_if_name}" \
    -ip_assign_scheme "${wan_ip_assign_scheme}" &&
    log "tools/device/set_bridge_mode.sh: WAN ${wan_if_name} DHCP disabled - Success" ||
    raise "Failed to disable DHCP on ${wan_if_name} - Failed" -l "tools/device/set_bridge_mode.sh" -tc

log "tools/device/set_bridge_mode.sh: Device is in BRIDGE mode - Success"
exit 0
