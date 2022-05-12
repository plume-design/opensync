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
export FUT_FSM_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/fsm_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Flow Control Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares test environmet for FSM testing.
#   Raises exception on fail in any of its steps.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   fsm_setup_test_environment
###############################################################################
fsm_setup_test_environment()
{
    log -deb "fsm_lib:fsm_setup_test_environment - Running FSM setup"

    device_init  &&
        log -deb "fsm_lib:fsm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "fsm_lib:fsm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "fsm_lib:fsm_setup_test_environment - OpenvSwitch started - Success"  ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "fsm_lib:fsm_setup_test_environment" -ds

    restart_managers
    log -deb "fsm_lib:fsm_setup_test_environment - Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "fsm_lib:fsm_setup_test_environment - AW_Debug table emptied - Success"  ||
        raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty table" -l "fsm_lib:fsm_setup_test_environment" -ds

    set_manager_log FSM TRACE &&
        log -deb "fsm_lib:fsm_setup_test_environment - Manager log for FSM set to TRACE - Success"||
        raise "FAIL: set_manager_log FSM TRACE - Could not set manager log severity" -l "fsm_lib:fsm_setup_test_environment" -ds

    log -deb "fsm_lib:fsm_setup_test_environment - FSM setup - end"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function check if a command is in the path.
#   Raises an exception if not in the path.
# INPUT PARAMETER(S):
#   $1  Command to check (string, required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   check_cmd 'ovsh'
#   check_cmd 'ovs-vsctl'
###############################################################################
check_cmd()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "fsm_lib:check_cmd requires ${NARGS} input arguments, $# given" -arg
    cmd=$1

    path_cmd=$(which "${cmd}")
    if [ -z "${path_cmd}" ]; then
        raise "FAIL: Could not find '${cmd}' command in path" -l "fsm_lib:check_cmd" -fc
        return 1
    fi
    log -deb "fsm_lib:check_cmd - Found '${cmd}' as '${path_cmd}' - Success"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function brings up tap interface.
# INPUT PARAMETER(S):
#   $1  Interface name (string, required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   tap_up_cmd br-home.tdns
###############################################################################
tap_up_cmd()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "fsm_lib:tap_up_cmd requires ${NARGS} input arguments, $# given" -arg
    intf=$1

    log -deb "fsm_lib:tap_up_cmd - Bringing tap interface '${intf}' up"
    ip link set "${intf}" up
}

###############################################################################
# DESCRIPTION:
#   Function marks interface as no-flood.
#   So, only the traffic matching the flow filter will hit the plugin.
# INPUT PARAMETER(S):
#   $1 Bridge name (string, required)
#   $2 Interface name (string, required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   gen_no_flood_cmd br-home br-home.tdns
###############################################################################
gen_no_flood_cmd()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "fsm_lib:gen_no_flood_cmd requires ${NARGS} input arguments, $# given" -arg
    bridge=$1
    intf=$2

    log -deb "fsm_lib:gen_no_flood_cmd: Mark interface '${intf}' as 'no-flood'"

    ovs-ofctl mod-port "${bridge}" "${intf}" no-flood
}
