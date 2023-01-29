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
export FUT_VPNM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/vpnm_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of Virtual Private Network Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for VPNM tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   vpnm_setup_test_environment
###############################################################################
vpnm_setup_test_environment()
{
    log "vpnm_lib:vpnm_setup_test_environment - Running VPNM setup"

    device_init &&
        log -deb "vpnm_lib:vpnm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "vpnm_lib:vpnm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "vpnm_lib:vpnm_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "vpnm_lib:vpnm_setup_test_environment" -ds

    restart_managers
        log -deb "vpnm_lib:vpnm_setup_test_environment - Executed restart_managers, exit code: $?"

    check_manager_alive "${OPENSYNC_ROOTDIR}/bin/vpnm" &&
        log -deb "vpnm_lib:vpnm_setup_test_environment - VPNM is running - Success" ||
        raise "FAIL: VPNM not running/crashed" -l "vpnm_lib:vpnm_setup_test_environment" -ds

    log -deb "vpnm_lib:vpnm_setup_test_environment - VPNM setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################
