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


# Clean up after tests for FSM.

# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

fsm_vif_name=${1}
shift
dut_if_lan_br_name=${1}
shift

log "fsm/fsm_cleanup.sh: Cleaning FSM OVSDB Config tables"
empty_ovsdb_table Openflow_Config
empty_ovsdb_table Flow_Service_Manager_Config
empty_ovsdb_table FSM_Policy

if [ "${fsm_vif_name}" != false ]; then
    log "fsm/fsm_cleanup.sh: Removing $fsm_vif_name from $dut_if_lan_br_name"
    remove_port_from_bridge "$dut_if_lan_br_name" "$fsm_vif_name" &&
        log "fsm/fsm_cleanup.sh: remove_port_from_bridge - Removed $fsm_vif_name from $dut_if_lan_br_name - Success" ||
        log -err "fsm/fsm_cleanup.sh: Failed to remove $fsm_vif_name from $dut_if_lan_br_name"
    log "fsm/fsm_cleanup.sh: Removing Wifi_VIF_Config '${fsm_vif_name}' entry"
    remove_ovsdb_entry Wifi_VIF_Config -w if_name "${fsm_vif_name}" &&
        log "fsm/fsm_cleanup.sh: Wifi_VIF_Config::if_name = ${fsm_vif_name} entry removed - Success" ||
        log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_VIF_Config::if_name = ${fsm_vif_name} entry"
    wait_ovsdb_entry_remove Wifi_VIF_State -w if_name "${fsm_vif_name}"  &&
        log "fsm/fsm_cleanup.sh: Wifi_VIF_State::if_name = ${fsm_vif_name} entry removed - Success" ||
        log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_VIF_State::if_name = ${fsm_vif_name} entry"

    log "fsm/fsm_cleanup.sh: Removing Wifi_Inet_Config '${fsm_vif_name}' entry"
    remove_ovsdb_entry Wifi_Inet_Config -w if_name "${fsm_vif_name}" &&
        log "fsm/fsm_cleanup.sh: Wifi_Inet_Config::if_name = ${fsm_vif_name} entry removed - Success" ||
        log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_Inet_Config::if_name = ${fsm_vif_name} entry"
    wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "${fsm_vif_name}"  &&
        log "fsm/fsm_cleanup.sh: Wifi_Inet_State::if_name = ${fsm_vif_name} entry removed - Success" ||
        log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_Inet_State::if_name = ${fsm_vif_name} entry"
fi

for fsm_inet_if_name in "$@"
    do
        log "fsm/fsm_cleanup.sh: Removing Wifi_Inet_Config '${fsm_inet_if_name}' entry"
        remove_ovsdb_entry Wifi_Inet_Config -w if_name "${fsm_inet_if_name}" &&
            log "fsm/fsm_cleanup.sh: Wifi_Inet_Config::if_name = ${fsm_inet_if_name} entry removed - Success" ||
            log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_Inet_Config::if_name = ${fsm_inet_if_name} entry"
        wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "${fsm_inet_if_name}" &&
            log "fsm/fsm_cleanup.sh: Wifi_Inet_State::if_name = ${fsm_inet_if_name} entry removed - Success" ||
            log -err "fsm/fsm_cleanup.sh: Failed to remove Wifi_Inet_State::if_name = ${fsm_inet_if_name} entry"
        log "fsm/fsm_cleanup.sh: Removing $fsm_inet_if_name from $dut_if_lan_br_name"
        remove_port_from_bridge "$dut_if_lan_br_name" "$fsm_inet_if_name" &&
            log "fsm/fsm_cleanup.sh: remove_port_from_bridge - Removed $fsm_inet_if_name from $dut_if_lan_br_name - Success" ||
            log -err "fsm/fsm_cleanup.sh: Failed to remove $fsm_inet_if_name from $dut_if_lan_br_name"
    done

print_tables Wifi_Inet_Config
print_tables Wifi_Inet_State
print_tables Wifi_VIF_Config
print_tables Wifi_VIF_State

ovs-vsctl show
