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
wm2/wm2_pre_cac_channel_change_validation.sh [-h] arguments
Testcase info:

Arguments:
    -h  show this help message
    (if_name)            : Wifi_Radio_Config::if_name        : (string)(required)
    (vif_if_name)        : Wifi_VIF_Config::if_name          : (string)(required)
    (vif_radio_idx)      : Wifi_VIF_Config::vif_radio_idx    : (int)(required)
    (ssid)               : Wifi_VIF_Config::ssid             : (string)(required)
    (channel_a)          : Wifi_Radio_Config::channel        : (int)(required)
    (channel_b)          : Wifi_Radio_Config::channel        : (int)(required)
    (ht_mode)            : Wifi_Radio_Config::ht_mode        : (string)(required)
    (hw_mode)            : Wifi_Radio_Config::hw_mode        : (string)(required)
    (mode)               : Wifi_VIF_Config::mode             : (string)(required)
    (channel_mode)       : Wifi_Radio_Config::channel_mode   : (string)(required)
    (enabled)            : Wifi_Radio_Config::enabled        : (string)(required)
    (reg_domain)         : Interface regulatory domain       : (string)(required)
    (wifi_security_type) : 'wpa' if wpa fields are used or 'legacy' if security fields are used: (string)(required)

Wifi Security arguments(choose one or the other):
    If 'wifi_security_type' == 'wpa' (preferred)
    (wpa)              : Wifi_VIF_Config::wpa              : (string)(required)
    (wpa_key_mgmt)     : Wifi_VIF_Config::wpa_key_mgmt     : (string)(required)
    (wpa_psks)         : Wifi_VIF_Config::wpa_psks         : (string)(required)
    (wpa_oftags)       : Wifi_VIF_Config::wpa_oftags       : (string)(required)
                    (OR)
    If 'wifi_security_type' == 'legacy' (deprecated)
    (security)         : Wifi_VIF_Config::security         : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_pre_cac_channel_change_validation.sh -if_name <IF_NAME> -vif_if_name <VIF_IF_NAME> -vif_radio_idx <VIF-RADIO-IDX> -ssid <SSID> -channel_a <CHANNEL_A> -channel_b <CHANNEL_B> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -wpa <WPA> -wpa_key_mgmt <WPA_KEY_MGMT> -wpa_psks <WPA_PSKS> -wpa_oftags <WPA_OFTAGS>
                             (OR)
                 Run: ./wm2/wm2_pre_cac_channel_change_validation.sh -if_name <IF_NAME> -vif_if_name <VIF_IF_NAME> -vif_radio_idx <VIF-RADIO-IDX> -ssid <SSID> -channel_a <CHANNEL_A> -channel_b <CHANNEL_B> -ht_mode <HT_MODE> -hw_mode <HW_MODE> -mode <MODE> -channel_mode <CHANNEL_MODE> -enabled <ENABLED> -wifi_security_type <WIFI_SECURITY_TYPE> -security <SECURITY>
Script usage example:
    ./wm2/wm2_pre_cac_channel_change_validation.sh -if_name wifi2 -vif_if_name home-ap-u50 -vif_radio_idx 2 -ssid FUTssid -channel_a 120 -channel_b 104 -ht_mode HT20 -hw_mode 11ac -mode ap -wifi_security_type wpa -wpa "true" -wpa_key_mgmt "wpa-psk" -wpa_psks '["map",[["key","FutTestPSK"]]]' -wpa_oftags '["map",[["key","home--1"]]]'
    ./wm2/wm2_pre_cac_channel_change_validation.sh -if_name wifi2 -vif_if_name home-ap-u50 -vif_radio_idx 2 -ssid FUTssid -channel_a 120 -channel_b 104 -ht_mode HT20 -hw_mode 11ac -mode ap -channel_mode manual -enabled true -wifi_security_type legacy -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"]]]'
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
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -arg

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
        -ht_mode | \
        -mode | \
        -vif_if_name | \
        -vif_radio_idx | \
        -hw_mode | \
        -ssid | \
        -channel_mode | \
        -enabled)
            radio_vif_args_a="${radio_vif_args_a} -${option#?} ${1}"
            shift
            ;;
        -if_name)
            if_name=${1}
            radio_vif_args_a="${radio_vif_args_a} -${option#?} ${if_name}"
            shift
            ;;
        -channel_a)
            channel_a=${1}
            radio_vif_args_a="${radio_vif_args_a} -channel ${channel_a}"
            shift
            ;;
        -channel_b)
            channel_b=${1}
            shift
            ;;
        -reg_domain)
            reg_domain=${1}
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
            [ "${wifi_security_type}" != "wpa" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -arg
            radio_vif_args_a="${radio_vif_args_a} -${option#?} ${1}"
            shift
            ;;
        -security)
            [ "${wifi_security_type}" != "legacy" ] && raise "FAIL: Incorrect combination of WPA and legacy wifi security type provided" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -arg
            radio_vif_args_a="${radio_vif_args_a} -${option#?} ${1}"
            shift
            ;;
        *)
            raise "FAIL: Wrong option provided: $option" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -arg
            ;;
    esac
done

log_title "wm2/wm2_pre_cac_channel_change_validation.sh: WM2 test - PRE-CAC - Using: '${channel_a}'->'${channel_b}'"

# Testcase:
# Configure radio, create VIF and apply channel
log "wm2/wm2_pre_cac_channel_change_validation.sh: Configuring Wifi_Radio_Config, creating interface in Wifi_VIF_Config."
log "wm2/wm2_pre_cac_channel_change_validation.sh: Waiting for ${channel_change_timeout}s for settings {channel:$channel_a}"
create_radio_vif_interface \
    ${radio_vif_args_a} \
    -timeout ${channel_change_timeout} \
    -disable_cac &&
        log "wm2/wm2_pre_cac_channel_change_validation.sh: create_radio_vif_interface {$if_name, $channel_a} - Success" ||
        raise "FAIL: create_radio_vif_interface {$if_name, $channel_a} - Interface not created" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

# Validate CAC elapsed for channel
validate_cac "${if_name}" &&
    log -deb "wm2/wm2_pre_cac_channel_change_validation.sh: - CAC validated for channel ${channel_b}" ||
    raise "FAIL: validate_cac - Failed to validate CAC for $channel_b" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

# Validate PRE-CAC behaviour for channel
validate_pre_cac_behaviour ${if_name} ${reg_domain} &&
    echo "wm2/wm2_pre_cac_channel_change_validation.sh: PRE-CAC validated." ||
    raise "FAIL: PRE-CAC is not correct" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

# Update channel to channel_b, validate CAC for range
log "wm2/wm2_pre_cac_channel_change_validation.sh: Changing channel to $channel_b"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel_b" &&
    log "wm2/wm2_pre_cac_channel_change_validation.sh: update_ovsdb_entry - Wifi_Radio_Config::channel is $channel_b - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Radio_Config::channel is not $channel_b" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel_b" &&
    log "wm2/wm2_pre_cac_channel_change_validation.sh: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::channel is $channel_b - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::channel is not $channel_b" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

# Validate CAC elapsed for channel
validate_cac "${if_name}" &&
    log -deb "wm2/wm2_pre_cac_channel_change_validation.sh: - CAC validated for channel ${channel_b}" ||
    raise "FAIL: validate_cac - Failed to validate CAC for $channel_b" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

# Validate PRE-CAC behaviour for channel
validate_pre_cac_behaviour ${if_name} ${reg_domain} &&
    echo "wm2/wm2_pre_cac_channel_change_validation.sh: PRE-CAC validated." ||
    raise "FAIL: PRE-CAC is not correct" -l "wm2/wm2_pre_cac_channel_change_validation.sh" -tc

pass
