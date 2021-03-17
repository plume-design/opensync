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

trap 'run_setup_if_crashed wm || true' EXIT SIGINT SIGTERM
tc_name="tools/device/$(basename "$0")"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Create/updates Radio/VIF interface and validate it in State table
Arguments:
    -h  show this help message
    -if_name          : Wifi_Radio_Config::if_name                 : (string)(optional)
    -vif_if_name      : Wifi_VIF_Config::if_name                   : (string)(optional)
    -vif_radio_idx    : Wifi_VIF_Config::vif_radio_idx             : (string)(optional)
    -channel          : Wifi_Radio_Config/Wifi_VIF_Config::channel : (string)(optional)
    -channel_mode     : Wifi_Radio_Config::channel_mode            : (string)(optional)
    -ht_mode          : Wifi_Radio_Config::ht_mode                 : (string)(optional)
    -hw_mode          : Wifi_Radio_Config::hw_mode                 : (string)(optional)
    -country          : Wifi_Radio_Config::country                 : (string)(optional)
    -enabled          : Wifi_Radio_Config/Wifi_VIF_Config::enabled : (string)(optional)
    -mode             : Wifi_VIF_Config::mode                      : (string)(optional)
    -ssid             : Wifi_VIF_Config::ssid                      : (string)(optional)
    -ssid_broadcast   : Wifi_VIF_Config::ssid_broadcast            : (string)(optional)
    -security         : Wifi_VIF_Config::security                  : (string)(optional)
    -parent           : Wifi_VIF_Config::parent                    : (string)(optional)
    -mac_list         : Wifi_VIF_Config::mac_list                  : (string)(optional)
    -mac_list_type    : Wifi_VIF_Config::mac_list_type             : (string)(optional)
    -tx_chainmask     : Wifi_Radio_Config::tx_chainmask            : (string)(optional)
    -tx_power         : Wifi_Radio_Config::tx_power                : (string)(optional)
    -fallback_parents : Wifi_VIF_Config::fallback_parents          : (string)(optional)
    -ap_bridge        : Wifi_VIF_Config::ap_bridge                 : (string)(optional)
    -bridge           : Wifi_VIF_Config::bridge                    : (string)(optional)
    -dynamic_beacon   : Wifi_VIF_Config::dynamic_beacon            : (string)(optional)
    -vlan_id          : Wifi_VIF_Config::vlan_id                   : (string)(optional)
Script usage example:
   ./${tc_name} -if_name wifi0 -enabled false -network false
   ./${tc_name} -if_name wifi0 -vif_if_name home-ap-24 -enabled true -network true -ht_mode HT40 -channel 6 -ssid test_ssid_name
usage_string
}
NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

log "tools/device/$(basename "$0"): create_radio_vif_interface - Bringing up interface"
create_radio_vif_interface "$@" &&
    log "tools/device/$(basename "$0"): create_radio_vif_interface - Success" ||
    raise "create_radio_vif_interface - Failed" -l "tools/device/$(basename "$0")" -tc

exit 0
