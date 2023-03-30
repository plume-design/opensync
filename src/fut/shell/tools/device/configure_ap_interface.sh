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
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" &> /dev/null
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" &> /dev/null

usage()
{
cat << usage_string
tools/device/configure_ap_interface [-h] arguments
Description:
    - Creates home/bhaul AP interface
    - If inet argument is provided
        - Creates inet interface
    - If bridge is set in the interface
        - Adds AP interface to bridge
Arguments:
    -h  show this help message
    -if_name, -vif_if_name, -vif_radio_idx, -channel,
    -ht_mode, -enabled, -mode, -bridge,
    -ssid, -ssid_broadcast,
    -inet_if_name, -if_type, -ip_assign_scheme,
    -network, -inet_enabled,
Wifi Security arguments(choose one or the other):
    If 'wifi_security_type' == 'wpa' (preferred)
    -wifi_security_type, -wpa, -wpa_key_mgmt, -wpa_psks, -wpa_oftags
                    (OR)
    If 'wifi_security_type' == 'legacy' (deprecated)
    -wifi_security_type, -security

Script usage example:
    ./tools/device/configure_ap_interface.sh -if_name wifi1 -vif_if_name home-ap-l50 -vif_radio_idx 2 -channel 52 -ht_mode HT40 -enabled true -mode ap -wifi_security_type legacy -security '["map",[["encryption","WPA-PSK"],["key","multi_psk_a"],["key-1","multi_psk_b"],["mode","2"],["oftag","home--1"],["oftag-key-1","home-1"]]]' -ssid FUT_ssid_dca632c8b9e2 -ssid_broadcast enabled -bridge br-home -inet_if_name home-ap-l50 -if_type vif -ip_assign_scheme none -NAT false -network true -inet_enabled true
usage_string
}

trap '
fut_ec=$?
fut_info_dump_line
print_tables Wifi_Radio_Config Wifi_Radio_State Wifi_VIF_Config Wifi_VIF_State Wifi_Inet_Config Wifi_Inet_State || true
check_restore_ovsdb_server
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM


NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/device/configure_ap_interface" -arg

log "tools/device/configure_ap_interface.sh: configure_ap_interface - Configure AP interface"


while [ -n "$1" ]; do
    option=$1
    shift
    case "$option" in
        -ht_mode | \
        -channel_mode | \
        -hw_mode | \
        -fallback_parents | \
        -tx_power | \
        -tx_chainmask | \
        -default_oftag | \
        -dpp_cc | \
        -vif_radio_idx | \
        -ssid_broadcast | \
        -parent | \
        -mac_list_type | \
        -dynamic_beacon | \
        -vlan_id | \
        -radius_srv_secret | \
        -radius_srv_addr | \
        -mac_list | \
        -credential_configs | \
        -ssid | \
        -ap_bridge | \
        -mode | \
        -enabled | \
        -country | \
        -channel | \
        -if_name | \
        -disable_cac)
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -wifi_security_type)
            wifi_security_type=${1}
            shift
            ;;
        -wpa | \
        -wpa_key_mgmt | \
        -wpa_psks | \
        -wpa_oftags)
            [ "${wifi_security_type}" != "wpa" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/configure_ap_interface.sh" -arg
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -security)
            [ "${wifi_security_type}" != "legacy" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/configure_ap_interface.sh" -arg
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -bridge)
            radio_vif_args="${radio_vif_args} -bridge ${1}"
            bridge=$1
            shift
            ;;
        -vif_if_name)
            radio_vif_args="${radio_vif_args} -vif_if_name $1"
            vif_if_name=$1
            shift
            ;;
        -timeout)
            radio_vif_args="${radio_vif_args} -channel_change_timeout $1"
            shift
            ;;
        -inet_if_name)
            inet_args="${inet_args} -if_name ${1}"
            inet_if_name=$1
            shift
            ;;
        -inet_enabled)
            inet_args="${inet_args} -enabled ${1}"
            shift
            ;;
        -network | \
        -if_type | \
        -inet_addr | \
        -dns | \
        -gateway | \
        -broadcast | \
        -ip_assign_scheme | \
        -mtu | \
        -NAT | \
        -upnp_mode | \
        -dhcpd | \
        -vlan_id | \
        -parent_ifname | \
        -gre_ifname | \
        -gre_remote_inet_addr | \
        -gre_local_inet_addr | \
        -dhcp_sniff | \
        -broadcast_n | \
        -inet_addr_n | \
        -subnet | \
        -netmask)
            inet_args="${inet_args} -${option#?} ${1}"
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "tools/device/configure_ap_interface.sh" -arg
            ;;
    esac
done

create_radio_vif_interface ${radio_vif_args} &&
    log -deb "tools/device/configure_ap_interface.sh: AP interface created - Success" ||
    raise "FAIL: AP interface not created" -l "tools/device/configure_ap_interface.sh" -tc

if [ $inet_if_name ]; then
    create_inet_entry ${inet_args} &&
        log -deb "tools/device/configure_ap_interface.sh: Inet interface ${inet_if_name} created - Success" ||
        raise "FAIL: Inet interface ${inet_if_name} not created" -l "tools/device/configure_ap_interface.sh" -tc
fi

if [ $bridge ]; then
    add_bridge_port "${bridge}" "${vif_if_name}" &&
        log -deb "tools/device/configure_ap_interface.sh: Interface ${vif_if_name} added to bridge ${bridge} - Success" ||
        raise "FAIL: Failed to add interface ${vif_if_name} to bridge ${bridge}" -l "tools/device/configure_ap_interface.sh" -tc
fi

exit 0
