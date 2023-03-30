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
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage()
{
cat << usage_string
othr/othr_verify_gre_iface_wifi_master_state.sh [-h] arguments
Description:
    - Script verifies wifi_master_state table is populated with GRE interface
Arguments:
    -h : show this help message
    \$1 (bhaul_ap_if_name) : used for bhaul ap interface name : (string)(required)
    \$2 (gre_if_name)      : used as GRE interface name       : (string)(required)
    \$3 (gre_mtu)          : used for GRE MTU                 : (string)(required)

    # Configure DUT bhaul-ap
    # On DUT: ./fut-base/shell//tools/device/vif_reset.sh
    # On DUT: ./fut-base/shell//tools/device/create_inet_interface.sh  -if_name br-home -if_type bridge -enabled true -network true -NAT false -ip_assign_scheme dhcp
    # On DUT: ./fut-base/shell//tools/device/create_radio_vif_interface.sh  -if_name wifi0 -vif_if_name bhaul-ap-24 -vif_radio_idx 1 \
        -channel 6 -ht_mode HT40 -hw_mode 11n -enabled true -mac_list '["set",["60:b4:f7:f0:0e:b6"]]'
        -mac_list_type whitelist -mode ap -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]'
        -ssid fut-5515.bhaul -ssid_broadcast disabled
    # On DUT: ./fut-base/shell//tools/device/create_inet_interface.sh  -if_name bhaul-ap-24 -if_type vif -broadcast_n 255 \
        -inet_addr_n 129 -subnet 169.254.6 -netmask 255.255.255.128 -ip_assign_scheme static
        -mtu 1600 -NAT false -enabled true -network true

    # Verify wifi_master_state table contents for GRE interface
    # On DUT: ./fut-base/shell//tests/dm/othr_verify_gre_iface_wifi_master_state.sh  bhaul-ap-24 gre-ifname-100 1562
Script usage example:
    ./othr/othr_verify_gre_iface_wifi_master_state.sh bhaul-ap-50 gre-ifname-100 1562
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
print_tables Wifi_Master_State
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=3
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "othr/othr_verify_gre_iface_wifi_master_state.sh" -arg
bhaul_ap_if_name=${1}
gre_name=${2}
gre_mtu=${3}

remote_inet_addr="1.1.1.1"
bhaul_ip_assign_scheme="none"

log "othr/othr_verify_gre_iface_wifi_master_state.sh: - Verify wifi_master_state table is populated with GRE interface"

${OVSH} s Wifi_Master_State
if [ $? -eq 0 ]; then
    log "othr/othr_verify_gre_iface_wifi_master_state.sh: Wifi_Master_State table exists"
else
    raise "FAIL: Wifi_Master_State table does not exist" -l "othr/othr_verify_gre_iface_wifi_master_state.sh" -tc
fi

ap_inet_addr=$(get_ovsdb_entry_value Wifi_Inet_Config inet_addr -w if_name "${bhaul_ap_if_name}" -r)

# TESTCASE:
log "othr/othr_verify_gre_iface_wifi_master_state.sh: Create GW GRE parent interface"
create_inet_entry \
    -if_name "${gre_name}" \
    -if_type "gre" \
    -gre_ifname "${bhaul_ap_if_name}" \
    -gre_local_inet_addr "${ap_inet_addr// /}" \
    -gre_remote_inet_addr "${remote_inet_addr}" \
    -ip_assign_scheme "${bhaul_ip_assign_scheme}" \
    -mtu "${gre_mtu}" \
    -network true \
    -enabled true &&
        log "othr/othr_verify_gre_iface_wifi_master_state.sh: Interface ${gre_name} created - Success" ||
        raise "FAIL: Failed to create interface ${gre_name}" -l "othr/othr_verify_gre_iface_wifi_master_state.sh" -ds

check_ovsdb_entry Wifi_Master_State -w if_name ${gre_name} &&
    log "othr/othr_verify_gre_iface_wifi_master_state.sh: Wifi_Master_State populated with GRE interface '${gre_name}' - Success" ||
    raise "FAIL: Wifi_Master_State not populated with GRE interface '${gre_name}'" -l "othr/othr_verify_gre_iface_wifi_master_state.sh" -tc

pass
