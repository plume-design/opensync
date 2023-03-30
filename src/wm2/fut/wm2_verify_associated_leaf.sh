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

manager_setup_file="wm2/wm2_setup.sh"
usage()
{
cat << usage_string
wm2/wm2_verify_associated_leaf.sh [-h] arguments
Description:
    - Script gets associated client from Wifi_VIF_State for the given interface and checks Wifi_Associated_Clients for leaf mac
Arguments:
    -h  show this help message
    \$1 (vif_if_name)   : Wifi_VIF_Config::if_name             : (string)(required)
    \$2 (mac_addr)      : leaf MAC address                     : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_verify_associated_leaf <VIF_IF_NAME> <MAC_ADDR>
Script usage example:
    ./wm2/wm2_verify_associated_leaf.sh wl1.1 84:1e:a3:89:87:53
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
    fut_info_dump_line
    print_tables Wifi_Associated_Clients
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "wm2/wm2_verify_associated_leaf.sh" -arg
vif_if_name=${1}
mac_addr=${2}

log_title "wm2/wm2_verify_associated_leaf.sh: WM2 test - Checks leaf $mac_addr is associated to $vif_if_name"

wait_for_function_response 0 "${OVSH} s Wifi_Associated_Clients" &&
    log "wm2/wm2_verify_associated_leaf.sh: Wifi_Associated_Clients table populated - Success" ||
    raise "FAIL: Wifi_Associated_Clients table not populated" -l "wm2/wm2_verify_associated_leaf.sh" -tc

wait_for_function_response 'notempty' "get_ovsdb_entry_value Wifi_VIF_State associated_clients -w if_name ${vif_if_name}" &&
    assoc_leaf=$(get_ovsdb_entry_value Wifi_VIF_State associated_clients -w if_name "${vif_if_name}") ||
    raise "FAIL: Failed to retrieve leaf associated to ${vif_if_name}" -l "wm2/wm2_verify_associated_leaf.sh" -tc

check_ovsdb_entry Wifi_Associated_Clients  -w _uuid "[\"uuid\",\"${assoc_leaf}\"]"  -w mac "${mac_addr}" &&
    log "wm2/wm2_verify_associated_leaf.sh: check_ovsdb_entry - Leaf '${mac_addr}' is  associated to ${vif_if_name} - Success" ||
    raise "FAIL: check_ovsdb_entry - Leaf ${mac_addr} not associated to ${vif_if_name}" -l "wm2/wm2_verify_associated_leaf.sh" -tc

pass
