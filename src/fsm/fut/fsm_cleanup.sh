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
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="fsm/$(basename "$0")"

fsm_vif_name=${1}
shift

log "${tc_name}: Cleaning FSM OVSDB Config tables"
empty_ovsdb_table Openflow_Config
empty_ovsdb_table Flow_Service_Manager_Config
empty_ovsdb_table FSM_Policy

if [ "${fsm_vif_name}" != false ]; then
    log "${tc_name}: Removing Wifi_VIF_Config '${fsm_vif_name}' entry"
    remove_ovsdb_entry Wifi_VIF_Config -w if_name "${fsm_vif_name}" &&
        log "${tc_name}: VIF entry removed" ||
        log -err "${tc_name}: Failed to remove VIF entry"
    wait_ovsdb_entry_remove Wifi_VIF_State -w if_name "${fsm_vif_name}"  &&
        log "${tc_name}: VIF entry removed from State" ||
        log -err "${tc_name}: Failed to remove VIF entry from State"

    log "${tc_name}: Removing Wifi_Inet_Config '${fsm_vif_name}' entry"
    remove_ovsdb_entry Wifi_Inet_Config -w if_name "${fsm_vif_name}" &&
        log "${tc_name}: INET entry removed" ||
        log -err "${tc_name}: Failed to remove INET entry"
    wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "${fsm_vif_name}"  &&
        log "${tc_name}: INET entry removed from State" ||
        log -err "${tc_name}: Failed to remove INET entry from State"
fi

for fsm_inet_if_name in "$@"
    do
        log "${tc_name}: Removing Wifi_Inet_Config '${fsm_inet_if_name}' entry"
        remove_ovsdb_entry Wifi_Inet_Config -w if_name "${fsm_inet_if_name}" &&
            log "${tc_name}: INET entry removed" ||
            log -err "${tc_name}: Failed to remove INET entry"
        wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "${fsm_inet_if_name}"  &&
            log "${tc_name}: INET entry removed from State" ||
            log -err "${tc_name}: Failed to remove INET entry from State"
    done
