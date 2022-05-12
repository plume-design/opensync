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

manager_setup_file="wm2/wm2_setup.sh"
usage()
{
cat << usage_string
wm2/wm2_set_radio_vif_configs.sh [-h] arguments
Description:
    - Script first checks if inputted channels are different from each other. Script then tries to delete chosen VIF_CONFIGS.
      This is relation field so when deleted, changes should not be propagated to *_State tables. If interface is not UP
      it brings up the interface, and tries to delete VIF_CONFIGS and checks that nothing was changed in *_State table.
      After that, it creates new VIF_CONFIGS to see if relation is really working and new changes was correctly propagated
      to *_State tables.
Arguments:
    -h  show this help message
    \$1  (radio_idx)      : Wifi_VIF_Config::vif_radio_idx                    : (int)(required)
    \$2  (if_name)        : Wifi_Radio_Config::if_name                        : (string)(required)
    \$3  (ssid)           : Wifi_VIF_Config::ssid                             : (string)(required)
    \$4  (password)       : Wifi_VIF_Config::security                         : (string)(required)
    \$5  (channel)        : Wifi_Radio_Config::channel                        : (int)(required)
    \$6  (ht_mode)        : Wifi_Radio_Config::ht_mode                        : (string)(required)
    \$7  (hw_mode)        : Wifi_Radio_Config::hw_mode                        : (string)(required)
    \$8  (mode)           : Wifi_VIF_Config::mode                             : (string)(required)
    \$9  (vif_if_name)    : Wifi_VIF_Config::if_name                          : (string)(required)
    \$10 (custom_channel) : used as custom channel in Wifi_Radio_Config table : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_set_radio_vif_configs.sh 2 wifi1 test_wifi_50L WifiPassword123 44 HT20 11ac ap home-ap-l50 48
Script usage example:
   ./wm2/wm2_set_radio_vif_configs.sh
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

NARGS=10
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_set_radio_vif_configs.sh" -arg
vif_radio_idx=$1
if_name=$2
ssid=$3
security=$4
channel=$5
ht_mode=$6
hw_mode=$7
mode=$8
vif_if_name=$9
custom_channel=${10}

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "wm2/wm2_set_radio_vif_configs.sh: WM2 test - Testing Wifi_Radio_Config vif_configs"

log "wm2/wm2_set_radio_vif_configs.sh: Check if input channels are different from each other"
if [ "$channel" != "$custom_channel" ]; then
  log "wm2/wm2_set_radio_vif_configs.sh: channel: $channel is different from custom_channel: $custom_channel - Success"
else
  raise "FAIL: channel: $channel is the same as custom_channel: $custom_channel"
fi

log "wm2/wm2_set_radio_vif_configs.sh: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -channel "$channel" \
    -security "$security" \
    -hw_mode "$hw_mode" \
    -mode "$mode" &&
        log "wm2/wm2_set_radio_vif_configs.sh: Radio/VIF states are valid" ||
            (
                log "wm2/wm2_set_radio_vif_configs.sh: Cleaning VIF_Config"
                vif_clean
                log "wm2/wm2_set_radio_vif_configs.sh: Radio/VIF states are not valid, creating interface..."
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
                    -vif_if_name "$vif_if_name" \
                    -disable_cac &&
                        log "wm2/wm2_set_radio_vif_configs.sh: create_radio_vif_interface - Interface $if_name created - Success"
            ) ||
        raise "FAIL: create_radio_vif_interface - Interface $if_name not created" -l "wm2/wm2_set_radio_vif_configs.sh" -ds

log "wm2/wm2_set_radio_vif_configs.sh: Save Wifi_Radio_Config::vif_configs field for later use"
original_vif_configs=$(get_ovsdb_entry_value Wifi_Radio_Config vif_configs -w if_name "$if_name" -r)

log "wm2/wm2_set_radio_vif_configs.sh: TEST1 - DELETE Wifi_Radio_Config::vif_configs"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u vif_configs "[\"set\",[]]" &&
    log "wm2/wm2_set_radio_vif_configs.sh: Wifi_Radio_Config::vif_configs deleted - Success" ||
    raise "FAIL: Failed to update Wifi_Radio_Config::vif_configs is not '[\"set\",[]]'" -l "wm2/wm2_set_radio_vif_configs.sh" -oe

log "wm2/wm2_set_radio_vif_configs.sh: TEST1 - UPDATE Wifi_Radio_Config::channel with $custom_channel"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$custom_channel" &&
    log "wm2/wm2_set_radio_vif_configs.sh: update_ovsdb_entry - Wifi_Radio_Config::channel set to $custom_channel - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::channel is not $custom_channel" -l "wm2/wm2_set_radio_vif_configs.sh" -oe

log "wm2/wm2_set_radio_vif_configs.sh: TEST1 - CHECK is Wifi_Radio_Config::channel changed to $custom_channel - should not be"
wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" -is channel "$custom_channel" -ec &&
    log "wm2/wm2_set_radio_vif_configs.sh: Wifi_VIF_State was not updated - channel $custom_channel - PASS1" ||
    raise "FAIL1: Wifi_VIF_State was updated without Wifi_Radio_Config::vif_configs relation - channel $custom_channel" -l "wm2/wm2_set_radio_vif_configs.sh" -tc

log "wm2/wm2_set_radio_vif_configs.sh: TEST2 - INSERT $original_vif_configs back into Wifi_Radio_Config::vif_configs"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u vif_configs "$original_vif_configs" &&
    log "wm2/wm2_set_radio_vif_configs.sh: Wifi_Radio_Config::vif_configs inserted - vif_configs $original_vif_configs - Success" ||
    raise "FAIL: Failed to update Wifi_Radio_Config for Wifi_Radio_Config::vif_configs is not $original_vif_configs" -l "wm2/wm2_set_radio_vif_configs.sh" -oe

log "wm2/wm2_set_radio_vif_configs.sh: TEST 2 - CHECK is Wifi_VIF_State::channel updated to $custom_channel - should be"
wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" -is channel "$custom_channel" &&
    log "wm2/wm2_set_radio_vif_configs.sh: Channel updated - $custom_channel - Success" ||
    raise "FAIL: Failed to update Wifi_VIF_State for channel $custom_channel" -l "wm2/wm2_set_radio_vif_configs.sh" -tc

pass
