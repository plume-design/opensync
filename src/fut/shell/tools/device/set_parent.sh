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
cat << usage_string
tools/device/set_parent.sh [-h] arguments
Description:
    - Script updates the 'parent' field in Wifi_VIF_Config with the provided MAC address to ensure the
      connection with the correct parent node
Arguments:
    -h  show this help message
    \$1  (if_name)     : Wifi_VIF_Config::if_name : (string)(required)
    \$2  (mac_address) : LEAF MAC address         : (string)(required)
Script usage example:
    ./tools/device/set_parent.sh wl0 12:1e:a3:89:87:0d
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

trap '
fut_ec=$?
fut_info_dump_line
if [ $fut_ec -ne 0 ]; then
    print_tables Wifi_VIF_Config Wifi_VIF_State
fi
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "tools/device/set_parent.sh" -arg
if_name=${1}
mac_address=${2}

log_title "tools/device/set_parent.sh: Updating Wifi_VIF_Config::parent to $mac_address"

update_ovsdb_entry Wifi_VIF_Config -w if_name "$if_name" -u parent "$mac_address" &&
    log "tools/device/set_parent.sh: update_ovsdb_entry - Wifi_VIF_Config::parent is $mac_address - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_VIF_Config::parent is not $mac_address" -l "tools/device/set_parent.sh" -tc

wait_ovsdb_entry Wifi_VIF_State -w if_name "$if_name" -is parent "$mac_address" &&
    log "tools/device/set_parent.sh: wait_ovsdb_entry - Wifi_VIF_Config reflected to Wifi_VIF_State::parent is $mac_address - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_VIF_Config to Wifi_VIF_State::parent is not $mac_address" -l "tools/device/set_parent.sh" -tc

pass

