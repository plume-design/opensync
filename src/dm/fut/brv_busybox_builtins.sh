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


# FUT environment loading
# shellcheck disable=SC1091
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

brv_setup_file="brv/brv_setup.sh"
usage()
{
cat << usage_string
brv/brv_busybox_builtins.sh [-h] arguments
Description:
    - Script checks if the specified tool is busybox builtin tool, fails otherwise
Arguments:
    -h : show this help message
    \$1 (builtin_tool) : name of the required busybox built-in tool : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${brv_setup_file} (see ${brv_setup_file} -h)
                 Run: ./brv/brv_busybox_builtins.sh <TOOL-NAME>
Script usage example:
   ./brv/brv_busybox_builtins.sh "tail"
usage_string
}
if [ -n "${1}" ]; then
    case "${1}" in
        help | \
        --help | \
        -h)
            usage && exit 1
            ;;
        *)
            ;;
    esac
fi
NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "brv/brv_busybox_builtins.sh" -arg

builtin_tool=$1

log_title "brv/brv_busybox_builtins.sh: BRV test - Verify '${builtin_tool}' is built into busybox"

is_tool_on_system "busybox"
rc=$?
if [ $rc != 0 ]; then
    raise "FAIL: Refusing tool search, busybox is not present on system" -l "brv/brv_busybox_builtins.sh" -nf
fi
is_busybox_builtin "${builtin_tool}"
rc=$?
if [ $rc == 0 ]; then
    log "brv/brv_busybox_builtins.sh: '${builtin_tool}' is built into busybox - Success"
else
    raise "FAIL: '${builtin_tool}' is not built into busybox" -l "brv/brv_busybox_builtins.sh" -tc
fi

pass
