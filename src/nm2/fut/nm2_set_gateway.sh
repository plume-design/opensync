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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="nm2/nm2_setup.sh"
create_radio_vif_file="tools/device/create_radio_vif_interface.sh"
if_type_default="vif"
gateway_default="10.10.10.200"
usage()
{
cat << usage_string
nm2/nm2_set_gateway.sh [-h] arguments
Description:
    - Script configures interfaces gateway through Wifi_inet_Config 'gateway' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name) : if_name field in Wifi_Inet_Config : (string)(required)
    \$2 (if_type) : if_type field in Wifi_Inet_Config : (string)(optional) : (default:${if_type_default})
    \$3 (gateway) : gateway field in Wifi_Inet_Config : (string)(optional) : (default:${gateway_default})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
          In case of if_type==vif:
                 Create radio-vif interface (see ${create_radio_vif_file} -h)
                 Run: ./nm2/nm2_set_gateway.sh <IF-NAME> <IF-TYPE> <GATEWAY>
Script usage example:
    ./nm2/nm2_set_gateway.sh eth0 eth 10.10.10.50
    ./nm2/nm2_set_gateway.sh wifi0 vif
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

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_set_gateway.sh" -arg
if_name=$1
if_type=${2:-${if_type_default}}
gateway=${3:-${gateway_default}}

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    reset_inet_entry $if_name || true
    check_restore_management_access || true
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_set_gateway.sh: NM2 test - Testing table Wifi_Inet_Config field gateway"

log "nm2/nm2_set_gateway.sh: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -inet_addr 10.10.10.30 \
    -netmask "255.255.255.0" \
    -if_type "$if_type" &&
        log "nm2/nm2_set_gateway.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_gateway.sh" -ds

log "nm2/nm2_set_gateway.sh: Setting GATEWAY for $if_name to $gateway"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u gateway "$gateway" &&
    log "nm2/nm2_set_gateway.sh: update_ovsdb_entry - Wifi_Inet_Config::gateway is $gateway - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::gateway is not $gateway" -l "nm2/nm2_set_gateway.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is gateway "$gateway" &&
    log "nm2/nm2_set_gateway.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::gateway is $gateway - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::gateway is $gateway" -l "nm2/nm2_set_gateway.sh" -tc

if [ $FUT_SKIP_L2 != 'true' ]; then
    gateway_check_cmd="ip route show default | grep -q $gateway' .* '$if_name"
    log "nm2/nm2_set_gateway.sh: Checking ifconfig for applied gateway for interface $if_name - LEVEL2"
    wait_for_function_response 0 "$gateway_check_cmd" &&
        log "nm2/nm2_set_gateway.sh: LEVEL2 - Gateway $gateway applied to system for interface $if_name - Success" ||
        raise "FAIL: LEVEL2 - Failed to apply gateway $gateway to System for interface $if_name" -l "nm2/nm2_set_gateway.sh" -tc
fi

log "nm2/nm2_set_gateway.sh: Removing GATEWAY $gateway for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u gateway "[\"set\",[]]" -u ip_assign_scheme none &&
    log "nm2/nm2_set_gateway.sh: update_ovsdb_entry - Wifi_Inet_Config::gateway is [\"set\",[]] - Success" ||
    raise "FAIL: update_ovsdb_entry - Wifi_Inet_Config::gateway is not [\"set\",[]]" -l "nm2/nm2_set_gateway.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme none &&
    log "nm2/nm2_set_gateway.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::ip_assign_scheme is 'none' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::ip_assign_scheme is not 'none'" -l "nm2/nm2_set_gateway.sh" -tc

# Wifi_Inet_State::gateway field can either be empty or "0.0.0.0"
wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is gateway "0.0.0.0"
if [ $? -eq 0 ]; then
    log "nm2/nm2_set_gateway.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::gateway is '0.0.0.0' - Success"
else
    log "nm2/nm2_set_gateway.sh: wait_ovsdb_entry - Wifi_Inet_State::gateway is not '0.0.0.0'"
    wait_for_function_response 'empty' "get_ovsdb_entry_value Wifi_Inet_State gateway -w if_name $if_name" &&
        log "nm2/nm2_set_gateway.sh: wait_for_function_response - Wifi_Inet_Config reflected to Wifi_Inet_State::gateway is 'empty' - Success" ||
        raise "FAIL: wait_for_function_response - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::gateway is not 'empty'" -l "nm2/nm2_set_gateway.sh" -tc
fi

if [ $FUT_SKIP_L2 != 'true' ]; then
    log "nm2/nm2_set_gateway.sh: Checking ifconfig for removed gateway - LEVEL2"
    wait_for_function_response 1 "$gateway_check_cmd" &&
        log "nm2/nm2_set_gateway.sh: LEVEL2 - Gateway $gateway removed from system for interface $if_name - Success" ||
        raise "FAIL: LEVEL2 - Failed to remove gateway $gateway from system for interface $if_name" -l "nm2/nm2_set_gateway.sh" -tc
fi

pass
