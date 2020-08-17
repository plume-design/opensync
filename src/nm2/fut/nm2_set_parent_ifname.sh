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


# TEST DESCRIPTION
# Try to set parent ifname to wireless interface.
#
# TEST PROCEDURE
# - Configure parent interface
# - Wake UP parent interface
# - Configure wireless interface with parent iface name
#
# EXPECTED RESULTS
# Test is passed:
# - if wireless interface is properly configured AND
# - parent iface is UP AND
# - VLAN config is valid
# Test fails:
# - parent iface is NOT UP OR
# - VLAN config is not valid

# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    delete_inet_interface "$if_name"
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2

where options are:
    -h  show this help message

where arguments are:
    parent_ifname=\$1 -- used as parent_ifname in Wifi_Inet_Config table - (string)(required)
    virtual_interface_id=\$2 -- used as vlan_id for virtual interface '100' in 'eth0.100'- (integer)(required)

this script is dependent on following:
    - running NM manager
    - running WM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_set_parent_ifname.sh eth0 100
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Provide at least 2 argument(s).
if [ $# -lt 2 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

# Fill variables with provided arguments or defaults.
parent_ifname=$1
vlan_id=$2
# Construct if_name from parent_ifname and vlan_id
if_name="$parent_ifname.$vlan_id"

tc_name="nm2/$(basename "$0")"

log "$tc_name: Creating Wifi_Inet_Config entries for $if_name (enabled=true, network=true, ip_assign_scheme=static)"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -inet_addr "10.10.10.$vlan_id" \
    -netmask "255.255.255.0" \
    -if_type vlan \
    -parent_ifname "$parent_ifname" \
    -dns "[\"map\",[]]" \
    -vlan_id "$vlan_id" &&
        log "$tc_name: create_vlan_inet_entry - Success" ||
        raise "create_vlan_inet_entry - Failed" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check is interface up"
wait_for_function_response 0 "interface_is_up $if_name" &&
    log "$tc_name: Interface is UP - interface $if_name" ||
    raise "Interface is NOT UP - interface $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check vlan config - VLAN exists"
wait_for_function_response 0 "grep -q \"$if_name\" /proc/net/vlan/$if_name" &&
    log "$tc_name: VLAN configuration IS VALID at OS level - interface $if_name" ||
    raise "VLAN configuration NOT VALID at OS level - interface $if_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check vlan config - PARENT ifname $parent_ifname"
wait_for_function_response 0 "grep -q \"Device: $parent_ifname\" /proc/net/vlan/$if_name" &&
    log "$tc_name: Parent device IS VALID at OS level - parent interface $parent_ifname" ||
    raise "Parent device NOT VALID at OS level - parent interface $parent_ifname" -l "$tc_name" -tc

pass
