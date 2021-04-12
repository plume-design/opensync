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
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="fsm/$(basename "$0")"
manager_setup_file="fsm/fsm_setup.sh"
create_rad_vif_if_file="tools/device/create_radio_vif_interface.sh"
create_inet_file="tools/device/create_inet_interface.sh"
add_bridge_port_file="tools/device/add_bridge_port.sh"
configure_lan_bridge_for_wan_connectivity_file="tools/device/configure_lan_bridge_for_wan_connectivity.sh"
client_connect_file="tools/rpi/connect_to_wpa2.sh"
fsm_dig_url_file="tools/rpi/fsm/fsm_dig_url.sh"
# Default of_port must be unique between fsm tests for valid testing
of_port=30002

usage() {
    cat <<usage_string
${tc_name} [-h] arguments
Description:
    - Script configures interfaces FSM settings for WallEye Plugin rules
Arguments:
    -h  show this help message
    \$1 (lan_bridge_if)    : Interface name used for LAN bridge        : (string)(required)
    \$2 (fsm_plugin)       : Path to FSM plugin under test             : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
            Create Radio/VIF interface
                Run: ./${create_rad_vif_if_file} (see ${create_rad_vif_if_file} -h)
            Create Inet entry for VIF interface
                Run: ./${create_inet_file} (see ${create_inet_file} -h)
            Create Inet entry for home bridge interface (br-home)
                Run: ./${create_inet_file} (see ${create_inet_file} -h)
            Add bridge port to VIF interface onto home bridge
                Run: ./${add_bridge_port_file} (see ${add_bridge_port_file} -h)
            Configure WAN bridge settings
                Run: ./${configure_lan_bridge_for_wan_connectivity_file} (see ${configure_lan_bridge_for_wan_connectivity_file} -h)
            Update Inet entry for home bridge interface for dhcpd (br-home)
                Run: ./${create_inet_file} (see ${create_inet_file} -h)
            Configure FSM for DNS plugin test
                Run: ./${tc_name} <LAN-BRIDGE-IF> <FSM-URL-BLOCK> <FSM-URL-REDIRECT>
   - On RPI Client:
                 Run: /.${client_connect_file} (see ${client_connect_file} -h)
                 Run: /.${fsm_dig_url_file} (see ${fsm_dig_url_file} -h)
Script usage example:
    ./${tc_name} br-home /usr/plume/lib/libfsm_walleye_dpi.so
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

log_title "$tc_name: FSM test - Configure walleye plugin"

log "$tc_name: Configuring TAP interfaces required for FSM testing"
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
        log -deb "$tc_name: Interface ${tap_dpi_if} successfully created" ||
        raise "Failed to create interface ${tap_dpi_if}" -l "$tc_name" -ds

log "$tc_name: Cleaning FSM OVSDB Config tables"
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
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i table 0 \
    -i priority 200 \
    -i bridge "${lan_bridge_if}" \
    -i action "resubmit(,7)" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i table 7 \
    -i priority 0 \
    -i bridge "${lan_bridge_if}" \
    -i action "NORMAL" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
     -i token "${of_out_token}" \
     -i bridge "${lan_bridge_if}" \
     -i table 7 \
     -i priority 200 \
     -i rule "${of_out_rule_ct}" \
     -i action "${of_out_action_ct}" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_inspect_new_conn}" \
    -i action "${of_out_action_ct_inspect_new_conn}" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_inspect}" \
    -i action "${of_out_action_ct_inspect}" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_passthru}" \
    -i action "${of_out_action_ct_passthru}" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "${of_out_token}" \
    -i bridge "${lan_bridge_if}" \
    -i table 7 \
    -i priority 200 \
    -i rule "${of_out_rule_ct_drop}" \
    -i action "${of_out_action_ct_drop}" &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

mqtt_hero_value="dev-test/dev_dpi_walleye/$(get_node_id)/$(get_location_id)"
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler "dev_dpi_walleye" \
    -i type "dpi_plugin" \
    -i plugin "${fsm_plugin}" \
    -i other_config '["map",[["mqtt_v","'"${mqtt_hero_value}"'"],["dso_init","walleye_dpi_plugin_init"],["dpi_dispatcher","core_dpi_dispatch"]]]' &&
        log "$tc_name: Inserting ingress rule" ||
        raise "Failed to insert_ovsdb_entry" -l "$tc_name" -oe

fsm_message_regex="$LOGREAD | tail -500 | grep walleye_signature_load | grep succeeded"
wait_for_function_response 0 "${fsm_message_regex}" 5 &&
    log -deb "$tc_name: walleye signature loaded" ||
    raise "walleye signature not loaded" -l "$tc_name" -tc

wait_ovsdb_entry Object_Store_State \
    -is name "app_signatures" \
    -is status "active" &&
        log "$tc_name: walleye signature added" ||
        raise "walleye signature not added" -l "$tc_name" -tc
