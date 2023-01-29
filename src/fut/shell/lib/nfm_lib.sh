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
export FUT_NFM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/nfm_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common NetFilter Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for NFM tests.
#   Raises exception on fail in any of its steps.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   nfm_setup_test_environment
###############################################################################
nfm_setup_test_environment()
{
    log -deb "nfm_lib:nfm_setup_test_environment - Running NFM setup"

    device_init &&
        log -deb "nfm_lib:nfm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "nfm_lib:nfm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "nfm_lib:nfm_setup_test_environment - Open Switch started - Success" ||
        raise "FAIL: Could not start Open Switch: start_openswitch" -l "nfm_lib:nfm_setup_test_environment" -ds

    restart_managers
        log -deb "nfm_lib:nfm_setup_test_environment: Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "nfm_lib:nfm_setup_test_environment - AW_Debug table emptied - Success"  ||
        raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "nfm_lib:nfm_setup_test_environment" -ds

    set_manager_log NFM TRACE &&
        log -deb "nfm_lib:nfm_setup_test_environment - Manager log for NFM set to TRACE - Success"||
        raise "FAIL: Could not set manager log severity: set_manager_log NFM TRACE" -l "nfm_lib:nfm_setup_test_environment" -ds

    log -deb "nfm_lib:nfm_setup_test_environment - NFM setup - end"

    return 0
}

####################### SETUP SECTION - STOP ##################################

###############################################################################
# DESCRIPTION:
#   Function checks if the given ebtables rule is configured on the device.
#   Raise exception it the rule is not configured.
# INPUT PARAMETER(S):
#   \$1 (table_name)      : table to use (filter, nat or broute)                         : (string)(required)
#   \$2 (chain_name)      : chain to use (eg. INPUT, FORWARD etc.)                       : (string)(required)
#   \$3 (ebtable_rule)    : condition to be matched                                      : (string)(required)
#   \$4 (ebtable_target)  : action to take when the rule match (ACCEPT, DROP, CONTINUE)  : (string)(required)
# RETURNS:
#   0 If the ebtables rule is configured.
#   See DESCRIPTION
# USAGE EXAMPLE(S):
#   is_ebtables_rule_configured "filter" "INPUT" "-d 11:11:11:11:11:11" "DROP"
###############################################################################
is_ebtables_rule_configured()
{
    local NARGS=4
    [ $# -ne ${NARGS} ] &&
        raise "nfm_lib:is_ebtables_rule_configured requires ${NARGS} input argument(s), $# given" -arg

    table_name="${1}"
    chain_name="${2}"
    ebtable_rule="${3}"
    ebtable_target="${4}"

    wait_for_function_response 0 "ebtables -t $table_name -L $chain_name | grep "${ebtable_target}" | grep -e \"$ebtable_rule\" " 10 &&
        log "nfm_lib/is_ebtables_rule_configured: ebtables rule \"$ebtable_rule\" configured on the device " ||
        raise "FAIL: ebtables rule \"$ebtable_rule\" not configured on the device" -l "nfm_lib/is_ebtables_rule_configured" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if the given ebtables rule is removed from the device.
#   Raise exception it the rule is present and not removed.
# INPUT PARAMETER(S):
#   \$1 (table_name)      : table to use (filter, nat or broute)                         : (string)(required)
#   \$2 (chain_name)      : chain to use (eg. INPUT, FORWARD etc.)                       : (string)(required)
#   \$3 (ebtable_rule)    : condition to be matched                                      : (string)(required)
#   \$4 (ebtable_target)  : action to take when the rule match (ACCEPT, DROP, CONTINUE)  : (string)(required)
# RETURNS:
#   0 If the rule is removed from the device
#   See DESCRIPTION
# USAGE EXAMPLE(S):
#   is_ebtables_rule_removed "filter" "INPUT" "-d 11:11:11:11:11:11" "DROP"
###############################################################################
is_ebtables_rule_removed()
{
    local NARGS=4
    [ $# -ne ${NARGS} ] &&
        raise "nfm_lib:is_ebtables_rule_removed requires ${NARGS} input argument(s), $# given" -arg

    table_name="${1}"
    chain_name="${2}"
    ebtable_rule="${3}"
    ebtable_target="${4}"

    wait_for_function_response 1 "ebtables -t $table_name -L $chain_name | grep "${ebtable_target}" | grep -e \"$ebtable_rule\" " 10 &&
        log "nfm_lib/is_ebtables_rule_removed: ebtables rule \"$ebtable_rule\" removed from the device " ||
        raise "FAIL: ebtables rule \"$ebtable_rule\" not removed from the device" -l "nfm_lib/is_ebtables_rule_configured" -tc

    return 0
}

###############################################################################
