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
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="wm2/$(basename "$0")"
manager_setup_file="wm2/wm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Testcase info:
    Problem statement (example):
        - start on ch108 and wait for cac to complete
        - When radar is detected, driver moves to ch120
        - while ch108 cac is in progress (cac=10minutes), announce switch to ch124
        - channnels 116,120,124,128 are on the same vht80 unii weather channel segment
        - 'device takes too long to set the channel'
    Script tests the following:
      - cac must be aborted on initial channel, if channel change is requested while cac is in progress.
      - correct transition to cac_started amd nop_finished states
    Simplified test steps (example):
        - Ensure <CHANNEL1> and <CHANNEL2> are available for cac
        - Set <CHANNEL2> in Wifi_Radio_Config, wait for <CHANNEL2> cac_started else fail after 30s
        - Set <CHANNEL1> in Wifi_Radio_Config, wait for <CHANNEL2> nop_finished
          and <CHANNEL1> is cac_started simultaneously, else fail after 30s
Arguments:
    -h  show this help messae
    \$1  (radio_idx)        : Wifi_VIF_Config::vif_radio_idx                 : (int)(required)
    \$2  (if_name)          : Wifi_Radio_Config::if_name                     : (string)(required)
    \$3  (ssid)             : Wifi_VIF_Config::ssid                          : (string)(required)
    \$4  (security)         : Wifi_VIF_Config::security                      : (string)(required)
    \$5  (channel1)         : Wifi_Radio_Config::channel                     : (int)(required)
    \$6  (channel2)         : Wifi_Radio_Config::channel                     : (int)(required)
    \$7  (ht_mode)          : Wifi_Radio_Config::ht_mode                     : (string)(required)
    \$8  (hw_mode)          : Wifi_Radio_Config::hw_mode                     : (string)(required)
    \$9  (mode)             : Wifi_VIF_Config::mode                          : (string)(required)
    \$10 (country)          : Wifi_Radio_Config::country                     : (string)(required)
    \$11 (vif_if_name)      : Wifi_VIF_Config::if_name                       : (string)(required)
    \$12 (default_channel)  : default channel when bringing up the interface : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <RADIO-IDX> <IF-NAME> <SSID> <PASSWORD> <CHANNEL1> <CHANNEL2> <HT-MODE> <HW-MODE> <MODE> <COUNTRY> <VIF-IF-NAME> <DEFAULT-CHANNEL>
Script usage example:
   ./${tc_name} 4 wifi2 50L test_wifi_50L WifiPassword123 100 120 HT80 11ac ap home-ap-l50 140
usage_string
}
while getopts h option; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            echo "Unknown argument" && exit 1
            ;;
    esac
done
NARGS=12
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM

vif_radio_idx=$1
if_name=$2
ssid=$3
security=$4
channel1=$5
channel2=$6
ht_mode=$7
hw_mode=$8
mode=$9
country=${10}
vif_if_name=${11}
default_channel=${12}

log_title "$tc_name: WM2 test - DFC CAC Aborted"

log "$tc_name: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -channel "$default_channel" \
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
                    -channel "$default_channel" \
                    -ht_mode "$ht_mode" \
                    -hw_mode "$hw_mode" \
                    -mode "$mode" \
                    -country "$country" \
                    -vif_if_name "$vif_if_name" &&
                        log "$tc_name: create_radio_vif_interface - Success"
            ) ||
        raise "$tc_name: create_radio_vif_interface - Failed" -l "$tc_name" -tc

log "$tc_name: Changing channel to channel1: $channel1"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel1" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - channel $channel1" ||
    raise "$tc_name: update_ovsdb_entry - Failed to update Wifi_Radio_Config - channel $channel1" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel1" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - channel $channel1" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - channel $channel1" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_cac_started $channel1 $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel1 - CAC STARTED" ||
    raise "$tc_name - wait_for_function_response - channel $channel1 - CAC NOT STARTED" -l "$tc_name" -tc

log "$tc_name: Do not wait for cac to finish, changing channel to $channel2"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel2" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - channel $channel2" ||
    raise "$tc_name: update_ovsdb_entry - Failed to update Wifi_Radio_Config - channel $channel2" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel2" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - channel $channel2" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - channel $channel2" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_nop_finished $channel1 $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel1 - NOP FINISHED" ||
    raise "$tc_name - wait_for_function_response - channel $channel1 - NOP NOT FINISHED" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_cac_started $channel2 $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel2 - CAC STARTED" ||
    raise "$tc_name - wait_for_function_response - channel $channel2 - CAC NOT STARTED" -l "$tc_name" -tc

pass
