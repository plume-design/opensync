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

def_association_timeout=10
usage()
{
cat << usage_string
wm2/wm2_verify_associated_clients.sh [-h] arguments
Description:
    - Script to verify Wifi_Associated_Clients table is populated with correct values when client is connected to DUT.
Arguments:
    -h  show this help message
    \$1 (vif_if_name)         : Wifi_VIF_Config::if_name                   : (string)(required)
    \$2 (mac_addr)            : MAC address of client connected to ap      : (string)(required)
    \$3 (association_timeout) : Time to wait for Client to be associated   : (string)(optional) : (default:${def_association_timeout})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_verify_associated_clients.sh <IF-NAME> <CLIENT MAC>
Script usage example:
    ./wm2/wm2_verify_associated_clients.sh wl0.2 a1:b2:c3:d4:e5:f6

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
[ $# -ne ${NARGS} ] && usage && raise "Requires at least ${NARGS} input argument" -l "wm2/wm2_verify_associated_clients.sh" -arg

vif_if_name=${1}
mac_addr=${2}
association_timeout=${3:-$def_association_timeout}

log_title "wm2/wm2_verify_associated_clients.sh: WM2 test - Verify Wifi_Associated_Clients table is populated with client MAC"

wait_for_function_response 0 "${OVSH} s Wifi_Associated_Clients" ${association_timeout} &&
    log "wm2/wm2_verify_associated_clients.sh: Wifi_Associated_Clients table populated - Success" ||
    raise "FAIL: Wifi_Associated_Clients table not populated" -l "wm2/wm2_verify_associated_clients.sh" -tc

wait_for_function_response 'notempty' "get_ovsdb_entry_value Wifi_VIF_State associated_clients -w if_name ${vif_if_name}" ${association_timeout} &&
    assoc_clients_res=$(get_ovsdb_entry_value Wifi_VIF_State associated_clients -w if_name "${vif_if_name}") ||
    raise "FAIL: Failed to retrieve client associated to VIF ${vif_if_name}" -l "wm2/wm2_verify_associated_clients.sh" -tc

wait_ovsdb_entry Wifi_Associated_Clients  -w _uuid "[\"uuid\",\"${assoc_clients_res}\"]" -is mac "${mac_addr}" -is state "active" -t ${association_timeout} &&
    log "wm2/wm2_verify_associated_clients.sh: check_ovsdb_entry - client '${mac_addr}' is  associated to ${vif_if_name} - Success" ||
    raise "FAIL: check_ovsdb_entry - client ${mac_addr} not associated to ${vif_if_name}" -l "wm2/wm2_verify_associated_clients.sh" -tc

pass
