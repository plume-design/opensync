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

manager_setup_file="nm2/nm2_setup.sh"
inet_addr_default="10.10.10.30"
patch_w2h_default="patch-w2h"
patch_h2w_default="patch-h2w"

usage()
{
cat << usage_string
nm2/nm2_set_ip_assign_scheme.sh [-h] arguments
Description:
    - Script configures interfaces ip_assign_scheme through Wifi_inet_Config 'ip_assign_scheme' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name)          : used as if_name in Wifi_Inet_Config table                : (string)(required)
    \$2 (if_type)          : used as if_type in Wifi_Inet_Config table                : (string)(required)
    \$3 (ip_assign_scheme) : used as ip_assign_scheme in Wifi_Inet_Config table       : (string)(required)
    \$4 (wan_eth)          : Interface used for WAN uplink (WAN bridge or eth WAN)    : (string)(required)
    \$5 (lan_bridge)       : Interface name of LAN bridge                             : (string)(required)
    \$6 (bhaul_ap_if_name) : used for bhaul ap interface name : (string)(required)
    \$7 (gre_mtu)          : used for GRE MTU                 : (string)(required)
    \$8 (inet_addr)        : used as inet_addr column in Wifi_Inet_Config/State table : (string)(optional) : (default:${inet_addr_default})

Testcase procedure:
    1. For GRE interfaces:
        # Configure DUT bhaul-ap
        # On DUT: ./fut-base/shell/tools/device/vif_clean.sh
        # On DUT: ./fut-base/shell/tools/device/create_inet_interface.sh  -if_name br-home -if_type bridge -enabled true -network true -NAT false -ip_assign_scheme dhcp
        # On DUT: ./fut-base/shell/tools/device/create_radio_vif_interface.sh  -if_name wifi0 -vif_if_name bhaul-ap-24 -vif_radio_idx 1 \
            -channel 6 -ht_mode HT40 -hw_mode 11n -enabled true -mac_list '["set",["60:b4:f7:f0:0e:b6"]]'
            -mac_list_type whitelist -mode ap -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]'
            -ssid fut-5515.bhaul -ssid_broadcast disabled
        # On DUT: ./fut-base/shell/tools/device/create_inet_interface.sh  -if_name bhaul-ap-24 -if_type vif -broadcast_n 255 \
            -inet_addr_n 129 -subnet 169.254.6 -netmask 255.255.255.128 -ip_assign_scheme static
            -mtu 1600 -NAT false -enabled true -network true

        # Verify Wifi_Inet_Config table contents for GRE interface
        # On DUT: ./nm2/nm2_set_ip_assign_scheme.sh <IF-NAME> <IF-TYPE> <IP-ASSIGN-SCHEME> <WAN_INTERFACE> <LAN_BRIDGE> <BHAUL_AP_INTERFACE> <GRE_MTU> <INET-ADDR>

    2. For other interface types:
    - On DUT: ./${manager_setup_file} (see ${manager_setup_file} -h)
              ./nm2/nm2_set_ip_assign_scheme.sh <IF-NAME> <IF-TYPE> <IP-ASSIGN-SCHEME> <INET-ADDR>

Script usage example:
   ./nm2/nm2_set_ip_assign_scheme.sh wifi0 vif static
   ./nm2/nm2_set_ip_assign_scheme.sh wifi0 vif dhcp
   ./nm2/nm2_set_ip_assign_scheme.sh gre-ifname-01 gre dhcp eth0 br-home bhaul-ap-24 1500
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

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_set_ip_assign_scheme.sh" -arg
if_name=${1}
if_type=${2}
ip_assign_scheme=${3}
wan_eth=${4}
lan_br=${5}
bhaul_ap_if_name=${6}
gre_mtu=${7}
patch_w2h=${8:-${patch_w2h_default}}
patch_h2w=${9:-${patch_h2w_default}}
inet_addr=${10:-${inet_addr_default}}
remote_inet_addr="1.1.1.1"

trap '
    fut_info_dump_line
    reset_inet_entry $if_name || true
    check_restore_management_access || true
    print_tables Wifi_Inet_Config Wifi_Inet_State
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_set_ip_assign_scheme.sh: NM2 test - Testing table Wifi_Inet_Config field ip_assign_scheme"

log "nm2/nm2_set_ip_assign_scheme.sh: Remove running clients first"
rm "/var/run/udhcpc-$if_name.pid" || log "Nothing to remove"
rm "/var/run/udhcpc_$if_name.opts" || log "Nothing to remove"

check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    is_wano="true" ||
    is_wano="false"

log "nm2/nm2_set_ip_assign_scheme.sh: Creating Wifi_Inet_Config entries for $if_name"

if [ "${if_type}" == "bridge" ];then
    if [ "${is_wano}" == "false" ];then
        add_bridge_port "${if_name}" "${wan_eth}"
        create_inet_entry \
            -if_name "${if_name}" \
            -enabled true \
            -network true \
            -ip_assign_scheme "${ip_assign_scheme}" \
            -if_type "bridge" &&
            log "nm2/nm2_set_ip_assign_scheme.sh: Interface $if_name created - Success" ||
            raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_ip_assign_scheme.sh" -ds

        add_bridge_port "${if_name}" "${patch_w2h}"
        set_interface_patch "${if_name}" "${patch_w2h}" "${patch_h2w}"
        add_bridge_port "${lan_br}" "${patch_h2w}"
        set_interface_patch "${lan_br}" "${patch_h2w}" "${patch_w2h}"
    else
        add_bridge_port "${lan_br}" "${wan_eth}"
        create_inet_entry \
            -if_name "${lan_br}" \
            -enabled true \
            -network true \
            -ip_assign_scheme "none" \
            -if_type "bridge" &&
            log "nm2/nm2_set_ip_assign_scheme.sh: Interface $if_name created - Success" ||
            raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_ip_assign_scheme.sh" -ds
    fi
elif [ "${if_type}" == "gre" ];then
    ap_inet_addr=$(get_ovsdb_entry_value Wifi_Inet_Config inet_addr -w if_name "${bhaul_ap_if_name}" -r)
    create_inet_entry \
        -if_name "${if_name}" \
        -if_type "${if_type}" \
        -gre_ifname "${bhaul_ap_if_name}" \
        -gre_local_inet_addr "${ap_inet_addr// /}" \
        -gre_remote_inet_addr "${remote_inet_addr}" \
        -ip_assign_scheme "none" \
        -mtu "${gre_mtu}" \
        -network true \
        -enabled true &&
        log "nm2/nm2_set_ip_assign_scheme.sh: Interface ${gre_name} created - Success" ||
        raise "FAIL: Failed to create interface ${gre_name}" -l "nm2/nm2_set_ip_assign_scheme.sh" -ds

else
    create_inet_entry \
        -if_name "$if_name" \
        -enabled true \
        -network true \
        -netmask "255.255.255.0" \
        -inet_addr "$inet_addr" \
        -ip_assign_scheme static \
        -if_type "$if_type" &&
        log "nm2/nm2_set_ip_assign_scheme.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_ip_assign_scheme.sh" -ds
fi

if [ "$ip_assign_scheme" = "dhcp" ]; then
    log "nm2/nm2_set_ip_assign_scheme.sh: Setting dhcp for $if_name to dhcp"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme dhcp &&
        log "nm2/nm2_set_ip_assign_scheme.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'dhcp' - Success" ||
        raise "FAIL: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is not 'dhcp'" -l "nm2/nm2_set_ip_assign_scheme.sh" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme dhcp &&
        log "nm2/nm2_set_ip_assign_scheme.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'dhcp' - Success" ||
        raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'dhcp'" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

    log "nm2/nm2_set_ip_assign_scheme.sh: Checking if DHCP client is alive - LEVEL2"
    wait_for_function_response 0 "check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"" &&
        log "nm2/nm2_set_ip_assign_scheme.sh: LEVEL2 - DHCP client process ACTIVE for interface $if_name - Success" ||
        raise "FAIL: LEVEL2 - DHCP client process NOT ACTIVE for interface $if_name" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

    log "nm2/nm2_set_ip_assign_scheme.sh: Setting dhcp for $if_name to none"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme none &&
        log "nm2/nm2_set_ip_assign_scheme.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is 'none' - Success" ||
        raise "FAIL: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme is not 'none'" -l "nm2/nm2_set_ip_assign_scheme.sh" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme none &&
        log "nm2/nm2_set_ip_assign_scheme.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'none' - Success" ||
        raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'none'" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

    log "nm2/nm2_set_ip_assign_scheme.sh: Checking if DHCP client is dead - LEVEL2"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "nm2/nm2_set_ip_assign_scheme.sh: LEVEL2 - DHCP client process NOT ACTIVE - Success" ||
        raise "FAIL: LEVEL2 - DHCP client process ACTIVE" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

elif [ "$ip_assign_scheme" = "static" ]; then
    log "nm2/nm2_set_ip_assign_scheme.sh: Setting ip_assign_scheme for $if_name to static"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u ip_assign_scheme static \
        -u inet_addr "$inet_addr" &&
            log "nm2/nm2_set_ip_assign_scheme.sh: update_ovsdb_entry - Wifi_Inet_Config::ip_assign_scheme, Wifi_Inet_Config::inet_addr - Success" ||
            raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::ip_assign_scheme, Wifi_Inet_Config::inet_addr" -l "nm2/nm2_set_ip_assign_scheme.sh" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
        -is ip_assign_scheme static \
        -is inet_addr "$inet_addr" &&
            log "nm2/nm2_set_ip_assign_scheme.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'static' - Success" ||
            raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'static'" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

    log "nm2/nm2_set_ip_assign_scheme.sh: Checking if settings are applied to ifconfig - LEVEL2"
    wait_for_function_response 0 "check_interface_ip_address_set_on_system $if_name | grep -q \"$inet_addr\"" &&
        log "nm2/nm2_set_ip_assign_scheme.sh: LEVEL2 - Settings applied to ifconfig for interface $if_name - Success" ||
        raise "FAIL: LEVEL2 - Failed to apply settings to ifconfig for interface $if_name" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc

    log "nm2/nm2_set_ip_assign_scheme.sh: Checking if DHCP client is DEAD - LEVEL2"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "nm2/nm2_set_ip_assign_scheme.sh: LEVEL2 - DHCP client process is DEAD for interface $if_name - Success" ||
        raise "FAIL: LEVEL2 - DHCP client process is NOT DEAD for interface $if_name" -l "nm2/nm2_set_ip_assign_scheme.sh" -tc
else
    raise "Wrong IP_ASSIGN_SCHEME parameter - $ip_assign_scheme" -l "nm2/nm2_set_ip_assign_scheme.sh" -arg
fi

pass
