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
    delete_inet_interface "$vif_name"
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1 \$2 \$3

where options are:
    -h  show this help message

where arguments are:
    if_name=\$1 -- used as if_name in Wifi_Inet_Config table - (string)(required)
    vlan_no=\$2 -- used as vlan identifier - example, vlan_no 100 will result in vif_name eth0.100 - (int)(required)
    if_type=\$3 -- used as if_type in Wifi_Inet_Config table - default 'vlan'- (string)(optional)

this script is dependent on following:
    - running NM manager

example of usage:
   /tmp/fut-base/shell/nm2/nm2_rapid_multiple_insert_delete_iface.sh eth0
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 1 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

if_name=$1
vlan_id=$2
if_type=${3:-vlan}
vif_name="$if_name.$vlan_id"
ip_address="10.10.10.$vlan_id"

tc_name="nm2/$(basename "$0")"

create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -netmask "255.255.255.0" \
    -inet_addr "10.10.10.$vlan_id" \
    -if_type "$if_type" \
    -dns "[\"map\",[]]" \
    -vlan_id "$vlan_id" &&
        log "$tc_name: create_vlan_inet_entry - Success" ||
        raise "create_vlan_inet_entry - Failed" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $ip_address was properly applied to $vif_name"

wait_for_function_response 0 "interface_ip_address $vif_name | grep \"$ip_address\"" &&
    log "$tc_name: Ip address $ip_address applied to ifconfig for interface $vif_name" ||
    raise "Failed to apply settings to ifconfig (ENABLED) - vif_if_name $vif_name" -l "$tc_name" -tc

delete_inet_interface "$vif_name" &&
    log "$tc_name: Interface $vif_name deleted" ||
    raise "Fail to delete created interface $vif_name" -l "$tc_name" -tc

log "$tc_name: LEVEL 2 - Check if IP ADDRESS: $ip_address was properly removed from ifconfig $vif_name"
wait_for_function_response 1 "interface_ip_address $if_name | grep -q \"$ip_address\"" &&
    log "$tc_name: Settings removed from ifconfig (DISABLE #2) - interface $if_name" ||
    raise "Failed to remove settings from ifconfig (DISABLE #2) - interface $if_name" -l "$tc_name" -tc

pass
