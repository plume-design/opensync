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

manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
nm2/nm2_set_nat.sh [-h] arguments
Description:
    - Script configures interfaces NAT through Wifi_inet_Config 'NAT' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name) : used as if_name in Wifi_Inet_Config table : (string)(required)
    \$2 (if_type) : used as if_type in Wifi_Inet_Config table : (string)(required)
    \$3 (NAT)     : used as NAT in Wifi_Inet_Config table     : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_set_nat.sh
Script usage example:
   ./nm2/nm2_set_nat.sh eth0 eth true
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
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_set_nat.sh" -arg
# Fill variables with provided arguments or defaults.
if_name=$1
if_type=$2
NAT=$3

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    reset_inet_entry $if_name || true
    check_restore_management_access || true
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_set_nat.sh: NM2 test - Testing table Wifi_Inet_Config field NAT - $NAT"

log "nm2/nm2_set_nat.sh: Creating Wifi_Inet_Config entry for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
if [ "${if_type}" == "vif" ]; then
    create_inet_entry \
        -if_name "$if_name" \
        -enabled true \
        -network true \
        -ip_assign_scheme static \
        -if_type "$if_type" \
        -inet_addr "10.10.10.30" \
        -netmask "255.255.255.0" &&
            log "nm2/nm2_set_nat.sh: Interface $if_name created - Success" ||
            raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_nat.sh" -ds
else
    create_inet_entry \
        -if_name "$if_name" \
        -enabled true \
        -network true \
        -ip_assign_scheme static \
        -if_type "$if_type" &&
            log "nm2/nm2_set_nat.sh: Interface $if_name created - Success" ||
            raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_set_nat.sh" -ds
fi

log "nm2/nm2_set_nat.sh: Setting NAT for interface $if_name to $NAT"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u NAT "$NAT" &&
    log "nm2/nm2_set_nat.sh: update_ovsdb_entry - Wifi_Inet_Config::NAT is $NAT - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not $NAT" -l "nm2/nm2_set_nat.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is NAT "$NAT" &&
    log "nm2/nm2_set_nat.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is $NAT - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not $NAT" -l "nm2/nm2_set_nat.sh" -tc

log "nm2/nm2_set_nat.sh: Checking state of NAT for $if_name (must be ON) - LEVEL2"
wait_for_function_response 0 "check_interface_nat_enabled $if_name" &&
    log "nm2/nm2_set_nat.sh: LEVEL2 - NAT applied to iptables for interface $if_name - Success" ||
    raise "FAIL: LEVEL2 - Failed to apply NAT to iptables for interface $if_name" -l "nm2/nm2_set_nat.sh" -tc

log "nm2/nm2_set_nat.sh: Disabling NAT for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u NAT false &&
    log "nm2/nm2_set_nat.sh: update_ovsdb_entry - Wifi_Inet_Config table::NAT is 'false' - Success" ||
    raise "FAIL: update_ovsdb_entry - Failed to update Wifi_Inet_Config::NAT is not 'false'" -l "nm2/nm2_set_nat.sh" -oe

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is NAT false &&
    log "nm2/nm2_set_nat.sh: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State::NAT is 'false' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State::NAT is not 'false'" -l "nm2/nm2_set_nat.sh" -tc

log "nm2/nm2_set_nat.sh: Checking state of NAT for $if_name (must be OFF) - LEVEL2"
wait_for_function_response 1 "check_interface_nat_enabled $if_name" &&
    log "nm2/nm2_set_nat.sh: LEVEL2 - NAT removed from iptables for interface $if_name - Success" ||
    raise "FAIL: LEVEL2 - Failed to remove NAT from iptables for interface $if_name" -l "nm2/nm2_set_nat.sh" -tc

pass
