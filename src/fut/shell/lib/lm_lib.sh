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
export FUT_LM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/lm_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Log Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for LM tests. Executes device_init to prevent
#   the device from rebooting unintentionally and to stop managers. Executes
#   start_openswitch to manually start OVS and OVSDB. Starts either LM or PM_LM
#   depending on Kconfig value CONFIG_MANAGER_LM or CONFIG_PM_ENABLE_LM.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   lm_setup_test_environment
###############################################################################
lm_setup_test_environment()
{
    fn_name="lm_lib:lm_setup_test_environment"

    log "$fn_name - Running LM setup"

    device_init &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    check_kconfig_option "CONFIG_MANAGER_LM" "y"
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Log Manager is standalone - lm"
        start_specific_manager lm &&
            log -deb "$fn_name - start_specific_manager lm - Success" ||
            raise "FAIL: Could not start manager: start_if_specific_manager lm" -l "$fn_name" -ds
    else
        check_kconfig_option "CONFIG_PM_ENABLE_LM" "y"
        if [ $? -eq 0 ]; then
            log -deb "$fn_name - Log Manager is a module of Platform Manager - pm"
            start_specific_manager pm &&
                log -deb "$fn_name - start_specific_manager pm - Success" ||
                raise "FAIL: Could not start manager: start_if_specific_manager pm" -l "$fn_name" -ds
        else
            raise "FAIL: Log Manager is not supported" -l "$fn_name" -ds
        fi
    fi

    log "$fn_name - LM setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### TEST CASE SECTION - START #############################

####################### TEST CASE SECTION - STOP ##############################
