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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="nm2/$(basename "$0")"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script creates VLAN through Wifi_Inet_Config table and validates its existence in Wifi_Inet_State table and on the
      system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (parent_ifname)        : used as parent_ifname in Wifi_Inet_Config table           : (string)(required)
    \$2 (vlan_id) : used as vlan_id for virtual interface '100' in 'eth0.100' : (integer)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <parent_ifname> <vlan_id>
Script usage example:
   ./${tc_name} eth0 100
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

check_kconfig_option "CONFIG_OSN_LINUX_VLAN" "y" &&
    log "${tc_name}: CONFIG_OSN_LINUX_VLAN==y - VLAN is enabled on this device" ||
    raise "CONFIG_OSN_LINUX_VLAN != y - VLAN is disabled on this device" -l "${tc_name}" -s

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap '
    delete_inet_interface "$if_name"
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

# Fill variables with provided arguments or defaults.
parent_ifname=$1
vlan_id=$2
# Construct if_name from parent_ifname and vlan_id (example: eth0.100).
if_name="$parent_ifname.$vlan_id"

log_title "$tc_name: NM2 test - Testing vlan_id"

log "$tc_name: Creating Wifi_Inet_Config entry for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
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
        log "$tc_name: create_vlan_inet_entry - Success" ||
        raise "create_vlan_inet_entry - Failed" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check is interface up - $if_name"
wait_for_function_response 0 "interface_is_up $if_name" &&
    log "$tc_name: wait_for_function_response - Interface is UP - $if_name" ||
    raise "wait_for_function_response - Interface is DOWN - $if_name" -l "$tc_name" -tc

vlan_pid="/proc/net/vlan/${if_name}"
log "$tc_name: LEVEL 2 - Check ${vlan_pid} existence"
wait_for_function_response 0 "[ -f ${vlan_pid} ]" &&
    log "$tc_name: PID ${vlan_pid} is runinng" ||
    raise "PID ${vlan_pid} is NOT running" -l "$tc_name" -tc

log "$tc_name: Output PID ${vlan_pid} info:"
cat "${vlan_pid}"

log "$tc_name: LEVEL 2 - Validate PID VLAN config - vlan_id == ${vlan_id}"
wait_for_function_response 0 "cat "${vlan_pid}" | grep 'VID: ${vlan_id}'" &&
    log "$tc_name: VID is set to 100" ||
    raise "VID is not set" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check parent device for VLAN"
wait_for_function_response 0 "cat "${vlan_pid}" | grep 'Device: ${parent_ifname}'" &&
    log "$tc_name: Device is set to ${parent_ifname}" ||
    raise "Device is not set to ${parent_ifname}" -l "$tc_name" -tc

log "$tc_name: Remove VLAN interface"
delete_inet_interface "$if_name" &&
    log "$tc_name: VLAN interface removed from device" ||
    raise "VLAN interface not removed from device" -l "$tc_name" -tc

pass
