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


# Clean up after tests for ONBRD.

# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="onbrd/$(basename "$0")"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script removes interface from Wifi_Inet_Config and Wifi_VIF_Config,
      and removes interface from lan_bridge on DUT device
Arguments:
    -h : show this help message
    \$1 (lan_bridge) : used for LAN bridge name : (string)(required)
    \$2 (if_name)    : used for interface name  : (string)(required)
Script usage example:
    ./${tc_name} br-home home-ap-l50
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

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
lan_bridge=${1}
if_name=${2}

log "$tc_name: Clean up interface from Wifi_Inet_Config: ${if_name}"
remove_ovsdb_entry Wifi_Inet_Config -w if_name "${if_name}" &&
    log "${tc_name}: OVSDB entry from Wifi_Inet_Config removed for $if_name" ||
    log -err "${tc_name}: Failed to remove OVSDB entry from Wifi_Inet_Config for $if_name"
remove_ovsdb_entry Wifi_VIF_Config -w if_name "${if_name}" &&
    log "${tc_name}: OVSDB entry from Wifi_VIF_Config removed for $if_name" ||
    log -err "${tc_name}: Failed to remove OVSDB entry from Wifi_VIF_Config for $if_name"

log "$tc_name: Removing $if_name from bridge ${lan_bridge}"
remove_port_from_bridge "${lan_bridge}" "${if_name}" &&
    log "$tc_name: remove_port_from_bridge - port $tap_if removed from $lan_bridge_if" ||
    raise "remove_port_from_bridge - port $tap_if NOT removed from $lan_bridge_if" -l "$tc_name" -tc

pass
