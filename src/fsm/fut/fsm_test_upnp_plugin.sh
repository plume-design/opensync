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
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="fsm/$(basename "$0")"
manager_setup_file="fsm/fsm_setup.sh"
create_rad_vif_if_file="tools/device/create_radio_vif_interface.sh"
create_inet_file="tools/device/create_inet_interface.sh"
add_bridge_port_file="tools/device/add_bridge_port.sh"
configure_lan_bridge_for_wan_connectivity_file="tools/device/configure_lan_bridge_for_wan_connectivity.sh"
client_connect_file="tools/rpi/connect_to_wpa2.sh"
client_upnp_server_file="tools/rpi/fsm/upnp/upnp_server.py"
usage() {
    cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script checks logs for FSM UPnP message creation - fsm_send_report
Arguments:
    -h  show this help message
    \$1 (deviceType)       : UPnP Device deviceType value       : (string)(required)
    \$2 (friendlyName)     : UPnP Device friendlyName value     : (string)(required)
    \$3 (manufacturer)     : UPnP Device manufacturer value     : (string)(required)
    \$4 (manufacturerURL)  : UPnP Device manufacturerURL value  : (string)(required)
    \$5 (modelDescription) : UPnP Device modelDescription value : (string)(required)
    \$6 (modelName)        : UPnP Device modelName value        : (string)(required)
    \$7 (modelNumber)      : UPnP Device modelNumber value      : (string)(required)
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
            Configure FSM for UPnP plugin test
                Run: ./${tc_name} <LAN-BRIDGE-IF> <FSM-URL-BLOCK> <FSM-URL-REDIRECT>
    - On RPI Client:
            Configure Client to DUT
                Run: /.${client_connect_file} (see ${client_connect_file} -h)
            Bring up UPnP Server on Client
                Run /.${client_upnp_server_file} (see ${client_upnp_server_file} -h)
    - On DEVICE: Run: ./${tc_name} <deviceType> <friendlyName> <manufacturer> <manufacturerURL> <modelDescription> <modelName> <modelNumber>
Script usage example:
    ./${tc_name} 'urn:plume-test:device:test:1' 'FUT test device' 'FUT testing, Inc' 'https://www.fut.com' 'FUT UPnP service' 'FUT tester' '1.0'
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

# INPUT ARGUMENTS:
NARGS=7
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input argument(s)" -arg
deviceType=${1}
friendlyName=${2}
manufacturer=${3}
manufacturerURL=${4}
modelDescription=${5}
modelName=${6}
modelNumber=${7}

client_mac=$(get_ovsdb_entry_value Wifi_Associated_Clients mac)
if [ -z "${client_mac}" ]; then
    raise "Could not acquire Client mac address from Wifi_Associated_Clients, is client connected?" -l "${tc_name}"
fi
# shellcheck disable=SC2018,SC2019
client_mac=$(echo "${client_mac}" | tr a-z A-Z)
# Use first MAC from Wifi_Associated_Clients
client_mac="${client_mac%%,*}"

# FSM logs objects in non-constant order, reason for multiple grep-s
fsm_message_regex="$LOGREAD |
 tail -500 |
 grep fsm_send_report |
 grep upnpInfo |
 grep deviceType |
 grep '${deviceType}' |
 grep friendlyName |
 grep '${friendlyName}' |
 grep manufacturer |
 grep '${manufacturer}' |
 grep manufacturerURL |
 grep '${manufacturerURL}' |
 grep modelDescription |
 grep '${modelDescription}' |
 grep modelName |
 grep '${modelName}' |
 grep modelNumber |
 grep '${modelNumber}' |
 grep deviceMac |
 grep '${client_mac}' |
 grep locationId |
 grep $(get_location_id) |
 grep nodeId |
 grep $(get_node_id)"
wait_for_function_response 0 "${fsm_message_regex}" 5 &&
    log -deb "$tc_name: FSM UPnP Plugin creation message found in logs" ||
    raise "Failed to find FSM UPnP message creation in logs, regex used: ${fsm_message_regex} " -l "$tc_name" -tc
