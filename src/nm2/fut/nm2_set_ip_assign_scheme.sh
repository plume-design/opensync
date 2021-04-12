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
inet_addr_default="10.10.10.30"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures interfaces ip_assign_scheme through Wifi_inet_Config 'ip_assign_scheme' field and checks if it is propagated
      into Wifi_Inet_State table and to the system, fails otherwise
Arguments:
    -h  show this help message
    \$1 (if_name)          : used as if_name in Wifi_Inet_Config table                : (string)(required)
    \$2 (if_type)          : used as if_type in Wifi_Inet_Config table                : (string)(required)
    \$3 (ip_assign_scheme) : used as ip_assign_scheme in Wifi_Inet_Config table       : (string)(required)
    \$4 (inet_addr)        : used as inet_addr column in Wifi_Inet_Config/State table : (string)(optional) : (default:${inet_addr_default})
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME> <IF-TYPE> <IP-ASSIGN-SCHEME> <INET-ADDR>
Script usage example:
   ./${tc_name} wifi0 vif static
   ./${tc_name} wifi0 vif dhcp
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

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1
if_type=$2
ip_assign_scheme=$3
inet_addr=${4:-${inet_addr_default}}

trap '
    reset_inet_entry $if_name || true
    run_setup_if_crashed nm || true
    check_restore_management_access || true
    fut_info_dump_line
    print_tables Wifi_Inet_Config Wifi_Inet_State
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "$tc_name: NM2 test - Testing table Wifi_Inet_Config field ip_assign_scheme"

log "$tc_name: Remove running clients first"
rm "/var/run/udhcpc-$if_name.pid" || log "Nothing to remove"
rm "/var/run/udhcpc_$if_name.opts" || log "Nothing to remove"

log "$tc_name: Creating Wifi_Inet_Config entries for $if_name"
create_inet_entry \
    -if_name "$if_name" \
    -enabled true \
    -network true \
    -netmask "255.255.255.0" \
    -inet_addr "$inet_addr" \
    -ip_assign_scheme static \
    -if_type "$if_type" &&
        log "$tc_name: Interface successfully created" ||
        raise "Failed to create interface" -l "$tc_name" -tc

if [ "$ip_assign_scheme" = "dhcp" ]; then
    log "$tc_name: Setting dhcp for $if_name to dhcp"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme dhcp &&
        log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=dhcp" ||
        raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=dhcp" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme dhcp &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=dhcp" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=dhcp" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is alive"
    wait_for_function_response 0 "check_pid_file alive \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process ACTIVE - interface $if_name" ||
        raise "DHCP client process NOT ACTIVE - interface $if_name" -l "$tc_name" -tc

    log "$tc_name: Setting dhcp for $if_name to none"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" -u ip_assign_scheme none &&
        log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=none" ||
        raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=none" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" -is ip_assign_scheme none &&
        log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State" ||
        raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is dead"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process NOT ACTIVE" ||
        raise "DHCP client process ACTIVE" -l "$tc_name" -tc

elif [ "$ip_assign_scheme" = "static" ]; then
    log "$tc_name: Setting ip_assign_scheme for $if_name to static"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u ip_assign_scheme static \
        -u inet_addr "$inet_addr" &&
            log "$tc_name: update_ovsdb_entry - Wifi_Inet_Config table updated - ip_assign_scheme=static" ||
            raise "update_ovsdb_entry - Failed to update Wifi_Inet_Config - ip_assign_scheme=static" -l "$tc_name" -tc

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
        -is ip_assign_scheme static \
        -is inet_addr "$inet_addr" &&
            log "$tc_name: wait_ovsdb_entry - Wifi_Inet_Config reflected to Wifi_Inet_State - ip_assign_scheme=static" ||
            raise "wait_ovsdb_entry - Failed to reflect Wifi_Inet_Config to Wifi_Inet_State - ip_assign_scheme=static" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2: Checking if settings are applied to ifconfig"
    wait_for_function_response 0 "get_interface_ip_address_from_system $if_name | grep -q \"$inet_addr\"" &&
        log "$tc_name: Settings applied to ifconfig - interface $if_name" ||
        raise "Failed to apply settings to ifconfig - interface $if_name" -l "$tc_name" -tc

    log "$tc_name: LEVEL 2 - Checking if DHCP client is DEAD"
    wait_for_function_response 0 "check_pid_file dead \"/var/run/udhcpc-$if_name.pid\"" &&
        log "$tc_name: DHCP client process is DEAD - interface $if_name" ||
        raise "DHCP client process is NOT DEAD - interface $if_name" -l "$tc_name" -tc
else
    raise "Wrong IP_ASSIGN_SCHEME parameter - $ip_assign_scheme" -l "$tc_name" -tc
fi

pass
