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
Description:
    - Script tries to set chosen channel. If interface is not UP it brings up the interface, checks channel
      validity, and tries to set channel to desired value.
Arguments:
    -h  show this help message
    \$1  (radio_idx)       : Wifi_VIF_Config::vif_radio_idx                         : (int)(required)
    \$2  (if_name)         : Wifi_Radio_Config::if_name                             : (string)(required)
    \$3  (ssid)            : Wifi_VIF_Config::ssid                                  : (string)(required)
    \$4  (security)        : Wifi_VIF_Config::security                              : (string)(required)
    \$5  (channel)         : Wifi_Radio_Config::channel                             : (int)(required)
    \$6  (ht_mode)         : Wifi_Radio_Config::ht_mode                             : (string)(required)
    \$7  (hw_mode)         : Wifi_Radio_Config::hw_mode                             : (string)(required)
    \$8  (mode)            : Wifi_VIF_Config::mode                                  : (string)(required)
    \$9  (country)         : Wifi_Radio_Config::country                             : (string)(required)
    \$10 (vif_if_name)     : Wifi_VIF_Config::if_name                               : (string)(required)
    \$11 (default_channel) : used as default channel when bringing up the interface : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <RADIO-IDX> <IF-NAME> <SSID> <PASSWORD> <CHANNEL> <HT-MODE> <HW-MODE> <MODE> <COUNTRY> <VIF-IF-NAME> <DEFAULT-CHANNEL>
Script usage example:
    ./${tc_name} 2 wifi2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 128 HT40 11ac ap US home-ap-u50 149
    ./${tc_name} 2 wifi1 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 36 HT20 11ac ap US home-ap-l50 44
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
NARGS=11
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM

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
default_channel=${11}

channel_change_timeout=60

log_title "$tc_name: WM2 test - Testing Wifi_Radio_Config field channel - '${channel}'"

log "$tc_name: Checking if Radio/VIF states are valid for test"
# Most iterations of this test will only change channel and ht_mode, so this
# state check will be OK for the same radio iface, since channel is not checked
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -security "$security" \
    -hw_mode "$hw_mode" \
    -mode "$mode" \
    -country "$country" &&
        log "$tc_name: Radio/VIF states are valid" ||
            (
                log "$tc_name: Cleaning VIF_Config"
                vif_clean
                log "$tc_name: Wifi_Radio_State and Wifi_VIF_State are not valid, creating interface..."
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
        raise "create_radio_vif_interface - Failed" -l "$tc_name" -tc

# Sanity check - is channel even allowed on the radio
check_is_channel_allowed "$channel" "$if_name" &&
    log -deb "$tc_name:check_is_channel_allowed - channel $channel is allowed on radio $if_name" ||
    raise "channel $channel is not allowed on radio $if_name" -l "$tc_name" -ds

# Testcase:
log "$tc_name: Changing channel to $channel"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u channel "$channel" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Radio_Config table updated - channel $channel" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Radio_Config - channel $channel" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$channel" -t ${channel_change_timeout} &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State - channel $channel" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State - channel $channel" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - checking channel $channel at OS level"
check_channel_at_os_level "$channel" "$vif_if_name" &&
    log "$tc_name: check_channel_at_os_level - Channel $channel set at OS level" ||
    raise "check_channel_at_os_level - Channel $channel not set at OS level" -l "$tc_name" -tc

pass
