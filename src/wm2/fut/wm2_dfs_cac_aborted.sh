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
# Wait for channel to change, not necessarily become usable (CAC for DFS)
channel_change_timeout=60

usage()
{
cat << usage_string
${tc_name} [-h] arguments
Testcase info:
    Problem statement (example):
        - Start on DFS channel_a and wait for CAC to complete before channel is usable
        - Radar is detected while channel_a CAC is in progress (cac = 1-10 min)
        - Driver should switch to channel_b immediately, and not wait for CAC to finish
    Script tests the following:
      - CAC must be aborted on channel_a, if channel change is requested while CAC is in progress.
      - Correct transition to "cac_started" on channel_a
      - Correct transition to "nop_finished" on channel_a after transition to channel_b
    Simplified test steps (example):
        - Ensure <CHANNEL_A> and <CHANNEL_B> are allowed
        - Configure radio, create VIF and apply <CHANNEL_A>
        - Verify if <CHANNEL_A> is applied
        - Verify if <CHANNEL_A> has started CAC
        - Change to <CHANNEL_B> while CAC is in progress
        - Verify if <CHANNEL_B> is applied
        - Verify if <CHANNEL_A> has stopped CAC and entered NOP_FINISHED
        - Verify if <CHANNEL_B> has started CAC
Arguments:
    -h  show this help message
    \$1  (if_name)          : Wifi_Radio_Config::if_name        : (string)(required)
    \$2  (vif_if_name)      : Wifi_VIF_Config::if_name          : (string)(required)
    \$3  (vif_radio_idx)    : Wifi_VIF_Config::vif_radio_idx    : (int)(required)
    \$4  (ssid)             : Wifi_VIF_Config::ssid             : (string)(required)
    \$5  (security)         : Wifi_VIF_Config::security         : (string)(required)
    \$6  (channel_a)        : Wifi_Radio_Config::channel        : (int)(required)
    \$7  (channel_b)        : Wifi_Radio_Config::channel        : (int)(required)
    \$8  (ht_mode)          : Wifi_Radio_Config::ht_mode        : (string)(required)
    \$9  (hw_mode)          : Wifi_Radio_Config::hw_mode        : (string)(required)
    \$10 (mode)             : Wifi_VIF_Config::mode             : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF_NAME> <VIF_IF_NAME> <VIF-RADIO-IDX> <SSID> <SECURITY> <CHANNEL_A> <CHANNEL_B> <HT_MODE> <HW_MODE> <MODE>
Script usage example:
    ./${tc_name} wifi2 home-ap-u50 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 120 104 HT20 11ac ap
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
NARGS=10
[ $# -lt ${NARGS} ] && usage && raise "Requires '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=${1}
vif_if_name=${2}
vif_radio_idx=${3}
ssid=${4}
security=${5}
channel_a=${6}
channel_b=${7}
ht_mode=${8}
hw_mode=${9}
mode=${10}

trap '
    run_setup_if_crashed wm || true
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - DFC CAC Aborted '${channel_a}'->'${channel_b}'"

# Sanity check - are channels even allowed on the radio
check_is_channel_allowed "$channel_a" "$if_name" &&
    log -deb "$tc_name:check_is_channel_allowed - channel $channel_a is allowed on radio $if_name" ||
    raise "channel $channel_a is not allowed on radio $if_name" -l "$tc_name" -ds
check_is_channel_allowed "$channel_b" "$if_name" &&
    log -deb "$tc_name:check_is_channel_allowed - channel $channel_b is allowed on radio $if_name" ||
    raise "channel $channel_b is not allowed on radio $if_name" -l "$tc_name" -ds

# Testcase:
# Configure radio, create VIF and apply channel
# This needs to be done simultaneously for the driver to bring up an active AP
# Function only checks if the channel is set in Wifi_Radio_State, not if it is
# available for immediate use, so CAC could be in progress. This is desired.
log "$tc_name: Configuring Wifi_Radio_Config, creating interface in Wifi_VIF_Config."
log "$tc_name: Waiting for ${channel_change_timeout}s for settings {channel:$channel_a}"
create_radio_vif_interface \
    -channel "$channel_a" \
    -channel_mode manual \
    -enabled true \
    -ht_mode "$ht_mode" \
    -hw_mode "$hw_mode" \
    -if_name "$if_name" \
    -mode "$mode" \
    -security "$security" \
    -ssid "$ssid" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -timeout ${channel_change_timeout} &&
        log "$tc_name: create_radio_vif_interface {$if_name, $channel_a} - Success" ||
        raise "create_radio_vif_interface {$if_name, $channel_a} - Failed" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel_a" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - channel $channel_a" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - channel $channel_a" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_cac_started $channel_a $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel_a - CAC STARTED" ||
    raise "$tc_name - wait_for_function_response - channel $channel_a - CAC NOT STARTED" -l "$tc_name" -tc

log "$tc_name: Do not wait for cac to finish, changing channel to $channel_b"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel_b" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - channel $channel_b" ||
    raise "$tc_name: update_ovsdb_entry - Failed to update Wifi_Radio_Config - channel $channel_b" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel_b" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - channel $channel_b" ||
    raise "$tc_name: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - channel $channel_b" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_nop_finished $channel_a $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel_a - NOP FINISHED" ||
    raise "$tc_name - wait_for_function_response - channel $channel_a - NOP NOT FINISHED" -l "$tc_name" -tc

wait_for_function_response 0 "check_is_cac_started $channel_b $if_name" &&
    log "$tc_name - wait_for_function_response - channel $channel_b - CAC STARTED" ||
    raise "$tc_name - wait_for_function_response - channel $channel_b - CAC NOT STARTED" -l "$tc_name" -tc

pass
