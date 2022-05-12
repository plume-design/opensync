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
#   Raises exception on fail in any of its steps.
# INPUT PARAMETER(S):
#   $1  wan_eth_if_name: uplink ethernet interface name (string, optional)
#   $2  wan_bridge_if_name: WAN bridge interface name (string, optional)
# RETURNS:
#   0   On success.
#   See description.
# USAGE EXAMPLE(S):
#   cm_setup_test_environment
#   cm_setup_test_environment eth0 br-wan
###############################################################################
cm_setup_test_environment()
{
    wan_eth_if_name=${1}
    wan_bridge_if_name=${2}
    use_fut_cloud=${3:-"false"}

    check_kconfig_option "CONFIG_MANAGER_WANO" "y" && is_wano=true || is_wano=false

    log -deb "cm2_lib:cm_setup_test_environment - Running CM2 setup"

    device_init &&
        log -deb "cm2_lib:cm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "cm2_lib:cm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "cm2_lib:cm_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "cm2_lib:cm_setup_test_environment" -ds

    manipulate_iptables_protocol unblock DNS &&
        log -deb "cm2_lib:cm_setup_test_environment - iptables unblock DNS - Success" ||
        raise "FAIL: manipulate_iptables_protocol unblock DNS - Could not unblock DNS traffic" -l "cm2_lib:cm_setup_test_environment" -ds

    manipulate_iptables_protocol unblock SSL &&
        log -deb "cm2_lib:cm_setup_test_environment - iptables unblock SSL - Success" ||
        raise "FAIL: Could not unblock SSL traffic: manipulate_iptables_protocol unblock SSL" -l "cm2_lib:cm_setup_test_environment" -ds

    # Legacy procedure requires manual adding of WAN ethernet interface into WAN bridge
    if [ -n "${wan_eth_if_name}" ] && [ -n "${wan_bridge_if_name}" ] && [ $is_wano == false ] ; then
        add_interface_to_bridge "${wan_bridge_if_name}" "${wan_eth_if_name}" &&
            log -deb "cm2_lib:cm_setup_test_environment - Interface added to bridge - Success" ||
            raise "FAIL: add_interface_to_bridge $wan_bridge_if_name $wan_eth_if_name - Could not add interface to bridge" -l "cm2_lib:cm_setup_test_environment" -ds
    else
        log -deb "cm2_lib:cm_setup_test_environment - Device does not require adding bridge interface"
        log -deb "Details:\nwan_eth_if_name: ${wan_eth_if_name}\nwan_bridge_if_name: ${wan_bridge_if_name}\nis_wano: ${is_wano}"
    fi

    restart_managers
    log -deb "cm2_lib:cm_setup_test_environment - Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "cm2_lib:cm_setup_test_environment - AW_Debug table emptied - Success" ||
        raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty table:" -l "cm2_lib:cm_setup_test_environment" -ds

    set_manager_log CM TRACE &&
        log -deb "cm2_lib:cm_setup_test_environment - Manager log for CM set to TRACE - Success" ||
        raise "FAIL: set_manager_log CM TRACE - Could not set manager log severity" -l "cm2_lib:cm_setup_test_environment" -ds

    set_manager_log NM TRACE &&
        log -deb "cm2_lib:cm_setup_test_environment - Manager log for NM set to TRACE - Success" ||
        raise "FAIL: set_manager_log NM TRACE - Could not set manager log severity" -l "cm2_lib:cm_setup_test_environment" -ds

    wait_for_function_response 0 "check_default_route_gw" &&
        log -deb "cm2_lib:cm_setup_test_environment - Default GW added to routes - Success" ||
        raise "FAIL: check_default_route_gw - Default GW not added to routes" -l "cm2_lib:cm_setup_test_environment" -ds

    if [ "${use_fut_cloud}" == "true" ]; then
        # Give time for CM to stabilize before connecting to FUT loud.
        # Some kind of race-condition issue
        log -deb "cm2_lib:cm_setup_test_environment: Sleeping for 10s before setting up FUT Cloud connection"
        sleep 10
        connect_to_fut_cloud -ip 5 &&
            log "cm2_lib:cm_setup_test_environment: Device connected to FUT cloud - Success" ||
            raise "FAIL: Failed to connect device to FUT cloud" -l "cm2_lib:cm_setup_test_environment" -ds
    fi

    log -deb "cm2_lib:cm_setup_test_environment - CM2 setup - end"

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
#   Function manipulates traffic by protocol type using iptables.
#   Adds (inserts) or removes (deletes) rules to OUTPUT chain.
#   Can block traffic by using block option.
#   Can unblock traffic by using unblock option.
#   Supports traffic types:
#       - DNS
#       - SSL
#   Supports manipulation types:
#       - block
#       - unblock
#   Raises exception if rule cannot be applied.
# INPUT PARAMETER(S):
#   $1  option, block or unblock traffic (string, required)
#   $2  traffic type (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   manipulate_iptables_protocol unblock SSL
#   manipulate_iptables_protocol unblock DNS
###############################################################################
manipulate_iptables_protocol()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "cm2_lib:manipulate_iptables_protocol requires ${NARGS} input argument(s), $# given" -arg
    option=$1
    traffic_type=$2

    log -deb "cm2_lib:manipulate_iptables_protocol - $option $traffic_type traffic"

    if [ "$option" == "block" ]; then
        iptable_option='I'
        exit_code=0
    elif [ "$option" == "unblock" ]; then
        iptable_option='D'
        # Waiting for exit code 1 if multiple iptables rules are inserted - safer way
        exit_code=1
    else
        raise "FAIL: Wrong option, given:$option, supported: block, unblock" -l "cm2_lib:manipulate_iptables_protocol" -arg
    fi

    if [ "$traffic_type" == "DNS" ]; then
        traffic_port="53"
        traffic_port_type="udp"
    elif [ "$traffic_type" == "SSL" ]; then
        traffic_port="443"
        traffic_port_type="tcp"
    else
        raise "FAIL: Wrong traffic_type, given:$option, supported: DNS, SSL" -l "cm2_lib:manipulate_iptables_protocol" -arg
    fi

    $(iptables -S | grep "OUTPUT -p $traffic_port_type -m $traffic_port_type --dport $traffic_port -j DROP")
    # Add rule if not already an identical one in table, but unblock always
    if [ "$?" -ne 0 ] || [ "$option" == "unblock" ]; then
        wait_for_function_response $exit_code "iptables -$iptable_option OUTPUT -p $traffic_port_type --dport $traffic_port -j DROP" &&
            log -deb "cm2_lib:manipulate_iptables_protocol - $traffic_type traffic ${option}ed - Success" ||
            raise "FAIL: Could not $option $traffic_type traffic" -l "cm2_lib:manipulate_iptables_protocol" -nf
    else
        log -deb "cm2_lib:manipulate_iptables_protocol - Add failure: Rule already in chain?"
    fi

    return 0
}

####################### TEST CASE SECTION - STOP ##############################

###############################################################################
# DESCRIPTION:
#   Function clears the DNS cache.
# STUB:
#   This function is a stub. It always raises an exception and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   If DNS cache on the device was cleared.
# USAGE EXAMPLE(S):
#   clear_dns_cache
###############################################################################
clear_dns_cache()
{
    log "cm2_lib:clear_dns_cache - Clearing DNS cache on the device."
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed." -l "cm2_lib:clear_dns_cache" -fc
}
