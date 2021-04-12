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


current_dir=$(dirname "$(realpath "$BASH_SOURCE")")
fut_topdir="$(realpath "$current_dir"/../..)"

# FUT environment loading
source "${fut_topdir}"/config/default_shell.sh
# Ignore errors for fut_set_env.sh sourcing
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh &> /dev/null
source "${fut_topdir}/shell/lib/onbrd_lib.sh"

tc_name="check_fw_pattern.sh"
usage()
{
cat << usage_string
${tc_name} [-h] [-file FILENAME] [-fw FW_STRING]
Description:
    Check fw version pattern validity
    The script is a development tool that is intended for execution on RPI
    server or local PC within the framework directory structure, not on DUT!
Arguments:
    -h              : show this help message
    -file FILENAME  : verify FW version string(s) from FILENAME
    -fw FW_STRING   : verify FW version string FW_STRING
Testcase procedure: execute on RPI server or local PC, not DUT
    - Export environment variable FUT_TOPDIR:
        export FUT_TOPDIR=~/git/device/core/src/fut
    - Run tool
Script usage example:
   ./${tc_name} -file files/fw_patterns
   ./${tc_name} -fw 2.4.3-72-g65b961c-dev-debug
usage_string
exit 1
}

NARGS=1
if [ $# -lt ${NARGS} ]; then
    log -err "${tc_name}: Requires at least ${NARGS} input argument(s)."
    usage
fi

in_file=""
fw_string=""

while [ -n "$1" ]; do
    option=$1
    shift
    case "$option" in
        -h)
            usage
            ;;
        -file)
            in_file="${1}"
            shift
            ;;
        -fw)
            fw_string="${1}"
            shift
            ;;
        *)
            raise "Unknown argument" -l "${tc_name}" -arg
            ;;
    esac
done

# TEST EXECUTION:
if [ -n "$in_file"  ]; then
    log "${tc_name}: Verifying FW version string(s) from file '${in_file}'."
    while IFS= read line
    do
        # Discard empty lines and comments
        echo "$line" | grep -q -e "^$" -e "^#"
        if [ "$?" = "0" ]; then
            echo "${line}"
            continue
        fi
        # Use subprocess to not exit upon error
        rv=$(verify_fw_pattern "${line}")
        rc=$?
        [ $rc -eq 0 ] &&
            log -deb "${tc_name}: FW version string ${line} is valid" ||
            log -err "${tc_name}: FW version string ${line} is not valid\n${rv}"
    done <"$in_file"
elif [ -n "$fw_string" ]; then
    log "${tc_name}: Verifying FW version string '${fw_string}'."
    rv=$(verify_fw_pattern "${fw_string}")
    [ $? -eq 0 ] &&
        log -deb "${tc_name}: FW version string ${fw_string} is valid" ||
        raise "FW version string ${fw_string} is not valid" -l "${tc_name}" -tc
else
    log -err "${tc_name}: Something went wrong..."
    usage
fi
