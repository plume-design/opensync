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
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" &> /dev/null
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" &> /dev/null

usage()
{
cat << usage_string
tools/device/prepare_radio_vif_interface.sh [-h] arguments
Description:
    - Checks if entry in *State tables for required Radio/VIF interface exists.
    It does not check channel and ht_mode values, but sets these values afterwards
    if entry exists.
    If entry does not exist, then it creates entry in *Config tables with the
    provided arguments and verifies the entry is reflected to *State tables.
Arguments:
    -h  show this help message
    -if_name          : Wifi_Radio_Config::if_name                 : (string)(required)
    -channel          : Wifi_Radio_Config/Wifi_VIF_Config::channel : (int)(required)
    -channel_mode     : Wifi_Radio_Config::channel_mode            : (string)(required)
    -enabled          : Wifi_Radio_Config/Wifi_VIF_Config::enabled : (string)(required)
    -ht_mode          : Wifi_Radio_Config::ht_mode                 : (string)(required)
    -mode             : Wifi_VIF_Config::mode                      : (string)(required)
    -security         : Wifi_VIF_Config::security                  : (string)(required)
    -ssid             : Wifi_VIF_Config::ssid                      : (string)(required)
    -ssid_broadcast   : Wifi_VIF_Config::ssid_broadcast            : (string)(required)
    -vif_if_name      : Wifi_VIF_Config::if_name                   : (string)(required)
    -vif_radio_idx    : Wifi_VIF_Config::vif_radio_idx             : (int)(required)
Script usage example:
    ./tools/device/prepare_radio_vif_interface.sh wl1 6 manual true HT20 ap '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]' FUT_ssid_e45f0141a75f enabled wl1.2 2
usage_string
}

trap '
fut_ec=$?
fut_info_dump_line
if [ $fut_ec -ne 0 ]; then
    print_tables Wifi_Radio_Config Wifi_Radio_State Wifi_VIF_Config Wifi_VIF_State
    check_restore_ovsdb_server
fi
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM


NARGS=11
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/device/prepare_radio_vif_interface.sh" -arg
if_name=$1
channel=$2
channel_mode=$3
enabled=$4
ht_mode=$5
mode=$6
security=$7
ssid=$8
ssid_broadcast=$9
vif_if_name=${10}
vif_radio_idx=${11}

log "tools/device/prepare_radio_vif_interface.sh: Preparing interface"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -security "$security" \
    -mode "$mode" &&
        (
            log "tools/device/prepare_radio_vif_interface.sh: Radio/VIF states are valid"
            log "tools/device/prepare_radio_vif_interface.sh: Setting channel and ht_mode only to required values"
            log "tools/device/prepare_radio_vif_interface.sh: Waiting for settings to apply to Wifi_Radio_Config {channel:$channel}"
            update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel" &&
                log "tools/device/prepare_radio_vif_interface.sh: update_ovsdb_entry - Wifi_Radio_Config::channel is $channel - Success" ||
                raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::channel is not $channel" -l "tools/device/prepare_radio_vif_interface.sh" -oe

            log "tools/device/prepare_radio_vif_interface.sh: Waiting for settings to apply to Wifi_Radio_Config {ht_mode:$ht_mode}"
            update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u ht_mode "$ht_mode" &&
                log "tools/device/prepare_radio_vif_interface.sh: update_ovsdb_entry - Wifi_Radio_Config::ht_mode is $ht_mode - Success" ||
                raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::ht_mode is not $ht_mode" -l "tools/device/prepare_radio_vif_interface.sh" -oe

            log "tools/device/prepare_radio_vif_interface.sh: Waiting for settings to apply to Wifi_Radio_State {channel:$channel}"
            wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel" &&
                log "tools/device/prepare_radio_vif_interface.sh: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::channel is $channel - Success" ||
                raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::channel is not $channel" -l "tools/device/prepare_radio_vif_interface.sh" -tc

            log "tools/device/prepare_radio_vif_interface.sh: Waiting for settings to apply to Wifi_Radio_State {ht_mode:$ht_mode}"
            wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is ht_mode "$ht_mode" &&
                log "tools/device/prepare_radio_vif_interface.sh: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::ht_mode is $ht_mode - Success" ||
                raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::ht_mode is not $ht_mode" -l "tools/device/prepare_radio_vif_interface.sh" -tc

            validate_cac "${if_name}" &&
                log "tools/device/prepare_radio_vif_interface.sh: CAC time elapsed or not needed" ||
                raise "FAIL: CAC failed. Channel is not usable" -l "tools/device/prepare_radio_vif_interface.sh" -ds
        ) ||
        (
            log "tools/device/prepare_radio_vif_interface.sh: Radio/VIF states are not valid"
            log "tools/device/prepare_radio_vif_interface.sh: Cleaning VIF_Config..."
            vif_clean
            log "tools/device/prepare_radio_vif_interface.sh: Creating interface..."
            create_radio_vif_interface \
                -vif_radio_idx "$vif_radio_idx" \
                -channel_mode "$channel_mode" \
                -enabled "$enabled" \
                -if_name "$if_name" \
                -ssid "$ssid" \
                -ssid_broadcast "$ssid_broadcast" \
                -security "$security" \
                -channel "$channel" \
                -ht_mode "$ht_mode" \
                -mode "$mode" \
                -vif_if_name "$vif_if_name" &&
                    log "tools/device/prepare_radio_vif_interface.sh: create_radio_vif_interface - Interface $if_name created - Success" ||
                    raise "FAIL: create_radio_vif_interface - Interface $if_name not created" -l "tools/device/prepare_radio_vif_interface.sh" -ds
        )

exit 0
