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

usage="$(basename "$0") [-h] [-c] [-s] [-fs] \$1 \$2 \$3 \$4 \$5 \$6 \$7 \$8 \$9 \$10 \$11 \$12

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
    tx_chainmask=\$11 -- used as tx_chainmask in Wifi_Radio_Config table (recomended 1, 3, 7, 15)- (int)(required)
    thermal_tx_chainmask=\$12 -- used as thermal_tx_chainmask in Wifi_Radio_Config table (recomended 1, 3, 7, 15) - (int)(required)

this script is dependent on following:
    - running both WM and NM manager

Script tries to set chosen THERMAL TX CHAINMASK. If interface is not UP it brings up the interface, and tries to set
THERMAL TX CHAINMASK to desired value.

Dependent on:
    - running WM/NM managers - min_wm2_setup

example of usage:
   $(basename "$0") 2 wifi1 test_wifi_50L WifiPassword123 44 HT20 11ac ap US home-ap-l50 36 5"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 12 ]; then
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
tx_chainmask=${11}
thermal_tx_chainmask=${12}

tc_name="wm2/$(basename "$0")"

log "$tc_name: Determining minimal value THERMAL TX CHAINMASK ($thermal_tx_chainmask) vs TX CHAINMASK ($tx_chainmask)"
if [ "$thermal_tx_chainmask" -gt "$tx_chainmask" ]; then
    value_to_check=$tx_chainmask
else
    value_to_check=$thermal_tx_chainmask
fi

log "$tc_name: Checking if THERMAL TX CHAINMASK $thermal_tx_chainmask is valid value"

if [ "$thermal_tx_chainmask" -gt 0 ] && [ "$thermal_tx_chainmask" -le 3 ]; then
    max_tx_chainmask_value=3
elif [ "$thermal_tx_chainmask" -gt 3 ] && [ "$thermal_tx_chainmask" -le 7 ]; then
    max_tx_chainmask_value=7
elif [ "$thermal_tx_chainmask" -gt 7 ] && [ "$thermal_tx_chainmask" -le 15 ]; then
    max_tx_chainmask_value=15
else [ "$thermal_tx_chainmask" -eq 0 ] || [ "$thermal_tx_chainmask" -gt 15 ]
    raise "THERMAL TX CHAINMASK $thermal_tx_chainmask is invalid" -l "$tc_name" -tc
fi

check_radio_mimo_config $max_tx_chainmask_value "$if_name" ||
    raise "check_radio_mimo_config THERMAL TX CHAINMASK - Failed" -l "$tc_name" -tc

log "$tc_name: Checking if TX CHAINMASK '$tx_chainmask' is valid"

if [ "$tx_chainmask" -gt 0 ] && [ "$tx_chainmask" -le 3 ]; then
    max_tx_chainmask_value=3
elif [ "$tx_chainmask" -gt 3 ] && [ "$tx_chainmask" -le 7 ]; then
    max_tx_chainmask_value=7
elif [ "$tx_chainmask" -gt 7 ] && [ "$tx_chainmask" -le 15 ]; then
    max_tx_chainmask_value=15
else [ "$tx_chainmask" -eq 0 ] || [ "$tx_chainmask" -gt 15 ]
    raise "TX CHAINMASK value is invalid" -l "$tc_name" -tc
fi

check_radio_mimo_config $max_tx_chainmask_value "$if_name" ||
    raise "check_radio_mimo_config TX CHAINMASK- Failed" -l "$tc_name" -tc

log "$tc_name: Checking is interface UP and running"
(interface_is_up "$if_name" && ${OVSH} s Wifi_VIF_State -w if_name=="$if_name") ||
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
            log "$tc_name create_radio_vif_interface - Success" ||
            raise "create_radio_vif_interface - Failed" -l "$tc_name" -tc

log "$tc_name: Changing tx_chainmask to $tx_chainmask"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u tx_chainmask "$tx_chainmask" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - tx_chainmask $tx_chainmask" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Radio_Config - tx_chainmask $tx_chainmask" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is tx_chainmask "$tx_chainmask" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - tx_chainmask $tx_chainmask" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - tx_chainmask $tx_chainmask" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - checking TX CHAINMASK $tx_chainmask at OS level"
check_tx_chainmask_at_os_level "$tx_chainmask" "$if_name" &&
    log "$tc_name: check_tx_chainmask_at_os_level - TX CHAINMASK $tx_chainmask is SET at OS level" ||
    raise "check_tx_chainmask_at_os_level - TX CHAINMASK $tx_chainmask is NOT set at" -l "$tc_name" -tc

log "$tc_name: Changing thermal_tx_chainmask to $thermal_tx_chainmask"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u thermal_tx_chainmask "$thermal_tx_chainmask" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - thermal_tx_chainmask $thermal_tx_chainmask" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Radio_Config - thermal_tx_chainmask $thermal_tx_chainmask" -l "$tc_name" -tc

log "$tc_name: Check did it change tx_chainmask to $value_to_check"
wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is tx_chainmask "$value_to_check" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - tx_chainmask $value_to_check" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - tx_chainmask $value_to_check" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - checking TX CHAINMASK $value_to_check at OS level"
check_tx_chainmask_at_os_level "$value_to_check" "$if_name" &&
    log "$tc_name: check_tx_chainmask_at_os_level - TX CHAINMASK $value_to_check is SET at OS level" ||
    raise "check_tx_chainmask_at_os_level - TX CHAINMASK $value_to_check is NOT set at" -l "$tc_name" -tc

pass
