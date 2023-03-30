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

UNIT_NAME := fut_tools_device

UNIT_DISABLE := n

# Template type:
UNIT_TYPE := FUT
# Output directory
UNIT_DIR := shell/tools/device

UNIT_FILE := add_bridge_port.sh
UNIT_FILE += check_channel_is_ready.sh
UNIT_FILE += check_device_in_bridge_mode.sh
UNIT_FILE += check_kconfig_option.sh
UNIT_FILE += check_lib_plugin_exists.sh
UNIT_FILE += check_reboot_file_exists.sh
UNIT_FILE += check_wan_connectivity.sh
UNIT_FILE += check_wifi_client_associated.sh
UNIT_FILE += check_wpa3_compatibility.sh
UNIT_FILE += configure_ap_interface.sh
UNIT_FILE += configure_awlan_node.sh
UNIT_FILE += configure_dns_plugin.sh
UNIT_FILE += configure_dpi_openflow_rules.sh
UNIT_FILE += configure_dpi_sni_plugin.sh
UNIT_FILE += configure_dpi_tap_interface.sh
UNIT_FILE += configure_gatekeeper_policy.sh
UNIT_FILE += configure_gre_tunnel_gw.sh
UNIT_FILE += configure_http_plugin.sh
UNIT_FILE += configure_sni_plugin_openflow_tags.sh
UNIT_FILE += configure_sta_interface.sh
UNIT_FILE += configure_tap_interfaces.sh
UNIT_FILE += configure_upnp_plugin.sh
UNIT_FILE += create_inet_interface.sh
UNIT_FILE += create_radio_vif_interface.sh
UNIT_FILE += create_vif_interface.sh
UNIT_FILE += device_init.sh
UNIT_FILE += fut_configure_mqtt.sh
UNIT_FILE += get_client_certificate.sh
UNIT_FILE += get_connected_cloud_controller_ip.sh
UNIT_FILE += get_count_reboot_status.sh
UNIT_FILE += get_redirector_hostname.sh
UNIT_FILE += prepare_radio_vif_interface.sh
UNIT_FILE += reboot_dut_w_reason.sh
UNIT_FILE += remove_tap_interfaces.sh
UNIT_FILE += remove_vif_interface.sh
UNIT_FILE += reset_sta_interface.sh
UNIT_FILE += restart_opensync.sh
UNIT_FILE += set_bridge_mode.sh
UNIT_FILE += set_parent.sh
UNIT_FILE += set_router_mode.sh
UNIT_FILE += validate_port_forward_entry_in_iptables.sh
UNIT_FILE += verify_dut_client_certificate_common_name.sh
UNIT_FILE += vif_clean.sh
UNIT_FILE += vif_reset.sh
