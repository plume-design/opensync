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


#
# Base library of shared functions with minimal prerequisites:
#   [
#   basename
#   date
#   echo
#   printf
#

raise()
{
    # The function has 5 parameters for the user to customize and 2 modes:
    # Parameters:
    #   exception_msg: Message to be displayed in terminal
    #   exception_location: Where did the error happen
    #   exception_name: Error name to report to framework:
    #                   "FunctionCall"
    #                   "DeviceSetup"
    #                   "OvsdbWait"
    #                   "OvsdbException"
    #                   "NativeFunction"
    #                   "TestFailure"
    #                   "InvalidOrMissingArgument"
    #   exception_type: Type of error to report to framework: ERROR|BROKEN
    #   exit_code: actual process exit code to return to calling function
    # Modes:
    #   is_skip=true: propagate skip condition to framework (exit_code=3)
    #   is_skip=false: an error occurred
    exception_msg=${1:-"Unknown error"}
    shift 1
    exception_location=$(basename "$0")
    exception_name="CommonShellException"
    exception_type="BROKEN"
    is_skip=false
    exit_code=1

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            # Use for generic failures
            -f)
                exception_type="FAIL"
                ;;
            # Use to define custom exit code
            -ec)
                exit_code=${1}
                shift
                ;;
            # Use to propagate "skip" condition to framework
            -s)
                exit_code=3
                is_skip=true
                ;;
            # Customize location of error, instead of this library
            -l)
                exception_location=${1}
                shift
                ;;
            # Use when error occurred during function call
            -fc)
                exception_name="FunctionCall"
                ;;
            # Use when error occurred during device setup
            -ds)
                exception_name="DeviceSetup"
                exception_type="FAIL"
                ;;
            # Use when error occurred during ovsdb-wait
            -ow)
                exception_name="OvsdbWait"
                exception_type="FAIL"
                ;;
            # Use when error occurred due to ovsdb issue
            -oe)
                exception_name="OvsdbException"
                ;;
            # Use when error occurred due to native function issue
            -nf)
                exception_name="NativeFunction"
                exception_type="FAIL"
                ;;
            # Use for testcase failures
            -tc)
                exception_name="TestFailure"
                exception_type="FAIL"
                ;;
            # Use when error occurred due to invalid or missing argument
            -arg)
                exception_name="InvalidOrMissingArgument"
                ;;
        esac
    done
    echo "$(date +%T) [ERROR] ${exception_location} - ${exception_msg}"
    if [ "$is_skip" = 'false' ]; then
        echo "FutShellException|FES|${exception_type}|FES|${exception_name}|FES|${exception_msg} AT ${exception_location}"
    else
        echo "${exception_msg} AT ${exception_location}"
    fi
    exit "$exit_code"
}

die()
{
    log -deb "[DEPRECATED] - Function die() deprecated, use raise() instead!"
    raise "$*"
}

die_with_code()
{
    code=$1
    shift
    log -deb "[DEPRECATED] - Function die_with_code() deprecated, use raise() instead!"
    raise "$*" -ec "$code"
}

pass()
{
    if [ $# -ge 1 ]; then
        echo -e "\n$(date +%T) [SHELL] $*"
    else
        echo -e "\n$(date +%T) [SHELL] TEST PASSED"
    fi
    exit 0
}

log_title()
{
    c=${2:-"*"}
    v=$(printf "%0.s$c" $(seq 1 $((${#1}+2))))
    echo -ne "${v}\n ${1} \n${v}\n"
}

log()
{
    msg_type="[SHELL]"
    exit_code=0
    if [ "$1" = "-deb" ]; then
        msg_type="[DEBUG]"
        shift
    elif [ "$1" = "-err" ]; then
        msg_type="[ERROR]"
        exit_code=1
        shift
    fi

    echo -e "$(date +%T) $msg_type $*"

    return $exit_code
}
