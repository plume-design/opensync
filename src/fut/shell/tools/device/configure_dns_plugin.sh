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
create_rad_vif_if_file="tools/device/create_radio_vif_interface.sh"
create_inet_file="tools/device/create_inet_interface.sh"
add_bridge_port_file="tools/device/add_bridge_port.sh"
configure_lan_bridge_for_wan_connectivity_file="tools/device/configure_lan_bridge_for_wan_connectivity.sh"
client_connect_file="tools/client/connect_to_wpa.sh"
fsm_test_dns_file="tools/client/fsm/fsm_test_dns_plugin.sh"

usage() {
    cat <<usage_string
tools/device/configure_dns_plugin.sh [-h] arguments
Description:
    - Script configures interfaces FSM settings for DNS blocking rules
Arguments:
    -h  show this help message
    \$1 (lan_bridge_if_name)  : Interface name used for LAN bridge        : (string)(required)
    \$2 (fsm_url_block)       : URL for site to be blocked trough FSM     : (string)(required)
    \$3 (fsm_url_redirect)    : IP address to redirect <FSM-URL-BLOCK> to : (string)(required)
    \$4 (fsm_plugin)          : Path to FSM plugin under test             : (string)(required)
    \$5 (wc_plugin)           : Path to Web Categorization plugin         : (string)(required)
    \$6 (mqtt_topic)          : Value of MQTT topic                       : (string)(required)
    \$7 (client_mac)          : Connected client MAC address              : (string)(required)
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
                Run: ./tools/device/configure_dns_plugin.sh <LAN-BRIDGE-IF> <FSM-URL-BLOCK> <FSM-URL-REDIRECT> <FSM-PLUGIN> <WC-PLUGIN> <MQTT-TOPIC> <CLIENT-MAC>
   - On Client:
                 Run: /.${client_connect_file} (see ${client_connect_file} -h)
                 Run: /.${fsm_test_dns_file} (see ${fsm_test_dns_file} -h)
Script usage example:
    ./tools/device/configure_dns_plugin.sh br-home google.com 1.2.3.4 /usr/opensync/lib/libfsm_dns.so /usr/opensync/lib/libfsm_wcnull.so fut_fsm_mqtt_topic ff:ff:ff:ff:ff
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
print_tables Wifi_Associated_Clients Openflow_Config
print_tables Flow_Service_Manager_Config FSM_Policy
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

# INPUT ARGUMENTS:
NARGS=7
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input argument(s)" -arg
# Input arguments specific to GW, required:
lan_bridge_if_name=${1}
fsm_url_block=${2}
fsm_url_redirect=${3}
fsm_plugin=${4}
wc_plugin=${5}
mqtt_topic=${6}
client_mac=${7}

tap_dns_if="${lan_bridge_if_name}.dns"
tap_tx_if="${lan_bridge_if_name}.tx"
of_port=$(ovs-vsctl get Interface "${tap_dns_if}" ofport)
in_port=$(ovs-vsctl get Interface "${tap_tx_if}" ofport)

log_title "tools/device/configure_dns_plugin.sh: FSM test - Configuring DNS plugin required for FSM testing"

log "tools/device/configure_dns_plugin.sh: Cleaning FSM OVSDB Config tables"
empty_ovsdb_table Openflow_Config
empty_ovsdb_table Flow_Service_Manager_Config
empty_ovsdb_table FSM_Policy
empty_ovsdb_table Openflow_Tag

log "tools/device/configure_dns_plugin.sh: Adding tags to Openflow_Tag"
insert_ovsdb_entry Openflow_Tag \
    -i name "client_mac" \
    -i cloud_value '["set",["'${client_mac}'"]]'

# Insert egress rule to Openflow_Config
insert_ovsdb_entry Openflow_Config \
    -i token "dev_flow_dns_out" \
    -i table 0 \
    -i rule 'dl_src=${client_mac},udp,tp_dst=53' \
    -i priority 200 \
    -i bridge "${lan_bridge_if_name}" \
    -i action "normal,output:${of_port}" &&
        log "tools/device/configure_dns_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "tools/device/configure_dns_plugin.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "dev_flow_dns_in" \
    -i table 0 \
    -i rule 'dl_dst=${client_mac},udp,tp_src=53' \
    -i priority 200 \
    -i bridge "${lan_bridge_if_name}" \
    -i action "output:${of_port}" &&
        log "tools/device/configure_dns_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "tools/device/configure_dns_plugin.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "dev_flow_dns_tx" \
    -i action "normal" \
    -i bridge "${lan_bridge_if_name}" \
    -i priority 300 \
    -i rule "in_port=${in_port}" \
    -i table 0 &&
        log "tools/device/configure_dns_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "tools/device/configure_dns_plugin.sh" -oe

insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler dev_wc_null \
    -i plugin "${wc_plugin}" \
    -i type web_cat_provider \
    -i other_config '["map",[["dso_init","fsm_wc_null_plugin_init"]]]' &&
        log "tools/device/configure_dns_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "tools/device/configure_dns_plugin.sh" -oe

insert_ovsdb_entry Flow_Service_Manager_Config \
    -i if_name "${tap_dns_if}" \
    -i pkt_capt_filter 'udp port 53' \
    -i other_config '["map",[["mqtt_v","'${mqtt_topic}'"],["dso_init","dns_plugin_init"],["provider_plugin","dev_wc_null"],["policy_table","dev_dns_policy"]]]' \
    -i handler dev_dns \
    -i type parser \
    -i plugin "${fsm_plugin}" &&
        log "tools/device/configure_dns_plugin.sh: Flow_Service_Manager_Config entry added - Success" ||
        raise "FAIL: Failed to insert Flow_Service_Manager_Config entry" -l "tools/device/configure_dns_plugin.sh" -oe

insert_ovsdb_entry FSM_Policy \
    -i policy dev_dns_policy \
    -i name dev_dns_policy_rule_0 \
    -i idx 9 \
    -i action allow \
    -i log all \
    -i redirect "A-${fsm_url_redirect}" \
    -i fqdns "${fsm_url_block}" \
    -i fqdn_op sfr_in &&
        log "tools/device/configure_dns_plugin.sh: Ingress rule inserted - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry" -l "tools/device/configure_dns_plugin.sh" -oe
