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
source "${FUT_TOPDIR}/shell/lib/dm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="dm/dm_setup.sh"
usage()
{
cat << usage_string
dm/dm_verify_counter_inc_reboot_status.sh [-h] arguments
Description:
    The test script checks if count is incremented and reboot type is USER after reboot
Arguments:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./dm/dm_verify_counter_inc_reboot_status.sh <COUNT_BEFORE_REBOOT> <COUNT_AFTER_REBOOT>
Script usage example:
   ./dm/dm_verify_counter_inc_reboot_status.sh 121 122
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

trap '
fut_info_dump_line
print_tables Reboot_Status
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=2
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "dm/dm_verify_counter_inc_reboot_status.sh" -arg
count_before_reboot=$1
count_after_reboot=$2

log_title "dm/dm_verify_counter_inc_reboot_status.sh: DM test - Verify Reboot_Status::count is incremented and reboot type is USER"

count_to_check=$(($count_before_reboot+1))
if [ $count_after_reboot -eq $count_to_check ]; then
    log "dm/dm_verify_counter_inc_reboot_status.sh: Reboot_Status::count field is incremented - Success"
    wait_for_function_response 0 "check_ovsdb_entry Reboot_Status -w count $count_after_reboot -w type 'USER'" &&
        log "dm/dm_verify_counter_inc_reboot_status.sh: Reboot reason is USER - Success" ||
        raise "FAIL: Reboot reason is not USER" -l "dm/dm_verify_counter_inc_reboot_status.sh" -tc
else
    raise "FAIL: Reboot_Status::count field is not incremented" -l "dm/dm_verify_counter_inc_reboot_status.sh" -tc
fi

pass
