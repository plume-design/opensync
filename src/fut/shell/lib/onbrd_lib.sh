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
#   Function prepares device for ONBRD tests. If called with parameters it waits
#   for radio interfaces in Wifi_Radio_State table.
#   Calling it without radio interface names, it skips the step checking the interfaces.
#   Raises exception on fail in any of its steps.
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
    log "onbrd_lib:onbrd_setup_test_environment - Running ONBRD setup"

    device_init &&
        log -deb "onbrd_lib:onbrd_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "onbrd_lib:onbrd_setup_test_environment" -ds

    start_openswitch &&
        log -deb "onbrd_lib:onbrd_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "onbrd_lib:onbrd_setup_test_environment" -ds

    restart_managers
    log -deb "onbrd_lib:onbrd_setup_test_environment: Executed restart_managers, exit code: $?"

    # Check if radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" &&
            log -deb "onbrd_lib:onbrd_setup_test_environment - Wifi_Radio_State::if_name '$if_name' present - Success" ||
            raise "FAIL: Wifi_Radio_State::if_name for '$if_name' does not exist" -l "onbrd_lib:onbrd_setup_test_environment" -ds
    done

    log -deb "onbrd_lib:onbrd_setup_test_environment - ONBRD setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### TEST CASE SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function echoes number of radio interfaces present in Wifi_Radio_State table.
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
#   $1  number of expected radios (int, required)
# RETURNS:
#   0   Number of radios is as expected.
#   1   Number of radios is not as expected.
# USAGE EXAMPLE(S):
#   check_number_of_radios 3
###############################################################################
check_number_of_radios()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "onbrd_lib:check_number_of_radios requires ${NARGS} input argument(s), $# given" -arg
    num_of_radios_1=$1
    num_of_radios_2=$(get_number_of_radios)

    log -deb "onbrd_lib:check_number_of_radios - Number of radios is $num_of_radios_2"

    if [ "$num_of_radios_1" = "$num_of_radios_2" ]; then
        return 0
    else
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if inet_addr at OS - LEVEL2 is the same as
#   in test case config.
# INPUT PARAMETER(S):
#   $1  Bridge interface name (string, required)
#   $2  Expected WAN IP (string, required)
# RETURNS:
#   0   IP is as expected.
#   1   WAN bridge has no IP assigned or IP not equal to OS LEVEL2 IP address.
# USAGE EXAMPLE(S):
#   verify_wan_ip_l2 br-wan 192.168.200.10
###############################################################################
verify_wan_ip_l2()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "onbrd_lib:verify_wan_ip_l2 requires ${NARGS} input argument(s), $# given" -arg
    br_wan=$1
    inet_addr_in=$2

    # LEVEL2
    inet_addr=$(ifconfig "$br_wan" | grep 'inet addr' | awk '/t addr:/{gsub(/.*:/,"",$2); print $2}')
    if [ -z "$inet_addr" ]; then
        log -deb "onbrd_lib:verify_wan_ip_l2 - inet_addr is empty"
        return 1
    fi

    if [ "$inet_addr_in" = "$inet_addr" ]; then
        log -deb "onbrd_lib:verify_wan_ip_l2 - OVSDB inet_addr '$inet_addr_in' equals LEVEL2 inet_addr '$inet_addr' - Success"
        return 0
    else
        log -deb "onbrd_lib:verify_wan_ip_l2 - FAIL: OVSDB inet_addr '$inet_addr_in' not equal to LEVEL2 inet_addr '$inet_addr'"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function creates patch interface on bridge.
# INPUT PARAMETER(S):
#   $1  Bridge name (string, required)
#   $2  Patch interface name (string, required)
#   $3  Patch interface name (string, required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   create_patch_interface br-wan patch-w2h patch-h2w
###############################################################################
create_patch_interface()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "onbrd_lib:create_patch_interface requires ${NARGS} input argument(s), $# given" -arg
    br_wan=$1
    patch_w2h=$2
    patch_h2w=$3

    num1=$(ovs-vsctl show | grep "$patch_w2h" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num1" -gt 0 ]; then
        # Add WAN-to-HOME patch port
        log -deb "onbrd_lib:create_patch_interface - '$patch_w2h' patch exists"
    else
        # Add WAN-to-HOME patch port
        log -deb "onbrd_lib:create_patch_interface - Adding '$patch_w2h' to patch port"
        add_bridge_port "$br_wan" "$patch_w2h"
        set_interface_patch "$br_wan" "$patch_w2h" "$patch_h2w"
    fi

    num2=$(ovs-vsctl show | grep "$patch_h2w" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num2" -gt 0 ]; then
        # Add HOME-to-WAN patch port
        log -deb "onbrd_lib:create_patch_interface - '$patch_h2w' patch exists"
    else
        # Add HOME-to-WAN patch port
        log -deb "onbrd_lib:create_patch_interface - Adding '$patch_h2w' to patch port"
        add_bridge_port "$br_wan" "$patch_h2w"
        set_interface_patch "$br_wan" "$patch_h2w" "$patch_w2h"
    fi

    ovs-vsctl show

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if patch exists.
#   Function uses ovs-vsctl command, different from native Linux bridge.
# INPUT PARAMETER(S):
#   $1  patch name (string, required)
# RETURNS:
#   0   Patch exists.
#   1   Patch does not exist.
# USAGE EXAMPLE(S):
#   check_if_patch_exists patch-h2w
#   check_if_patch_exists patch-w2h
###############################################################################
check_if_patch_exists()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "onbrd_lib:check_if_patch_exists requires ${NARGS} input argument(s), $# given" -arg
    patch=$1

    num=$(ovs-vsctl show | grep "$patch" | grep Interface | awk '{print $2}' | wc -l)
    if [ "$num" -gt 0 ]; then
        log -deb "onbrd_lib:check_if_patch_exists - '$patch' interface exists"
        return 0
    else
        log -deb "onbrd_lib:check_if_patch_exists - '$patch' interface does not exist"
        return 1
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if provided firmware version string is a valid pattern.
#   Raises an exception if firmware version string has invalid pattern.
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
#   $1  FW version (string, required)
# RETURNS:
#   0   Firmware version string is valid
#   See DESCRIPTION.
#   Function will send an exit singnal upon error, use subprocess to avoid this
# USAGE EXAMPLE(S):
#   verify_fw_pattern 3.0.0-29-g100a068-dev-debug
#   verify_fw_pattern 2.0.2.0-70-gae540fd-dev-academy
###############################################################################
verify_fw_pattern()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "onbrd_lib:verify_fw_pattern requires ${NARGS} input argument(s), $# given" -arg
    fw_version="${1}"

    [ -n "${fw_version}" ] ||
        raise "FAIL: Firmware version string '${fw_version}' is empty!" -l "onbrd_lib:verify_fw_pattern"

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
            raise "FAIL: Build number '${build_number}' must contain 1-6 numerals, not ${#build_number}" -l "onbrd_lib:verify_fw_pattern"
        echo ${build_number} | grep -E "^[0-9]*$" ||
            raise "FAIL: Build number '${build_number}' contains non numeral characters!" -l "onbrd_lib:verify_fw_pattern"
    fi

    # Verify the version segment before splitting
    [ -n "${fw_segment_0}" ] ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' is empty!" -l "onbrd_lib:verify_fw_pattern"
    echo "${fw_segment_0}" | grep -E "^[0-9.]*$" ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' contains invalid characters!" -l "onbrd_lib:verify_fw_pattern"
    # At least major and minor versions are needed, so one dot "." is required
    echo "${fw_segment_0}" | grep [.] ||
        raise "FAIL: Firmware version segment '${fw_segment_0}' does not contain the delimiter '.'" -l "onbrd_lib:verify_fw_pattern"
    ### Split by delimiter '.' to get version segments
    ver_major="$(echo "$fw_segment_0" | cut -d'.' -f1)"
    ver_minor="$(echo "$fw_segment_0" | cut -d'.' -f2)"
    ver_micro="$(echo "$fw_segment_0" | cut -d'.' -f3)"
    ver_nano="$(echo "$fw_segment_0" | cut -d'.' -f4)"
    ver_overflow="$(echo "$fw_segment_0" | cut -d'.' -f5-)"
    # Allow 2 to 4 elements, else fail
    [ -n "${ver_major}" ] ||
        raise "FAIL: Major version ${ver_major} is empty!" -l "onbrd_lib:verify_fw_pattern"
    [ -n "${ver_minor}" ] ||
        raise "FAIL: Minor version ${ver_minor} is empty!" -l "onbrd_lib:verify_fw_pattern"
    [ -z "${ver_overflow}" ] ||
        raise "FAIL: Firmware version ${fw_segment_0} has too many segments (2-4), overflow: '${ver_overflow}'" -l "onbrd_lib:verify_fw_pattern"
    # Non-empty segments must have 1-4 numerals
    [ ${#ver_major} -ge 1 ] && [ ${#ver_major} -le 3 ] ||
        raise "FAIL: Major version '${ver_major}' must contain 1-3 numerals, not ${#ver_major}" -l "onbrd_lib:verify_fw_pattern"
    [ ${#ver_minor} -ge 1 ] && [ ${#ver_minor} -le 3 ] ||
        raise "FAIL: Minor version '${ver_minor}' must contain 1-3 numerals, not ${#ver_minor}" -l "onbrd_lib:verify_fw_pattern"
    if [ -n "${ver_micro}" ]; then
        [ ${#ver_micro} -ge 1 ] && [ ${#ver_micro} -le 3 ] ||
            raise "FAIL: Micro version '${ver_micro}' must contain 1-3 numerals, not ${#ver_micro}" -l "onbrd_lib:verify_fw_pattern"
    fi
    if [ -n "${ver_nano}" ]; then
        [ ${#ver_nano} -ge 1 ] && [ ${#ver_nano} -le 3 ] ||
            raise "FAIL: Nano version '${ver_nano}' must contain 1-3 numerals, not ${#ver_nano}" -l "onbrd_lib:verify_fw_pattern"
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function compares CN (Common Name) of the certificate to
#   several parameters:
#       - device model string,
#       - device id,
#       - WAN eth port MAC address.
#   NOTE: CN verification is optional, this function just echoes these
#         parameters. If the validation should be required, please overload the
#         function in the device shell library overload file.
# INPUT PARAMETER(S):
#   $1  Common Name stored in the certificate (string, required)
#   $2  Device model string (string, optional)
#   $3  Device id (string, optional)
#   $4  MAC address of device WAN eth port (string, optional)
# RETURNS:
#   0   Always.
# USAGE EXAMPLE(S):
#   verify_certificate_cn 1A2B3C4D5E6F 1A2B3C4D5E6F PP203X 00904C324057
###############################################################################
verify_certificate_cn()
{
    local NARGS=1
    [ $# -lt ${NARGS} ] &&
        raise "onbrd_lib:verify_certificate_cn requires at least ${NARGS} input argument(s), $# given" -arg

    local comm_name=${1}
    echo "Common Name of the certificate: $comm_name"
    local device_model=${2}
    echo "Device model: $device_model"
    local device_id=${3}
    echo "Device ID: $device_id"
    local wan_eth_mac=${4}
    echo "WAN eth port MAC address: $wan_eth_mac"

    return 0
}

####################### TEST CASE SECTION - STOP ##############################
