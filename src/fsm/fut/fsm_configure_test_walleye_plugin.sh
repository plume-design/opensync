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
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="fsm/fsm_setup.sh"
server_start_mqtt='tools/server/start_mqtt'
device_connect_mqtt='tools/device/fut_configure_mqtt'
# Default of_port must be unique between fsm tests for valid testing
of_port=10004

usage() {
    cat <<usage_string
fsm/fsm_configure_test_walleye_plugin.sh [-h] arguments
Description:
    - Script configures interfaces FSM settings for WallEye Plugin rules
Arguments:
    -h  show this help message
    \$1 (lan_bridge_if)  : Interface name used for LAN bridge  : (string)(required)
    \$2 (fsm_plugin)     : Path to FSM plugin under test       : (string)(required)
Testcase procedure:
    - On Server: Configure MQTT on server
            Run: ./${server_start_mqtt} --start (see ${create_rad_vif_if_file} -h)
    - On DEVICE: Connect DUT to MQTT server on RPI server
            Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
            Run: ./${device_connect_mqtt} (see ${create_rad_vif_if_file} -h)
Script usage example:
    ./fsm/fsm_configure_test_walleye_plugin.sh br-home /usr/opensync/lib/libfsm_walleye_dpi.so
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
print_tables Openflow_Config Openflow_State
print_tables Flow_Service_Manager_Config FSM_Policy
print_tables Object_Store_State
fut_info_dump_line
' EXIT SIGINT SIGTERM

# INPUT ARGUMENTS:
NARGS=2
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
# Input arguments specific to GW, required:
lan_bridge_if=${1}
fsm_plugin=${2}

of_out_token=dev_flow_demo_dpi_out
of_out_rule_ct="\"ct_state=-trk,ip\""
of_out_action_ct="\"ct(table=7,zone=1)\""
of_out_rule_ct_inspect_new_conn="\"ct_state=+trk,ct_mark=0,ip\""
of_out_action_ct_inspect_new_conn="\"ct(commit,zone=1,exec(load:0x1->NXM_NX_CT_MARK[])),NORMAL,output:${of_port}\""
of_out_rule_ct_inspect="\"ct_zone=1,ct_state=+trk,ct_mark=1,ip\""
of_out_action_ct_inspect="\"NORMAL,output:${of_port}\""
of_out_rule_ct_passthru="\"ct_zone=1,ct_state=+trk,ct_mark=2,ip\""
of_out_action_ct_passthru="\"NORMAL\""
of_out_rule_ct_drop="\"ct_state=+trk,ct_mark=3,ip\""
of_out_action_ct_drop="\"DROP\""

tap_dpi_if="${lan_bridge_if}.dpiwn"

log_title "fsm/fsm_configure_test_walleye_plugin.sh: FSM test - Configure walleye plugin"

log "fsm/fsm_configure_test_walleye_plugin.sh: Configuring TAP interfaces required for FSM testing"
add_bridge_port "${lan_bridge_if}" "${tap_dpi_if}"
set_ovs_vsctl_interface_option "${tap_dpi_if}" "type" "internal"
set_ovs_vsctl_interface_option "${tap_dpi_if}" "ofport_request" "${of_port}"
create_inet_entry \
    -if_name "${tap_dpi_if}" \
    -if_type "tap" \
    -ip_assign_scheme "none" \
    -dhcp_sniff "false" \
    -network true \
    -enabled true &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Interface ${tap_dpi_if} created - Success" ||
        raise "FAIL: Failed to create interface ${tap_dpi_if}" -l "fsm/fsm_configure_test_walleye_plugin.sh" -ds

log "fsm/fsm_configure_test_walleye_plugin.sh: Cleaning FSM OVSDB Config tables"
empty_ovsdb_table Openflow_Config
empty_ovsdb_table Flow_Service_Manager_Config
empty_ovsdb_table FSM_Policy

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i table 0 \
    -i priority 0 \
    -i bridge "${lan_bridge_if}" \
    -i action "NORMAL" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i table 0 \
    -i priority 200 \
    -i bridge "${lan_bridge_if}" \
    -i action "resubmit\(,7\)" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i table 7 \
    -i priority 0 \
    -i bridge "${lan_bridge_if}" \
    -i action "NORMAL" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
     -i token "${of_out_token}" \
     -i bridge "${lan_bridge_if}" \
     -i table 7 \
     -i priority 200 \
     -i rule "${of_out_rule_ct}" \
     -i action "${of_out_action_ct}" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_inspect_new_conn}" \
    -i action "${of_out_action_ct_inspect_new_conn}" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_inspect}" \
    -i action "${of_out_action_ct_inspect}" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_passthru}" \
    -i action "${of_out_action_ct_passthru}" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_drop}" \
    -i action "${of_out_action_ct_drop}" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

mqtt_hero_value="dev-test/dev_dpi_walleye/$(get_node_id)/$(get_location_id)"
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler "dev_dpi_walleye" \
    -i type "dpi_plugin" \
    -i plugin "${fsm_plugin}" \
    -i other_config '["map",[["mqtt_v","'"${mqtt_hero_value}"'"],["dso_init","walleye_dpi_plugin_init"],["dpi_dispatcher","core_dpi_dispatch"]]]' &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "fsm/fsm_configure_test_walleye_plugin.sh" -oe

wait_ovsdb_entry Object_Store_State \
    -is name "app_signatures" \
    -is status "active" &&
        log "fsm/fsm_configure_test_walleye_plugin.sh: walleye signature added - Success" ||
        raise "FAIL: walleye signature not added" -l "fsm/fsm_configure_test_walleye_plugin.sh" -tc
