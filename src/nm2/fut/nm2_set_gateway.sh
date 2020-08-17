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
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
' EXIT SIGINT SIGTERM

tc_name="nm2/$(basename "$0")"
usage()
{
cat << EOF
${tc_name} [-h] if_name [if_type] [gateway]
Options:
    -h  show this help message
Arguments:
    if_name=$1 -- if_name field in Wifi_Inet_Config - (string)(required)
    if_type=$2 -- if_type field in Wifi_Inet_Config - (string)(optional, default: vif)
    gateway=$3 -- gateway field in Wifi_Inet_Config - (string)(optional, default: 10.10.10.200)
Dependencies:
    NM manager, WM manager
Example:
    ${tc_name} eth0 eth 10.10.10.50
    ${tc_name} wifi0 vif
EOF
exit 1
}

while getopts h option; do
    case "$option" in
        h)
            usage
            ;;
    esac
done

NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1
if_type=${2:-"vif"}
gateway=${3:-"10.10.10.200"}

log_title "${tc_name}: Testing table Wifi_Inet_Config field gateway"

log "${tc_name}: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -ip_assign_scheme static \
    -inet_addr 10.10.10.30 \
    -netmask "255.255.255.0" \
    -if_type "$if_type" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

log "$tc_name: Setting GATEWAY for $if_name to $gateway"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u gateway "$gateway" &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - gateway $gateway" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - gateway $gateway" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is gateway "$gateway" &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - gateway $gateway" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - gateway $gateway" -l "$tc_name" -tc

gateway_check_cmd="ip route show default | grep -q $gateway' .* '$if_name"
log "$tc_name: LEVEL 2: Checking ifconfig for applied gateway - interface $if_name"
wait_for_function_response 0 "$gateway_check_cmd" &&
    log "$tc_name: LEVEL 2: Gateway $gateway applied to OS - interface $if_name" ||
    raise "LEVEL 2: Failed to apply gateway $gateway to OS - interface $if_name" -l "$tc_name" -tc

log "$tc_name: Removing GATEWAY $gateway for $if_name"
update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u gateway "[\"set\",[]]" -u ip_assign_scheme none &&
    log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - gateway [\"set\",[]]" ||
    raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - gateway [\"set\",[]]" -l "$tc_name" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme none &&
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme none" ||
    raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme none" -l "$tc_name" -tc

# gateway field can either be empty or "0.0.0.0"
wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is gateway "0.0.0.0"
if [ $? -eq 0 ]; then
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - gateway 0.0.0.0"
else
    log "$tc_name: wait_ovsdb_entry - Wifi_Inet_State::gateway is not 0.0.0.0"
    wait_for_function_response 'empty' "get_ovsdb_entry_value Wifi_Inet_State gateway -w if_name $if_name" &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - gateway empty" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - gateway empty" -l "$tc_name" -tc
fi

log "$tc_name: LEVEL 2: Checking ifconfig for removed gateway"
wait_for_function_response 1 "$gateway_check_cmd" &&
    log "$tc_name: LEVEL 2: Gateway $gateway removed from OS - interface $if_name" ||
    raise "LEVEL 2: Failed to remove gateway $gateway from OS - interface $if_name" -l "$tc_name" -tc

pass
