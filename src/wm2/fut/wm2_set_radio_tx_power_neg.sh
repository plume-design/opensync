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
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="wm2/$(basename "$0")"
manager_setup_file="wm2/wm2_setup.sh"
channel_change_timeout=60
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Make sure all radio interfaces for this device are up and have valid
      configuration. If not create new interface with configuration
      parameters from test case configuration.
    - Set tx_power to requested valid "tx_power" for creating interface.
    - Change tx_power to mismatch_tx_power. Update Wifi_Radio_Config table.
    - Check if mismatch_tx_power is applied to Wifi_Radio_State table. If applied
      test fails.
    - Check if mismatch_tx_power is applied to system. If applied test fails.
    - Check if WIRELESS MANAGER is still running.
Arguments:
    -h  show this help message
    \$1  (radio_idx)    : Wifi_VIF_Config::vif_radio_idx              : (int)(required)
    \$2  (if_name)      : Wifi_Radio_Config::if_name                  : (string)(required)
    \$3  (ssid)         : Wifi_VIF_Config::ssid                       : (string)(required)
    \$4  (password)     : Wifi_VIF_Config::security                   : (string)(required)
    \$5  (channel)      : Wifi_Radio_Config::channel                  : (int)(required)
    \$6  (ht_mode)      : Wifi_Radio_Config::ht_mode                  : (string)(required)
    \$7  (hw_mode)      : Wifi_Radio_Config::hw_mode                  : (string)(required)
    \$8  (mode)         : Wifi_VIF_Config::mode                       : (string)(required)
    \$9  (vif_if_name)  : Wifi_VIF_Config::if_name                    : (string)(required)
    \$10 (tx_power)     : used as tx_power in Wifi_Radio_Config table : (int)(required)
    \$11 (mismatch_tx_power) : used as mismatch_tx_power              : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name}
Script usage example:
   ./${tc_name} 2 wifi1 test_wifi_50L WifiPassword123 44 HT20 11ac ap home-ap-l50 23 25
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

NARGS=11
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
vif_radio_idx=$1
if_name=$2
ssid=$3
security=$4
channel=$5
ht_mode=$6
hw_mode=$7
mode=$8
vif_if_name=$9
tx_power=${10}
mismatch_tx_power=${11}

if [ $mismatch_tx_power -lt 1 ] && [ $mismatch_tx_power -gt 32 ]; then
    raise "$mismatch_tx_power is not between 1 and 32" -l "$tc_name" -s
fi

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - Testing Wifi_Radio_Config field mismatch_tx_power - '${mismatch_tx_power}'"

log "$tc_name: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -channel "$channel" \
    -security "$security" \
    -hw_mode "$hw_mode" \
    -mode "$mode" &&
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
                    -tx_power "$tx_power" \
                    -mode "$mode" \
                    -vif_if_name "$vif_if_name" \
                    -timeout ${channel_change_timeout} &&
                        log "$tc_name: create_radio_vif_interface - Interface $if_name created - Success"
            ) ||
        raise "FAIL: create_radio_vif_interface - Interface $if_name not created" -l "$tc_name" -tc

log "$tc_name: Changing tx_power to $mismatch_tx_power"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u tx_power "$mismatch_tx_power" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config::tx_power is $mismatch_tx_power - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::tx_power is not $mismatch_tx_power" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is tx_power "$mismatch_tx_power" -t ${channel_change_timeout} &&
    raise "FAIL: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::tx_power is $mismatch_tx_power" -l "$tc_name" -ow ||
    log "$tc_name: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::tx_power is not $mismatch_tx_power - Success"

# LEVEL2 check. Passes if system reports original tx_power is still set.
tx_power_from_os=$(get_tx_power_from_os "$vif_if_name") ||
    raise "FAIL: Error while fetching tx_power from system" -l "$tc_name" -fc

if [ "$tx_power_from_os" = "" ]; then
    raise "FAIL: Error while fetching tx_power from system" -l "$tc_name" -fc
else
    if [ "$tx_power_from_os" != "$mismatch_tx_power" ]; then
        log "$tc_name: tx_power '$mismatch_tx_power' not applied to system. System reports current tx_power '$tx_power_from_os' - Success"
    else
        raise "FAIL: tx_power '$mismatch_tx_power' applied to system. System reports current tx_power '$tx_power_from_os'" -l "$tc_name" -tc
    fi
fi

log "$tc_name: Reversing tx_power to normal value"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u tx_power "$tx_power" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - tx_power $tx_power" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Radio_Config - tx_power $tx_power" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is tx_power "$tx_power" -t ${channel_change_timeout} &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - tx_power $tx_power" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - tx_power $tx_power" -l "$tc_name" -tc

# Check if manager survived.
manager_bin_file="${OPENSYNC_ROOTDIR}/bin/wm"
wait_for_function_response 0 "check_manager_alive $manager_bin_file" &&
    log "$tc_name: Success: WIRELESS MANAGER is running" ||
    raise "FAIL: WIRELESS MANAGER not running/crashed" -l "$tc_name" -tc

pass

