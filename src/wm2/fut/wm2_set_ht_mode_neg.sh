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
# Wait for channel to change, not necessarily become usable (CAC for DFS)
channel_change_timeout=60

usage()
{
cat << usage_string
wm2/wm2_set_ht_mode_neg.sh [-h] arguments
Description:
    - Make sure all radio interfaces for this device are up and have valid
      configuration. If not create new interface with configuration parameters
      from test case configuration.
    - Set ht_mode to requested valid "ht_mode" for creating interface.
    - Change ht_mode to mismatch_ht_mode. Update Wifi_Radio_Config table.
    - Check if mismatch_ht_mode is applied to Wifi_Radio_State table. If applied
      test fails.
    - Check if mismatch_ht_mode is applied to system. If applied test fails.
    - Check if WIRELESS MANAGER is still running.
Arguments:
    -h  show this help message
    (if_name)         : Wifi_Radio_Config::if_name        : (string)(required)
    (vif_if_name)     : Wifi_VIF_Config::if_name          : (string)(required)
    (vif_radio_idx)   : Wifi_VIF_Config::vif_radio_idx    : (int)(required)
    (ssid)            : Wifi_VIF_Config::ssid             : (string)(required)
    (channel)         : Wifi_Radio_Config::channel        : (int)(required)
    (ht_mode)         : Wifi_Radio_Config::ht_mode        : (string)(required)
    (hw_mode)         : Wifi_Radio_Config::hw_mode        : (string)(required)
    (mode)            : Wifi_VIF_Config::mode             : (string)(required)
    (mismatch_ht_mode): mismatch ht_mode to verify        : (string)(required)
    (channel_mode)    : Wifi_Radio_Config::channel_mode   : (string)(required)
    (enabled)         : Wifi_Radio_Config::enabled        : (string)(required)
    (wifi_security_type) : 'wpa' if wpa fields are used or 'legacy' if security fields are used: (string)(required)

Wifi Security arguments(choose one or the other):
    If 'wifi_security_type' == 'wpa' (preferred)
    (wpa)             : Wifi_VIF_Config::wpa              : (string)(required)
    (wpa_key_mgmt)    : Wifi_VIF_Config::wpa_key_mgmt     : (string)(required)
    (wpa_psks)        : Wifi_VIF_Config::wpa_psks         : (string)(required)
    (wpa_oftags)      : Wifi_VIF_Config::wpa_oftags       : (string)(required)
                    (OR)
    If 'wifi_security_type' == 'legacy' (deprecated)
    (security)        : Wifi_VIF_Config::security         : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_set_ht_mode_neg.sh -if_name <IF_NAME> -vif_if_name <VIF_IF_NAME> -vif_radio_idx <VIF-RADIO-IDX> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -wpa <WPA> -wpa_key_mgmt <WPA_KEY_MGMT> -wpa_psks <WPA_PSKS> -wpa_oftags <WPA_OFTAGS>
                        (OR)
                 Run: ./wm2/wm2_set_ht_mode_neg.sh -if_name <IF_NAME> -vif_if_name <VIF_IF_NAME> -vif_radio_idx <VIF-RADIO-IDX> -ssid <SSID> -channel <CHANNEL> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -security <SECURITY>
Script usage example:
    ./wm2/wm2_set_ht_mode_neg.sh -if_name wifi2 -vif_if_name home-ap-u50 -vif_radio_idx 2 -ssid FUTssid -channel 108 -ht_mode HT40 -hw_mode 11ac -mode ap -mismatch_ht_mode HT160 -channel_mode manual -enabled "true" -wifi_security_type wpa -wpa "true" -wpa_key_mgmt "wpa-psk" -wpa_psks '["map",[["key","FutTestPSK"]]]' -wpa_oftags '["map",[["key","home--1"]]]'
    ./wm2/wm2_set_ht_mode_neg.sh -if_name wifi1 -vif_if_name home-ap-l50 -vif_radio_idx 2 -ssid FUTssid -channel 36 -ht_mode HT20 -hw_mode 11ac -mode ap -mismatch_ht_mode HT160 -channel_mode manual -enabled "true" -wifi_security_type legacy -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"]]]'
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

NARGS=24
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_set_ht_mode_neg.sh" -arg

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
        -ht_mode | \
        -vif_radio_idx)
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
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
        -vif_if_name)
            vif_if_name=${1}
            radio_vif_args="${radio_vif_args} -${option#?} ${vif_if_name}"
            shift
            ;;
        -channel_mode | \
        -enabled)
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -mismatch_ht_mode)
            mismatch_ht_mode=${1}
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
            [ "${wifi_security_type}" != "wpa" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_set_ht_mode_neg.sh" -arg
            create_radio_vif_args="${create_radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        -security)
            [ "${wifi_security_type}" != "legacy" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_set_ht_mode_neg.sh" -arg
            radio_vif_args="${radio_vif_args} -${option#?} ${1}"
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "wm2/wm2_set_ht_mode_neg.sh" -arg
            ;;
    esac
done

log_title "wm2/wm2_set_ht_mode_neg.sh: WM2 test - Verify mismatch ht_mode cannot be set - '${mismatch_ht_mode}'"

log "wm2/wm2_set_ht_mode_neg.sh: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    ${radio_vif_args} &&
        log "wm2/wm2_set_ht_mode_neg.sh: Radio/VIF states are valid" ||
            (
                log "wm2/wm2_set_ht_mode_neg.sh: Radio/VIF states are not valid, creating interface..."
                create_radio_vif_interface \
                    ${radio_vif_args} \
                    ${create_radio_vif_args} \
                    -timeout ${channel_change_timeout} \
                    -disable_cac &&
                        log "wm2/wm2_set_ht_mode_neg.sh: create_radio_vif_interface - Interface $if_name created - Success"
            ) ||
        raise "FAIL: create_radio_vif_interface {$if_name, $channel} - Interface not created" -l "wm2/wm2_set_ht_mode_neg.sh" -ds

# Update Wifi_Radio_Config with mismatched ht_mode
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u ht_mode $mismatch_ht_mode &&
    log "wm2/wm2_set_ht_mode_neg.sh: update_ovsdb_entry - Wifi_Radio_Config::ht_mode is $mismatch_ht_mode - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::ht_mode is not $mismatch_ht_mode" -l "wm2/wm2_set_ht_mode_neg.sh" -oe

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is ht_mode "$mismatch_ht_mode" -t ${channel_change_timeout} &&
    raise "FAIL: wait_ovsdb_entry - Reflected Wifi_Radio_Config to Wifi_Radio_State::ht_mode is $mismatch_ht_mode" -l "wm2/wm2_set_ht_mode_neg.sh" -tc ||
    log "wm2/wm2_set_ht_mode_neg.sh: wait_ovsdb_entry - Wifi_Radio_Config is not reflected to Wifi_Radio_State::ht_mode is not $mismatch_ht_mode - Success"

if [ $FUT_SKIP_L2 != 'true' ]; then
    # LEVEL2 check. Passes if system reports original ht_mode is still set.
    ht_mode_from_os=$(get_ht_mode_from_os "$vif_if_name" "$channel") ||
        raise "FAIL: Error while fetching ht_mode from system" -l "wm2/wm2_set_ht_mode_neg.sh" -fc

    if [ "$ht_mode_from_os" = "" ]; then
        raise "FAIL: Error while fetching ht_mode from system" -l "wm2/wm2_set_ht_mode_neg.sh" -fc
    else
        if [ "$ht_mode_from_os" != "$mismatch_ht_mode" ]; then
            log "wm2/wm2_set_ht_mode_neg.sh: ht_mode '$mismatch_ht_mode' not applied to system. System reports current ht_mode '$ht_mode_from_os' - Success"
        else
            raise "FAIL: ht_mode '$mismatch_ht_mode' applied to system. System reports current ht_mode '$ht_mode_from_os'" -l "wm2/wm2_set_ht_mode_neg.sh" -tc
        fi
    fi
fi

# Check if manager survived.
manager_bin_file="${OPENSYNC_ROOTDIR}/bin/wm"
wait_for_function_response 0 "check_manager_alive $manager_bin_file" &&
    log "wm2/wm2_set_ht_mode_neg.sh: WIRELESS MANAGER is running - Success" ||
    raise "FAIL: WIRELESS MANAGER not running/crashed" -l "wm2/wm2_set_ht_mode_neg.sh" -tc

pass
