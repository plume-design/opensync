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


# Test case name
tc_name="nm2/nm2_verify_native_bridge.sh"
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
nm2/nm2_verify_native_bridge.sh [-h] arguments
Description:
    - The script creates the interface and then creates a Linux Native bridge.
      The interface is added to the bridge and is validated using brctl command. The
      hairpin is configured on the interface and validated.
Arguments:
    -h  show this help message
    \$1 (bridge)  : Bridge name : (string)(required)
    \$2 (if_name) : Interface name : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_verify_native_bridge.sh br-test br-home.dhcp
Script usage example:
   ./nm2/nm2_verify_native_bridge.sh br-test br-home.dhcp
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

linux_native_bridge_enabled ||
    raise "linux_native_bridge_enabled is false - The test case is only applicable only when Native Linux Bridge is enabled" -l "$tc_name" -s

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "$tc_name" -arg
bridge=$1
if_name=$2
NM2_DELAY="2"

trap '
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State Bridge Port Interface
    reset_inet_entry $if_name || true
    reset_inet_entry $bridge || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "$tc_name: - Configuring and validating Native Linux Bridge - $bridge $if_name"

# create inet entry for the interface
log "$tc_name: Creating Wifi_Inet_Config entries for $if_name (enabled=true, network=true, ip_assign_scheme=none)"
create_inet_entry \
    -if_name "${if_name}" \
    -if_type "tap" \
    -NAT false \
    -ip_assign_scheme "none" \
    -dhcp_sniff "false" \
    -network true \
    -enabled true &&
        log "$tc_name: Interface ${if_name} created - Success" ||
        raise "FAIL: Failed to create interface ${if_name}" -l "$tc_name" -ds

# create inet entry for the bridge
log "$tc_name: Creating Wifi_Inet_Config entries for $bridge (enabled=true, network=false, ip_assign_scheme=none)"
create_inet_entry \
    -if_name "${bridge}" \
    -if_type "bridge" \
    -ip_assign_scheme "none" \
    -network false \
    -enabled true &&
        log "$tc_name: Interface ${bridge} created - Success" ||
        raise "FAIL: Failed to create interface ${bridge}" -l "$tc_name" -ds

# create the native bridge by updating Bridge, Port and Interface table.
log "$tc_name: Creating bridge $bridge"
wait_for_function_response 0 "ovs_create_bridge $bridge" &&
    log "$tc_name: Creating bridge '$bridge' - Success" ||
    raise "FAIL: Creating bridge $bridge" -l "$tc_name" -tc

# Check if the bridge is configured in the systema (LEVEL2)
sleep $NM2_DELAY
brctl show | grep -q "$bridge"
if [ $? = 0 ]; then
    log "$tc_name: - LEVEL2 - bridge '$bridge' created - Success"
else
    raise "FAIL: - LEVEL2 - bridge '$bridge' not created" -l "$tc_name" -tc
fi

# add interface to bridge
add_bridge_port "$bridge" "$if_name"
# Check if interface is added to the bridge (LEVEL2)
log "$tc_name: Checking if port '$if_name' is added to bridge '$bridge'"
wait_for_function_response 0 "check_if_port_in_bridge $if_name $bridge" &&
    log "$tc_name: check_if_port_in_bridge - LEVEL2 - port '$if_name' added to '$bridge' - Success" ||
    raise "FAIL: check_if_port_in_bridge - LEVEL2 - port '$if_name' not added to $bridge" -l "$tc_name" -tc

# enable hairpin configuration on the interface
log "$tc_name: Validating enabling hairpin configuration on port '$if_name'"
wait_for_function_response 0 "nb_configure_hairpin $if_name "on"" &&
    log "$tc_name: enabled hairpin mode on port '$if_name' - Success" ||
    raise "FAIL: enabled hairpin mode on port '$if_name' " -l "$tc_name" -tc

sleep $NM2_DELAY
# validate if hairpin configuration is enabled on the interface
hairpin_config=$(brctl showstp "$bridge" | grep -A 7 "$if_name" | grep "hairpin" | awk '{print $3}' | wc -l)
log "$tc_name: returned hairpin mode is '$hairpin' for bridge '$bridge'"
if [ "$hairpin_config" -eq 1 ]; then
    log "$tc_name: enabling hairpin mode on port $if_name - Success"
else
    log "$tc_name: enabling hairpin mode on port $if_name - Fail"
    raise "FAIL: LEVEL2 - hairpin is not enabled on $if_name " -l "$tc_name" -tc
fi

# diable hairpin configuration on the interface
log "$tc_name: Validating disabling hairpin configuration on port '$if_name'"
wait_for_function_response 0 "nb_configure_hairpin $if_name "off"" &&
    log "$tc_name: disable hairpin mode on port $if_name - Success" ||
    raise "FAIL: disable hairpin mode on port '$if_name' " -l "$tc_name" -tc

sleep $NM2_DELAY
# validate if hairpin configuration is disabled on the interface
hairpin_config=$(brctl showstp "$bridge" | grep -A 7 "$if_name"| grep "hairpin" | awk '{print $3}' | wc -l)
log "$tc_name: returned hairpin mode is $hairpin for bridge '$bridge'"
if [ "$hairpin_config" -eq 0 ]; then
    log "$tc_name: disabling hairpin mode on port $if_name - Success"
else
    log "$tc_name: disabling hairpin mode on port $if_name - Fail"
    raise "FAIL: nb_configure_hairpin - LEVEL2 - hairpin is not disabled on $if_name " -l "$tc_name" -tc
fi

# remove the interface from the bridge
remove_port_from_bridge "$bridge" "$if_name"
sleep $NM2_DELAY

# validate is the interface is removed from the bridge
check_if_port_in_bridge "$if_name" "$bridge"
# returns 0 if port is found in the bridge
if [ "$?" -eq 0 ]; then
    log "$tc_name: Interface $if_name not removed from the bridge $bridge - Fail"
    raise "FAIL: Interface $if_name exists on system, but should NOT" -l "$tc_name" -tc
fi

# clean up Bridge, Interface and Port tables
ovs_delete_bridge "$bridge"
sleep $NM2_DELAY
# nb_check_if_bridge_present "$bridge"
brctl show | grep -q "$bridge"
if [ "$?" -eq 0 ]; then
    log "$tc_name: Bridge $bridge not removed from the system - Fail"
    raise "FAIL: Interface $bridge exists on system, but should NOT" -l "$tc_name" -tc
fi

log "$tc_name: Remove interface $if_name"
delete_inet_interface "$if_name" &&
    log "$tc_name: interface $if_name removed from device - Success" ||
    raise "FAIL: interface $if_name not removed from device" -l "$tc_name" -tc

log "$tc_name: Remove interface $bridge"
delete_inet_interface "$bridge" &&
    log "$tc_name: interface $bridge removed from device - Success" ||
    raise "FAIL: interface $bridge not removed from device" -l "$tc_name" -tc

# Check if manager survived.
manager_pid_file="${OPENSYNC_ROOTDIR}/bin/nm"
wait_for_function_response 0 "check_manager_alive $manager_pid_file" &&
    log "$tc_name: NETWORK MANAGER is running - Success" ||
    raise "FAIL: NETWORK MANAGER not running/crashed" -l "$tc_name" -tc

pass
