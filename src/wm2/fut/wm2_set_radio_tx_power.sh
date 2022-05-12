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
wm2/wm2_set_radio_tx_power.sh [-h] arguments
Description:
    - Script tries to set chosen TX POWER. If interface is not UP it brings up the interface, and tries to set
      TX POWER to desired value.
Arguments:
    -h  show this help message
    \$1  (radio_idx)    : Wifi_VIF_Config::vif_radio_idx              : (int)(required)
    \$2  (if_name)      : Wifi_Radio_Config::if_name                  : (string)(required)
    \$3  (ssid)         : Wifi_VIF_Config::ssid                       : (string)(required)
    \$4  (password)     : Wifi_VIF_Config::security                   : (string)(required)
    \$5  (channel)      : Wifi_Radio_Config::channel                  : (int)(required)
    \$6  (ht_mode)      : Wifi_Radio_Config::ht_mode                  : (string)(required)
    \$7  (hw_mode)      : Wifi_Radio_Config::hw_mode                  : (string)(required)
    \$8  (mode)         : Wifi_VIF_Config::mode                       : (string)(required)
    \$9  (vif_if_name)  : Wifi_VIF_Config::if_name                    : (string)(required)
    \$10 (tx_power)     : used as tx_power in Wifi_Radio_Config table : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_set_radio_tx_power.sh
Script usage example:
   ./wm2/wm2_set_radio_tx_power.sh 2 wifi1 test_wifi_50L WifiPassword123 44 HT20 11ac ap home-ap-l50 23
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "wm2/wm2_set_radio_tx_power.sh" -arg
vif_radio_idx=$1
if_name=$2
ssid=$3
security=$4
channel=$5
ht_mode=$6
hw_mode=$7
mode=$8
vif_if_name=$9
tx_power=${10}

trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "wm2/wm2_set_radio_tx_power.sh: WM2 test - Testing OVSDB and LEVEL2 - Tx_power - '${tx_power}'"

log "wm2/wm2_set_radio_tx_power.sh: Checking if Radio/VIF states are valid for test"
check_radio_vif_state \
    -if_name "$if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -channel "$channel" \
    -security "$security" \
    -hw_mode "$hw_mode" \
    -mode "$mode" &&
        log "wm2/wm2_set_radio_tx_power.sh: Radio/VIF states are valid" ||
            (
                log "wm2/wm2_set_radio_tx_power.sh: Cleaning VIF_Config"
                vif_clean
                log "wm2/wm2_set_radio_tx_power.sh: Radio/VIF states are not valid, creating interface..."
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
                        log "wm2/wm2_set_radio_tx_power.sh: create_radio_vif_interface - Interface $if_name created - Success"
            ) ||
        raise "FAIL: create_radio_vif_interface - Interface $if_name not created" -l "wm2/wm2_set_radio_tx_power.sh" -ds

log "wm2/wm2_set_radio_tx_power.sh: Changing tx_power to $tx_power"
update_ovsdb_entry Wifi_Radio_Config -w if_name "$if_name" -u tx_power "$tx_power" &&
    log "wm2/wm2_set_radio_tx_power.sh: update_ovsdb_entry - Wifi_Radio_Config::tx_power is $tx_power - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Radio_Config::tx_power is not $tx_power" -l "wm2/wm2_set_radio_tx_power.sh" -oe

wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is tx_power "$tx_power" &&
    log "wm2/wm2_set_radio_tx_power.sh: wait_ovsdb_entry - Wifi_Radio_Config reflected to Wifi_Radio_State::tx_power is $tx_power - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Radio_Config to Wifi_Radio_State::tx_power is not $tx_power" -l "wm2/wm2_set_radio_tx_power.sh" -ow

log "wm2/wm2_set_radio_tx_power.sh: Checking tx_power $tx_power at system level - LEVEL2"
check_tx_power_at_os_level "$tx_power" "$vif_if_name" "$if_name" &&
    log "wm2/wm2_set_radio_tx_power.sh: LEVEL2 - check_tx_power_at_os_level - tx_power $tx_power set at system level - Success" ||
    raise "FAIL: LEVEL2 - check_tx_power_at_os_level - tx_power $tx_power not set at system level" -l "wm2/wm2_set_radio_tx_power.sh" -tc

pass
