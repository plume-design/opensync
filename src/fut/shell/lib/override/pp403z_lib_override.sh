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
#   PP403Z libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/pp403z_lib_override.sh sourced"

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
    log -deb "pp403z_lib_override:disable_watchdog - Disabling watchdog"
    ${OPENSYNC_ROOTDIR}/bin/wpd --set-auto
    sleep 1
    # shellcheck disable=SC2034
    PID=$(pidof wpd) || raise "wpd not running" -l "pp403z_lib_override:disable_watchdog" -ds
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
        log -deb "pp403z_lib_override:device_init - Watchdog disabled - Success" ||
        raise "FAIL: disable_watchdog - Could not disable watchdog" -l "pp403z_lib_override:device_init" -ds

    stop_managers &&
        log -deb "pp403z_lib_override:device_init - Managers stopped - Success" ||
        raise "FAIL: stop_managers - Could not stop managers" -l "pp403z_lib_override:device_init" -ds

    stop_healthcheck &&
        log -deb "pp403z_lib_override:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: stop_healthcheck - Could not stop healthcheck" -l "pp403z_lib_override:device_init" -ds

    disable_fatal_state_cm &&
        log -deb "pp403z_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: disable_fatal_state_cm - Could not disable CM fatal state" -l "pp403z_lib_override:device_init" -ds

    return $?
}

####################### UNIT OVERRIDE SECTION - STOP ##########################
