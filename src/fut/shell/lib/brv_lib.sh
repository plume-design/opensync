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
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source ${FUT_TOPDIR}/shell/config/default_shell.sh
fi
source ${FUT_TOPDIR}/shell/lib/base_lib.sh

file_name="$(basename $0)"

brv_setup_env()
{
    local fn_name="${file_name}:brv_setup_env"
    log -deb "${fn_name} - Running BRV setup"
    # There are currently no setup steps
    return 0
}

_get_ovs_version()
{
    local fn_name="${file_name}:_get_ovs_version"
    local OVS_NAME="ovs-vswitchd"
    local OVS_CMD=$(command -v $OVS_NAME)
    # try which if command utility is not available
    [ -z ${OVS_CMD} ] && OVS_CMD=$(which $OVS_NAME)
    [ -z ${OVS_CMD} ] && raise "Can not call ${OVS_NAME}" -l "${fn_name}" -nf

    OVS_ACTUAL_VER=$(${OVS_CMD} -V | cut -d' ' -f4)
    ec=$?
    [ ${ec} -ne 0 ] && raise "Error calling ${OVS_CMD}" -l "${fn_name}" -ec ${ec} -fc
    [ -z ${OVS_ACTUAL_VER} ] && raise "Failed to get ovs version" -l "${fn_name}" -f

    echo "${OVS_ACTUAL_VER}"
}

check_ovs_version()
{
    local fn_name="${file_name}:check_ovs_version"
    log -deb "${fn_name} - Getting device ovs version"
    local NARGS=1
    [ $# -ne ${NARGS} ] && raise "Requires ${NARGS} input arguments" -l "${fn_name}" -arg

    local OVS_EXPECTED_VER=$1
    OVS_ACTUAL_VER="$(_get_ovs_version)"
    ec=$?
    [ ${ec} -ne 0 ] && raise "Failed to get ovs version" -l "${fn_name}" -ec ${ec} -arg

    if [ "${OVS_ACTUAL_VER}" != "${OVS_EXPECTED_VER}" ]; then
        raise "Actual ovs version mismatches expected ovs version" -l "${fn_name}" -tc
    fi
    return 0
}

is_tool_on_system()
{
    local fn_name="${file_name}:is_tool_on_system"
    log -deb "${fn_name} - Checking tool presence on system"
    local NARGS=1
    [ $# -ne ${NARGS} ] && raise "Requires ${NARGS} input arguments" -l "${fn_name}" -arg

    command -v $1
    rc=$?
    # "command [-vV] command_name" builtin utility exit codes:
    # 0:   Successful completion.
    # 126: The utility specified by command_name was found but could not be invoked.
    # >0:  The command_name could not be found or an error occurred.
    if [ $rc -gt 0 -a $rc -ne 126 ]; then
        which $1
        rc=$?
    fi
    return ${rc}
}

is_busybox_builtin()
{
    local fn_name="${file_name}:is_busybox_builtin"
    log -deb "${fn_name} - Checking if tool is built into busybox"
    local NARGS=1
    [ $# -ne ${NARGS} ] && raise "Requires '${NARGS}' input arguments" -l "${fn_name}" -arg

    busybox --list | grep -wF $1
    return $?
}
