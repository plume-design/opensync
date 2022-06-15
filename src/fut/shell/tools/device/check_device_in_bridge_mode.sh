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
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "" -olfm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "" -olfm

usage()
{
cat << usage_string
tools/device/check_device_in_bridge_mode.sh [-h] arguments
Description:
    - Script checks if device operates in 'bridge' mode
    - If device operates in 'bridge', script exits with exit code 0
    - If device does not operate in 'bridge' mode, script exits with exit code 1
Arguments:
    -h  show this help message
    - \$1 (wan_interface) : WAN interface name       : (string)(required)
    - \$2 (lan_bridge)    : LAN bridge name          : (string)(optional)
    - \$3 (wan_bridge)    : WAN bridge name          : (string)(optional)
    - \$4 (patch_w2h)     : Patch WAN-HOME port name : (string)(optional)
    - \$5 (patch_h2w)     : Patch HOME-WAN port name : (string)(optional)
Script usage example:
   ./tools/device/check_device_in_bridge_mode.sh eth0 br-lan br-wan patch-w2h patch-h2w
usage_string
}

if [ -n "${1}" ] > /dev/null 2>&1; then
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
NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
wan_if_name=${1}
lan_bridge=${2:-false}
wan_bridge=${3:-false}
patch_w2h=${4:-false}
patch_h2w=${5:-false}

trap '
fut_ec=$?
fut_info_dump_line
if [ $fut_ec -ne 0 ]; then 
    print_tables Port Bridge Wifi_Route_State
    check_restore_ovsdb_server
fi
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM

check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    is_wano=0 ||
    is_wano=1

# Check if WANO is enabled on device
if [ ${is_wano} -eq 1 ] && [ "${lan_bridge}" != false ] && [ "${wan_bridge}" != false ] && [ "${patch_w2h}" != false ] && [ "${patch_h2w}" != false ]; then
    # Check if patch ports are present in Port table, exit with 1 if not
    port_w2h=$(get_ovsdb_entry_value Port _uuid -w name "$patch_w2h" -r) || exit 1
    port_h2w=$(get_ovsdb_entry_value Port _uuid -w name "$patch_h2w" -r) || exit 1

    # Check if patch Port entries (uuid) are present in Bridge table, exit with 1 if not
    bridge_w2h=$(get_ovsdb_entry_value Bridge name -w ports "'${port_w2h}'") || exit 1
    bridge_h2w=$(get_ovsdb_entry_value Bridge name -w ports "'${port_h2w}'") || exit 1

    # Check if bridge entries are matching bridge names, exit with 0 if this applies, device is in bridge mode
    [ "${bridge_w2h}" == "${wan_bridge}" ] && [ "${bridge_h2w}" == "${lan_bridge}" ] && exit 0
else
    # If WANO is enabled, Wifi_Route_State should contain routing to 0.0.0.0 and interface name should match WAN interface name
    # exit with 0 if this applies, device is in bridge mode
    default_route_if_name=$(get_ovsdb_entry_value Wifi_Route_State if_name -w dest_addr 0.0.0.0) || exit 1
    [ "${default_route_if_name}" != "${wan_if_name}" ] || exit 0
fi

exit 1
