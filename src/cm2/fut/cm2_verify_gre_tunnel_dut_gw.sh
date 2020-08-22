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


source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut_set_env.sh" ] && source /tmp/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

tc_name="cm2/$(basename "$0")"
help()
{
cat << EOF
${tc_name} [-h] arguments

The test establishes GRE tunnel via wireless backhaul connection to LEAF device.

Options:
    -h: show this help message
Positional arguments:
    GW_WAN_ETH_IFACE=$1: Interface name of the wired ethernet uplink (str, required)
    GW_BHAUL_AP_IFNAME=$2: Interface name of the backhaul AP interface (str, required)
    GW_BHAUL_VIF_RADIO_IDX=$3: VIF index number of the backhaul AP interface (int, required)
    GW_RADIO_IF=$4: Interface name of the radio linked to the GW backhaul AP interface (str, required)
    GW_RADIO_CHANNEL=$5: Desired channel for the radio linked to the backhaul AP interface (int, required)
    GW_RADIO_HT_MODE=$6: Desired ht_mode for the radio linked to the backhaul AP interface (str, required)
    GW_RADIO_HW_MODE=$7: hw_mode of the radio linked to the backhaul AP interface (str, required)
    GW_WAN_MTU=$8: MTU of the GW WAN interface (int, required)
    GW_GRE_MTU=$9: MTU of the GW GRE parent interface (int, required)
    GW_BHAUL_MTU=${10}: MTU of the GW backhaul interface (int, required)
    LEAF_RADIO_MAC_RAW=${11}: Physical (MAC) address of the LEAF radio linked to the backhaul STA interface (str, required)
    LEAF_WAN_INET_ADDR=${12}: WAN IP address of the LEAF (str, required)
    BHAUL_PSK=${13} Backhaul Pre-Shared Key (str, required)
    BHAUL_SSID=${14} Backhaul SSID (str, required)
    GW_LAN_BRIDGE=${15}: Interface name for GW LAN bridge (str, optional, default: "br-home")
    GW_WAN_BRIDGE=${16}: Interface name for GW WAN bridge (str, optional, default: "br-wan")
    GW_PATCH_HOME_TO_WAN=${17}: Interface name for GW LAN-to-WAN patch port (str, optional, default: "patch-h2w")
    GW_PATCH_WAN_TO_HOME=${18}: Interface name for GW WAN-to-LAN patch port (str, optional, default: "patch-w2h")
    UPSTREAM_ROUTER_IP=${19}: IP address of the upstream router - RPI server (str, optional, default: "192.168.200.1")
    INTERNET_CHECK_IP=${20}: IP address to test internet connection (str, optional, default: "1.1.1.1")
    ASSOCIATE_RETRY_COUNT=${21}: Number of checks for LEAF backhaul association to GW (int, optional, default: 30)
    ASSOCIATE_RETRY_SLEEP=${22}: Seconds between checks for LEAF backhaul association to GW (int, optional, default: 5})
    N_PING=${23}: Number of ping packets to send when checking connectivity (int, optional, default: 10)

Dependencies:
    This script is executed on GW, simultaneously as the corresponding script on LEAF
    Coordinate GW and LEAF backhaul radios, channels
    GW operates in bridge mode
Examples of usage:
   ${tc_name} "eth0" "bhaul-ap-50" "1" "wifi1" "44" "HT80" "11ac" "1500" "1562" "1600" \
              "60:b4:f7:f0:21:47" 192.168.200.11" "PreSharedKey" "fut.bhaul"
   ${tc_name} "eth0" "bhaul-ap-50" "1" "wifi1" "44" "HT80" "11ac" "1500" "1562" "1600" \
              "60:b4:f7:f0:21:47" "192.168.200.11" "PreSharedKey" "fut.bhaul" \
              "br-home" "br-wan" "patch-h2w" "patch-w2h" "192.168.200.1" "1.1.1.1" "11" "4" "2"
EOF
raise "Printed help and usage string" -l "$tc_name" -arg
}

while getopts h option; do
    case "$option" in
        h)
            help
            ;;
    esac
done

# INPUT ARGUMENTS:
NARGS=14
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
# Input arguments specific to GW, required:
GW_WAN_ETH_IFACE=${1}
GW_BHAUL_AP_IFNAME=${2}
GW_BHAUL_VIF_RADIO_IDX=${3}
GW_RADIO_IF=${4}
GW_RADIO_CHANNEL=${5}
GW_RADIO_HT_MODE=${6}
GW_RADIO_HW_MODE=${7}
GW_WAN_MTU=${8}
GW_GRE_MTU=${9}
GW_BHAUL_MTU=${10}
LEAF_RADIO_MAC_RAW=${11}
LEAF_WAN_INET_ADDR=${12}
# Input arguments common to GW and LEAF, required:
BHAUL_PSK=${13}
BHAUL_SSID=${14}
# Enforce required arguments:
[ -z "${GW_WAN_ETH_IFACE}" ] && raise "Empty parameter GW_WAN_ETH_IFACE" -l "${tc_name}" -arg
[ -z "${GW_BHAUL_AP_IFNAME}" ] && raise "Empty parameter GW_BHAUL_AP_IFNAME" -l "${tc_name}" -arg
[ -z "${GW_BHAUL_VIF_RADIO_IDX}" ] && raise "Empty parameter GW_BHAUL_VIF_RADIO_IDX" -l "${tc_name}" -arg
[ -z "${GW_RADIO_IF}" ] && raise "Empty parameter GW_RADIO_IF" -l "${tc_name}" -arg
[ -z "${GW_RADIO_CHANNEL}" ] && raise "Empty parameter GW_RADIO_CHANNEL" -l "${tc_name}" -arg
[ -z "${GW_RADIO_HT_MODE}" ] && raise "Empty parameter GW_RADIO_HT_MODE" -l "${tc_name}" -arg
[ -z "${GW_RADIO_HW_MODE}" ] && raise "Empty parameter GW_RADIO_HW_MODE" -l "${tc_name}" -arg
[ -z "${GW_WAN_MTU}" ] && raise "Empty parameter GW_WAN_MTU" -l "${tc_name}" -arg
[ -z "${GW_GRE_MTU}" ] && raise "Empty parameter GW_GRE_MTU" -l "${tc_name}" -arg
[ -z "${GW_BHAUL_MTU}" ] && raise "Empty parameter GW_BHAUL_MTU" -l "${tc_name}" -arg
[ -z "${LEAF_RADIO_MAC_RAW}" ] && raise "Empty parameter LEAF_RADIO_MAC_RAW" -l "${tc_name}" -arg
[ -z "${LEAF_WAN_INET_ADDR}" ] && raise "Empty parameter LEAF_WAN_INET_ADDR" -l "${tc_name}" -arg
[ -z "${BHAUL_PSK}" ] && raise "Empty parameter BHAUL_PSK" -l "${tc_name}" -arg
[ -z "${BHAUL_SSID}" ] && raise "Empty parameter BHAUL_SSID" -l "${tc_name}" -arg
# Input arguments specific to GW, optional:
GW_LAN_BRIDGE=${15:-"br-home"}
GW_WAN_BRIDGE=${16:-"br-wan"}
GW_PATCH_HOME_TO_WAN=${17:-"patch-h2w"}
GW_PATCH_WAN_TO_HOME=${18:-"patch-w2h"}
# Input arguments common to GW and LEAF, optional:
UPSTREAM_ROUTER_IP=${19:-"192.168.200.1"}
INTERNET_CHECK_IP=${20:-"1.1.1.1"}
ASSOCIATE_RETRY_COUNT=${21:-"30"}
ASSOCIATE_RETRY_SLEEP=${22:-"5"}
N_PING=${23:-"10"}
# Variables constructed from input arguments, constants:
GW_BHAUL_AP_MODE="ap"
GW_BHAUL_IP_ASSIGN_SCHEME="none"
GW_RADIO_CHANNEL_MODE="manual"
GW_BHAUL_BROADCAST_N="255"
GW_BHAUL_INET_ADDR_N="129"
GW_BHAUL_NETMASK="255.255.255.128"
GW_BHAUL_SUBNET="169.254.5"
GW_BHAUL_BROADCAST="${GW_BHAUL_SUBNET}.${GW_BHAUL_BROADCAST_N}"
GW_BHAUL_DHCPD_START="${GW_BHAUL_SUBNET}.$((GW_BHAUL_INET_ADDR_N + 1))"
GW_BHAUL_DHCPD_STOP="${GW_BHAUL_SUBNET}.$((GW_BHAUL_BROADCAST_N - 1))"
GW_BHAUL_INET_ADDR="${GW_BHAUL_SUBNET}.${GW_BHAUL_INET_ADDR_N}"
GW_BHAUL_DHCPD='["map",[["dhcp_option","26,1600"],["force","false"],["lease_time","12h"],["start","'${GW_BHAUL_DHCPD_START}'"],["stop","'${GW_BHAUL_DHCPD_STOP}'"]]]'
log "$tc_name: Get the corresponding LEAF radio interface MAC address, for whitelisting"
LEAF_RADIO_MAC="$(echo "$LEAF_RADIO_MAC_RAW" | tr [A-Z] [a-z])"

# SETUP:
log "$tc_name: GW initial setup"
cm_disable_fatal_state
device_init
start_openswitch
start_wireless_driver
start_specific_manager nm
start_specific_manager wm

log "$tc_name: Configure GW uplink"
MAC_ETH0=$(mac_get "${GW_WAN_ETH_IFACE}" | tr [A-Z] [a-z])
[ -z "${MAC_ETH0}" ] && raise "Ethernet MAC 0 empty" -arg
MAC_ETH1=$(printf "%02x:%s" $(( 0x${MAC_ETH0%%:*} | 0x2 )) "${MAC_ETH0#*:}")
[ -z "${MAC_ETH1}" ] && raise "Ethernet MAC 1 empty" -arg

add_ovs_bridge "${GW_WAN_BRIDGE}" "${MAC_ETH0}" "${GW_WAN_MTU}"
add_ovs_bridge "${GW_LAN_BRIDGE}" "${MAC_ETH1}"
add_bridge_port "${GW_WAN_BRIDGE}" "${GW_WAN_ETH_IFACE}"

remove_sta_interfaces 60 &&
    log -deb "$tc_name: STA interfaces removed from GW" ||
    raise "Failed to remove STA interfaces from GW" -l "$tc_name" -ds

create_inet_entry2 \
    -if_name "${GW_WAN_BRIDGE}" \
    -if_type "bridge" \
    -ip_assign_scheme "dhcp" \
    -upnp_mode "external" \
    -NAT true \
    -network true \
    -enabled true &&
        log -deb "$tc_name: Interface ${GW_WAN_BRIDGE} successfully created" ||
        raise "Failed to create interface ${GW_WAN_BRIDGE}" -l "$tc_name" -ds

check_pid_udhcp "${GW_WAN_BRIDGE}"
# Enforce router connectivity, check-only internet connectivity
wait_for_function_response 0 "ping -c${N_PING} ${UPSTREAM_ROUTER_IP}" &&
    log -deb "$tc_name: Can ping router" ||
    raise "Can not ping router" -ds
wait_for_function_response 0  "ping -c${N_PING} ${INTERNET_CHECK_IP}" &&
    log -deb "$tc_name: Can ping internet" ||
    log -deb "$tc_name: Can not ping internet"

log "$tc_name: Configure GW radio half way"
configure_radio_interface \
    -if_name "${GW_RADIO_IF}" \
    -channel_mode "${GW_RADIO_CHANNEL_MODE}" \
    -hw_mode "${GW_RADIO_HW_MODE}" \
    -enabled true &&
        log -deb "$tc_name: Success initial configure_radio_interface" ||
        raise "Failure initial configure_radio_interface" -l "$tc_name" -ds

log "$tc_name: Create GW backhaul AP"
create_vif_interface \
    -radio_if_name "${GW_RADIO_IF}" \
    -if_name "${GW_BHAUL_AP_IFNAME}" \
    -dynamic_beacon false \
    -mac_list '["set",["'${LEAF_RADIO_MAC//" "/}'"]]' \
    -mac_list_type whitelist \
    -mode "${GW_BHAUL_AP_MODE}" \
    -security '["map",[["encryption","WPA-PSK"],["key","'${BHAUL_PSK}'"],["mode","2"]]]' \
    -ssid "${BHAUL_SSID}" \
    -ssid_broadcast "disabled" \
    -vif_radio_idx "${GW_BHAUL_VIF_RADIO_IDX}" \
    -enabled true &&
        log -deb "$tc_name - Success create_vif_interface" ||
        raise "Failure create_vif_interface" -l "$tc_name" -ds

log "$tc_name: Configure GW radio channel and ht_mode now that AP is created"
configure_radio_interface \
    -if_name "${GW_RADIO_IF}" \
    -channel "${GW_RADIO_CHANNEL}" \
    -ht_mode "${GW_RADIO_HT_MODE}" &&
        log -deb "$tc_name - Success subsequent configure_radio_interface" ||
        raise "Failure subsequent configure_radio_interface" -l "$tc_name" -ds

log "$tc_name: Configure GW backhaul AP"
create_inet_entry2 \
    -if_name "${GW_BHAUL_AP_IFNAME}" \
    -broadcast "${GW_BHAUL_BROADCAST}" \
    -dhcpd "${GW_BHAUL_DHCPD}" \
    -if_type "vif" \
    -inet_addr "${GW_BHAUL_INET_ADDR}" \
    -ip_assign_scheme "static" \
    -mtu "${GW_BHAUL_MTU}" \
    -NAT false \
    -netmask "${GW_BHAUL_NETMASK}" \
    -network true \
    -enabled true &&
        log -deb "$tc_name: Interface ${GW_BHAUL_AP_IFNAME} successfully created" ||
        raise "Failed to create interface ${GW_BHAUL_AP_IFNAME}" -l "$tc_name" -ds

log "$tc_name: Waiting for LEAF backhaul STA to associate to GW backhaul AP"
fnc_str="get_ovsdb_entry_value DHCP_leased_IP inet_addr -w hwaddr ${LEAF_RADIO_MAC} -raw"
wait_for_function_output "notempty" "${fnc_str}" ${ASSOCIATE_RETRY_COUNT} ${ASSOCIATE_RETRY_SLEEP}
if [ $? -eq 0 ]; then
    LEAF_STA_INET_ADDR=$($fnc_str) &&
        log -deb "$tc_name: LEAF ${LEAF_STA_INET_ADDR} associated" ||
        raise "Failure: LEAF ${LEAF_STA_INET_ADDR} not associated"  -l "$tc_name" -ds
else
    raise "$tc_name - LEAF ${LEAF_STA_INET_ADDR} NOT associated" -l "$tc_name" -ds
fi
GW_GRE_NAME="pgd$(echo ${LEAF_STA_INET_ADDR//./-}| cut -d'-' -f3-4)"
echo "${GW_GRE_NAME}"
GW_AP_INET_ADDR=$(${OVSH} s Wifi_Inet_Config -w if_name=="${GW_BHAUL_AP_IFNAME}" inet_addr -r)
echo "$GW_AP_INET_ADDR"

# TESTCASE:
log "$tc_name: Create GW GRE parent interface"
create_inet_entry2 \
    -if_name "${GW_GRE_NAME}" \
    -if_type "gre" \
    -gre_ifname "${GW_BHAUL_AP_IFNAME}" \
    -gre_local_inet_addr "${GW_AP_INET_ADDR// /}" \
    -gre_remote_inet_addr "$LEAF_STA_INET_ADDR" \
    -ip_assign_scheme "${GW_BHAUL_IP_ASSIGN_SCHEME}" \
    -mtu "${GW_GRE_MTU}" \
    -network true \
    -enabled true &&
        log -deb "$tc_name: Interface ${GW_GRE_NAME} successfully created" ||
        raise "Failed to create interface ${GW_GRE_NAME}" -l "$tc_name" -tc

wait_for_function_exitcode 0 "interface_is_up ${GW_GRE_NAME}" "${ASSOCIATE_RETRY_COUNT}" "${ASSOCIATE_RETRY_SLEEP}" &&
    log -deb "$tc_name: Interface ${GW_GRE_NAME} is up on system" ||
    raise "Interface ${GW_GRE_NAME} is not up on system" -l "$tc_name" -tc

log "$tc_name: Put GW GRE interface into LAN bridge, create WAN-LAN patch ports"
add_bridge_port "${GW_LAN_BRIDGE}" "${GW_GRE_NAME}"
add_bridge_port "${GW_WAN_BRIDGE}" "${GW_PATCH_WAN_TO_HOME}"
set_interface_patch "${GW_WAN_BRIDGE}" "${GW_PATCH_WAN_TO_HOME}" "${GW_PATCH_HOME_TO_WAN}"
add_bridge_port "${GW_LAN_BRIDGE}" "${GW_PATCH_HOME_TO_WAN}"
set_interface_patch "${GW_LAN_BRIDGE}" "${GW_PATCH_HOME_TO_WAN}" "${GW_PATCH_WAN_TO_HOME}"

log "$tc_name: Test GW connectivity to LEAF GRE and WAN IP"

LEAF_GRE_INET_ADDR=$(cat /tmp/dhcp.leases | grep "${LEAF_RADIO_MAC}" | awk '{print $3}')
[ -z "${LEAF_GRE_INET_ADDR}" ] && raise "Failed to get LEAF GRE IP" -l "${tc_name}" -tc
ping -c${N_PING} "${LEAF_GRE_INET_ADDR}" && log -deb "Can ping LEAF GRE" || raise "Can not ping LEAF GRE" -tc

[ -z "${LEAF_WAN_INET_ADDR}" ] && raise "Failed to get LEAF WAN IP" -l "${tc_name}" -tc
ping -c${N_PING} "${LEAF_WAN_INET_ADDR}" && log -deb "Can ping LEAF WAN" || raise "Can not ping LEAF WAN" -tc

pass
