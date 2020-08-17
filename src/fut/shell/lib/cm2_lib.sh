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
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source ${FUT_TOPDIR}/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/unit_lib.sh
source ${LIB_OVERRIDE_FILE}


############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common Connection Manager functions
#
############################################ INFORMATION SECTION - STOP ################################################

############################################ SETUP SECTION - START #####################################################

cm_setup_test_environment()
{
    cm2_if_name=${1:-eth0}
    [ "$2" == "true" ] && cm2_wait_payload=true || cm2_wait_payload=false
    [ "$3" == "gw" ] && cm2_is_gw=true || cm2_is_gw=false
    fn_name="cm2_lib:cm_setup_test_environment"

    log -deb "$fn_name - Running CM2 setup"

    device_init ||
        raise "device_init" -l "$fn_name" -fc
    cm_disable_fatal_state ||
        raise "cm_disable_fatal_state" -l "$fn_name" -fc
    start_openswitch ||
        raise "start_openswitch" -l "$fn_name" -fc
    manipulate_iptables_protocol unblock DNS ||
        raise "manipulate_iptables_protocol unblock DNS" -l "$fn_name" -fc
    manipulate_iptables_protocol unblock SSL ||
        raise "manipulate_iptables_protocol unblock SSL" -l "$fn_name" -fc

    # This needs to execute before we start the managers. Flow is essential.
    if [ "$cm2_is_gw" == "true" ]; then
        add_bridge_interface br-wan "$cm2_if_name" ||
            raise "add_bridge_interface $cm2_if_name" -l "$fn_name" -fc
    fi

    start_specific_manager cm -v ||
        raise "start_specific_manager cm" -l "$fn_name" -fc
    start_specific_manager nm ||
        raise "start_specific_manager nm" -l "$fn_name" -fc
    empty_ovsdb_table AW_Debug ||
        raise "empty_ovsdb_table AW_Debug" -l "$fn_name" -fc
    set_manager_log CM TRACE ||
        raise "set_manager_log CM TRACE" -l "$fn_name" -fc
    set_manager_log NM TRACE ||
        raise "set_manager_log NM TRACE" -l "$fn_name" -fc

    if [ "$cm2_is_gw" == "true" ]; then
        wait_for_function_response 0 "check_default_route_gw" ||
            raise "Default GW not added to routes" -l "$fn_name" -ds
    fi

    if [ "$cm2_wait_payload" == "true" ]; then
        wait_ovsdb_entry AW_Bluetooth_Config -is payload 75:00:00:00:00:00 &&
            log -deb "$fn_name AW_Bluetooth_Config changed {payload:=75:00:00:00:00:00}" ||
            raise "AW_Bluetooth_Config - {payload:=75:00:00:00:00:00}" -l "$fn_name" -ow
    fi

    return 0
}

cm2_teardown()
{
    fn_name="cm2_lib:cm2_teardown"
    log -deb "$fn_name - Running CM2 teardown"
    remove_bridge_interface br-wan &&
        log -deb "$fn_name - Success: remove_bridge_interface br-wan" ||
        log -deb "$fn_name - Failed remove_bridge_interface br-wan"

    log -deb "$fn_name - Killing CM pid"
    cm_pids=$(pgrep "cm")
    kill $cm_pids &&
        log -deb "$fn_name - CM pids killed" ||
        log -deb "$fn_name - Failed to kill CM pids"
}

############################################ SETUP SECTION - STOP #####################################################

############################################ CLOUD SECTION - START #####################################################

wait_cloud_state()
{
    wait_for_cloud_state=$1
    fn_name="cm2_lib:wait_cloud_state"
    log -deb "$fn_name - Waiting for cloud state $wait_for_cloud_state"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep -q \"$wait_for_cloud_state\"" &&
        log -deb "$fn_name - Cloud state is $wait_for_cloud_state" ||
        raise "Manager - {status:=$wait_for_cloud_state}" -l "$fn_name" -ow
    print_tables Manager
}


wait_cloud_state_not()
{
    wait_for_cloud_state_not=$1
    fn_name="cm2_lib:wait_cloud_state_not"

    log -deb "$fn_name - Waiting for cloud state not to be $wait_for_cloud_state_not"
    wait_for_function_response 0 "${OVSH} s Manager status -r | grep -q \"$wait_for_cloud_state_not\"" &&
        raise "Manager - {status:=$wait_for_cloud_state_not}" -l "$fn_name" -ow ||
        log -deb "$fn_name - Cloud state is $wait_for_cloud_state_not"

    print_tables Manager
}

############################################ CLOUD SECTION - STOP ######################################################

############################################ ROUTE SECTION - START #####################################################

check_default_route_gw()
{
    default_gw=$(route -n | tr -s ' ' | grep -i UG | awk '{printf $2}';)
    if [ -z "$default_gw" ]; then
        return 1
    else
        return 0
    fi
}

############################################ ROUTE SECTION - STOP ######################################################


############################################ LINKS SECTION - START #####################################################

############################################ LINKS SECTION - STOP ######################################################


############################################ TEST CASE SECTION - START #################################################

# Adds (inserts) or removes (deletes) rules to iptable.
# Supports DNS and SSL rules.
# Can block traffic by inserting rule (-I option)
# Can unblock traffic by deleting rule (-D option)
manipulate_iptables_protocol()
{
    option=$1
    traffic_type=$2
    fn_name="cm2_lib:manipulate_iptables_protocol"

    log -deb "$fn_name - $option $traffic_type traffic"

    if [ "$option" == "block" ]; then
        iptable_option='I'
        exit_code=0
    elif [ "$option" == "unblock" ]; then
        iptable_option='D'
        # Waiting for exit code 1 if multiple iptables rules are inserted - safer way
        exit_code=1
    else
        raise "option -> {given:$option, supported: block, unblock}" -l "$fn_name" -arg
    fi

    if [ "$traffic_type" == "DNS" ]; then
        traffic_port="53"
        traffic_port_type="udp"
    elif [ "$traffic_type" == "SSL" ]; then
        traffic_port="443"
        traffic_port_type="tcp"
    else
        raise "traffic_type -> {given:$option, supported: DNS, SSL}" -l "$fn_name" -arg
    fi

    $(iptables -S | grep -q "OUTPUT -p $traffic_port_type -m $traffic_port_type --dport $traffic_port -j DROP")
    # Add rule if not already an identical one in table, but unblock always
    if [ "$?" -ne 0 ] || [ "$option" == "unblock" ]; then
        wait_for_function_response $exit_code "iptables -$iptable_option OUTPUT -p $traffic_port_type --dport $traffic_port -j DROP" &&
            log -deb "$fn_name - $traffic_type traffic ${option}ed" ||
            raise "Failed to $option $traffic_type traffic" -l "$fn_name" -nf
    else
        log "lib/$fn_name - Add failure: Rule already in chain"
    fi
}

# Adds (inserts) or removes (deletes) rules to iptables for OUTPUT chain.
# Can block OUTPUT traffic by inserting rule (-I option) for provided source address
# Can unblock OUTPUT traffic by deleting rule (-D option) for provided source address
manipulate_iptables_address()
{
    option=$1
    address=$2
    fn_name="cm2_lib:manipulate_iptables_address"

    log -deb "$fn_name - $option $address internet"

    if [ "$option" == "block" ]; then
        iptable_option='I'
        exit_code=0
    elif [ "$option" == "unblock" ]; then
        iptable_option='D'
        # Waiting for exit code 1 if multiple iptables rules are inserted - safer way
        exit_code=1
    else
        raise "option -> {given:$option, supported: block, unblock}" -l "$fn_name" -arg
    fi

    $(iptables -S | grep -q "OUTPUT -s $address -j DROP")
    # Add rule if not already an identical one in table, but unblock always
    if [ "$?" -ne 0 ] || [ "$option" == "unblock" ]; then
        wait_for_function_response $exit_code "iptables -$iptable_option OUTPUT -s $address -j DROP" &&
            log -deb "$fn_name - internet ${option}ed" ||
            raise "Failed to $option internet" -l "$fn_name" -nf
    else
        log "lib/$fn_name - Add failure: Rule already in chain"
    fi
}

############################################ TEST CASE SECTION - STOP ##################################################
