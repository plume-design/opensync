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


# Include basic environment config
export FUT_NM2_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/nm2_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Network Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for NM tests.  If called with parameters it waits
#   for radio interfaces in Wifi_Radio_State table.
#   Calling it without radio interface names, it skips the step checking the interfaces.
#   Raises exception on fail in any of its steps.
# INPUT PARAMETER(S):
#   $@  Interfaces (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   nm_setup_test_environment <interface names>
###############################################################################
nm_setup_test_environment()
{
    log -deb "nm2_lib:nm_setup_test_environment - Running NM2 setup"

    device_init &&
        log -deb "nm2_lib:nm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init Could not initialize device" -l "nm2_lib:nm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "nm2_lib:nm_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "nm2_lib:nm_setup_test_environment" -ds

    start_wireless_driver &&
        log -deb "nm2_lib:nm_setup_test_environment - Wireless driver started - Success" ||
        raise "FAIL: start_wireless_driver - Could not start wireless driver" -l "nm2_lib:nm_setup_test_environment" -ds

    restart_managers
    log -deb "fsm_lib:fsm_setup_test_environment - Executed restart_managers, exit code: $?"

    # Check if radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" &&
            log -deb "nm2_lib:nm_setup_test_environment - Wifi_Radio_State::if_name '$if_name' present - Success" ||
            raise "FAIL: Wifi_Radio_State::if_name for '$if_name' does not exist" -l "nm2_lib:nm_setup_test_environment" -ds
    done

    empty_ovsdb_table AW_Debug  &&
        log -deb "nm2_lib:nm_setup_test_environment - AW_Debug table emptied - Success" ||
        raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty table" -l "nm2_lib:nm_setup_test_environment" -ds

    set_manager_log WM TRACE &&
        log -deb "nm2_lib:nm_setup_test_environment - Manager log for WM set to TRACE - Success" ||
        raise "FAIL: set_manager_log WM TRACE - Could not set WM manager log severity" -l "nm2_lib:nm_setup_test_environment" -ds

    set_manager_log NM TRACE &&
        log -deb "nm2_lib:nm_setup_test_environment - Manager log for NM set to TRACE - Success" ||
        raise "FAIL: set_manager_log NM TRACE - Could not set NM manager log severity" -l "nm2_lib:nm_setup_test_environment" -ds

    log -deb "nm2_lib:nm_setup_test_environment - NM2 setup - end"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates entry to Wifi_Inet_Config table.
#   It then waits for config to reflect in Wifi_Inet_State table.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   See fields in table Wifi_Inet_Config.
#   Mandatory parameter: if_name (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   create_inet_entry -if_name "eth1" -if_type "eth" -enabled "true"
###############################################################################
create_inet_entry()
{
    args=""
    add_cfg_args=""
    replace="func_arg"

    # Parse parameters
    while [ -n "$1" ]; do
        option=${1}
        shift
        case "${option}" in
            -if_name)
                if_name="${1}"
                args="${args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -enabled | \
            -network | \
            -if_type | \
            -inet_addr | \
            -bridge | \
            -dns | \
            -gateway | \
            -broadcast | \
            -ip_assign_scheme | \
            -mtu | \
            -NAT | \
            -upnp_mode | \
            -dhcpd | \
            -vlan_id | \
            -parent_ifname | \
            -gre_ifname | \
            -gre_remote_inet_addr | \
            -gre_local_inet_addr)
                args="${args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -dhcp_sniff)
                add_cfg_args="${add_cfg_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -no_flood)
                add_cfg_args="${add_cfg_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -collect_stats)
                add_cfg_args="${add_cfg_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -broadcast_n)
                broadcast_n="${1}"
                shift
                ;;
            -inet_addr_n)
                inet_addr_n="${1}"
                shift
                ;;
            -subnet)
                subnet="${1}"
                shift
                ;;
            -netmask)
                netmask="${1}"
                args="${args} ${replace} ${option#?} ${1}"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "nm2_lib:create_inet_entry" -arg
                ;;
        esac
    done

    # Make sure if_name parameter is set
    [ -z "${if_name}" ] &&
        raise "FAIL: Interface name argument empty" -l "nm2_lib:create_inet_entry" -arg

    if [ -n "${broadcast_n}" ] && [ -n "${inet_addr_n}" ] && [ -n "${netmask}" ] && [ -n "${subnet}" ]; then
        log -deb "nm2_lib:create_inet_entry - Setting additional parameters from partial info: broadcast, dhcpd_start, dhcpd_stop, inet_addr"
        broadcast="${subnet}.${broadcast_n}"
        dhcpd_start="${subnet}.$((inet_addr_n + 1))"
        dhcpd_stop="${subnet}.$((broadcast_n - 1))"
        inet_addr="${subnet}.${inet_addr_n}"
        dhcpd='["map",[["dhcp_option","26,1600"],["force","false"],["lease_time","12h"],["start","'${dhcpd_start}'"],["stop","'${dhcpd_stop}'"]]]'
        args="${args} ${replace} broadcast ${broadcast}"
        args="${args} ${replace} inet_addr ${inet_addr}"
        args="${args} ${replace} dhcpd ${dhcpd}"
    fi

    # Check if entry for given interface already exists, and if exists perform update action instead of insert
    check_ovsdb_entry Wifi_Inet_Config -w if_name "${if_name}"
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:create_inet_entry - Updating existing interface in Wifi_Inet_Config"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        log -deb "nm2_lib:create_inet_entry - Creating interface in Wifi_Inet_Config"
        function_to_call="insert_ovsdb_entry"
        function_arg="-i"
    fi

    # Perform action insert/update
    func_params=${args//$replace/$function_arg}
    func_params_add=${add_cfg_args//$replace/$function_arg}
    # shellcheck disable=SC2086
    $function_to_call Wifi_Inet_Config -w if_name "$if_name" $func_params $func_params_add &&
        log -deb "nm2_lib:create_inet_entry - $function_to_call Wifi_Inet_Config -w if_name $if_name $func_params $func_params_add - Success" ||
        raise "FAIL: $function_to_call Wifi_Inet_Config -w if_name $if_name $func_params $func_params_add" -l "nm2_lib:create_inet_entry" -oe

    # Validate action insert/update
    func_params=${args//$replace/-is}
    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" $func_params &&
        log -deb "nm2_lib:create_inet_entry - wait_ovsdb_entry Wifi_Inet_State -w if_name $if_name $func_params - Success" ||
        raise "FAIL: wait_ovsdb_entry Wifi_Inet_State -w if_name $if_name $func_params" -l "nm2_lib:create_inet_entry" -ow

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function sets entry values for interface in Wifi_Inet_Config
#   table to default.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   reset_inet_entry eth0
#   reset_inet_entry wifi0
###############################################################################
reset_inet_entry()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:reset_inet_entry requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    log -deb "nm2_lib:reset_inet_entry - Setting Wifi_Inet_Config for $if_name to default values"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u NAT "false" \
        -u broadcast "[\"set\",[]]" \
        -u dhcpd "[\"map\",[]]" \
        -u dns "[\"map\",[]]" \
        -u enabled "true" \
        -u gateway "[\"set\",[]]" \
        -u gre_ifname "[\"set\",[]]" \
        -u gre_local_inet_addr "[\"set\",[]]" \
        -u gre_remote_inet_addr "[\"set\",[]]" \
        -u inet_addr "[\"set\",[]]" \
        -u ip_assign_scheme "none" \
        -u mtu "[\"set\",[]]" \
        -u netmask "[\"set\",[]]" \
        -u network "true" \
        -u parent_ifname "[\"set\",[]]" \
        -u softwds_mac_addr "[\"set\",[]]" \
        -u softwds_wrap "[\"set\",[]]" \
        -u upnp_mode "[\"set\",[]]" \
        -u vlan_id "[\"set\",[]]" &&
            log -deb "nm2_lib:reset_inet_entry - Wifi_Inet_Config updated - Success" ||
            raise "FAIL: Could not update Wifi_Inet_Config" -l "nm2_lib:reset_inet_entry" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function deletes entry values in Wifi_Inet_Config table.
#   It then waits for config to reflect in Wifi_Inet_State table.
#   It checks if configuration is reflected to system.
#   Raises exception if interface is not removed and then forces deletion.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   delete_inet_interface eth0
###############################################################################
delete_inet_interface()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:delete_inet_interface requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    log -deb "nm2_lib:delete_inet_interface - Removing interface '$if_name'"

    remove_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" ||
        raise "FAIL: Could not remove Wifi_Inet_Config::if_name" -l "nm2_lib:delete_inet_interface" -oe

    wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "$if_name" ||
        raise "FAIL: Could not remove Wifi_Inet_State::if_name" -l "nm2_lib:delete_inet_interface" -ow

    wait_for_function_response 1 "ip link show $if_name" &&
        log -deb "nm2_lib:delete_inet_interface - LEVEL2: Interface $if_name removed - Success" ||
        force_purge_interface_raise "$if_name"

    log -deb "nm2_lib:delete_inet_interface - Interface '$if_name' deleted from ovsdb and OS - LEVEL2"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function deletes interface from system by force.
#   Raises exception.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   See DESCRIPTION
# USAGE EXAMPLE(S):
#   force_purge_interface_raise eth0
###############################################################################
force_purge_interface_raise()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:force_purge_interface_raise requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    log -deb "nm2_lib:force_purge_interface_raise - Interface force removal"
    ip link delete "$if_name" || true

    wait_for_function_response 1 "ip link show $if_name" &&
        raise "FAIL: Interface '$if_name' removed forcefully" -l "nm2_lib:force_purge_interface_raise" -tc ||
        raise "FAIL: Interface still present, could not delete interface '$if_name'" -l "nm2_lib:force_purge_interface_raise" -tc
}

###############################################################################
# DESCRIPTION:
#   Function enables or disables DHCP server on interface.
#   It waits for Wifi_Inet_Config to reflect in Wifi_Inet_State.
#   Raises exception if DHCP server is not configured.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
#   $2  IP address start pool (string, optional)
#   $3  IP address end pool (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   configure_dhcp_server_on_interface eth1 10.10.10.20 10.10.10.50
#   configure_dhcp_server_on_interface eth1
###############################################################################
configure_dhcp_server_on_interface()
{
    NARGS_MIN=1
    NARGS_MAX=3
    [ $# -eq ${NARGS_MIN} ] || [ $# -eq ${NARGS_MAX} ] ||
        raise "nm2_lib:configure_dhcp_server_on_interface requires ${NARGS_MIN} or ${NARGS_MAX} input arguments, $# given" -arg
    if_name=$1
    start_pool=$2
    end_pool=$3

    if [ -z "$start_pool" ] && [ -z "$end_pool" ]; then
        # One or both arguments are missing.
        dhcpd=''
    else
        dhcpd='["start","'$start_pool'"],["stop","'$end_pool'"]'
    fi

    log -deb "nm2_lib:configure_dhcp_server_on_interface - Configuring DHCP server on interface '$if_name'"

    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u enabled true \
        -u network true \
        -u dhcpd '["map",['$dhcpd']]' ||
            raise "FAIL: Could not update Wifi_Inet_Config" -l "nm2_lib:configure_dhcp_server_on_interface" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
        -is enabled true \
        -is network true \
        -is dhcpd '["map",['$dhcpd']]' ||
            raise "FAIL: Wifi_Inet_Config not reflected to Wifi_Inet_State" -l "nm2_lib:configure_dhcp_server_on_interface" -ow

    log -deb "nm2_lib:configure_dhcp_server_on_interface - DHCP server created on interface '$if_name' - Success"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function populates DNS settings for given interface to Wifi_Inet_Config.
#   It waits for Wifi_Inet_Config to reflect in Wifi_Inet_State.
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
#   $2  Primary DNS IP (string, optional)
#   $3  Secondary DNS IP (string, optional)
# RETURNS:
#   0   On Success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   configure_custom_dns_on_interface eth0 16.17.18.19 20.21.22.23
#   configure_custom_dns_on_interface eth0
###############################################################################
configure_custom_dns_on_interface()
{
    NARGS_MIN=1
    NARGS_MAX=3
    [ $# -eq ${NARGS_MIN} ] || [ $# -eq ${NARGS_MAX} ] ||
        raise "nm2_lib:configure_custom_dns_on_interface requires ${NARGS_MIN} or ${NARGS_MAX} input arguments, $# given" -arg
    if_name=$1
    primary_dns=$2
    secondary_dns=$3

    dns='["map",[["primary","'$primary_dns'"],["secondary","'$secondary_dns'"]]]'
    if [ -z "$primary_dns" ] && [ -z "$secondary_dns" ]; then
        dns=''
    fi

    log -deb "nm2_lib:configure_custom_dns_on_interface - Creating DNS on interface '$if_name'"

    update_ovsdb_entry Wifi_Inet_Config -w if_name "$if_name" \
        -u enabled true \
        -u network true \
        -u ip_assign_scheme static \
        -u dns $dns ||
            raise "FAIL: Could not update Wifi_Inet_Config" -l "nm2_lib:configure_custom_dns_on_interface" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$if_name" \
        -is enabled true \
        -is network true \
        -is dns $dns ||
            raise "FAIL: Wifi_Inet_Config not reflected to Wifi_Inet_State" -l "nm2_lib:configure_custom_dns_on_interface" -ow

    log -deb "nm2_lib:configure_custom_dns_on_interface - DNS created on interface '$if_name' - Success"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function sets port forwarding for given interface.
#   Raises exception if port forwarding is not set.
# INPUT PARAMETER(S):
#   $1  Source interface name (string, required)
#   $2  Source port (string, required)
#   $3  Destination IP address (string, required)
#   $4  Destination port (string, required)
#   $5  Protocol (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION
# USAGE EXAMPLE(S):
#   set_ip_port_forwarding bhaul-sta-24 8080 10.10.10.123 80 tcp
###############################################################################
set_ip_port_forwarding()
{
    local NARGS=5
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:set_ip_port_forwarding requires ${NARGS} input argument(s), $# given" -arg
    src_ifname=$1
    src_port=$2
    dst_ipaddr=$3
    dst_port=$4
    protocol=$5

    log -deb "nm2_lib:set_ip_port_forwarding - Creating port forward on interface '$src_ifname'"

    insert_ovsdb_entry IP_Port_Forward \
        -i dst_ipaddr "$dst_ipaddr" \
        -i dst_port "$dst_port" \
        -i src_port "$src_port" \
        -i protocol "$protocol" \
        -i src_ifname "$src_ifname" ||
            raise "FAIL: Could not insert entry to IP_Port_Forward table" -l "nm2_lib:set_ip_port_forwarding" -oe

    log -deb "nm2_lib:set_ip_port_forwarding - Port forward created on interface '$src_ifname' - Success"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function deletes port forwarding on interface by force.
#   Uses iptables tool.
#   Raises exception.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
#   $2  table type in iptables list (string, required)
#   $3  IP:Port (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   force_delete_ip_port_forward_raise bhaul-sta-24 <tabletype> 10.10.10.123:80
###############################################################################
force_delete_ip_port_forward_raise()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:force_delete_ip_port_forward_raise requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    ip_table_type=$2
    ip_port_forward_ip=$3

    log -deb "nm2_lib:force_delete_ip_port_forward_raise - iptables not empty. Force delete"

    port_forward_line_number=$(iptables -t nat --list -v --line-number | tr -s ' ' | grep "$ip_table_type" | grep "$if_name" | grep  "$ip_port_forward_ip" | cut -d ' ' -f1)
    if [ -z "$port_forward_line_number" ]; then
        log -deb "nm2_lib:force_delete_ip_port_forward_raise - Could not get iptables line number, skipping..."
        return 0
    fi

    wait_for_function_response 0 "iptables -t nat -D $ip_table_type $port_forward_line_number" &&
        raise "FAIL: IP port forward forcefully removed from iptables" -l "nm2_lib:force_delete_ip_port_forward_raise" -tc ||
        raise "FAIL: Could not to remove IP port forward from iptables" -l "nm2_lib:force_delete_ip_port_forward_raise" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks if NAT is enabled for interface at OS - LEVEL2.
#   Uses iptables tool.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   NAT is enabled on interface.
#   1   NAT is not enabled on interface.
# USAGE EXAMPLE(S):
#   check_interface_nat_enabled eth0
###############################################################################
check_interface_nat_enabled()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_interface_nat_enabled requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    iptables -t nat --list -v  | tr -s ' ' / | grep '/MASQUERADE/' | grep "$if_name"
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:check_interface_nat_enabled - Interface '${if_name}' NAT enabled"
        return 0
    else
        log -deb "nm2_lib:check_interface_nat_enabled - Interface '${if_name}' NAT disabled"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if IP port forwarding is enabled on given interface.
#   Uses iptables tool.
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   0   Port forwarding enabled on interface.
#   1   Port forwarding not enabled on interface.
# USAGE EXAMPLE(S):
#   check_ip_port_forwarding eth0
###############################################################################
check_ip_port_forwarding()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_ip_port_forwarding requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    iptables -t nat --list -v  | tr -s ' ' / | grep '/DNAT/' | grep "$if_name"
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:check_ip_port_forwarding - IP port forward set for interface '${if_name}'"
        return 0
    else
        log -deb "nm2_lib:check_ip_port_forwarding - IP port forward not set for interface '${if_name}'"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function verifies if broadcast address for interface is set at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   Broadcast address set on interface.
#   1   Broadcast address not set on interface.
# USAGE EXAMPLE(S):
#   check_interface_broadcast_set_on_system eth0
###############################################################################
check_interface_broadcast_set_on_system()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_interface_broadcast_set_on_system requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 6
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:check_interface_broadcast_set_on_system - Broadcast set for interface '${if_name}'"
        return 0
    else
        log -deb "nm2_lib:check_interface_broadcast_set_on_system - Broadcast not set for interface '${if_name}'"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function verifies if netmask for interface is set at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   Netmask set on interface.
#   1   Netmask not set on interface.
# USAGE EXAMPLE(S):
#   check_interface_netmask_set_on_system eth0
###############################################################################
check_interface_netmask_set_on_system()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_interface_netmask_set_on_system requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 8
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:check_interface_netmask_set_on_system - Netmask set for interface '${if_name}'"
        return 0
    else
        log -deb "nm2_lib:check_interface_netmask_set_on_system - Netmask not set for interface '${if_name}'"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function verifies if MTU for interface is set at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   MTU set on interface.
#   1   MTU not set on interface.
# USAGE EXAMPLE(S):
#   check_interface_mtu_set_on_system eth0
###############################################################################
check_interface_mtu_set_on_system()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_interface_mtu_set_on_system requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1

    ifconfig "$if_name" | tr -s ' ' | grep "MTU" | cut -d ":" -f2 | awk '{print $1}'
    if [ $? -eq 0 ]; then
        log -deb "nm2_lib:check_interface_mtu_set_on_system - MTU set for interface '${if_name}'"
        return 0
    else
        log -deb "nm2_lib:check_interface_mtu_set_on_system - MTU not set for interface '${if_name}'"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function verifies if dhcp for interface is configured at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
#   $2  Start pool (string, required)
#   $3  End pool (string, required)
# RETURNS:
#   0   dhcp is configured on interface.
#   1   dhcp is not configured on interface.
# USAGE EXAMPLE(S):
#   check_dhcp_from_dnsmasq_conf wifi0 10.10.10.16 10.10.10.32
###############################################################################
check_dhcp_from_dnsmasq_conf()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_dhcp_from_dnsmasq_conf requires ${NARGS} input argument(s), $# given" -arg
    if_name=$1
    start_pool=$2
    end_pool=$3

    grep "dhcp-range=$if_name,$start_pool,$end_pool" /var/etc/dnsmasq.conf &&
        return 0 ||
        return 1
}

###############################################################################
# DESCRIPTION:
#   Function verifies if DNS is configured at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  primary DNS IP (string, required)
# RETURNS:
#   0   DNS is configured.
#   1   DNS is not configured.
# USAGE EXAMPLE(S):
#   check_resolv_conf 1.2.3.4
###############################################################################
check_resolv_conf()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_resolv_conf requires ${NARGS} input argument(s), $# given" -arg
    primary_dns=$1

    cat /tmp/resolv.conf | grep "nameserver $primary_dns" &&
        return 0 ||
        return 1
}

###############################################################################
# DESCRIPTION:
#   Function checks if interface exists on system.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   0   Interface exists.
#   1   Interface does not exists.
# USAGE EXAMPLE(S):
#   check_interface_exists test1
###############################################################################
check_interface_exists()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_interface_exists requires ${NARGS} input argument(s), $# given" -arg
    local if_name=$1

    log -deb "nm2_lib:check_interface_exists - Checking if interface '${if_name}' exists on OS - LEVEL2"

    ifconfig | grep -wE "$if_name"
    if [ "$?" -eq 0 ]; then
        log -deb "nm2_lib:check_interface_exists - Interface '${if_name}' exists on OS - LEVEL2"
        return 0
    else
        log -deb "nm2_lib:check_interface_exists - Interface '${if_name}' does not exist on OS - LEVEL2"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if vlan interface exists at OS level - LEVEL2.
# STUB:
#   This function is a stub. It always raises an exception and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  Parent interface name (string, required)
#   $2  VLAN ID (int, required)
# RETURNS:
#   0   vlan interface exists on system.
# USAGE EXAMPLE(S):
#  check_vlan_iface eth0 100
###############################################################################
check_vlan_iface()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_vlan_iface requires ${NARGS} input argument(s), $# given" -arg
    parent_ifname=$1
    vlan_id=$2

    log "nm2_lib:check_vlan_iface - Checking vlan interface at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for each platform." -l "nm2_lib:check_vlan_iface" -fc
}


###############################################################################
# DESCRIPTION:
#   Function creates the configuration required for adding bridge
#   to Open_vSwitch table.
#   This function is used as a helper function to add the bridge.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
# RETURNS:
#   NONE
# USAGE EXAMPLE(S):
#   ovs_gen_bridge_config br-home
###############################################################################
ovs_gen_bridge_config()
{
    bridge=$1
    cat <<EOF
[
    "Open_vSwitch",
    {
        "op" : "insert",
        "table" : "Bridge",
        "uuid-name": "newBridge",
        "row": {
            "datapath_id": "00026df9edfc63",
            "name": "${bridge}"
        }
    },
    {
        "op" : "mutate",
        "table" : "Open_vSwitch",
        "where" : [["cur_cfg", "==", 0]],
        "mutations": [["bridges", "insert", ["set", [["named-uuid", "newBridge"]]]]]
    }
]
EOF
}

###############################################################################
# DESCRIPTION:
#   Function creates the OVS bridge by creating an entry in Open_vSwitch and Bridge
#   table
# INPUT PARAMETER(S):
#   $1  bridge name (string, required)
# RETURNS:
#   0
# USAGE EXAMPLE(S):
#   ovs_create_bridge br-home
###############################################################################
ovs_create_bridge()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib.sh:ovs_create_bridge requires ${NARGS} input argument(s), $# given" -arg
    bridge=$1

    ovs_gen_bridge_config "$bridge" | xargs -0 ovsdb-client transact
}

###############################################################################
# DESCRIPTION:
#   Function deletes the given bridge from Open_vSwitch and Bridge table.
# INPUT PARAMETER(S):
#   $1  bridge name (string, required)
# RETURNS:
#   0
# USAGE EXAMPLE(S):
#   ovs_delete_bridge br-home
###############################################################################
ovs_delete_bridge()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib.sh:ovs_delete_bridge requires ${NARGS} input argument(s), $# given" -arg
    bridge=$1

    bridge_uuid=$(${OVSH} -rU s Bridge _uuid -w name=="${bridge}")
    log "nm2_lib:ovs_delete_bridge Removing Bridge ${bridge} from Open_vSwitch table"
    ${OVSH} u Open_vSwitch bridges:del:'["set", ['"${bridge_uuid}"']]'

    log "nm2_lib:ovs_delete_bridge Removing ${bridge} from Bridge table"
    ${OVSH} d Bridge -w name=="${bridge}"
}

###############################################################################
# DESCRIPTION:
#   Function checks if the Traffic Control rule of the given type (ingress or egress)
#   is configured on the device - LEVEL2. Linux TC command should be available on
# . the device.
# INPUT PARAMETER(S):
#   $1  interface name (string, required)
#   $2  expected value to check (string, required)
#   $3  rule type whether ingress or egres (string, required)
# RETURNS:
#   0   if the Traffic Control rule is configured on the device
# USAGE EXAMPLE(S):
#  nb_is_tc_rule_configured "br-home" "8080" "ingress"
###############################################################################
nb_is_tc_rule_configured()
{
    ifname=$1
    expected_str=$2
    rule_type=$3

    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:nb_is_tc_rule_configured requires ${NARGS} input argument(s), $# given" -arg

    log "nm2_lib:nb_is_tc_rule_configured - Checking if $rule_type Traffic Control rule is applied on the device - LEVEL2"

    if [ $rule_type = "ingress" ]; then
        cmd="tc filter show dev ${ifname} parent ffff: | grep \"${expected_str}\" "
    else
        cmd="tc filter show dev ${ifname} | grep \"${expected_str}\" "
    fi
    log "nm2_lib:nb_is_tc_rule_configured - Executing ${cmd}"
    wait_for_function_response 0 "${cmd}" 10 &&
        log -deb "nm2_lib:nb_is_tc_rule_configured -$rule_type Traffic Control rule is applied on the device - Success" ||
        raise "FAIL: $rule_type Traffic Control rule is not applied on the device" -l "nm2_lib:nb_is_tc_rule_configured" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   When the device runs in Linux Native Bridge configuration, the function
#   parses the other_config in the Ports table for hairpin configuration and
#   returns the hairpin configuration.
# INPUT PARAMETER(S):
#   $1  interface name (string, required)
# RETURNS:
#   hairpin configuration
# USAGE EXAMPLE(S):
#   nb_get_current_hairpin_mode br-home.dns
###############################################################################
nb_get_current_hairpin_mode()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib.sh:nb_get_current_hairpin_mode requires ${NARGS} input argument(s), $# given" -arg

    ifname=$1
    ${OVSH} -M s Port -w name=="$ifname" other_config | \
        awk -F'"' '{for (i=1;i<NF;i++) {if ($(i)=="hairpin_mode"){print $(i+2)}}}'
}

###############################################################################
# DESCRIPTION:
#   Function checks if the hairpin configuration is present for the interface.
#   If configured, the hairpin configuration is removed from Ports table.
# INPUT PARAMETER(S):
#   $1  interface name (string, required)
# RETURNS:
#   None
# USAGE EXAMPLE(S):
#   nb_del_hairpin_config_if_present br-home.dns
###############################################################################
nb_del_hairpin_config_if_present()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib.sh:nb_del_hairpin_config_if_present requires ${NARGS} input argument(s), $# given" -arg

    ifname=$1
    curr_hairpin_mode=$(nb_get_current_hairpin_mode "$ifname")
    log -deb "curr_hairpin_mode: $curr_hairpin_mode"

    # remove hairpin mode if present
    if [ -n "${curr_hairpin_mode}" ]; then
        ${OVSH} -v U Port -w name=="$ifname" other_config:del:"[\"map\",[[\"hairpin_mode\", \"${curr_hairpin_mode}\"]]]"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function enables/disables hairpin mode on the interface.
# INPUT PARAMETER(S):
#   $1 interface name (string, required)
#   $2 "on/off" (string, required)
# RETURNS:
#   0 - hairpin configuration is successful
#   1 - when hairpin configuration fails
# USAGE EXAMPLE(S):
#   nb_configure_hairpin br-home.dns "on"
#   nb_configure_hairpin br-home.dns "off"
###############################################################################
nb_configure_hairpin()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:nb_configure_hairpin requires ${NARGS} input argument(s), $# given" -arg

    ifname=$1
    hairpin=$2

    port=$(${OVSH} s Port -w name=="$ifname" -r | wc -l)
    if [ "${port}" -ne 0 ]; then
        # delete existing hairpin configuration if present
        nb_del_hairpin_config_if_present "$ifname"
        log -deb "nm2_lib:nb_configure_hairpin - configuring hairpin '$hairpin' on interface '$ifname'"
        ${OVSH} U Port -w name=="$ifname" other_config:ins:"[\"map\",[[\"hairpin_mode\", \"${hairpin}\"]]]"
    fi

    sleep 2
    # verify if the configuration is applied
    hairpin_config=$(nb_get_current_hairpin_mode "$ifname")
    log -deb "nm2_lib:nb_configure_hairpin - hairpin configuration is $hairpin_config "
    if [ -z "${hairpin_config}" ]; then
        log -deb "nm2_lib:nb_configure_hairpin - Failed to configure hairpin mode on interface '$ifname'"
        return 1
    else
        log -deb "nm2_lib:nb_configure_hairpin - Configured hairpin mode on interface '$ifname' - Success"
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if the Traffic Control rule of the given type (ingress or egress)
#   is removed from the device - LEVEL2. Linux TC command should be available on
# . the device.
# INPUT PARAMETER(S):
#   $1  interface name (string, required)
#   $2  expected value to check (string, required)
#   $3  rule type whether ingress or egres (string, required)
# RETURNS:
#   0   if the Traffic Control rule is removed from the device
# USAGE EXAMPLE(S):
#  nb_is_tc_rule_removed "br-home" "8080" "ingress"
###############################################################################
nb_is_tc_rule_removed()
{
    ifname=$1
    expected_str=$2
    rule_type=$3

    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:nb_is_tc_rule_removed requires ${NARGS} input argument(s), $# given" -arg

    log "nm2_lib:nb_is_tc_rule_removed - Checking if $rule_type Traffic Control rule is removed from the device - LEVEL2"

    if [ $rule_type = "ingress" ]; then
        cmd="tc filter show dev ${ifname} parent ffff: | grep \"${expected_str}\" "
    else
        cmd="tc filter show dev ${ifname} | grep \"${expected_str}\" "
    fi
    log "nm2_lib:nb_is_tc_rule_removed - Executing ${cmd}"
    wait_for_function_response 1 "${cmd}" 10 &&
        log -deb "nm2_lib:nb_is_tc_rule_removed -$rule_type Traffic Control rule is removed from the device - Success" ||
        raise "FAIL: $rule_type Traffic Control rule is not removed from the device" -l "nm2_lib:nb_is_tc_rule_configured" -ds

    return 0
}
