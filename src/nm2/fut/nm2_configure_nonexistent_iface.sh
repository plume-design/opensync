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
nm2/nm2_configure_nonexistent_iface.sh [-h] arguments
Description:
    - Script creates undefined interface through Wifi_Inet_Config table
      Script fails if:
        - Undefined interface is not present in Wifi_Inet_State
        - Undefined interface is present on system
        - NM crashes during creation of undefined interface
Arguments:
    -h  show this help message
    \$1 (if_name)   : used as if_name in Wifi_Inet_Config table   : (string)(required)
    \$2 (if_type)   : used as if_type in Wifi_Inet_Config table   : (string)(required)
    \$3 (inet_addr) : used as inet_addr in Wifi_Inet_Config table : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_configure_nonexistent_iface.sh <IF-NAME> <IF-TYPE> <INET-ADDR>
Script usage example:
   ./nm2/nm2_configure_nonexistent_iface.sh test1 eth 10.10.10.15
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

# Fill variables with provided arguments or defaults.
NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_configure_nonexistent_iface.sh" -arg
if_name=$1
if_type=$2
ip_address=$3

# Execute on EXIT signal.
trap '
fut_info_dump_line
print_tables Wifi_Inet_Config Wifi_Inet_State
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_configure_nonexistent_iface.sh: NM2 test - Configure non-existent interface"

log "nm2/nm2_configure_nonexistent_iface.sh: Creating NONEXISTENT interface $if_name of type $if_type"
insert_ovsdb_entry Wifi_Inet_Config \
    -i if_name "$if_name" \
    -i if_type "$if_type" \
    -i enabled true \
    -i network true \
    -i NAT false \
    -i inet_addr "$ip_address" \
    -i netmask "255.255.255.0" \
    -i broadcast "10.10.10.255" \
    -i ip_assign_scheme static \
    -i parent_ifname eth1 \
    -i mtu 1500 &&
        log "nm2/nm2_configure_nonexistent_iface.sh: NONEXISTENT interface $if_name created - Success" ||
        raise "FAIL: Failed to insert_ovsdb_entry for $if_name" -l "nm2/nm2_configure_nonexistent_iface.sh" -oe

log "nm2/nm2_configure_nonexistent_iface.sh: Checking if NONEXISTENT interface $if_name was created"
# Interface must be present in Wifi_Inet_State table...
wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is if_type "$if_type" &&
    log "nm2/nm2_configure_nonexistent_iface.sh: NONEXISTENT interface present in Wifi_Inet_State::if_name = $if_name - Success" ||
    raise "FAIL: Wifi_Inet_State::if_name = $if_name not present" -l "nm2/nm2_configure_nonexistent_iface.sh" -ow

# ...but not on system.
wait_for_function_response 1 "check_interface_exists $if_name" &&
    log "nm2/nm2_configure_nonexistent_iface.sh: Interface $if_name of type $if_type does not exist on system - Success" ||
    raise "FAIL: Interface $if_name of type $if_type exists on system, but should NOT" -l "nm2/nm2_configure_nonexistent_iface.sh" -tc

# Check if manager survived.
manager_pid_file="${OPENSYNC_ROOTDIR}/bin/nm"
wait_for_function_response 0 "check_manager_alive $manager_pid_file" &&
    log "nm2/nm2_configure_nonexistent_iface.sh: NETWORK MANAGER is running - Success" ||
    raise "FAIL: NETWORK MANAGER not running/crashed" -l "nm2/nm2_configure_nonexistent_iface.sh" -tc

pass
