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
tools/device/configure_sta_interface.sh [-h] arguments
Description:
    - Configures STA interface and validates it in Wifi_VIF_State table
Arguments:
    -h  show this help message
    See wm2_lib::configure_sta_interface for more information
Script usage example:
   ./tools/device/configure_sta_interface.sh -if_name bhaul-sta-l50 -ssid fut-2568.bhaul -onboard_type gre -channel 36 -clear_wcc -wait_ip -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]'
usage_string
}

trap '
fut_ec=$?
fut_info_dump_line
print_tables Wifi_Radio_Config Wifi_Radio_State Wifi_VIF_Config Wifi_VIF_State Wifi_Inet_Config Wifi_Inet_State Wifi_Credentials_Config || true
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM


NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/device/configure_sta_interface.sh" -arg

log "tools/device/$(basename "$0"): configure_sta_interface - Configuring STA interface"
configure_sta_interface "$@" &&
    log "tools/device/$(basename "$0"): configure_sta_interface - Success" ||
    raise "configure_sta_interface - Failed" -l "tools/device/$(basename "$0")" -tc

exit 0
