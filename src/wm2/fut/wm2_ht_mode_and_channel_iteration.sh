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
    - Script configures interface through Wifi_Radio_Config/Wifi_VIF_Config table and checks if it is reflected into Wifi_Radio_State/Wifi_VIF_State
      Script fails if ht_mode fails to reflect to Wifi_Radio_State or channel fails to reflect to Wifi_VIF_State
Arguments:
    -h  show this help message
    \$1  (if_name)       : Wifi_Radio_Config::if_name     : (string)(required)
    \$2  (vif_if_name)   : Wifi_VIF_Config::if_name       : (string)(required)
    \$3  (vif_radio_idx) : Wifi_VIF_Config::vif_radio_idx : (int)(required)
    \$4  (ssid)          : Wifi_VIF_Config::ssid          : (string)(required)
    \$5  (security)      : Wifi_VIF_Config::security      : (string)(required)
    \$6  (channel)       : Wifi_Radio_Config::channel     : (int)(required)
    \$7  (ht_mode)       : Wifi_Radio_Config::ht_mode     : (string)(required)
    \$8  (hw_mode)       : Wifi_Radio_Config::hw_mode     : (string)(required)
    \$9  (mode)          : Wifi_VIF_Config::mode          : (string)(required)
    \$10 (country)       : Wifi_Radio_Config::country     : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <VIF-IF-NAME> <VIF-RADIO-IDX> <SSID> <SECURITY> <CHANNEL> <HT-MODE> <HW-MODE> <MODE> <COUNTRY>
Script usage example:
    ./${tc_name} wifi1 home-ap-l50 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 36 HT20 11ac ap US
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
[ $# -ne ${NARGS} ] && usage && raise "Requires '${NARGS}' input argument(s)" -l "${tc_name}" -arg

if_name=${1}
vif_if_name=${2}
vif_radio_idx=${3}
ssid=${4}
security=${5}
channel=${6}
ht_mode=${7}
hw_mode=${8}
mode=${9}
country=${10}

channel_change_timeout=60

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - HT Mode and Channel Iteration - '${ht_mode}'-'${channel}'"

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
                # Do not set channel and ht_mode here yet, they will be set below as part of testcase
                create_radio_vif_interface \
                    -vif_radio_idx "$vif_radio_idx" \
                    -channel_mode manual \
                    -if_name "$if_name" \
                    -ssid "$ssid" \
                    -security "$security" \
                    -enabled true \
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
log "$tc_name: Applying interface settings {ht_mode:$ht_mode, channel:$channel}"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" \
    -u ht_mode "$ht_mode" \
    -u channel "$channel" &&
        log "$tc_name: Wifi_Radio_Config table updated - {ht_mode:$ht_mode, channel:$channel}" ||
        raise "Failed to update Wifi_Radio_Config - {ht_mode:$ht_mode, channel:$channel}" -l "$tc_name" -tc

log "$tc_name: Waiting for settings to apply to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}"
wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" \
    -is channel "$channel" \
    -is ht_mode "$ht_mode" \
    -t ${channel_change_timeout} &&
        log "$tc_name: Settings applied to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}" ||
        raise "Failed to apply settings to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}" -l "$tc_name" -tc

log "$tc_name: Waiting for channel to apply to Wifi_VIF_State {channel:$channel}"
wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" \
    -is channel "$channel" &&
        log "$tc_name: Settings applied to Wifi_VIF_State {channel:$channel}" ||
        raise "Failed to apply settings to Wifi_VIF_State {channel:$channel}" -l "$tc_name" -tc

pass
