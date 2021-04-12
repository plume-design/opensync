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
export FUT_CM2_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/cm2_lib.sh sourced"
####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Connection Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for CM tests.
#   Can be used with parameter to wait for bluetooth payload from CM.
#   Can be used with parameter to make device a gateway, adding WAN interface
#   to bridge.
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  interface name (optional, default: eth0)
#   $2  is gateway (optional, default: true)
# RETURNS:
#   0   On success.
#   See description.
# USAGE EXAMPLE(S):
#   cm_setup_test_environment
#   cm_setup_test_environment eth0 true
#   cm_setup_test_environment eth0 false
###############################################################################
cm_setup_test_environment()
{
    fn_name="cm2_lib:cm_setup_test_environment"
    cm2_if_name=${1:-eth0}
    cm2_is_gw=${2:-true}

    log "$fn_name - Running CM2 setup"

    device_init &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    manipulate_iptables_protocol unblock DNS &&
        log -deb "$fn_name - iptables unblock DNS - Success" ||
        raise "FAIL: Could not unblock DNS traffic: manipulate_iptables_protocol unblock DNS" -l "$fn_name" -ds

    manipulate_iptables_protocol unblock SSL &&
        log -deb "$fn_name - iptables unblock SSL - Success" ||
        raise "FAIL: Could not unblock SSL traffic: manipulate_iptables_protocol unblock SSL" -l "$fn_name" -ds

    # This needs to execute before we start the managers. Flow is essential.
    if [ "$cm2_is_gw" == "true" ]; then
        add_bridge_interface br-wan "$cm2_if_name" &&
            log -deb "$fn_name - interface '$cm2_if_name' added to bridge 'br-wan' - Success" ||
            raise "FAIL: Could not add interface to br-wan bridge: add_bridge_interface br-wan $cm2_if_name" -l "$fn_name" -ds
    fi

    start_specific_manager cm -v &&
        log -deb "$fn_name - start_specific_manager cm - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager cm" -l "$fn_name" -ds

    start_specific_manager nm &&
        log -deb "$fn_name - start_specific_manager nm - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager nm" -l "$fn_name" -ds

    check_kconfig_option "CONFIG_MANAGER_WANO" "y"
    if [ $? -eq 0 ]; then
        start_specific_manager wano &&
            log -deb "$fn_name - start_specific_manager wano - Success" ||
            raise "FAIL: Could not start manager: start_if_specific_manager wano" -l "$fn_name" -ds
    fi

    empty_ovsdb_table AW_Debug &&
        log -deb "$fn_name - AW_Debug table emptied - Success" ||
        raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "$fn_name" -ds

    set_manager_log CM TRACE &&
        log -deb "$fn_name - Manager log for CM set to TRACE - Success" ||
        raise "FAIL: Could not set manager log severity: set_manager_log CM TRACE" -l "$fn_name" -ds

    set_manager_log NM TRACE &&
        log -deb "$fn_name - Manager log for NM set to TRACE - Success" ||
        raise "FAIL: Could not set manager log severity: set_manager_log NM TRACE" -l "$fn_name" -ds

    if [ "$cm2_is_gw" == "true" ]; then
        wait_for_function_response 0 "check_default_route_gw" &&
            log -deb "$fn_name - Default GW added to routes - Success" ||
            raise "FAIL: Default GW not added to routes" -l "$fn_name" -ds
    fi

    log "$fn_name - CM setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### ROUTE SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function checks if default gateway route exists.
#   Function uses route tool. Must be installed on device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Default route exists.
#   1   Default route does not exist.
# USAGE EXAMPLE(S):
#   check_default_route_gw
###############################################################################
check_default_route_gw()
{
    default_gw=$(route -n | tr -s ' ' | grep -i UG | awk '{printf $2}';)
    if [ -z "$default_gw" ]; then
        return 1
    else
        return 0
    fi
}

####################### ROUTE SECTION - STOP ##################################

####################### LINKS SECTION - START #################################

####################### LINKS SECTION - STOP ##################################


####################### TEST CASE SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function manipulates traffic by protocol using iptables.
#   Adds (inserts) or removes (deletes) rules to OUTPUT chain.
#   Can block traffic by using block option.
#   Can unblock traffic by using unblock option.
#   Supports traffic types:
#       - DNS
#       - SSL
#   Raises exception if rule cannot be applied.
# INPUT PARAMETER(S):
#   $1  option, block or unblock traffic (required)
#   $2  traffic type (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   manipulate_iptables_protocol unblock SSL
#   manipulate_iptables_protocol unblock DNS
###############################################################################
manipulate_iptables_protocol()
{
    fn_name="cm2_lib:manipulate_iptables_protocol"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    option=$1
    traffic_type=$2

    log -deb "$fn_name - $option $traffic_type traffic"

    if [ "$option" == "block" ]; then
        iptable_option='I'
        exit_code=0
    elif [ "$option" == "unblock" ]; then
        iptable_option='D'
        # Waiting for exit code 1 if multiple iptables rules are inserted - safer way
        exit_code=1
    else
        raise "FAIL: Wrong option, given:$option, supported: block, unblock" -l "$fn_name" -arg
    fi

    if [ "$traffic_type" == "DNS" ]; then
        traffic_port="53"
        traffic_port_type="udp"
    elif [ "$traffic_type" == "SSL" ]; then
        traffic_port="443"
        traffic_port_type="tcp"
    else
        raise "FAIL: Wrong traffic_type, given:$option, supported: DNS, SSL" -l "$fn_name" -arg
    fi

    $(iptables -S | grep -q "OUTPUT -p $traffic_port_type -m $traffic_port_type --dport $traffic_port -j DROP")
    # Add rule if not already an identical one in table, but unblock always
    if [ "$?" -ne 0 ] || [ "$option" == "unblock" ]; then
        wait_for_function_response $exit_code "iptables -$iptable_option OUTPUT -p $traffic_port_type --dport $traffic_port -j DROP" &&
            log -deb "$fn_name - $traffic_type traffic ${option}ed" ||
            raise "FAIL: Could not $option $traffic_type traffic" -l "$fn_name" -nf
    else
        log "$fn_name - Add failure: Rule already in chain"
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function manipulates traffic by source address using iptables.
#   Adds (inserts) or removes (deletes) rules to OUTPUT chain.
#   Can block traffic by using block option.
#   Can unblock traffic by using unblock option.
#   Raises exception is rule cannot be applied.
# INPUT PARAMETER(S):
#   $1  option, block or unblock traffic
#   $2  source address to be blocked
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   manipulate_iptables_address block 192.168.200.10
###############################################################################
manipulate_iptables_address()
{
    fn_name="cm2_lib:manipulate_iptables_address"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    option=$1
    address=$2

    log -deb "$fn_name - $option $address internet"

    if [ "$option" == "block" ]; then
        iptable_option='I'
        exit_code=0
    elif [ "$option" == "unblock" ]; then
        iptable_option='D'
        # Waiting for exit code 1 if multiple iptables rules are inserted - safer way
        exit_code=1
    else
        raise "FAIL: Wrong option, given:$option, supported: block, unblock" -l "$fn_name" -arg
    fi

    $(iptables -S | grep -q "OUTPUT -s $address -j DROP")
    # Add rule if not already an identical one in table, but unblock always
    if [ "$?" -ne 0 ] || [ "$option" == "unblock" ]; then
        wait_for_function_response $exit_code "iptables -$iptable_option OUTPUT -s $address -j DROP" &&
            log -deb "$fn_name - internet ${option}ed" ||
            raise "FAIL: Could not $option internet" -l "$fn_name" -nf
    else
        log "$fn_name - Add failure: Rule already in chain"
    fi

    return 0
}

####################### TEST CASE SECTION - STOP ##############################
