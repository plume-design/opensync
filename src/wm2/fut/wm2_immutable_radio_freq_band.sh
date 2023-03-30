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
wm2/wm2_immutable_radio_freq_band.sh [-h] arguments
Description:
    - Script tries to set chosen FREQ BAND. This is IMMUTABLE field and it can't be changed. If interface is not UP it brings up
      the interface, and tries to set FREQ BAND to desired value. IF IMMUTABLE field is changed test will FAIL.
Arguments:
    -h  show this help message
    (radio_idx)        : Wifi_VIF_Config::vif_radio_idx               : (int)(required)
    (if_name)          : Wifi_Radio_Config::if_name                   : (string)(required)
    (ssid)             : Wifi_VIF_Config::ssid                        : (string)(required)
    (channel)          : Wifi_Radio_Config::channel                   : (int)(required)
    (ht_mode)          : Wifi_Radio_Config::ht_mode                   : (string)(required)
    (hw_mode)          : Wifi_Radio_Config::hw_mode                   : (string)(required)
    (mode)             : Wifi_VIF_Config::mode                        : (string)(required)
    (vif_if_name)      : Wifi_VIF_Config::if_name                     : (string)(required)
    (freq_band)        : used as freq_band in Wifi_Radio_Config table : (string)(required)
    (channel_mode)     : Wifi_Radio_Config::channel_mode              : (string)(required)
    (enabled)          : Wifi_Radio_Config::enabled                   : (string)(required)
    (wifi_security_type) : 'wpa' if wpa fields are used or 'legacy' if security fields are used: (string)(required)

Wifi Security arguments(choose one or the other):
    If 'wifi_security_type' == 'wpa' (preferred)
    (wpa)              : Wifi_VIF_Config::wpa                         : (string)(required)
    (wpa_key_mgmt)     : Wifi_VIF_Config::wpa_key_mgmt                : (string)(required)
    (wpa_psks)         : Wifi_VIF_Config::wpa_psks                    : (string)(required)
    (wpa_oftags)       : Wifi_VIF_Config::wpa_oftags                  : (string)(required)
                    (OR)
    If 'wifi_security_type' == 'legacy' (deprecated)
    (security)         : Wifi_VIF_Config::security                    : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_immutable_radio_freq_band.sh -vif_radio_idx <RADIO-IDX> -if_name <IF-NAME> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT-MODE> -hw_mode <HW-MODE> -mode <MODE> -vif_if_name <VIF-IF-NAME> -freq_band <FREQ-BAND> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -wpa <WPA> -wpa_key_mgmt <WPA_KEY_MGMT> -wpa_psks <WPA_PSKS> -wpa_oftags <WPA_OFTAGS>
                    (OR)
                 Run: ./wm2/wm2_immutable_radio_freq_band.sh -vif_radio_idx <RADIO-IDX> -if_name <IF-NAME> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT-MODE> -hw_mode <HW-MODE> -mode <MODE> -vif_if_name <VIF-IF-NAME> -freq_band <FREQ-BAND> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -security <SECURITY>
Script usage example:
   ./wm2/wm2_immutable_radio_freq_band.sh -vif_radio_idx 2 -if_name wifi1 -ssid test_wifi_50L -channel 44 -ht_mode HT20 -hw_mode 11ac -mode ap -vif_if_name home-ap-l50 -freq_band 5GU -channel_mode manual -enabled "true" -wifi_security_type wpa -wpa "true" -wpa_key_mgmt "wpa-psk" -wpa_psks '["map",[["key","FutTestPSK"]]]' -wpa_oftags '["map",[["key","home--1"]]]'
   ./wm2/wm2_immutable_radio_freq_band.sh -vif_radio_idx 2 -if_name wifi1 -ssid test_wifi_50L -channel 44 -ht_mode HT20 -hw_mode 11ac -mode ap -vif_if_name home-ap-l50 -freq_band 5GU -channel_mode manual -enabled "true" -wifi_security_type legacy -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"]]]'
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
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_immutable_radio_freq_band.sh" -arg

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
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
        -channel | \
        -vif_if_name | \
        -vif_radio_idx)
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -if_name)
            if_name=${1}
            radio_vif_args="${radio_vif_args} -${option#?} ${if_name}"
            shift
            ;;
        -channel_mode | \
        -enabled | \
        -ht_mode)
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -freq_band)
            freq_band=${1}
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
            [ "${wifi_security_type}" != "wpa" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_immutable_radio_freq_band.sh" -arg
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -security)
            [ "${wifi_security_type}" != "legacy" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_immutable_radio_freq_band.sh" -arg
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "wm2/wm2_immutable_radio_freq_band.sh" -arg
            ;;
    esac
done

log_title "wm2/wm2_immutable_radio_freq_band.sh: WM2 test - Immutable radio frequency band - '${freq_band}'"

log "wm2/wm2_immutable_radio_freq_band.sh: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    ${radio_vif_args} &&
        log "wm2/wm2_immutable_radio_freq_band.sh: Radio/VIF states are valid" ||
            (
                log "wm2/wm2_immutable_radio_freq_band.sh: Cleaning VIF_Config"
                vif_reset
                log "wm2/wm2_immutable_radio_freq_band.sh: Radio/VIF states are not valid, creating interface..."
                create_radio_vif_interface \
                    ${radio_vif_args} \
                    ${create_radio_vif_args} \
                    -disable_cac &&
                        log "wm2/wm2_immutable_radio_freq_band.sh: create_radio_vif_interface - Interface $if_name created - Success"
            ) ||
        raise "FAIL: create_radio_vif_interface - Interface $if_name not created" -l "wm2/wm2_immutable_radio_freq_band.sh" -ds

original_band=$(get_ovsdb_entry_value Wifi_Radio_State freq_band -w if_name "$if_name")

if [ "$freq_band" = "$original_band" ]; then
    raise "FAIL: Chosen FREQ BAND ($freq_band) needs to be DIFFERENT from default FREQ BAND ($original_band) - ['2.4G', '5G', '5GL', '5GU']" -l "wm2/wm2_immutable_radio_freq_band.sh" -arg
fi

log "wm2/wm2_immutable_radio_freq_band.sh: Changing FREQ BAND to $freq_band"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u freq_band "$freq_band" &&
    log "wm2/wm2_immutable_radio_freq_band.sh: update_ovsdb_entry - Wifi_Radio_Config::freq_band is $freq_band - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Radio_Config::freq_band is not $freq_band" -l "wm2/wm2_immutable_radio_freq_band.sh" -oe

res=$(wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is freq_band "$freq_band" -ec)

log "wm2/wm2_immutable_radio_freq_band.sh: Reversing FREQ BAND to normal value"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u freq_band "$original_band" &&
    log "wm2/wm2_immutable_radio_freq_band.sh: update_ovsdb_entry - Wifi_Radio_Config table::freq_band is $original_band - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Radio_Config::freq_band is not $original_band" -l "wm2/wm2_immutable_radio_freq_band.sh" -oe

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is freq_band "$original_band" &&
    log "wm2/wm2_immutable_radio_freq_band.sh: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::freq_band is $original_band - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::freq_band is not $original_band" -l "wm2/wm2_immutable_radio_freq_band.sh" -tc

if [ "$res" -eq 0 ]; then
    raise "FAIL: Immutable field freq_band was changed to $freq_band" -l "wm2/wm2_immutable_radio_freq_band.sh" -tc
fi

pass
