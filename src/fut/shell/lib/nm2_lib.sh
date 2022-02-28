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
#   create_inet_entry -if_name "br-wan" -if_type "vif"
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
                nm2_if_name="${1}"
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
    [ -z "${nm2_if_name}" ] &&
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
    check_ovsdb_entry Wifi_Inet_Config -w if_name "${nm2_if_name}"
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
    $function_to_call Wifi_Inet_Config -w if_name "$nm2_if_name" $func_params $func_params_add &&
        log -deb "nm2_lib:create_inet_entry - $function_to_call Wifi_Inet_Config -w if_name $nm2_if_name $func_params $func_params_add - Success" ||
        raise "FAIL: $function_to_call Wifi_Inet_Config -w if_name $nm2_if_name $func_params $func_params_add" -l "nm2_lib:create_inet_entry" -oe

    # Validate action insert/update
    func_params=${args//$replace/-is}
    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" $func_params &&
        log -deb "nm2_lib:create_inet_entry - wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name $func_params - Success" ||
        raise "FAIL: wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name $func_params" -l "nm2_lib:create_inet_entry" -ow

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
    nm2_if_name=$1

    log -deb "nm2_lib:reset_inet_entry - Setting Wifi_Inet_Config for $nm2_if_name to default values"
    update_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" \
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
    nm2_if_name=$1

    log -deb "nm2_lib:delete_inet_interface - Removing interface '$nm2_if_name'"

    remove_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" ||
        raise "FAIL: Could not remove Wifi_Inet_Config::if_name" -l "nm2_lib:delete_inet_interface" -oe

    wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "$nm2_if_name" ||
        raise "FAIL: Could not remove Wifi_Inet_State::if_name" -l "nm2_lib:delete_inet_interface" -ow

    wait_for_function_response 1 "ip link show $nm2_if_name" &&
        log -deb "nm2_lib:delete_inet_interface - LEVEL2: Interface $nm2_if_name removed - Success" ||
        force_purge_interface_raise "$nm2_if_name"

    log -deb "nm2_lib:delete_inet_interface - Interface '$nm2_if_name' deleted from ovsdb and OS - LEVEL2"

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
    nm2_if_name=$1

    log -deb "nm2_lib:force_purge_interface_raise - Interface force removal"
    ip link delete "$nm2_if_name" || true

    wait_for_function_response 1 "ip link show $nm2_if_name" &&
        raise "FAIL: Interface '$nm2_if_name' removed forcefully" -l "nm2_lib:force_purge_interface_raise" -tc ||
        raise "FAIL: Interface still present, could not delete interface '$nm2_if_name'" -l "nm2_lib:force_purge_interface_raise" -tc
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
    nm2_if_name=$1
    nm2_start_pool=$2
    nm2_end_pool=$3

    if [ -z "$nm2_start_pool" ] && [ -z "$nm2_end_pool" ]; then
        # One or both arguments are missing.
        nm2_dhcpd=''
    else
        nm2_dhcpd='["start","'$nm2_start_pool'"],["stop","'$nm2_end_pool'"]'
    fi

    log -deb "nm2_lib:configure_dhcp_server_on_interface - Configuring DHCP server on interface '$nm2_if_name'"

    update_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" \
        -u enabled true \
        -u network true \
        -u dhcpd '["map",['$nm2_dhcpd']]' ||
            raise "FAIL: Could not update Wifi_Inet_Config" -l "nm2_lib:configure_dhcp_server_on_interface" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" \
        -is enabled true \
        -is network true \
        -is dhcpd '["map",['$nm2_dhcpd']]' ||
            raise "FAIL: Wifi_Inet_Config not reflected to Wifi_Inet_State" -l "nm2_lib:configure_dhcp_server_on_interface" -ow

    log -deb "nm2_lib:configure_dhcp_server_on_interface - DHCP server created on interface '$nm2_if_name' - Success"

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
    nm2_if_name=$1
    nm2_primary_dns=$2
    nm2_secondary_dns=$3

    nm2_dns='["map",[["primary","'$nm2_primary_dns'"],["secondary","'$nm2_secondary_dns'"]]]'
    if [ -z "$nm2_primary_dns" ] && [ -z "$nm2_secondary_dns" ]; then
        nm2_dns=''
    fi

    log -deb "nm2_lib:configure_custom_dns_on_interface - Creating DNS on interface '$nm2_if_name'"

    update_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" \
        -u enabled true \
        -u network true \
        -u ip_assign_scheme static \
        -u dns $nm2_dns ||
            raise "FAIL: Could not update Wifi_Inet_Config" -l "nm2_lib:configure_custom_dns_on_interface" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" \
        -is enabled true \
        -is network true \
        -is dns $nm2_dns ||
            raise "FAIL: Wifi_Inet_Config not reflected to Wifi_Inet_State" -l "nm2_lib:configure_custom_dns_on_interface" -ow

    log -deb "nm2_lib:configure_custom_dns_on_interface - DNS created on interface '$nm2_if_name' - Success"

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
    nm2_src_ifname=$1
    nm2_src_port=$2
    nm2_dst_ipaddr=$3
    nm2_dst_port=$4
    nm2_protocol=$5

    log -deb "nm2_lib:set_ip_port_forwarding - Creating port forward on interface '$nm2_src_ifname'"

    insert_ovsdb_entry IP_Port_Forward \
        -i dst_ipaddr "$nm2_dst_ipaddr" \
        -i dst_port "$nm2_dst_port" \
        -i src_port "$nm2_src_port" \
        -i protocol "$nm2_protocol" \
        -i src_ifname "$nm2_src_ifname" ||
            raise "FAIL: Could not insert entry to IP_Port_Forward table" -l "nm2_lib:set_ip_port_forwarding" -oe

    log -deb "nm2_lib:set_ip_port_forwarding - Port forward created on interface '$nm2_src_ifname' - Success"

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
    nm2_if_name=$1
    nm2_ip_table_type=$2
    nm2_ip_port_forward_ip=$3

    log -deb "nm2_lib:force_delete_ip_port_forward_raise - iptables not empty. Force delete"

    nm2_port_forward_line_number=$(iptables -t nat --list -v --line-number | tr -s ' ' | grep "$nm2_ip_table_type" | grep "$nm2_if_name" | grep  "$nm2_ip_port_forward_ip" | cut -d ' ' -f1)
    if [ -z "$nm2_port_forward_line_number" ]; then
        log -deb "nm2_lib:force_delete_ip_port_forward_raise - Could not get iptables line number, skipping..."
        return 0
    fi

    wait_for_function_response 0 "iptables -t nat -D $nm2_ip_table_type $nm2_port_forward_line_number" &&
        raise "FAIL: IP port forward forcefully removed from iptables" -l "nm2_lib:force_delete_ip_port_forward_raise" -tc ||
        raise "FAIL: Could not to remove IP port forward from iptables" -l "nm2_lib:force_delete_ip_port_forward_raise" -tc
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
#   $1  Internal interface name (string, required)
#   $2  External interface name (string, required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   N/A
###############################################################################
check_upnp_configuration_valid()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "nm2_lib:check_upnp_configuration_valid requires ${NARGS} input argument(s), $# given" -arg
    nm2_internal_if=$1
    nm2_external_if=$2

    log -deb "nm2_lib:check_upnp_configuration_valid - LEVEL2 - Checking if '$nm2_internal_if' set as internal interface"
    $(cat /var/miniupnpd/miniupnpd.conf | grep "listening_ip=$nm2_internal_if")
    if [ "$?" -eq 0 ]; then
        log -deb "nm2_lib:check_upnp_configuration_valid - UPnP configuration VALID for internal interface '$nm2_internal_if' - Success"
    else
        raise "FAIL: UPnP configuration not valid for internal interface '$nm2_internal_if'" -l "nm2_lib:check_upnp_configuration_valid" -tc
    fi

    log -deb "nm2_lib:check_upnp_configuration_valid - LEVEL2 - Checking if '$nm2_external_if' set as external interface"
    $(cat /var/miniupnpd/miniupnpd.conf | grep "ext_ifname=$nm2_external_if")
    if [ "$?" -eq 0 ]; then
        log -deb "nm2_lib:check_upnp_configuration_valid - UPnP configuration valid for external interface '$nm2_external_if' - Success"
    else
        raise "FAIL: UPnP configuration not valid for external interface '$nm2_external_if'" -l "nm2_lib:check_upnp_configuration_valid" -tc
    fi

    return 0
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
    nm2_primary_dns=$1

    cat /tmp/resolv.conf | grep "nameserver $nm2_primary_dns" &&
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

#########################################################################################################
# DESCRIPTION:
#   Function checks vlan interface existence at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  parent_ifname (required)
#   $2  vlan_id (required)
# RETURNS:
#   0   vlan interface exists on system.
#   Stub function always fails.
# NOTE:
#   This is a stub function. Provide function for each platform in overrides.
# USAGE EXAMPLE(S):
#  check_vlan_iface eth0 100
#########################################################################################################
check_vlan_iface()
{
    raise "FAIL: This is a stub function. Override implementation needed for each platform." -l "nm2_lib:check_vlan_iface" -ds
}
