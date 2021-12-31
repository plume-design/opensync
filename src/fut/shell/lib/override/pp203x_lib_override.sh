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


####################### INFORMATION SECTION - START ###########################
#
#   PP203X libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/pp203x_lib_override.sh sourced"

####################### UNIT OVERRIDE SECTION - START #########################

###############################################################################
# DESCRIPTION:
#   Function disables watchdog.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   disable_watchdog
###############################################################################
disable_watchdog()
{
    log -deb "pp203x_lib_override:disable_watchdog - Disabling watchdog"
    ${OPENSYNC_ROOTDIR}/bin/wpd --set-auto
    sleep 1
    # shellcheck disable=SC2034
    PID=$(pidof wpd) || raise "wpd not running" -l "pp203x_lib_override:disable_watchdog" -ds
}

###############################################################################
# DESCRIPTION:
#   Function stops healthcheck process and disables it.
#   Checks if healthcheck already stopped, does nothing if already stopped.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   1   healthcheck process is not stopped.
#   0   healthcheck process is stopped.
# USAGE EXAMPLE(S):
#   stop_healthcheck
###############################################################################
stop_healthcheck()
{
    if [ -n "$(get_pid "healthcheck")" ]; then
        log -deb "pp203x_lib_override:stop_healthcheck - Disabling healthcheck."
        /etc/init.d/healthcheck stop || true

        log -deb "pp203x_lib_override:stop_healthcheck - Check if healthcheck is disabled"
        wait_for_function_response 1 "$(get_process_cmd) | grep -e 'healthcheck' | grep -v 'grep'"
        if [ "$?" -ne 0 ]; then
            log -deb "pp203x_lib_override:stop_healthcheck - Healthcheck is not disabled ! PID: $(get_pid "healthcheck")"
            return 1
        else
            log -deb "pp203x_lib_override:stop_healthcheck - Healthcheck is disabled."
        fi
    else
        log -deb "pp203x_lib_override:stop_healthcheck - Healthcheck is already disabled."
    fi
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function initializes device for use in FUT.
#   It disables watchdog to prevent the device from rebooting.
#   It stops healthcheck service to prevent the device from rebooting.
#   It calls a function that instructs CM to prevent the device from rebooting.
#   It stops all managers.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   device_init
###############################################################################
device_init()
{
    disable_watchdog &&
        log -deb "pp203x_lib_override:device_init - Watchdog disabled - Success" ||
        raise "FAIL: device_init - Could not disable watchdog" -l "pp203x_lib_override:device_init" -ds

    stop_managers &&
        log -deb "pp203x_lib_override:device_init - Managers stopped - Success" ||
        raise "FAIL: stop_managers - Could not stop managers" -l "pp203x_lib_override:device_init" -ds

    stop_healthcheck &&
        log -deb "pp203x_lib_override:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: stop_healthcheck - Could not stop healthcheck" -l "pp203x_lib_override:device_init" -ds

    cm_disable_fatal_state &&
        log -deb "pp203x_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: cm_disable_fatal_state - Could not disable CM fatal state" -l "pp203x_lib_override:device_init" -ds

    return $?
}

####################### UNIT OVERRIDE SECTION - STOP ##########################

####################### CERTIFICATE OVERRIDE SECTION - START ##################

###############################################################################
# DESCRIPTION:
#   Function compares CN(Common Name) of the certificate to several parameters:
#   device model string, device id, WAN eth port MAC address.
#   NOTE: CN verification is optional, this function just echoes these
#         parameters. If the validation should be required, please overload the
#         function in the device shell library overload file.
# INPUT PARAMETER(S):
#   $1  Common Name stored in the certificate (string, required)
#   $2  Device model string (string, optional)
#   $3  Device id (string, optional)
#   $4  MAC address of device WAN eth port (string, optional)
# RETURNS:
#   0   CN matches any input parameter
#   1   CN mismatches all input parameters
# USAGE EXAMPLE(S):
#   verify_certificate_cn 1A2B3C4D5E6F 1A2B3C4D5E6F PP203X 00904C324057
###############################################################################
verify_certificate_cn()
{
    local NARGS=1
    [ $# -lt ${NARGS} ] &&
        raise "pp203x_lib_override:verify_certificate_cn requires at least ${NARGS} input argument(s), $# given" -arg
    comm_name=${1}
    device_model=${2}
    device_id=${3}
    wan_eth_mac_string=$(echo "$4" | sed -e 's/://g' | tr '[:lower:]' '[:upper:]')

    [ "$comm_name" = "$device_model" ] &&
        echo "Common Name of certificate: $comm_name matches device model: $device_model" && return 0
    [ "$comm_name" = "$device_id" ] &&
        echo "Common Name of certificate: $comm_name matches device ID: $device_id" && return 0
    [ "$comm_name" = "$wan_eth_mac_string" ] &&
        echo "Common Name of certificate: $comm_name matches device WAN eth MAC address: $wan_eth_mac_string" && return 0

    return 1
}

####################### CERTIFICATE OVERRIDE SECTION - STOP ###################
