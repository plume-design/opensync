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

dm_setup_file="dm/dm_setup.sh"
usage()
{
cat << usage_string
dm/dm_verify_reboot_reason.sh [-h]
Description:
    - Script checks if reboot file holds the last reboot reason.
Arguments:
    -h  show this help message
    \$1 (reboot_reason) : Reboot reason to be verified    : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${dm_setup_file} (see ${dm_setup_file} -h)
    - On DEVICE: Run: ./dm/dm_verify_reboot_reason.sh
Script usage example:
    ./dm/dm_verify_reboot_reason.sh USER
    ./dm/dm_verify_reboot_reason.sh CLOUD
usage_string
}

trap '
fut_ec=$?
fut_info_dump_line
if [ $fut_ec -ne 0 ]; then
    cat /var/run/osp_reboot_reason
fi
check_restore_ovsdb_server
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM

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

check_kconfig_option "CONFIG_OSP_REBOOT_PSTORE" "y" ||
    raise "CONFIG_OSP_REBOOT_PSTORE != y - Testcase not applicable REBOOT PERSISTENT STORAGE not supported" -l "dm/dm_verify_reboot_reason.sh" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "dm/dm_verify_reboot_reason.sh" -arg
# Fill variables with provided arguments.
reason_to_check=$1

reboot_file_path="/var/run/osp_reboot_reason"

log_title "dm/dm_verify_reboot_reason.sh: DM test - Verify last reboot reason matches $reason_to_check"

[ -e "$reboot_file_path" ] &&
    log "dm/dm_verify_reboot_reason.sh: reboot file $reboot_file_path exists - Success" ||
    raise "FAIL: reboot file - $reboot_file_path is missing" -l "dm/dm_verify_reboot_reason.sh" -tc
[ -s "$reboot_file_path" ] &&
    log "dm/dm_verify_reboot_reason.sh: reboot file $reboot_file_path is not empty - Success" ||
    raise "FAIL: reboot file $reboot_file_path is empty" -l "dm/dm_verify_reboot_reason.sh" -tc

cat $reboot_file_path | grep -q "REBOOT"
if [ $? = 0 ]; then
    log "dm/dm_verify_reboot_reason.sh: REBOOT string found in file $reboot_file_path"
    reason=$(cat $reboot_file_path | awk '{print $2}')
    case "$reason_to_check" in
        "USER" | \
        "CLOUD" | \
        "CRASH")
            if [ $reason = $reason_to_check ]; then
                log "dm/dm_verify_reboot_reason.sh: Found reason: $reason"
            else
                raise "FAIL: Could not find $reason_to_check string in file" -l "dm/dm_verify_reboot_reason.sh" -tc
            fi
        ;;
        *)
            raise "FAIL: Unknown reason to check: $reason_to_check" -l "dm/dm_verify_reboot_reason.sh" -arg
        ;;
    esac
else
    raise "FAIL: Could not find REBOOT string in file - $reboot_file_path" -l "dm/dm_verify_reboot_reason.sh" -tc
fi

pass
