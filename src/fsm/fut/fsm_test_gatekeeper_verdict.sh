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
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage() {
    cat << usage_string
fsm/fsm_test_gatekeeper_verdict.sh [-h] arguments
Description:
    - Script checks logs from the FSM Gatekeeper plugin containing the verdict.
Arguments:
    -h  show this help message
    \$1 (url)              : Url for which verdict is requested   : (string)(required)
    \$2 (expected_verdict) : Expected verdict from the Gatekeeper : (string)(required)
Script usage example:
    ./fsm/fsm_test_gatekeeper_verdict.sh fut.opensync.io/test/url allow
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

# gatekeeper_get_verdict(): verdict for 'http://fut.opensync.io/gatekeeper/test/block' is blocked

trap '
fut_info_dump_line
print_tables Wifi_Associated_Clients
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

# INPUT ARGUMENTS:
NARGS=2
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
url=${1}
expected_verdict=${2}

fsm_message_regex="$LOGREAD |
 tail -3000 |
 grep gatekeeper_get_verdict |
 grep ${url} |
 grep 'is ${expected_verdict}'"
wait_for_function_response 0 "${fsm_message_regex}" 5 &&
    log "fsm/fsm_test_gatekeeper_verdict.sh: FSM gatekeeper verdict message found in logs - Success" ||
    raise "FAIL: Failed to find FSM gatekeeper verdict message in logs, regex used: ${fsm_message_regex} " -l "fsm/fsm_test_gatekeeper_verdict.sh" -tc
