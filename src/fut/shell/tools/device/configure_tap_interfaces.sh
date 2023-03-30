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

usage()
{
    cat <<usage_string
tools/device/configure_tap_interfaces.sh [-h] arguments
Description:
    - Script configures all required TAP interfaces for home bridge
    - Bridges that will be configured:
        - <LAN-BRIDGE>.mdns
        - <LAN-BRIDGE>.ndp
        - <LAN-BRIDGE>.dns
        - <LAN-BRIDGE>.dpi
        - <LAN-BRIDGE>.upnp
        - <LAN-BRIDGE>.l2uf
        - <LAN-BRIDGE>.tx
        - <LAN-BRIDGE>.dhcp
        - <LAN-BRIDGE>.http
Arguments:
    -h  show this help message
    - \$1 (lan_bridge) : Name of LAN bridge interface : (string)(required)
Script usage example:
   ./tools/device/configure_tap_interfaces.sh br-home
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
NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
lan_bridge_if_name=${1}
tap_interface_list="l2uf mdns ndp dns dpi upnp tx dhcp http"
for tap_name in $tap_interface_list; do
    tap_if_name="${lan_bridge_if_name}.${tap_name}"
    case "${tap_name}" in
        mdns)
            of_port_request=216
            ;;
        ndp)
            of_port_request=208
            ;;
        dns)
            of_port_request=202
            ;;
        dpi)
            of_port_request=20001
            ;;
        upnp)
            of_port_request=205
            ;;
        l2uf)
            of_port_request=207
            ;;
        tx)
            of_port_request=204
            ;;
        http)
            of_port_request=203
            ;;
        dhcp)
            of_port_request=201
            ;;
        *)
            raise "Invalid interface type ${tap_name} - Failed" -l "tools/device/configure_tap_interfaces.sh" -tc
            ;;
    esac
    log "tools/device/configure_tap_interfaces.sh: Adding TAP interface ${tap_if_name}"
    add_tap_interface "${lan_bridge_if_name}" "${tap_if_name}" "${of_port_request}"
done

for tap_name in $tap_interface_list; do
    tap_if_name="${lan_bridge_if_name}.${tap_name}"
    create_fields=""
    case "${tap_name}" in
        dhcp)
            create_fields="-if_name ${tap_if_name} -no_flood true -dhcp_sniff true -network true -ip_assign_scheme none -if_type tap -enabled true"
            ;;
        mdns)
            create_fields="-if_name ${tap_if_name} -no_flood false -dhcp_sniff false -network true -ip_assign_scheme none -if_type tap -enabled true"
            ;;
        *)
            create_fields="-if_name ${tap_if_name} -no_flood true -dhcp_sniff false -network true -ip_assign_scheme none -if_type tap -enabled true"
            ;;
    esac
    create_inet_entry ${create_fields}
done

log "tools/device/configure_tap_interfaces.sh: LAN Bridge ${lan_bridge_if_name} TAP interfaces configured - Success"
exit 0
