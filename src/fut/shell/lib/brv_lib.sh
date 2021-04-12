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


# Library of functions for EPV test cases. Keep prerequisites to minimum
# Prerequisites:
#   [
#   basename
#   busybox
#   command
#   echo
#   egrep
#   getopts
#   grep
#   lib/base_lib.sh
#   source
#   which

# Include basic environment config
export FUT_BRV_LIB_SRC=true
[ "${FUT_BASE_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/base_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/brv_lib.sh sourced"
####################### INFORMATION SECTION - START ###########################
#
#   Base library of common BRV functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function prepares device for BRV tests.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   Always.
# USAGE EXAMPLE(S):
#   brv_setup_env
###############################################################################
brv_setup_env()
{
    local fn_name="brv_lib:brv_setup_env"
    log -deb "${fn_name} - Running BRV setup"
    # There are currently no setup steps
    return 0
}

####################### SETUP SECTION - STOP ##################################

###############################################################################
# DESCRIPTION:
#   Function echoes current ovs version.
#   Raises an exception if cannot obtain actual ovs version.
# INPUT PARAMETER(S):
#   None.
# ECHOES:
#   Echoes ovs version.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_ovs_version
###############################################################################
get_ovs_version()
{
    local fn_name="brv_lib:_get_ovs_version"
    local OVS_NAME="ovs-vswitchd"
    local OVS_CMD

    OVS_CMD=$(command -v $OVS_NAME)
    # try which if command utility is not available
    [ -z "${OVS_CMD}" ] &&
        OVS_CMD=$(which $OVS_NAME)
    [ -z "${OVS_CMD}" ] &&
        raise "FAIL: Can not call ${OVS_NAME}" -l "${fn_name}" -nf

    OVS_ACTUAL_VER=$(${OVS_CMD} -V | head -n1 | cut -d' ' -f4)
    ec=$?
    [ ${ec} -ne 0 ] &&
        raise "FAIL: Error calling ${OVS_CMD}" -l "${fn_name}" -ec ${ec} -fc
    [ -z "${OVS_ACTUAL_VER}" ] &&
        raise "FAIL: Could not get ovs version" -l "${fn_name}" -f

    echo "${OVS_ACTUAL_VER}"
}

###############################################################################
# DESCRIPTION:
#   Function checks if actual ovs version is equal to expected ovs version.
# INPUT PARAMETER(S):
#   $1  expected ovs version (required)
#   $2  actual ovs version (required)
# RETURNS:
#   0   Versions match.
#   1   Versions do not match.
# USAGE EXAMPLE(S):
#   check_ovs_version 2.8.7 2.8.7
#   check_ovs_version 2.8.7 2.8.6
###############################################################################
check_ovs_version()
{
    local fn_name="brv_lib:check_ovs_version"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    local OVS_EXPECTED_VER=$1
    local OVS_ACTUAL_VER=$2

    if [ "${OVS_ACTUAL_VER}" != "${OVS_EXPECTED_VER}" ]; then
        log -deb "${fn_name} - Actual ovs version mismatches expected ovs version"
        return 1
    else
        return 0
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if tool is installed on system.
# INPUT PARAMETER(S):
#   $1  command/tool name (required)
# RETURNS:
#   0   Successful completion.
#   126 The utility specified by command name was found but could not be invoked.
#   >0  The command name could not be found or an error occurred.
# USAGE EXAMPLE(S):
#   is_tool_on_system arping
#   is_tool_on_system cat
###############################################################################
is_tool_on_system()
{
    local fn_name="brv_lib:is_tool_on_system"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    cmd_name=$1

    log -deb "${fn_name} - Checking tool presence on system"
    command -v "$cmd_name"
    rc=$?
    if [ $rc -gt 0 ] && [ $rc -ne 126 ]; then
        which "$cmd_name"
        rc=$?
    fi
    if [ $rc -gt 0 ] && [ $rc -ne 126 ]; then
        type "$cmd_name"
        rc=$?
    fi
    return ${rc}
}

###############################################################################
# DESCRIPTION:
#   Function checks if script is present in filesystem.
# INPUT PARAMETER(S):
#   $1  script path, can be relative or absolute (required)
# RETURNS:
#   0   Script found on system
#   >0  Script NOT found on system or in PATH if relative path is provided
# USAGE EXAMPLE(S):
#   is_script_on_system /tmp/resolv.conf
#   is_script_on_system /sbin/udhcpc
#   is_script_on_system /etc/init.d/opensync
#   is_script_on_system /dev/null
###############################################################################
is_script_on_system()
{
    local fn_name="brv_lib:is_script_on_system"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    script_path=$1

    log -deb "${fn_name} - Checking script ${script_path} presence"
    test -e "${script_path}"
    rc=$?
    return ${rc}
}

###############################################################################
# DESCRIPTION:
#   Function checks if tool is built into busybox.
# INPUT PARAMETER(S):
#   $1 command/tool name (required)
# ECHOES:
#   is tool shell keyword, builtin or not found
# RETURNS:
#   0   Successful completion.
#   >0  The command_name could not be found or an error occurred.
# USAGE EXAMPLE(S):
#   is_busybox_builtin arping
#   is_busybox_builtin chmod
###############################################################################
is_busybox_builtin()
{
    local fn_name="brv_lib:is_busybox_builtin"
    log -deb "${fn_name} - Checking if tool is built into busybox"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    cmd_name=$1
    type "${cmd_name}"
    return $?
}
