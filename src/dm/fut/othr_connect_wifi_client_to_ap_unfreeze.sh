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
source "${FUT_TOPDIR}/shell/lib/othr_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

freeze_src_token="device_freeze_src"
freeze_dst_token="device_freeze_dst"

usage() {
    cat <<usage_string
othr/othr_connect_wifi_client_to_ap_unfreeze.sh [-h] arguments
Description:
    - Script removes client freeze rules from Openflow tables (Openflow_Tag, Openflow_Config).
Arguments:
    -h  show this help message
Script usage example:
    ./othr/othr_connect_wifi_client_to_ap_unfreeze.sh
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
print_tables Openflow_Tag Openflow_Config
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=0
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input arguments" -arg

log_title "othr/othr_connect_wifi_client_to_ap_unfreeze.sh: OTHR test - Delete the Openflow rules from the tables to unfreeze the client"

log "othr/othr_connect_wifi_client_to_ap_unfreeze.sh: Removing client freeze rules from Openflow tables"
remove_ovsdb_entry Openflow_Tag -w name "frozen" &&
    log "othr/othr_connect_wifi_client_to_ap_unfreeze.sh: Removed entry for name 'frozen' from Openflow_Tag table - Success" ||
    raise "FAIL: Could not remove entry for name 'frozen' from Openflow_Tag table" -l "othr/othr_connect_wifi_client_to_ap_unfreeze.sh"

remove_ovsdb_entry Openflow_Config -w token "${freeze_src_token}" &&
    log "othr/othr_connect_wifi_client_to_ap_unfreeze.sh: Removed entry for token '${freeze_src_token}' from Openflow_Config table - Success" ||
    raise "FAIL: Could not remove entry for token '${freeze_src_token}' from Openflow_Config table" -l "othr/othr_connect_wifi_client_to_ap_unfreeze.sh"

remove_ovsdb_entry Openflow_Config -w token "${freeze_dst_token}" &&
    log "othr/othr_connect_wifi_client_to_ap_unfreeze.sh: Removed entry for token '${freeze_dst_token}' from Openflow_Config table - Success" ||
    raise "FAIL: Could not remove entry for token '${freeze_dst_token}' from Openflow_Config table" -l "othr/othr_connect_wifi_client_to_ap_unfreeze.sh"

pass
