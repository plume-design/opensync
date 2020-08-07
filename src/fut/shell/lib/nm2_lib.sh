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
source ${FUT_TOPDIR}/shell/lib/wm2_lib.sh
source ${LIB_OVERRIDE_FILE}


############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common Network Manager functions
#
############################################ INFORMATION SECTION - STOP ################################################

nm_setup_test_environment()
{
    fn_name="nm2_lib:nm_setup_test_environment"
    log -deb "$fn_name - Running NM2 setup"

    device_init ||
        raise "device_init" -l "$fn_name" -fc

    start_openswitch ||
        raise "start_openswitch" -l "$fn_name" -fc

    start_wireless_driver ||
        raise "start_wireless_driver" -l "$fn_name" -fc

    start_specific_manager wm ||
        raise "start_specific_manager wm" -l "$fn_name" -fc

    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" ||
            raise "Wifi_Radio_State - {if_name==$if_name}, {if_name:=$if_name}" -l "$fn_name" -ow
    done

    start_specific_manager nm ||
        raise "start_specific_manager nm" -l "$fn_name" -fc

    empty_ovsdb_table AW_Debug ||
        raise "empty_ovsdb_table AW_Debug" -l "$fn_name" -fc

    set_manager_log WM TRACE ||
        raise "set_manager_log WM TRACE" -l "$fn_name" -fc

    set_manager_log NM TRACE ||
        raise "set_manager_log NM TRACE" -l "$fn_name" -fc

    log -deb "$fn_name - NM2 setup - end"
}

create_inet_entry2()
{
    args=""
    replace="func_arg"
    fn_name="nm2_lib:create_inet_entry2"
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
            -netmask | \
            -dns | \
            -gateway | \
            -broadcast | \
            -ip_assign_scheme | \
            -mtu | \
            -NAT | \
            -upnp_mode | \
            -dhcpd | \
            -gre_ifname | \
            -gre_remote_inet_addr | \
            -gre_local_inet_addr)
                args="${args} ${replace} ${option#?} ${1}"
                shift
                ;;
        esac
    done

    [ -z ${nm2_if_name} ] && raise "Interface name argument empty" -l "${fn_name}" -arg
    check_ovsdb_entry Wifi_Inet_Config -w if_name "${nm2_if_name}"
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Updating existing inet interface"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        log -deb "$fn_name - Creating inet interface"
        function_to_call="insert_ovsdb_entry"
        function_arg="-i"
    fi

    # Perform action insert/update
    func_params=${args//$replace/$function_arg}
    $function_to_call Wifi_Inet_Config -w if_name "$nm2_if_name" $func_params &&
        log -deb "$fn_name - Success $function_to_call Wifi_Inet_Config -w if_name $nm2_if_name $func_params" ||
        raise "Failure $function_to_call Wifi_Inet_Config -w if_name $nm2_if_name $func_params" -l "$fn_name" -oe

    # Validate action insert/update
    func_params=${args//$replace/-is}
    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" $func_params &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name $func_params" ||
        raise "Failure wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name $func_params" -l "$fn_name" -ow
}

create_inet_entry()
{
    args=""
    nm2_if_name=false
    replace="func_arg"
    fn_name="nm2_lib:create_inet_entry"
    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -if_name)
                args="$args $replace if_name $1"
                nm2_if_name=$1
                shift
                ;;
            -enabled)
                args="$args $replace enabled $1"
                shift
                ;;
            -network)
                args="$args $replace network $1"
                shift
                ;;
            -if_type)
                args="$args $replace if_type $1"
                shift
                ;;
            -inet_addr)
                args="$args $replace inet_addr $1"
                shift
                ;;
            -netmask)
                args="$args $replace netmask $1"
                shift
                ;;
            -dns)
                args="$args $replace dns $1"
                shift
                ;;
            -gateway)
                args="$args $replace gateway $1"
                shift
                ;;
            -broadcast)
                args="$args $replace broadcast $1"
                shift
                ;;
            -ip_assign_scheme)
                args="$args $replace ip_assign_scheme $1"
                shift
                ;;
            -mtu)
                args="$args $replace mtu $1"
                shift
                ;;
            -NAT)
                args="$args $replace NAT $1"
                shift
                ;;
            -upnp_mode)
                args="$args $replace upnp_mode $1"
                shift
                ;;
            -dhcpd)
                args="$args $replace dhcpd $1"
                shift
                ;;
            -gre_ifname)
                args="$args $replace gre_ifname $1"
                shift
                ;;
            -gre_remote_inet_addr)
                args="$args $replace gre_remote_inet_addr $1"
                shift
                ;;
            -gre_local_inet_addr)
                args="$args $replace gre_local_inet_addr $1"
                shift
                ;;
        esac
    done

    log -deb "$fn_name - Creating Inet interface"

    function_to_call="insert_ovsdb_entry"
    function_arg="-i"

      ${OVSH} s Wifi_Inet_Config -w if_name=="$nm2_if_name" && update=0 || update=1
      if [ "$update" -eq 0 ]; then
          log -deb "$fn_name - Inet entry exists, updating instead"
          function_to_call="update_ovsdb_entry"
          function_arg="-u"
      fi

    func_params=${args//$replace/$function_arg}

    $function_to_call Wifi_Inet_Config -w if_name "$nm2_if_name" $func_params &&
        log -deb "$fn_name - Success $function_to_call Wifi_Inet_Config -w if_name $nm2_if_name $func_params" ||
        raise "{Wifi_Inet_Config -> $function_to_call}" -l "$fn_name" -oe

    func_params=${args//$replace/-is}

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" $func_params &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name $func_params" ||
        raise "Wifi_Inet_State" -l "$fn_name" -ow

    log -deb "$fn_name - Inet interface created"
}

reset_inet_entry()
{
    nm2_if_name=$1
    fn_name="nm2_lib:reset_inet_entry"
    log -deb "$fn_name - Setting Inet_Config for $nm2_if_name to default values"

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
            log -deb "$fn_name - Wifi_Inet_Config updated" ||
            raise "Failed to update Wifi_Inet_Config" -l "$fn_name" -oe
}

delete_inet_interface()
{
    nm2_if_name=$1
    fn_name="nm2_lib:delete_inet_interface"

    log -deb "$fn_name - Removing interface '$nm2_if_name'"

    remove_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" ||
        raise "{Wifi_Inet_Config -> remove}" -l "$fn_name" -oe

    wait_ovsdb_entry_remove Wifi_Inet_State -w if_name "$nm2_if_name" ||
        raise "{Wifi_Inet_State -> wait}" -l "$fn_name" -ow

    wait_for_function_response 1 "ip link show $nm2_if_name" &&
        log "$fn_name - LEVEL2: Interface $nm2_if_name removed" ||
        interface_force_purge_die $nm2_if_name

    log -deb "$fn_name - Interface '$nm2_if_name' deleted"
}

interface_force_purge_die()
{
    nm2_if_name=$1
    fn_name="nm2_lib:interface_force_purge_die"

    log -deb "$fn_name - Interface force removal"
    ip link delete "$nm2_if_name" || true
    wait_for_function_response 1 "ip link show $nm2_if_name" &&
        raise "Interface removed $nm2_if_name forcefully" -l "$fn_name" -tc ||
        raise "Interface still present, couldn't delete interface $nm2_if_name" -l "$fn_name" -tc
}

enable_disable_dhcp_server()
{
    nm2_if_name=$1
    nm2_start_pool=$2
    nm2_end_pool=$3
    fn_name="nm2_lib:enable_disable_dhcp_server"

    nm2_dhcpd='["start","'$nm2_start_pool'"],["stop","'$nm2_end_pool'"]'

    if [ -z $nm2_start_pool ] && [ -z $nm2_end_pool ]; then
        nm2_dhcpd=''
    fi

    log -deb "$fn_name - Creating DHCP on $nm2_if_name"

    update_ovsdb_entry Wifi_Inet_Config -w if_name "$nm2_if_name" \
        -u enabled true \
        -u network true \
        -u dhcpd '["map",['$nm2_dhcpd']]' ||
            raise "{Wifi_Inet_Config -> update}" -l "$fn_name" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name "$nm2_if_name" \
        -is enabled true \
        -is network true \
        -is dhcpd '["map",['$nm2_dhcpd']]' ||
            raise "Wifi_Inet_State" -l "$fn_name" -ow

    log -deb "$fn_name - DHCP created on $nm2_if_name"
}

enable_disable_custom_dns()
{
    nm2_if_name=$1
    nm2_primary_dns=$2
    nm2_secondary_dns=$3
    fn_name="nm2_lib:nm_setup_test_environment"

    nm2_dns='["map",[["primary","'$nm2_primary_dns'"],["secondary","'$nm2_secondary_dns'"]]]'
    if [ -z $nm2_primary_dns ] && [ -z $nm2_secondary_dns ]; then
        nm2_dns=''
    fi

    log -deb "$fn_name - Creating DNS on $nm2_if_name"

    update_ovsdb_entry Wifi_Inet_Config -w if_name $nm2_if_name \
        -u enabled true \
        -u network true \
        -u ip_assign_scheme static \
        -u dns $nm2_dns ||
            raise "{Wifi_Inet_Config -> update}" -l "$fn_name" -oe

    wait_ovsdb_entry Wifi_Inet_State -w if_name $nm2_if_name \
        -is enabled true \
        -is network true \
        -is dns $nm2_dns ||
            raise "Wifi_Inet_State" -l "$fn_name" -ow

    log -deb "$fn_name - DNS created on $nm2_if_name"
}

set_ip_forward()
{
    nm2_src_ifname=$1
    nm2_src_port=$2
    nm2_dst_ipaddr=$3
    nm2_dst_port=$4
    nm2_protocol=$5
    fn_name="nm2_lib:nm_setup_test_environment"

    log -deb "$fn_name - Creating port forward on $nm2_src_ifname"

    insert_ovsdb_entry IP_Port_Forward \
        -i dst_ipaddr "$nm2_dst_ipaddr" \
        -i dst_port "$nm2_dst_port" \
        -i src_port "$nm2_src_port" \
        -i protocol "$nm2_protocol" \
        -i src_ifname "$nm2_src_ifname" ||
            raise "{IP_Port_Forward -> insert}" -l "$fn_name" -oe

    log -deb "$fn_name - Port forward created on $nm2_src_ifname"
}

force_delete_ip_port_forward_die()
{
    nm2_if_name=$1
    nm2_ip_table_type=$2
    nm2_ip_port_forward_ip=$3
    nm2_port_forward_line_number=$(iptables -t nat --list -v --line-number | tr -s ' ' | grep $nm2_ip_table_type | grep $nm2_if_name | grep  $nm2_ip_port_forward_ip | cut -d ' ' -f1)
    fn_name="nm2_lib:force_delete_ip_port_forward_die"

    log -deb "$fn_name - iptables not empty. Force delete"

    if [ -z "$nm2_port_forward_line_number" ]; then
        log -deb "$fn_name - Couldn't get iptable line number, skipping..."
        return 0
    fi

    wait_for_function_response 0 "iptables -t nat -D $nm2_ip_table_type $nm2_port_forward_line_number" &&
        raise "Ip port forward forcefully removed from iptables" -l "$fn_name" -tc ||
        raise "Failed to remove ip port forward from iptables" -l "$fn_name" -tc
}

check_upnp_conf()
{
    nm2_internal_if=$1
    nm2_external_if=$2
    fn_name="nm2_lib:check_upnp_conf"

    log "$fn_name - LEVEL 2 - Checking if '$nm2_internal_if' set as internal interface"
    $(cat /var/miniupnpd/miniupnpd.conf | grep -q "listening_ip=$nm2_internal_if")
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - UPnP configuration VALID for internal interface"
    else
        raise "UPnP configuration NOT VALID for internal interface" -l "$fn_name" -tc
    fi

    log -deb "$fn_name - LEVEL 2 - Checking if '$nm2_external_if' set as external interface"
    $(cat /var/miniupnpd/miniupnpd.conf | grep -q "ext_ifname=$nm2_external_if")
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - UPnP configuration VALID for external interface"
    else
        raise "UPnP configuration NOT VALID for external interface" -l "$fn_name" -tc
    fi

    return 0
}

interface_nat_enabled()
{
    iptables -t nat --list -v  | tr -s ' ' / | grep '/MASQUERADE/' | grep -q "$1"
}

ip_port_forward()
{
    iptables -t nat --list -v  | tr -s ' ' / | grep '/DNAT/' | grep -q "$1"
}

interface_broadcast()
{
    ifconfig "$1" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 6
}

interface_netmask()
{
    ifconfig "$1" | tr -s ' :' '@' | grep -e '^@inet@' | cut -d '@' -f 8
}

interface_mtu()
{
    ifconfig "$1" | tr -s ' ' | grep "MTU" | cut -d ":" -f2 | awk '{print $1}'
}

wait_for_dnsmasq()
{
    if_name=$1
    start_pool=$2
    end_pool=$3

    $(grep -q "dhcp-range=$if_name,$start_pool,$end_pool" /var/etc/dnsmasq.conf) || $(return 1)

    return $?
}

check_resolv_conf()
{
    nm2_primary_dns=$1

    $(cat /tmp/resolv.conf | grep -q "nameserver $nm2_primary_dns") || $(return 1)

    return $?
}

# Check if interface exists on system
check_interface_exists()
{
    fn_name="nm2_lib:check_interface_exists"
    log -deb "[DEPRECATED] - Function ${fn_name} deprecated in favor of check_interface_exists2"
    if_name=$1

    log "lib/nm2_lib: check_interface_exists - LEVEL 2 - Checking if interface '$if_name' exists on system"

    $(ifconfig | grep $if_name)
    if [ "$?" -eq 0 ]; then
        log -deb "lib/nm2_lib: check_interface_exists - interface '$if_name' exists on system."
        return 0
    else
        log "lib/nm2_lib: check_interface_exists - interface '$if_name' does NOT exist on system."
        return 1
    fi
}

check_interface_exists2()
{
    local fn_name="nm2_lib:check_interface_exists2"
    local if_name=$1

    log -deb "${fn_name} - Checking if interface ${if_name} exists on system"
    $(ifconfig | grep -qwE $if_name)
    if [ "$?" -eq 0 ]; then
        log -deb "${fn_name} - interface ${if_name} exists on system"
        return 0
    else
        log -deb "${fn_name} - interface ${if_name} does NOT exist on system"
        return 1
    fi
}

# Check if manager is running by checking if its PID exists
# Return 0 if exists
# Return 1 if does not exist
check_manager_alive()
{
    manager_pid_file=$1
    pid_of_manager=$(get_pid "$manager_pid_file")
    if [ -z "$pid_of_manager" ]; then
        log "nm2/$(basename "$0"): $manager_pid_file PID NOT found"
        return 1
    else
        log "nm2/$(basename "$0"): $manager_pid_file PID found"
        return 0
    fi
}
