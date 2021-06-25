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
    - Script sets fields ssid, security and onboard_type of valid values into
      Wifi_Credential_Config and verify the applied field values.
Arguments:
    -h  show this help message
    \$1  (ssid)          : Wifi_Credential_Config::ssid          : (string)(required)
    \$2  (security)      : Wifi_Credential_Config::security      : (string)(required)
    \$3  (onboard_type)  : Wifi_Credential_Config::onboard_type  : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <SSID> <SECURITY> <ONBOARD_TYPE>
Script usage example:
    ./${tc_name} FUTssid '["map",[["encryption","WPA-PSK"],["key","FUTpsk"]]]' gre
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
NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
ssid=${1}
security=${2}
onboard_type=${3}

trap '
    fut_info_dump_line
    print_tables Wifi_Credential_Config
    fut_info_dump_line
    run_setup_if_crashed wm || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: WM2 test - sets valid field values to Wifi_Credential_Config and verify fields are applied"

check_kconfig_option "CONFIG_MANAGER_WM" "y" ||
    raise "FAIL: CONFIG_MANAGER_WM != y - WM is not present on the device" -l "$tc_name" -tc

log "$tc_name: Inserting values into Wifi_Credential_Config"
${OVSH} i Wifi_Credential_Config ssid:="$ssid" security:="$security" onboard_type:="$onboard_type" &&
    log "$tc_name: insert_ovsdb_entry - field values inserted into Wifi_Credential_Config table" ||
    raise "FAIL: insert_ovsdb_entry - Failed to insert field values Wifi_Credential_Config" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Credential_Config -w ssid $ssid -is security $security -is onboard_type $onboard_type &&
    log "$tc_name - Field values are applied to Wifi_Credential_Config" ||
    raise "FAIL: Could not apply field values to Wifi_Credential_Config" -l "$fn_name" -tc

pass
