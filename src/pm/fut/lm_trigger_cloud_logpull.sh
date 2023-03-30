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

manager_setup_file="lm/lm_setup.sh"
usage()
{
cat << usage_string
lm/lm_trigger_cloud_logpull.sh [-h] arguments
Description:
    - Validate Cloud trigered logpull event. The test simulates a cloud triggered
      logpull by setting an upload location and upload token in the AW_LM_Config
      table. The logpull service starts collecting system logs, states and current
      configuraton of nodes and creates a tarball. The test also checks if the
      logpull tarball was created by verifying that the directory which includes
      the created tarball is not empty. The logpull tarball is uploaded to the
      specified location, using the upload token as credentials.
Arguments:
    -h  show this help message
    \$1 (upload_location)  : AW_LM_Config::upload_location : (string)(required)
    \$2 (upload_token)     : AW_LM_Config::upload_token : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./lm/lm_trigger_cloud_logpull.sh <UPLOAD_LOCATION> <UPLOAD_TOKEN>
Script usage example:
   ./lm/lm_trigger_cloud_logpull.sh <UPLOAD_LOCATION> <UPLOAD_TOKEN>
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
upload_location=${1}
upload_token=${2}

log_title "lm/lm_trigger_cloud_logpull.sh: LM test - Verify Cloud trigered logpull event"

trap '
    fut_info_dump_line
    print_tables AW_Debug AW_LM_Config
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

wait_for_function_response 1 "check_if_logpull_dir_empty" &&
    log "lm/lm_trigger_cloud_logpull.sh: /tmp/logpull/ folder is empty - Success" ||
    raise "FAIL: /tmp/logpull/ folder is not empty" -l "lm/lm_trigger_cloud_logpull.sh" -tc

empty_ovsdb_table AW_Debug &&
    log -deb "lm/lm_trigger_cloud_logpull.sh - AW_Debug table emptied - Success" ||
    raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty table:" -l "lm/lm_trigger_cloud_logpull.sh" -ds

log "lm/lm_trigger_cloud_logpull.sh: For PM set log severity to DEBUG"
set_manager_log PM DEBUG &&
    log "lm/lm_trigger_cloud_logpull.sh - set_manager_log PM DEBUG - Success" ||
    raise "FAIL: set_manager_log PM DEBUG" -l "lm/lm_trigger_cloud_logpull.sh" -tc

${OVSH} U AW_LM_Config upload_location:="$upload_location" upload_token:="$upload_token" &&
    log "lm/lm_trigger_cloud_logpull.sh: AW_LM_Config values inserted - Success" ||
    raise "FAIL: Failed to insert_ovsdb_entry" -l "lm/lm_trigger_cloud_logpull.sh" -oe

wait_for_function_response 0 "check_pm_report_log" &&
    log "lm/lm_trigger_cloud_logpull.sh: PM logpull log found - Success" ||
    raise "FAIL: PM logpull log not found" -l "lm/lm_trigger_cloud_logpull.sh" -tc

# By checking if the logpull directory is not empty we can verify that the logpull tarball was
# succesfully generated.
wait_for_function_response 0 "check_if_logpull_dir_empty" &&
    log "lm/lm_trigger_cloud_logpull.sh: /tmp/logpull/ folder is not empty - Success" ||
    raise "FAIL: /tmp/logpull/ folder is empty" -l "lm/lm_trigger_cloud_logpull.sh" -tc

# By checking if the logpull directory is empty we can verify that the logpull tarball was deleted
# after it was sent to the upload location.
wait_for_function_response 1 "check_if_logpull_dir_empty" 60 &&
    log "lm/lm_trigger_cloud_logpull.sh: /tmp/logpull/ folder is empty - Success" ||
    raise "FAIL: /tmp/logpull/ folder is not empty" -l "lm/lm_trigger_cloud_logpull.sh" -tc

pass
