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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures interfaces upnp through Wifi_inet_Config 'upnp_mode' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (internal_if) : used to set internal_if in Wifi_Inet_Config table as internal UPnP interface : (string)(required)
    \$2 (external_if) : used to set external_if in Wifi_Inet_Config table as external UPnP interface : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <INTERNAL-IF> <EXTERNAL-IF>
Script usage example:
   ./${tc_name} eth0 br-wan
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

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
internal_if=$1
external_if=$2

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    fut_info_dump_line
    reset_inet_entry $internal_if
    reset_inet_entry $external_if
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

log_title "$tc_name: NM2 test - Testing UPnP mode"

log "$tc_name: Creating Wifi_Inet_Config entry for interface $internal_if"
create_inet_entry \
    -if_name "$internal_if" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -netmask 255.255.255.0 \
    -inet_addr 10.10.10.30 &&
        log "$tc_name: Interface $internal_if created - Success" ||
        raise "FAIL: Failed to create $internal_if interface" -l "$tc_name" -ds

log "$tc_name: Setting UPNP_MODE internal on $internal_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$internal_if" -u upnp_mode internal &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is 'internal' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not 'internal'" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$internal_if" -is upnp_mode internal &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'internal' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'internal'" -l "$tc_name" -tc

log "$tc_name: Setting UPNP_MODE external on $external_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$external_if" -u upnp_mode external &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config::upnp_mode is 'external' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not 'external'" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$external_if" -is upnp_mode external &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'external' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'external'" -l "$tc_name" -tc

log "$tc_name: Checking UPnP configuration is valid - LEVEL2"
wait_for_function_response 0 "check_upnp_configuration_valid $internal_if $external_if" &&
    log "$tc_name: LEVEL2 - UPNP applied to system - Success" ||
    raise "FAIL: LEVEL2 - Failed to apply UPNP to system" -l "$tc_name" -tc

log "$tc_name: Disabling UPNP_MODE on $internal_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$internal_if" -u upnp_mode "[\"set\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table::upnp_mode is [\"set\",[]]" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not [\"set\",[]]" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$internal_if" -is upnp_mode "disabled" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'disabled' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'disabled'" -l "$tc_name" -tc

log "$tc_name: Disabling UPNP_MODE on $external_if"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$external_if" -u upnp_mode "[\"set\",[]]" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated::upnp_mode is [\"set\",[]] - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::upnp_mode is not [\"set\",[]]" -l "$tc_name" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$external_if" -is upnp_mode "disabled" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::upnp_mode is 'disabled' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::upnp_mode is not 'disabled'" -l "$tc_name" -tc

log "$tc_name: Checking UPnP configuration is valid - LEVEL2"
wait_for_function_response 1 "check_upnp_configuration_valid $internal_if $external_if" &&
    log "$tc_name: LEVEL2 - UPNP removed from system - Success" ||
    raise "FAIL: LEVEL2 - Failed to remove UPNP from system" -l "$tc_name" -tc

pass
