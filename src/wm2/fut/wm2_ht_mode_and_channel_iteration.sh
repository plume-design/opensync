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
Description:
    - Script configures radio in Wifi_Radio_Config, creates interface in
      Wifi_VIF_Config table and checks if settings are reflected in tables
      Wifi_Radio_State and Wifi_VIF_State.
      The script waits for the channel change for ${channel_change_timeout}s.
      Even if the channel is set in Wifi_Radio_State, it is not necessarily
      available for immediate use if CAC is in progress for DFS channels, but
      this is not under test in this scipt.
      Script fails if ht_mode fails to reflect to Wifi_Radio_State.
      Script fails if channel fails to reflect to Wifi_VIF_State.
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
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <VIF-IF-NAME> <VIF-RADIO-IDX> <SSID> <SECURITY> <CHANNEL> <HT-MODE> <HW-MODE> <MODE>
Script usage example:
    ./${tc_name} wifi1 home-ap-l50 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 36 HT20 11ac ap
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
NARGS=9
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

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - HT Mode and Channel Iteration - '${ht_mode}'-'${channel}'"

# Sanity check - is channel even allowed on the radio
check_is_channel_allowed "$channel" "$if_name" &&
    log -deb "$tc_name:check_is_channel_allowed - channel $channel is allowed on radio $if_name" ||
    raise "channel $channel is not allowed on radio $if_name" -l "$tc_name" -ds

# Testcase:
# Configure radio, create VIF and apply ht_mode and channel
# This needs to be done simultaneously for the driver to bring up an active AP
log "$tc_name: Configuring Wifi_Radio_Config, creating interface in Wifi_VIF_Config."
log "$tc_name: Waiting for ${channel_change_timeout}s for settings {ht_mode:$ht_mode, channel:$channel}"
create_radio_vif_interface \
    -channel "$channel" \
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
        log "$tc_name: create_radio_vif_interface {$if_name, $ht_mode, $channel} - Success" ||
        raise "create_radio_vif_interface {$if_name, $ht_mode, $channel} - Failed" -l "$tc_name" -tc

log "$tc_name: Waiting for settings to apply to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}"
wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" \
    -is channel "$channel" \
    -is ht_mode "$ht_mode" &&
        log "$tc_name: Settings applied to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}" ||
        raise "Failed to apply settings to Wifi_Radio_State {channel:$channel, ht_mode:$ht_mode}" -l "$tc_name" -tc

log "$tc_name: Waiting for channel to apply to Wifi_VIF_State {channel:$channel}"
wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" \
    -is channel "$channel" &&
        log "$tc_name: Settings applied to Wifi_VIF_State {channel:$channel}" ||
        raise "Failed to apply settings to Wifi_VIF_State {channel:$channel}" -l "$tc_name" -tc

pass
