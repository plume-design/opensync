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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage()
{
cat << usage_string
othr/othr_verify_gre_tunnel_gw.sh [-h] arguments
Description:
    - Script verifies communication to LEAF device (ping)
Pre-requirements:
    - Configured bhaul-ap on DUT device
    - Configured bhaul-sta on LEAF device
    - LEAF device associated to DUT
Arguments:
    -h : show this help message
    \$1 (leaf_radio_mac)   : used for LEAF radio mac          : (string)(required)
Testcase procedure (FUT scripts call only)(example):
    # Initial DUT and REF setup
    # On DUT: ./fut-base/shell//tests/dm/othr_setup.sh wifi0 wifi1 wifi2
    # On REF: ./fut-base/shell//tests/dm/othr_setup.sh wifi0 wifi1 wifi2
    # On REF: ./fut-base/shell//tools/device/ovsdb/get_radio_mac_from_ovsdb.sh  if_name==wifi0
    # On REF: ./fut-base/shell//tools/device/ovsdb/remove_ovsdb_entry.sh  Wifi_Credential_Config -w ssid fut-5515.bhaul
    # On REF: ./fut-base/shell//tools/device/ovsdb/insert_ovsdb_entry.sh  Wifi_Credential_Config -i onboard_type gre -i ssid fut-5515.bhaul \
        -i security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]'
    # On REF: ./fut-base/shell//tools/device/ovsdb/get_ovsdb_entry_value.sh  Wifi_Credential_Config _uuid ssid fut-5515.bhaul true
    # On DUT: ./fut-base/shell//tools/device/check_wan_connectivity.sh

    # Configure DUT bhaul-ap
    # On DUT: ./fut-base/shell//tools/device/vif_reset.sh
    # On DUT: ./fut-base/shell//tools/device/create_inet_interface.sh  -if_name br-home -if_type bridge -enabled true -network true -NAT false -ip_assign_scheme dhcp
    # On DUT: ./fut-base/shell//tools/device/configure_lan_bridge_for_wan_connectivity.sh  eth0 br-wan br-home 1500
    # On DUT: ./fut-base/shell//tools/device/create_radio_vif_interface.sh  -if_name wifi0 -vif_if_name bhaul-ap-24 -vif_radio_idx 1 \
        -channel 6 -ht_mode HT40 -hw_mode 11n -enabled true -mac_list '["set",["60:b4:f7:f0:0e:b6"]]'
        -mac_list_type whitelist -mode ap -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]'
        -ssid fut-5515.bhaul -ssid_broadcast disabled
    # On DUT: ./fut-base/shell//tools/device/create_inet_interface.sh  -if_name bhaul-ap-24 -if_type vif -broadcast_n 255 \
        -inet_addr_n 129 -subnet 169.254.6 -netmask 255.255.255.128 -ip_assign_scheme static
        -mtu 1600 -NAT false -enabled true -network true

    # Configure REF bhaul-sta
    # On REF: ./fut-base/shell//tools/device/create_vif_interface.sh  -if_name bhaul-sta-24 \
        -credential_configs '["set",[["uuid","b2e198a8-895c-4f49-8fc9-aa3401f5b056"]]]'
        -ssid fut-5515.bhaul -enabled true

    # Configure GRE tunnel on DUT
    # ./fut-base/shell//tools/device/configure_gre_tunnel_gw.sh  bhaul-ap-24 60:b4:f7:f0:0e:b6 1562 br-home
    # Verify GRE tunnel on DUT and REF
    # On DUT: ./fut-base/shell//tests/dm/othr_verify_gre_tunnel_gw.sh  60:b4:f7:f0:0e:b6
    # On REF: ./fut-base/shell//tests/dm/othr_verify_gre_tunnel_leaf.sh  bhaul-sta-24 br-home
Script usage example:
    ./othr/othr_verify_gre_tunnel_gw.sh 60:b4:f7:f2:f3:15
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
print_tables Wifi_Inet_Config Wifi_Inet_State
print_tables Wifi_VIF_Config Wifi_VIF_State
print_tables DHCP_leased_IP
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "othr/othr_verify_gre_tunnel_gw.sh" -arg
leaf_radio_mac=${1}
n_ping="5"

log "othr/othr_verify_gre_tunnel_gw.sh: Test GW connectivity to LEAF GRE and WAN IP"
wait_for_function_response 'notempty' "get_associated_leaf_ip ${leaf_radio_mac}" 30 ||
    raise "FAIL #3: Failed to get LEAF GRE IP" -l "othr/othr_verify_gre_tunnel_gw.sh" -tc

leaf_gre_inet_addr="$(get_associated_leaf_ip "${leaf_radio_mac}")"
[ -z "${leaf_gre_inet_addr}" ] &&
    raise "FAIL #4: Failed to get LEAF GRE IP" -l "othr/othr_verify_gre_tunnel_gw.sh" -tc

# Try 3 times to ping LEAF, could not be immediately available
wait_for_function_exit_code 0 "ping -c${n_ping} ${leaf_gre_inet_addr}" 3 &&
    log "othr/othr_verify_gre_tunnel_gw.sh: Can ping LEAF GRE - Success" ||
    raise "FAIL: Can not ping LEAF GRE" -l "othr/othr_verify_gre_tunnel_gw.sh" -tc

pass
