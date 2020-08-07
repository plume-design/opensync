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


# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi

source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    check_restore_management_access || true
    run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

where arguments are:
    if_name=\$@ -- used as connection interface - (string)(required)
    wan_interface=\$@ -- used as name of WAN interface - (string)(required)
    wan_ip=\$@ -- used as IP address of WAN interface - (string)(required)

this script is dependent on following:
    - running CM manager
    - running NM manager

example of usage:
    /tmp/fut-base/shell/cm2/$(basename "$0") eth0 br-wan 192.168.200.10
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 3 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1
wan_interface=$2
wan_ip=$3

tc_name="cm2/$(basename "$0")"
log "$tc_name: CM2 test - Link Lost"

log "$tc_name: Dropping interface $if_name connection"
ifconfig "$if_name" down &&
    log "$tc_name: Interface $if_name is down" ||
    raise "Couldn't bring down interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for Cloud status to go to BACKOFF"
wait_cloud_state BACKOFF &&
    log "$tc_name: wait_cloud_state - Cloud set to BACKOFF" ||
    raise "Failed to set cloud to BACKOFF" -l "$tc_name" -tc

log "$tc_name: Waiting for has_L2 -> false on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 false &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L2 -> false" ||
    raise "Connection_Manager_Uplink - {has_L2:=false}" -l "$tc_name" -ow

log "$tc_name: Sleeping for 10 seconds"
sleep 10

log "$tc_name: Bringing up interface $if_name"
ifconfig "$if_name" up &&
    log "$tc_name: Interface $if_name is up" ||
    raise "Couldn't bring up interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L2 -> true for ifname==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 true &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L2 -> true" ||
    raise "Connection_Manager_Uplink - {has_L2:=true}" -l "$tc_name" -ow

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L3 -> true for ifname==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L3 true &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L3 -> true" ||
    raise "Connection_Manager_Uplink - {has_L3:=true}" -l "$tc_name" -ow


log "$tc_name: Waiting for Connection_Manager_Uplink::is_used -> true for ifname==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is is_used true &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name is_used -> true" ||
    raise "wait_ovsdb_entry - Connection_Manager_Uplink - {is_used:=true}" -l "$tc_name" -ow

log "$tc_name: Checking $if_name is added to $wan_interface"
wait_for_function_response 0 "check_if_port_in_bridge $if_name $wan_interface" &&
    log "$tc_name: check_if_port_in_bridge - port $if_name added to interface $wan_interface" ||
    raise "check_if_port_in_bridge - port $if_name NOT added to interface $wan_interface" -l "$tc_name" -tc

log "$tc_name: Waiting for WAN interface to get WAN IP"
wait_ovsdb_entry Wifi_Inet_State -w if_name "$wan_interface" -is inet_addr "$wan_ip" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config - inet_addr is $wan_ip" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - inet_addr is NOT $wan_ip" -l "$tc_name" -tc

log "$tc_name: Waiting for Connection_Manager_Uplink::ntp_state -> true for ifname==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is ntp_state true &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name ntp_state -> true" ||
    raise "wait_ovsdb_entry - Connection_Manager_Uplink - {ntp_state:=true}" -l "$tc_name" -ow

log "$tc_name: Verify manager hostname resolved, waiting for Manager::is_connected -> true"
wait_ovsdb_entry Manager -is is_connected true &&
    log "$tc_name: wait_ovsdb_entry - Manager is_connected is true" ||
    raise "wait_ovsdb_entry - Manager - {is_connected:=true}" -l "$tc_name" -tc

log "$tc_name: Waiting for Cloud status to go to ACTIVE"
wait_cloud_state ACTIVE &&
    log "$tc_name: wait_cloud_state - Cloud set to ACTIVE" ||
    raise "Failed to set cloud to ACTIVE" -l "$tc_name" -tc

pass
