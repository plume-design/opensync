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
    - Script creates AP with WPA3-Personal (SAE) authentication on chosen radio interface.
      Then waits for AP to set up properly by checking Wifi_VIF_State
Arguments:
    -h  show this help message
    \$1  (vif_radio_idx) : Wifi_VIF_Config::vif_radio_idx             : (int)(required)
    \$2  (radio_if_name) : Wifi_Radio_Config::if_name                 : (string)(required)
    \$3  (ssid)          : Wifi_VIF_Config::ssid                      : (string)(required)
    \$4  (ssid_broadcast): Wifi_VIF_Config::ssid_broadcast            : (string)(required)
    \$5  (wpa_psks)      : Wifi_VIF_Config::wpa_psks                  : (string)(required)
    \$6  (wpa_oftags)    : Wifi_VIF_Config::wpa_oftags                : (string)(required)
    \$7  (channel)       : Wifi_Radio_Config::channel                 : (int)(required)
    \$8  (ht_mode)       : Wifi_Radio_Config::ht_mode                 : (string)(required)
    \$9  (hw_mode)       : Wifi_Radio_Config::hw_mode                 : (string)(required)
    \$10 (vif_if_name)   : Wifi_VIF_Config::if_name                   : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} 2 wl1 fut-wpa3-ssid enabled '["map",[["key-0","fut-wpa3-psk"]]]' '["map",[["key-0","home--1"]]]' 1 HT20 11ax wl1.2
Script usage example:
   ./${tc_name} 2 wl1 fut-wpa3-ssid enabled '["map",[["key-0","fut-wpa3-psk"]]]' '["map",[["key-0","home--1"]]]' 1 HT20 11ax wl1.2
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "${tc_name}" -arg
trap '
    fut_info_dump_line
    print_tables Wifi_Radio_Config Wifi_Radio_State
    print_tables Wifi_VIF_Config Wifi_VIF_State
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

vif_radio_idx=$1
radio_if_name=$2
ssid=$3
ssid_broadcast=$4
wpa_psks=$5
wpa_oftags=$6
channel=$7
ht_mode=$8
hw_mode=$9
vif_if_name=$10

log_title "$tc_name: WM2 test - Testing WPA3 AP creation"

log "$tc_name: Cleaning VIF_Config"
vif_clean

log "$tc_name: Creating VIF interface"
create_radio_vif_interface \
    -if_name "$radio_if_name" \
    -vif_if_name "$vif_if_name" \
    -vif_radio_idx "$vif_radio_idx" \
    -ssid "$ssid" \
    -ssid_broadcast "$ssid_broadcast" \
    -wpa "true" \
    -wpa_key_mgmt "sae" \
    -wpa_psks "$wpa_psks" \
    -wpa_oftags "$wpa_oftags" \
    -channel "$channel" \
    -hw_mode "$hw_mode" \
    -mode "ap" \
    -enabled "true" \
    -ht_mode "$ht_mode" \
    -channel_mode "manual" &&
log "$tc_name: create_radio_vif_interface - Success" ||    
raise "create_radio_vif_interface - Failed" -l "$tc_name" -tc

pass
