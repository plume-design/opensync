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
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM
tc_name="tools/device/$(basename "$0")"
patch_w2h_default="patch-w2h"
patch_h2w_default="patch-h2w"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures WAN connectivity on LAN bridge.
    - The procedure is different depending on the device implementing WANO or not! Example for "wan_br" parameter:
        HAS_WANO==false: "wan_br" is required, it will be used for LAN connectivity.
        HAS_WANO==true:  "wan_br" is still required by the script, but will not be used for WAN connectivity, as the
                         "wan_eth" is put into "lan_br" directly.
                         It is recommended to input a dummy value, like "None".
Arguments:
    -h  show this help message
    - \$1 (wan_eth)     : Physical ethernet interface name for WAN uplink : (string)(required)
    - \$2 (wan_br)      : WAN bridge name (only if HAS_WANO=true)         : (string)(required)
    - \$3 (lan_br)      : LAN bridge name                                 : (string)(required)
    If NOT HAS_WANO:
    - \$4 (patch_w2h)   : WAN-to-LAN patch port name                      : (string)(optional)(default: $patch_w2h_default)
    - \$5 (patch_h2w)   : LAN-to-WAN patch port name                      : (string)(optional)(default: $patch_h2w_default)
Script usage example:
   ./${tc_name} eth0 br-wan br-home
usage_string
}
while getopts h option > /dev/null 2>&1; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
done

check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    is_wano="true" ||
    is_wano="false"
log "${tc_name}: Is WANO enabled: ${is_wano}"

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

wan_eth=${1}
wan_br=${2}
lan_br=${3}
patch_w2h=${4:-$patch_w2h_default}
patch_h2w=${5:-$patch_h2w_default}

if [ "${is_wano}" == "false" ];then
    # Patch WAN and LAN bridges
    add_bridge_port "${wan_br}" "${wan_eth}"
    create_inet_entry2 \
        -if_name "${wan_br}" \
        -if_type "bridge" \
        -ip_assign_scheme "dhcp" \
        -network "true" \
        -enabled "true" &&
            log -deb "$tc_name: Interface ${wan_br} successfully created" ||
            raise "Failed to create interface ${wan_br}" -l "$tc_name" -ds
    add_bridge_port "${wan_br}" "${patch_w2h}"
    set_interface_patch "${wan_br}" "${patch_w2h}" "${patch_h2w}"
    add_bridge_port "${lan_br}" "${patch_h2w}"
    set_interface_patch "${lan_br}" "${patch_h2w}" "${patch_w2h}"
    udhcpc_if=${wan_br}
else
    # Put WAN ETH interface into LAN bridge directly
    add_bridge_port "${lan_br}" "${wan_eth}"
    create_inet_entry2 \
        -if_name "${lan_br}" \
        -if_type "bridge" \
        -ip_assign_scheme "dhcp" \
        -network "true" \
        -enabled "true" &&
            log -deb "$tc_name: Interface ${lan_br} successfully created" ||
            raise "Failed to create interface ${lan_br}" -l "$tc_name" -ds
    udhcpc_if=${lan_br}
fi

wait_for_function_response 0 "check_pid_udhcp ${udhcpc_if}" 30 &&
    log -deb "UDHCPC running on ${udhcpc_if}" ||
    raise "UDHCPC not running on ${udhcpc_if}" -ds
