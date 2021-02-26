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
export FUT_ONBRD_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh sourced"
####################### INFORMATION SECTION - START ###########################
#
#   Base library of common On-boarding functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for ONBRD tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   onbrd_setup_test_environment
###############################################################################
onbrd_setup_test_environment()
{
    fn_name="onbrd_lib:onbrd_setup_test_environment"

    log "$fn_name - Running ONBRD setup"

    device_init &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    cm_disable_fatal_state &&
        log -deb "$fn_name - Fatal state disabled - Success" ||
        raise "FAIL: Could not disable fatal state: cm_disable_fatal_state" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    restart_managers
    log "${fn_name}: Executed restart_managers, exit code: $?"

    # Check if all radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" &&
            log -deb "$fn_name - Wifi_Radio_State::if_name '$if_name' present - Success" ||
            raise "FAIL: Wifi_Radio_State::if_name for $if_name does not exist" -l "$fn_name" -ds
    done

    log "$fn_name - ONBRD setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### TEST CASE SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function echoes number of radios in Wifi_Radio_State table.
# INPUT PARAMETER(S):
#   None.
# ECHOES:
#   Echoes number of radios.
# USAGE EXAMPLE(S):
#   get_number_of_radios
###############################################################################
get_number_of_radios()
{
    num=$(${OVSH} s Wifi_Radio_State if_name -r | wc -l)
    echo "$num"
}

###############################################################################
# DESCRIPTION:
#   Function checks if number of radios for device is as expected in parameter.
# INPUT PARAMETER(S):
#   $1  number of expected radios (required)
# RETURNS:
#   0   Number of radios is as expected.
#   1   Number of radios is not as expected.
# USAGE EXAMPLE(S):
#   check_number_of_radios 3
###############################################################################
check_number_of_radios()
{
    fn_name="onbrd_lib:check_number_of_radios"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    num_of_radios_1=$1
    num_of_radios_2=$(get_number_of_radios)

    log -deb "$fn_name - Number of radios is $num_of_radios_2"

    if [ "$num_of_radios_1" = "$num_of_radios_2" ]; then
        return 0
    else
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if system (LEVEL2) inet_addr is the same as
#   in test case config.
# INPUT PARAMETER(S):
#   $1  bridge interface name (required)
#   $2  expected WAN IP (required)
# RETURNS:
#   0   IP is as expected.
#   1   bridge has no IP assigned.
# USAGE EXAMPLE(S):
#   verify_wan_ip_l2 br-wan 192.168.200.10
###############################################################################
verify_wan_ip_l2()
{
    fn_name="onbrd_lib:verify_wan_ip_l2"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    br_wan=$1
    inet_addr_in=$2

    # LEVEL2
    inet_addr=$(ifconfig "$br_wan" | grep 'inet addr' | awk '/t addr:/{gsub(/.*:/,"",$2); print $2}')

    if [ -z "$inet_addr" ]; then
        log -deb "$fn_name - inet_addr is empty"
        return 1
    fi

    if [ "$inet_addr_in" = "$inet_addr" ]; then
        log -deb "$fn_name - Success: OVSDB inet_addr '$inet_addr_in' equals LEVEL2 inet_addr '$inet_addr'"
        return 0
    else
        log -deb "$fn_name - FAIL: OVSDB inet_addr '$inet_addr_in' not equal to LEVEL2 inet_addr '$inet_addr'"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function creates patch interface.
# INPUT PARAMETER(S):
#   $1  bridge name (required)
#   $2  patch interface name (required)
#   $3  patch interface name (required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   create_patch_interface br-wan patch-w2h patch-h2w
###############################################################################
create_patch_interface()
{
    fn_name="onbrd_lib:create_patch_interface"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    br_wan=$1
    patch_w2h=$2
    patch_h2w=$3


    num1=$(ovs-vsctl show | grep "$patch_w2h" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num1" -gt 0 ]; then
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - '$patch_w2h' patch exists"
    else
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - Adding '$patch_w2h' to patch port"
        add_bridge_port "$br_wan" "$patch_w2h"
        set_interface_patch "$br_wan" "$patch_w2h" "$patch_h2w"
    fi

    num2=$(ovs-vsctl show | grep "$patch_h2w" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num2" -gt 0 ]; then
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - '$patch_h2w' patch exists"
    else
        # Add WAN-to-HOME patch port
        log -deb "$fn_name - Adding $patch_h2w to patch port"
        add_bridge_port "$br_wan" "$patch_h2w"
        set_interface_patch "$br_wan" "$patch_h2w" "$patch_w2h"
    fi

    ovs-vsctl show
}

###############################################################################
# DESCRIPTION:
#   Function checks if patch exists.
#   Function uses ovs-vsctl command, different from native Linux bridge.
# INPUT PARAMETER(S):
#   $1  patch name (required)
# RETURNS:
#   0   Patch exists.
#   1   Patch does not exist.
# USAGE EXAMPLE(S):
#   check_if_patch_exists patch-h2w
#   check_if_patch_exists patch-w2h
###############################################################################
check_if_patch_exists()
{
    fn_name="onbrd_lib:check_if_patch_exists"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    patch=$1

    num=$(ovs-vsctl show | grep "$patch" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num" -gt 0 ]; then
        log -deb "$fn_name - '$patch' interface exists"
        return 0
    else
        log -deb "$fn_name - '$patch' interface does not exist"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if provided firmware version string is a valid pattern.
# FIELDS OF INTEREST:
#             (optional) build description
#             (optional) nano version    |
#        (required) minor version   |    |
#                               |   |    |
#   For the FW version string 2.0.2.0-70-gae540fd-dev-academy
#                             |   |   |
#      (required) major version   |   |
#          (optional) micro version   |
#               (optional) build number
# INPUT PARAMETER(S):
#   $1  FW version string
# RETURNS:
#   0   Firmware version string is valid
#   1   Firmware version string is not valid
#   Function will send an exit singnal upon error, use subprocess to avoid this
# USAGE EXAMPLE(S):
#   onbrd_verify_fw_pattern 3.0.0-29-g100a068-dev-debug
#   onbrd_verify_fw_pattern 2.0.2.0-70-gae540fd-dev-academy
###############################################################################
onbrd_verify_fw_pattern()
{
    fn_name="onbrd_lib:onbrd_verify_fw_pattern"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    fw_version="${1}"

    [ -n "${fw_version}" ] ||
        raise "Firmware version string '${fw_version}' is empty!" -l "${fn_name}"

    ### Split by delimiter '-' to separate version and build information
    # only three elements are of interest
    fw_segment_0="$(echo "$fw_version" | cut -d'-' -f1)"
    fw_segment_1="$(echo "$fw_version" | cut -d'-' -f2)"
    fw_segment_2="$(echo "$fw_version" | cut -d'-' -f3-)"
    # If delimiter is not present, segment is empty, not equal to previous
    [ "${fw_segment_2}" == "${fw_segment_1}" ] && fw_segment_2=''
    [ "${fw_segment_1}" == "${fw_segment_0}" ] && fw_segment_1=''
    # Determine build number, if present
    build_number="${fw_segment_1}"
    if [ -n "${build_number}" ]; then
        # If not empty, must be integer between 1 and 6 numerals
        [ ${#build_number} -ge 1 ] && [ ${#build_number} -le 6 ] ||
            raise "FAIL: Build number '${build_number}' must contain 1-6 numerals, not ${#build_number}" -l "${fn_name}"
        echo ${build_number} | grep -q -E "^[0-9]*$" ||
            raise "FAIL: Build number '${build_number}' contains non numeral characters!" -l "${fn_name}"
    fi

    # Verify the version segment before splitting
    [ -n "${fw_segment_0}" ] ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' is empty!" -l "${fn_name}"
    echo "${fw_segment_0}" | grep -q -E "^[0-9.]*$" ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' contains invalid characters!" -l "${fn_name}"
    # At least major and minor versions are needed, so one dot "." is required
    echo "${fw_segment_0}" | grep -q [.] ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' does not contain the delimiter '.'" -l "${fn_name}"
    ### Split by delimiter '.' to get version segments
    ver_major="$(echo "$fw_segment_0" | cut -d'.' -f1)"
    ver_minor="$(echo "$fw_segment_0" | cut -d'.' -f2)"
    ver_micro="$(echo "$fw_segment_0" | cut -d'.' -f3)"
    ver_nano="$(echo "$fw_segment_0" | cut -d'.' -f4)"
    ver_overflow="$(echo "$fw_segment_0" | cut -d'.' -f5-)"
    # Allow 2 to 4 elements, else fail
    [ -n "${ver_major}" ] ||
        raise "FAIL: Major version ${ver_major} is empty!" -l "${fn_name}"
    [ -n "${ver_minor}" ] ||
        raise "FAIL: Minor version ${ver_minor} is empty!" -l "${fn_name}"
    [ -z "${ver_overflow}" ] ||
        raise "FAIL: Firmware version ${fw_segment_0} has too many segments (2-4), overflow: '${ver_overflow}'" -l "${fn_name}"
    # Non-empty segments must have 1-4 numerals
    [ ${#ver_major} -ge 1 ] && [ ${#ver_major} -le 3 ] ||
        raise "FAIL: Major version '${ver_major}' must contain 1-4 numerals, not ${#ver_major}" -l "${fn_name}"
    [ ${#ver_minor} -ge 1 ] && [ ${#ver_minor} -le 3 ] ||
        raise "FAIL: Minor version '${ver_minor}' must contain 1-4 numerals, not ${#ver_minor}" -l "${fn_name}"
    if [ -n "${ver_micro}" ]; then
        [ ${#ver_micro} -ge 1 ] && [ ${#ver_micro} -le 3 ] ||
            raise "FAIL: Micro version '${ver_micro}' must contain 1-4 numerals, not ${#ver_micro}" -l "${fn_name}"
    fi
    if [ -n "${ver_nano}" ]; then
        [ ${#ver_nano} -ge 1 ] && [ ${#ver_nano} -le 3 ] ||
            raise "FAIL: Nano version '${ver_nano}' must contain 1-4 numerals, not ${#ver_nano}" -l "${fn_name}"
    fi

    return 0
}

####################### TEST CASE SECTION - STOP ##############################
