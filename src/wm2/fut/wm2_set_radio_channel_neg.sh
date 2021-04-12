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
    - Make sure all radio interfaces for this device are up and have valid
      configuration. If not create new interface with configuration parameters
      from test case configuration.
    - Set channel to requested "channel" value first.
    - Check for mismatch_channel is not allowed on the radio.
    - Change channel to mismatch_channel. Update Wifi_Radio_Config table.
    - Check if channel setting is applied to Wifi_Radio_State table. If applied
      test fails.
    - Check if channel setting is applied to system. If applied test fails.
    - Check if WIRELESS MANAGER is still running.
Arguments:
    -h  show this help message
    \$1  (if_name)         : Wifi_Radio_Config::if_name        : (string)(required)
    \$2  (vif_if_name)     : Wifi_VIF_Config::if_name          : (string)(required)
    \$3  (vif_radio_idx)   : Wifi_VIF_Config::vif_radio_idx    : (int)(required)
    \$4  (ssid)            : Wifi_VIF_Config::ssid             : (string)(required)
    \$5  (security)        : Wifi_VIF_Config::security         : (string)(required)
    \$6  (channel)         : Wifi_Radio_Config::channel        : (int)(required)
    \$7  (ht_mode)         : Wifi_Radio_Config::ht_mode        : (string)(required)
    \$8  (hw_mode)         : Wifi_Radio_Config::hw_mode        : (string)(required)
    \$9  (mode)            : Wifi_VIF_Config::mode             : (string)(required)
    \$10 (mismatch_channel): mismatch channel to verify        : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF_NAME> <VIF_IF_NAME> <VIF-RADIO-IDX> <SSID> <SECURITY> <CHANNEL> <HT_MODE> <HW_MODE> <MODE> <MISMATCH_CHANNEL>
Script usage example:
    ./${tc_name} wifi2 home-ap-u50 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 108 HT40 11ac ap 180
    ./${tc_name} wifi1 home-ap-l50 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 36 HT20 11ac ap 134
    ./${tc_name} wifi0 home-ap-24 2 FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"],["mode","2"]]]' 1 HT20 11n ap 36
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
mismatch_channel=${10}

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - Verify mismatching channels cannot be set - '${mismatch_channel}'"

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
                    -mode "$mode" \
                    -vif_if_name "$vif_if_name" \
                    -timeout ${channel_change_timeout} &&
                        log "$tc_name: create_radio_vif_interface - Success"
            ) ||
        raise "create_radio_vif_interface - Failed" -l "$tc_name" -tc

wait_for_function_response 'notempty' "get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name ${if_name}" &&
allowed_channels=$(get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name "$if_name" -raw)
echo "$allowed_channels" | grep -qwF "$mismatch_channel" &&
    raise "FAIL:  Radio $if_name supports channel $mismatch_channel" -l $tc_name -tc ||
    log "$tc_name: Radio $if_name not supports channel $mismatch_channel - SUCCESS"

update_ovsdb_entry Wifi_Radio_Config -w if_name $if_name -u channel $mismatch_channel &&
    log "$tc_name: wait_ovsdb_entry - Updated channel $mismatch_channel to Wifi_Radio_Config" ||
    raise "FAIL: wait_ovsdb_entry - Could not channel $mismatch_channel to Wifi_Radio_Config" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is channel "$mismatch_channel" -t ${channel_change_timeout} &&
    raise "FAIL: wait_ovsdb_entry - Reflected Wifi_Radio_Config to Wifi_Radio_State - channel $mismatch_channel" -l "$tc_name" -tc ||
    log "$tc_name: wait_ovsdb_entry - Wifi_Radio_Config is not reflected to Wifi_Radio_State - channel $mismatch_channel - SUCCESS"

channel_from_os=$(get_channel_from_os $vif_if_name)
if [ $? -eq 1 ]; then
    raise "FAIL: Requires one input parameter" -l $tc_name -tc
elif [ "$channel_from_os" == "" ]; then
        raise "FAIL: Error while fetching channel from os" -l $tc_name -tc
else
    if [ "$channel_from_os" != "$mismatch_channel" ]; then
        log "$tc_name: get_channel_from_os - Channel $mismatch_channel not applied to system - SUCCESS"
    else
        raise "FAIL: get_channel_from_os - Channel $mismatch_channel applied to system" -l "$tc_name" -tc
    fi
fi

# Check if manager survived.
manager_bin_file="${OPENSYNC_ROOTDIR}/bin/wm"
wait_for_function_response 0 "check_manager_alive $manager_bin_file" &&
    log "$tc_name: SUCCESS: WIRELESS MANAGER is running" ||
    raise "FAIL: WIRELESS MANAGER not running/crashed" -l "$tc_name" -tc

pass

