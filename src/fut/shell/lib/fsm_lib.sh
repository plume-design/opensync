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
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   fsm_setup_test_environment
###############################################################################
fsm_setup_test_environment()
{
    fn_name="fsm_lib:fsm_setup_test_environment"

    log "$fn_name - Running FSM setup"

    device_init  &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    cm_disable_fatal_state &&
        log -deb "$fn_name - Fatal state disabled - Success"  ||
        raise "FAIL: Could not disable fatal state: cm_disable_fatal_state" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success"  ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    restart_managers
    log "${fn_name} - Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "$fn_name - AW_Debug table emptied - Success"  ||
        raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "$fn_name" -ds

    set_manager_log FSM TRACE &&
        log -deb "$fn_name - Manager log for FSM set to TRACE - Success"||
        raise "FAIL: Could not set manager log severity: set_manager_log FSM TRACE" -l "$fn_name" -ds

    log "$fn_name - FSM setup - end"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function check if a command is in the path.
#   Raises an exception if not in the path.
# INPUT PARAMETER(S):
#   $1  command to check (required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   check_cmd 'ovsh'
#   check_cmd 'ovs-vsctl'
###############################################################################
check_cmd()
{
    fn_name="fsm_lib:check_cmd"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input arguments, $# given" -arg
    cmd=$1

    path_cmd=$(which "${cmd}")
    if [ -z "${path_cmd}" ]; then
        raise "FAIL: Could not find ${cmd} command in path" -l "$fn_name" -fc
        return 1
    fi
    log "${fn_name} - Found ${cmd} as ${path_cmd}"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates tap interface on bridge with selected Openflow port.
#   Raises an exception if not in the path.
# INPUT PARAMETER(S):
#   $1  bridge name (required)
#   $2  interface name (required)
#   $3  open flow port (required)
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
#   gen_tap_cmd br-home br-home.tdns 3001
#   gen_tap_cmd br-home br-home.tx 401
###############################################################################
gen_tap_cmd()
{
    fn_name="fsm_lib:gen_tap_cmd"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input arguments, $# given" -arg
    bridge=$1
    intf=$2
    ofport=$3

    log "${fn_name} - Generating tap interface ${intf} on bridge ${bridge}"

    ovs-vsctl add-port "${bridge}" "${intf}"  \
        -- set interface "${intf}"  type=internal \
        -- set interface "${intf}"  ofport_request="${ofport}"
}

###############################################################################
# DESCRIPTION:
#   Function brings up tap interface.
# INPUT PARAMETER(S):
#   $1  interface name (required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   tap_up_cmd br-home.tdns
###############################################################################
tap_up_cmd()
{
    fn_name="fsm_lib:tap_up_cmd"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input arguments, $# given" -arg
    intf=$1

    log "${fn_name} - Bringing tap interface ${intf} up"
    ip link set "${intf}" up
}

###############################################################################
# DESCRIPTION:
#   Function marks interface as no-flood.
#   So, only the traffic matching the flow filter will hit the plugin.
# INPUT PARAMETER(S):
#   $1 bridge name (required)
#   $2 interface name (required)
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   gen_no_flood_cmd br-home br-home.tdns
###############################################################################
gen_no_flood_cmd()
{
    fn_name="fsm_lib:tap_up_cmd"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input arguments, $# given" -arg
    bridge=$1
    intf=$2

    log "${fn_name}: mark interface ${intf} no flood"

    ovs-ofctl mod-port "${bridge}" "${intf}" no-flood
}
