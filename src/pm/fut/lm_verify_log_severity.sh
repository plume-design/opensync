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
source "${FUT_TOPDIR}/shell/lib/lm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="lm/lm_setup.sh"
usage()
{
cat << usage_string
lm/lm_verify_log_severity.sh [-h] arguments
Description:
    - Validate dynamic changes to log severity during device runtime. The test
      sets log severity for service in AW_Debug table and checks the content of
      the file, determined by the Kconfig option TARGET_PATH_LOG_STATE value.
Arguments:
    -h  show this help message
    \$1 (name)         : Name of the service or manager in AW_Debug::name : (string)(required)
    \$2 (log_severity) : Log severity level in AW_Debug::log_severity     : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./lm/lm_verify_log_severity.sh <NAME> <LOG_SEVERITY>
Script usage example:
   ./lm/lm_verify_log_severity.sh SM TRACE
   ./lm/lm_verify_log_severity.sh FSM DEBUG
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
NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires '${NARGS}' input argument(s)" -l "lm/lm_verify_log_severity.sh" -arg
name=${1}
log_severity=${2}

log_title "lm/lm_verify_log_severity.sh: LM test - Verify dynamic changes to log severity during device runtime"

log_state_value=$(get_kconfig_option_value "TARGET_PATH_LOG_STATE")
# Clean string of quotes:
log_state_file=$(echo ${log_state_value} | tr -d '"')
[ -z ${log_state_file} ] && raise "Kconfig option TARGET_PATH_LOG_STATE has no value" -l "lm/lm_verify_log_severity.sh" -arg
# Trap needs to come after "log_state_file"
trap '
    fut_info_dump_line
    cat $log_state_file
    print_tables AW_Debug
    empty_ovsdb_table AW_Debug
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log "lm/lm_verify_log_severity.sh: Test setup - clean AW_Debug table"
empty_ovsdb_table AW_Debug  &&
    log "lm/lm_verify_log_severity.sh - AW_Debug table empty - Success" ||
    raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "lm/lm_verify_log_severity.sh" -ds

log "lm/lm_verify_log_severity.sh: Set log severity ${log_severity} for ${name}"
set_manager_log ${name} ${log_severity} &&
    log "lm/lm_verify_log_severity.sh - set_manager_log ${name} ${log_severity} - Success" ||
    raise "FAIL: set_manager_log ${name} ${log_severity}" -l "lm/lm_verify_log_severity.sh" -tc

log "lm/lm_verify_log_severity.sh: Ensure ${log_state_file} exists"
[ -f ${log_state_file} ] &&
    log "lm/lm_verify_log_severity.sh - File ${log_state_file} exists - Success" ||
    raise "FAIL: File ${log_state_file} does not exist" -l "lm/lm_verify_log_severity.sh" -tc

log "lm/lm_verify_log_severity.sh: Ensure content of ${log_state_file} is correct"
#   ":a"        create a label
#   "N"         append the current and next line to the pattern space
#   "$!ba"      branch to label if not on last line
#   's/\n/ /g'  substitute newline with space for the whole file, get one line
#   's/}/}\n/g' break line at every "}", get json dicts in single lines
sed -e ':a' -e 'N' -e '$!ba' -e 's/\n/ /g' -e 's/}/}\n/g' ${log_state_file} | grep ${name} | grep ${log_severity} &&
    log "lm/lm_verify_log_severity.sh - ${log_state_file} contains ${name}:${log_severity} - Success" ||
    raise "FAIL: ${log_state_file} does not contain ${name}:${log_severity}" -l "lm/lm_verify_log_severity.sh" -tc
# Alternative, drawback: will match if individual lines are present, but not related!
# sed -n -e '/'${name}'/,/log_severity/ p' ${log_state_file} | grep ${log_severity}

pass
