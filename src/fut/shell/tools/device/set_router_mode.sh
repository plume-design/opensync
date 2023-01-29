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
tools/device/set_router_mode.sh [-h] arguments
Description:
    - Scripts sets device working mode into Router mode
Arguments:
    -h  show this help message
    - \$1 (i_if)        : Name of interface to set UPnP mode 'internal' : (string)(required)
    - \$2 (i_dhcpd)     : Internal interface dhcpd                      : (string)(required)
    - \$3 (i_inet_addr) : Internal interface inet_addr                  : (string)(required)
    - \$4 (e_if)        : Name of interface to set UPnP mode 'external' : (string)(required)
Script usage example:
   ./tools/device/set_router_mode.sh br-home home-ap-l50
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
NARGS=4
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
internal_if_name=${1}
internal_dhcpd=${2}
internal_inet_addr=${3}
external_if_name=${4}

# Fixed args internal
internal_NAT=false
internal_ip_assign_scheme=static
internal_upnp_mode=internal
internal_network=true
internal_enabled=true
internal_netmask=255.255.255.0

# Fixed args external
external_NAT=true
external_ip_assign_scheme=dhcp
external_upnp_mode=external
external_network=true
external_enabled=true

log_title "tools/device/set_router_mode.sh: Putting device into router mode" ||
    log "tools/device/set_router_mode.sh: Removing bridge port ${external_if_name} from bridge ${internal_if_name} if present"
remove_bridge_port "${internal_if_name}" "${external_if_name}" &&
    log "tools/device/set_router_mode.sh: Bridge port removed or it did not existed in first place" ||
    raise "Removal of bridge port ${external_if_name} from bridge ${internal_if_name} - Failed" -l "tools/device/set_router_mode.sh" -tc

log "tools/device/set_router_mode.sh: Configuring internal interface - ${internal_if_name}"
create_inet_entry \
    -if_name "${internal_if_name}" \
    -dhcpd "${internal_dhcpd}" \
    -inet_addr "${internal_inet_addr}" \
    -netmask "${internal_netmask}" \
    -NAT "${internal_NAT}" \
    -ip_assign_scheme "${internal_ip_assign_scheme}" \
    -upnp_mode "${internal_upnp_mode}" \
    -network "${internal_network}" \
    -enabled "${internal_enabled}" &&
    log "tools/device/set_router_mode.sh: Internal interface ${internal_if_name} configured - Success" ||
    raise "Internal interface ${internal_if_name} configuration - Failed" -l "tools/device/set_router_mode.sh" -tc

log "tools/device/set_router_mode.sh: Configuring external interface - ${external_if_name}"
create_inet_entry \
    -if_name "${external_if_name}" \
    -NAT "${external_NAT}" \
    -ip_assign_scheme "${external_ip_assign_scheme}" \
    -upnp_mode "${external_upnp_mode}" \
    -network "${external_network}" \
    -enabled "${external_enabled}" &&
    log "tools/device/set_router_mode.sh: External interface ${external_if_name} configured - Success" ||
    raise "External interface ${external_if_name} configuration - Failed" -l "tools/device/set_router_mode.sh" -tc

log "tools/device/set_router_mode.sh: Device is in ROUTER mode - Success"
exit 0
