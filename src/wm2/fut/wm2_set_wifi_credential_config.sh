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
wm2/wm2_set_wifi_credential_config.sh [-h] arguments
Description:
    - Script sets fields ssid, security and onboard_type of valid values into
      Wifi_Credential_Config and verify the applied field values.
Arguments:
    -h  show this help message
    \$1  (ssid)          : Wifi_Credential_Config::ssid          : (string)(required)
    \$2  (security)      : Wifi_Credential_Config::security      : (string)(required)
    \$3  (onboard_type)  : Wifi_Credential_Config::onboard_type  : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./wm2/wm2_set_wifi_credential_config.sh <SSID> <SECURITY> <ONBOARD_TYPE>
Script usage example:
    ./wm2/wm2_set_wifi_credential_config.sh FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"]]]' gre
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
NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "wm2/wm2_set_wifi_credential_config.sh" -arg
ssid=${1}
security=${2}
onboard_type=${3}

trap '
    fut_info_dump_line
    print_tables Wifi_Credential_Config
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "wm2/wm2_set_wifi_credential_config.sh: WM2 test - Set valid field values to Wifi_Credential_Config and verify fields are applied"

check_kconfig_option "CONFIG_MANAGER_WM" "y" ||
    raise "FAIL: CONFIG_MANAGER_WM != y - WM is not present on the device" -l "wm2/wm2_set_wifi_credential_config.sh" -tc

log "wm2/wm2_set_wifi_credential_config.sh: Inserting ssid, security and onboard_type values into Wifi_Credential_Config"
${OVSH} i Wifi_Credential_Config ssid:="$ssid" security:="$security" onboard_type:="$onboard_type" &&
    log "wm2/wm2_set_wifi_credential_config.sh: insert_ovsdb_entry - Values inserted into Wifi_Credential_Config table - Success" ||
    raise "FAIL: insert_ovsdb_entry - Failed to insert values into Wifi_Credential_Config" -l "wm2/wm2_set_wifi_credential_config.sh" -oe

wait_ovsdb_entry Wifi_Credential_Config -w ssid $ssid -is security $security -is onboard_type $onboard_type &&
    log "wm2/wm2_set_wifi_credential_config.sh: Values applied into Wifi_Credential_Config - Success" ||
    raise "FAIL: Could not apply values into Wifi_Credential_Config" -l "wm2/wm2_set_wifi_credential_config.sh" -tc

pass
