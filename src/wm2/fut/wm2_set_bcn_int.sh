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
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM

usage="$(basename "$0") [-h] [-c] [-s] [-fs] \$1 \$2 \$3 \$4 \$5 \$6 \$7 \$8 \$9 \$10

where options are:
    -h  show this help message

where arguments are:
    radio_idx=\$1 -- used as vif_radio_idx in Wifi_VIF_Config table - (int)(required)
    if_name=\$2 -- used as if_name in Wifi_Radio_Config table - (string)(required)
    ssid=\$3 -- used as ssid in Wifi_VIF_Config table - (string)(required)
    password=\$4 -- used as ssid password at security column in Wifi_VIF_Config table - (string)(required)
    channel=\$5 -- used as channel in Wifi_Radio_Config table - (int)(required)
    ht_mode=\$6 -- used as ht_mode in Wifi_Radio_Config table - (string)(required)
    hw_mode=\$7 -- used as hw_mode in Wifi_Radio_Config table - (string)(required)
    mode=\$8 -- used as mode in Wifi_VIF_Config table - (string)(required)
    country=\$9 -- used as country in Wifi_Radio_Config table - (string)(required)
    vif_if_name=\$10 -- used as if_name in Wifi_VIF_Config table - (string)(required)
    bcn_int=\$11 -- used as bcn_int in Wifi_Radio_Config table - (int)(required)

this script is dependent on following:
    - running both WM and NM manager

Script tries to set chosen BEACON INTERVAL. If interface is not UP it brings up the interface, and tries to set
BEACON INTERVAL to desired value.

Dependent on:
    - running WM/NM managers - min_wm2_setup

example of usage:
   $(basename "$0") 2 wifi1 test_wifi_50L WifiPassword123 44 HT20 11ac ap US home-ap-l50 36 200"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 11 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

vif_radio_idx=$1
if_name=$2
ssid=$3
security=$4
channel=$5
ht_mode=$6
hw_mode=$7
mode=$8
country=$9
vif_if_name=${10}
bcn_int=${11}

tc_name="wm2/$(basename "$0")"

log "$tc_name: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -channel "$channel" \
    -security "$security" \
    -hw_mode "$hw_mode" \
    -mode "$mode" \
    -country "$country" &&
        log "$tc_name: Radio/VIF states are valid" ||
            (
                log "$tc_name: Cleaning VIF_Config"
                vif_clean
                log "$tc_name: Radio/VIF states are not valid, creating interface..."
                create_radio_vif_interface \
                    -vif_radio_idx "$vif_radio_idx" \
                    -channel_mode manual \
                    -if_name "$if_name" \
                    -ssid "$ssid" \
                    -security "$security" \
                    -enabled true \
                    -channel "$channel" \
                    -ht_mode "$ht_mode" \
                    -hw_mode "$hw_mode" \
                    -mode "$mode" \
                    -country "$country" \
                    -vif_if_name "$vif_if_name" &&
                        log "$tc_name: create_radio_vif_interface - Success"
            ) ||
        raise "create_radio_vif_interface - Failed" -l "$tc_name" -tc

log "$tc_name: Changing bcn_int to $bcn_int"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u bcn_int "$bcn_int" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - bcn_int $bcn_int" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Radio_Config - bcn_int $bcn_int" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is bcn_int "$bcn_int" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - bcn_int $bcn_int" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - bcn_int $bcn_int" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - checking BEACON INTERVAL at OS level"
check_beacon_interval_at_os_level "$bcn_int" "$vif_if_name" ||
    log "$tc_name: check_beacon_interval_at_os_level - BEACON INTERVAL set at OS level - bcn_int $bcn_int" ||
    raise "check_beacon_interval_at_os_level - BEACON INTERVAL not set at OS level - bcn_int $bcn_int" -l "$tc_name" -tc

pass
