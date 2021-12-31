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
nm2/nm2_vlan_interface.sh [-h] arguments
Description:
    - Script creates VLAN through Wifi_Inet_Config table and validates its existence in Wifi_Inet_State table and on the
      system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (parent_ifname)  : used as parent_ifname in Wifi_Inet_Config table           : (string)(required)
    \$2 (vlan_id)        : used as vlan_id for virtual interface '100' in 'eth0.100' : (integer)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_vlan_interface.sh <parent_ifname> <vlan_id>
Script usage example:
   ./nm2/nm2_vlan_interface.sh eth0 100
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

check_kconfig_option "CONFIG_OSN_LINUX_VLAN" "y" &&
    log "nm2/nm2_vlan_interface.sh: CONFIG_OSN_LINUX_VLAN==y - VLAN is enabled on this device" ||
    raise "CONFIG_OSN_LINUX_VLAN != y - VLAN is disabled on this device" -l "nm2/nm2_vlan_interface.sh" -s

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_vlan_interface.sh" -arg
# Fill variables with provided arguments or defaults.
parent_ifname=$1
vlan_id=$2

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    delete_inet_interface "$if_name"
    check_restore_management_access || true
    fut_info_dump_line
' EXIT SIGINT SIGTERM

# Construct if_name from parent_ifname and vlan_id (example: eth0.100).
if_name="$parent_ifname.$vlan_id"

log_title "nm2/nm2_vlan_interface.sh: NM2 test - Testing vlan_id"

log "nm2/nm2_vlan_interface.sh: Creating Wifi_Inet_Config entry for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -inet_addr "10.10.10.$vlan_id" \
    -netmask "255.255.255.0" \
    -if_type vlan \
    -vlan_id "$vlan_id" \
    -parent_ifname "$parent_ifname" &&
        log "nm2/nm2_vlan_interface.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create $if_name interface" -l "nm2/nm2_vlan_interface.sh" -ds

log "nm2/nm2_vlan_interface.sh: Check is interface $if_name up - LEVEL2"
wait_for_function_response 0 "get_eth_interface_is_up $if_name" &&
    log "nm2/nm2_vlan_interface.sh: wait_for_function_response - Interface $if_name is UP - Success" ||
    raise "FAIL: wait_for_function_response - Interface $if_name is DOWN" -l "nm2/nm2_vlan_interface.sh" -ds

log "nm2/nm2_vlan_interface.sh: Check if VLAN interface $if_name exists at OS level - LEVEL2"
check_vlan_iface "$parent_ifname" "$vlan_id" &&
    log "nm2/nm2_vlan_interface.sh: VLAN interface $if_name exists at OS level - Success" ||
    raise "FAIL: VLAN interface $if_name does not exist at OS level" -l "nm2/nm2_vlan_interface.sh" -tc

log "nm2/nm2_vlan_interface.sh: Remove VLAN interface"
delete_inet_interface "$if_name" &&
    log "nm2/nm2_vlan_interface.sh: VLAN interface $if_name removed from device - Success" ||
    raise "FAIL: VLAN interface $if_name not removed from device" -l "nm2/nm2_vlan_interface.sh" -tc

pass
