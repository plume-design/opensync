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
export FUT_LTEM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/ltem_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Long-Term Evolution Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for LTEM tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  lte_if_name: Lte Interface name (string, required)
#   $2  access_point_name: access point name of the SIM card used (string, required)
#   $3  os_persist: os_persist (bool, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   ltem_setup_test_environment wwan0 data.icore.name true
#   ltem_setup_test_environment wwan0 data.icore.name false
###############################################################################
ltem_setup_test_environment()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "ltem_lib:ltem_setup_test_environment requires ${NARGS} input argument(s), $# given" -arg
    lte_if_name=${1}
    access_point_name=${2}
    os_persist=${3}

    log -deb "ltem_lib:ltem_setup_test_environment - Running LTEM setup"

    device_init &&
        log -deb "ltem_lib:ltem_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "ltem_lib:ltem_setup_test_environment" -ds

    start_openswitch &&
        log -deb "ltem_lib:ltem_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "ltem_lib:ltem_setup_test_environment" -ds

    restart_managers
    log -deb "ltem_lib:ltem_setup_test_environment: Executed restart_managers, exit code: $?"

    check_ovsdb_table_exist Lte_Config &&
        log -deb "ltem_lib:ltem_setup_test_environment - Lte_Config table exists in OVSDB - Success" ||
        raise "FAIL: Lte_Config table does not exist in OVSDB" -l "ltem_lib:ltem_setup_test_environment" -s

    check_field=$(${OVSH} s Lte_Config -w if_name==$lte_if_name)
    if [ -z "$check_field" ]; then
        insert_ovsdb_entry Lte_Config -w if_name "$lte_if_name" -i if_name "$lte_if_name" \
            -i manager_enable "true" \
            -i lte_failover_enable "true" \
            -i ipv4_enable "true" \
            -i modem_enable "true" \
            -i force_use_lte "false" \
            -i apn "$access_point_name" \
            -i report_interval "60" \
            -i active_simcard_slot "0" \
            -i os_persist "$os_persist" &&
                log -deb "ltem_lib:ltem_setup_test_environment - Lte_Config::lte interface $lte_if_name was inserted - Success" ||
                raise "FAIL: Lte_Config::lte interface $lte_if_name did not inserted" -l "ltem_lib:ltem_setup_test_environment" -ds
    else
        raise "SKIP: Lte_Config:: Entry for $lte_if_name interface exists in the table" -l "ltem_lib:ltem_setup_test_environment" -s
    fi

    check_field=$(${OVSH} s Wifi_Inet_Config -w if_name==$lte_if_name)
    if [ -z "$check_field" ]; then
        insert_ovsdb_entry Wifi_Inet_Config -w if_name "$lte_if_name" -i if_name "$lte_if_name" \
            -i if_type "lte" \
            -i ip_assign_scheme "dhcp" \
            -i enabled "true" \
            -i network "true" \
            -i NAT "true" \
            -i os_persist "$os_persist" &&
                log -deb "ltem_lib:ltem_setup_test_environment - Insert entry for $lte_if_name interface in Wifi_Inet_Config - Success" ||
                raise "FAIL: Insert was not done for the entry of $lte_if_name interface in Wifi_Inet_Config " -l "ltem_lib:ltem_setup_test_environment" -ds
    else
        log -deb "ltem_lib:ltem_setup_test_environment - Entry already exists, skipping..."
    fi

    log -deb "ltem_lib:ltem_setup_test_environment - LTEM setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### ROUTE ENTRY CHECK SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function checks if default gateway route exists for LTE.
#   Function uses route tool. Must be installed on device.
# INPUT PARAMETER(S):
#   $1  Lte Interface name (string, required)
#   $2  metric value (int, required)
#   $3  route tool path (string, required)
# RETURNS:
#   0   Default route exists.
#   1   Default route does not exist / tool not present on device.
# USAGE EXAMPLE(S):
#   check_default_route_gw wwan0 100 /sbin/route
###############################################################################
check_default_lte_route_gw()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "ltem_lib:check_default_lte_route_gw requires ${NARGS} input argument(s), $# given" -arg
    if_name=${1}
    metric=${2}
    tool_path=${3}

    is_tool_on_system "${tool_path}" ||
        raise "ltem_lib:ltem_setup_test_environment - Tool '${tool_path}' could not be found on the device" -nf
    default_gw=$(${tool_path} -n | grep -i $if_name | grep -i 'UG' | awk '{print $5}' | grep -i $metric;)
    if [ -z "$default_gw" ]; then
        return 1
    else
        return 0
    fi
}

####################### ROUTE ENTRY CHECK SECTION - STOP ##################################
