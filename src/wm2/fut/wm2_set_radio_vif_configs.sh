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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
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
    (radio_idx)      : Wifi_VIF_Config::vif_radio_idx                    : (int)(required)
    (if_name)        : Wifi_Radio_Config::if_name                        : (string)(required)
    (ssid)           : Wifi_VIF_Config::ssid                             : (string)(required)
    (channel)        : Wifi_Radio_Config::channel                        : (int)(required)
    (ht_mode)        : Wifi_Radio_Config::ht_mode                        : (string)(required)
    (hw_mode)        : Wifi_Radio_Config::hw_mode                        : (string)(required)
    (mode)           : Wifi_VIF_Config::mode                             : (string)(required)
    (vif_if_name)    : Wifi_VIF_Config::if_name                          : (string)(required)
    (custom_channel) : used as custom channel in Wifi_Radio_Config table : (string)(required)
    (channel_mode)   : Wifi_Radio_Config::channel_mode                   : (string)(required)
    (enabled)        : Wifi_Radio_Config::enabled                        : (string)(required)
    (wifi_security_type) : 'wpa' if wpa fields are used or 'legacy' if security fields are used: (string)(required)

Wifi Security arguments(choose one or the other):
    If 'wifi_security_type' == 'wpa' (preferred)
    (wpa)            : Wifi_VIF_Config::wpa                              : (string)(required)
    (wpa_key_mgmt)   : Wifi_VIF_Config::wpa_key_mgmt                     : (string)(required)
    (wpa_psks)       : Wifi_VIF_Config::wpa_psks                         : (string)(required)
    (wpa_oftags)     : Wifi_VIF_Config::wpa_oftags                       : (string)(required)
                    (OR)
    If 'wifi_security_type' == 'legacy' (deprecated)
    (security)       : Wifi_VIF_Config::security                         : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_set_radio_vif_configs.sh  -vif_radio_idx <VIF-RADIO-IDX> -if_name <IF_NAME> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -vif_if_name <VIF_IF_NAME> -custom_channel <CUSTOM_CHANNEL> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -wpa <WPA> -wpa_key_mgmt <WPA_KEY_MGMT> -wpa_psks <WPA_PSKS> -wpa_oftags <WPA_OFTAGS>
                    (OR)
                 Run: ./wm2/wm2_set_radio_vif_configs.sh  -vif_radio_idx <VIF-RADIO-IDX> -if_name <IF_NAME> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -vif_if_name <VIF_IF_NAME> -custom_channel <CUSTOM_CHANNEL> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -security <SECURITY>
Script usage example:
   ./wm2/wm2_set_radio_vif_configs.sh -vif_radio_idx 2 -if_name wifi1 -ssid test_wifi_50L -channel 44 -ht_mode HT20 -hw_mode 11ac -mode ap -vif_if_name home-ap-l50 -custom_channel 48 -channel_mode manual -enabled "true" -wifi_security_type wpa -wpa "true" -wpa_key_mgmt "wpa-psk" -wpa_psks '["map",[["key","FutTestPSK"]]]' -wpa_oftags '["map",[["key","home--1"]]]'
   ./wm2/wm2_set_radio_vif_configs.sh -vif_radio_idx 2 -if_name wifi1 -ssid test_wifi_50L -channel 44 -ht_mode HT20 -hw_mode 11ac -mode ap -vif_if_name home-ap-l50 -custom_channel 48 -channel_mode manual -enabled "true" -wifi_security_type legacy -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"]]]'
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

NARGS=26
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_set_radio_vif_configs.sh" -arg

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

# Parsing arguments passed to the script.
while [ -n "$1" ]; do
    option=$1
    shift
    case "$option" in
        -hw_mode | \
        -mode | \
        -ssid | \
        -vif_radio_idx)
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -vif_if_name)
            vif_if_name=${1}
            radio_vif_args="${radio_vif_args} -${option#?} ${vif_if_name}"
            shift
            ;;
        -if_name)
            if_name=${1}
            radio_vif_args="${radio_vif_args} -${option#?} ${if_name}"
            shift
            ;;
        -channel)
            channel=${1}
            radio_vif_args="${radio_vif_args} -${option#?} ${channel}"
            shift
            ;;
        -ht_mode | \
        -channel_mode | \
        -enabled)
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -custom_channel)
            custom_channel=${1}
            shift
            ;;
        -wifi_security_type)
            wifi_security_type=${1}
            shift
            ;;
        -wpa | \
        -wpa_key_mgmt | \
        -wpa_psks | \
        -wpa_oftags)
            [ "${wifi_security_type}" != "wpa" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_set_radio_vif_configs.sh" -arg
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -security)
            [ "${wifi_security_type}" != "legacy" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_set_radio_vif_configs.sh" -arg
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "wm2/wm2_set_radio_vif_configs.sh" -arg
            ;;
    esac
done

log_title "wm2/wm2_set_radio_vif_configs.sh: WM2 test - Testing Wifi_Radio_Config vif_configs"

log "wm2/wm2_set_radio_vif_configs.sh: Check if input channels are different from each other"
if [ "$channel" != "$custom_channel" ]; then
  log "wm2/wm2_set_radio_vif_configs.sh: channel: $channel is different from custom_channel: $custom_channel - Success"
else
  raise "FAIL: channel: $channel is the same as custom_channel: $custom_channel"
fi

log "wm2/wm2_set_radio_vif_configs.sh: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
     ${radio_vif_args} &&
        log "wm2/wm2_set_radio_vif_configs.sh: Radio/VIF states are valid" ||
            (
                log "wm2/wm2_set_radio_vif_configs.sh: Cleaning VIF_Config"
                vif_reset
                log "wm2/wm2_set_radio_vif_configs.sh: Radio/VIF states are not valid, creating interface..."
                create_radio_vif_interface \
                    ${radio_vif_args} \
                    ${create_radio_vif_args} \
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
