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

The test checks for an established GRE tunnel via wireless backhaul connection to GW device.
This script is only complementary to the one running on GW device. The assumption is, that the LEAF device
is a tested reference device, and GW device acts DUT, where all procedures are done manually. This script
does minimal configuring, only ensuring that backhaul SSID and PSK are configured correctly, as this is usually
preconfigured into device firmware. Intermediate steps are executed automatically by OpenSync managers,
running independently from FUT scripts, and only checks are made by the script, not configuration steps.

Options:
    -h: show this help message
Arguments:
    LEAF_BHAUL_STA_IFNAME=$1: Interface name of the backhaul STA interface (str, required)
    LEAF_WAN_BRIDGE=$2: Interface name for LEAF WAN bridge (str, required)
    BHAUL_PSK=$3 Backhaul Pre-Shared Key (str, required)
    BHAUL_SSID=$4 Backhaul SSID (str, required)
    UPSTREAM_ROUTER_IP=$5: IP address of the upstream router - RPI server (str, optional, default: "192.168.200.1")
    INTERNET_CHECK_IP=$6: IP address to test internet connection (str, optional, default: "1.1.1.1")
    ASSOCIATE_RETRY_COUNT=$7: Number of checks for LEAF backhaul association to GW (int, optional, default: 30)
    ASSOCIATE_RETRY_SLEEP=$8: Seconds between checks for LEAF backhaul association to GW (int, optional, default: 5})
    N_PING=$9: Number of ping packets to send when checking connectivity (int, optional, default: 5)
Dependencies:
    It is assumed that LEAF has been configured to "default" OpenSync state, without prior FUT configuration: utility/device/default_setup.sh
    This script is executed on LEAF, simultaneously as the corresponding script on GW
    Coordinate GW and LEAF backhaul radios, channels
    GW operates in bridge mode
Examples of usage:
   ${tc_name} "bhaul-sta-l50" "br-wan" "PreSharedKey" "fut.bhaul"
   ${tc_name} "bhaul-sta-l50" "br-wan" "PreSharedKey" "fut.bhaul" "192.168.200.1" "1.1.1.1" "10" "3" "2"
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
NARGS=4
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
# Input arguments specific to LEAF, required:
LEAF_BHAUL_STA_IFNAME=${1}
LEAF_WAN_BRIDGE=${2}
# Input arguments common to GW and LEAF, required:
BHAUL_PSK=${3}
BHAUL_SSID=${4}
# Enforce required arguments:
[ -z "${LEAF_BHAUL_STA_IFNAME}" ] && raise "Empty parameter LEAF_BHAUL_STA_IFNAME" -l "${tc_name}" -arg
[ -z "${LEAF_WAN_BRIDGE}" ] && raise "Empty parameter LEAF_WAN_BRIDGE" -l "${tc_name}" -arg
[ -z "${BHAUL_PSK}" ] && raise "Empty parameter BHAUL_PSK" -l "${tc_name}" -arg
[ -z "${BHAUL_SSID}" ] && raise "Empty parameter BHAUL_SSID" -l "${tc_name}" -arg
# Input arguments common to GW and LEAF, optional:
UPSTREAM_ROUTER_IP=${5:-"192.168.200.1"}
INTERNET_CHECK_IP=${6:-"1.1.1.1"}
ASSOCIATE_RETRY_COUNT=${7:-"30"}
ASSOCIATE_RETRY_SLEEP=${8:-"5"}
N_PING=${9:-"5"}
# Variables constructed from input arguments, constants:
LEAF_GRE_NAME="g-${LEAF_BHAUL_STA_IFNAME}"

log "$tc_name: Check LEAF backhaul STA interface"
fnc_str="check_interface_exists2 ${LEAF_BHAUL_STA_IFNAME}"
wait_for_function_response 0 "${fnc_str}" &&
    log -deb "$tc_name: Interface ${LEAF_BHAUL_STA_IFNAME} exists on system" ||
    raise"Interface ${LEAF_BHAUL_STA_IFNAME} does NOT exist on system" -l "$tc_name" -ds
check_ovsdb_entry Wifi_VIF_Config -w if_name "${LEAF_BHAUL_STA_IFNAME}" &&
    log -deb "$tc_name: Entry ${LEAF_BHAUL_STA_IFNAME} exists in Wifi_VIF_Config" ||
    raise "Entry ${LEAF_BHAUL_STA_IFNAME} does NOT exist in Wifi_VIF_Config" -l "${tc_name}" -ds

log "$tc_name: Insert new LEAF backhaul test credentials"
log -deb "$tc_name: LEAF backhaul credentials are preset on device FW, changing them serves test purposes only"
insert_ovsdb_entry2 Wifi_Credential_Config \
    -i onboard_type "gre" \
    -i ssid "${BHAUL_SSID}" \
    -i security '["map",[["encryption","WPA-PSK"],["key","'${BHAUL_PSK}'"],["mode","2"]]]' &&
        log -deb "$tc_name: Success insert_ovsdb_entry Wifi_Credential_Config" ||
        raise "Failure insert_ovsdb_entry Wifi_Credential_Config" -l "$tc_name" -tc
cred_uuid=$(get_ovsdb_entry_value Wifi_Credential_Config _uuid -w ssid "${BHAUL_SSID}" -r | cut -d'"' -f4) &&
    log -deb "$tc_name: Retrieved Wifi_Credential_Config UUID" ||
    raise "Failed to retrieve Wifi_Credential_Config UUID" -l "$tc_name" -tc

log "$tc_name: Configure LEAF backhaul test credentials"
create_vif_interface \
    -if_name "${LEAF_BHAUL_STA_IFNAME}" \
    -credential_configs '["set",[["uuid","'${cred_uuid}'"]]]' \
    -security '["map",[["encryption","WPA-PSK"],["key","'${BHAUL_PSK}'"],["mode","2"]]]' \
    -ssid "${BHAUL_SSID}" &&
        log -deb "$tc_name - Successfully configured LEAF backhaul test credentials" ||
        raise "Failed to configure LEAF backhaul test credentials" -l "$tc_name" -ds

log "$tc_name: Waiting for LEAF backhaul STA to associate to GW backhaul AP"
check_sta_associated "${LEAF_BHAUL_STA_IFNAME}" "${ASSOCIATE_RETRY_COUNT}" "${ASSOCIATE_RETRY_SLEEP}" &&
    log -deb "$tc_name - LEAF backhaul STA associated to GW backhaul AP" ||
    raise "Failed to associate to GW backhaul AP in ${ASSOCIATE_RETRY_COUNT} retries" -l "$tc_name" -ow

log "$tc_name: LEAF creates GRE interface"
check_interface_exists2 "${LEAF_GRE_NAME}"

log "$tc_name: Check that GRE interface is in WAN bridge on LEAF node"
check_if_port_in_bridge "${LEAF_WAN_BRIDGE}" "${LEAF_GRE_NAME}"

log "$tc_name: Check that LEAF starts DHCP client on br-wan to get WAN IP"
check_pid_udhcp "${LEAF_WAN_BRIDGE}"

# Enforce router connectivity, check-only internet connectivity
log "$tc_name: Check that LEAF has WAN connectivity via GRE tunnel"
wait_for_function_response 0 "ping -c${N_PING} ${UPSTREAM_ROUTER_IP}" &&
    log -deb "$tc_name: Can ping router" ||
    raise "Can not ping router" -tc
wait_for_function_response 0 "ping -c${N_PING} ${INTERNET_CHECK_IP}" &&
    log -deb "$tc_name: Can ping internet" ||
    log -deb "$tc_name: Can not ping internet"

pass
