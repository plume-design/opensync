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
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="dm/$(basename "$0")"
dm_setup_file="dm/dm_setup.sh"

usage()
{
cat << usage_string
${tc_name} [-h]
Description:
    - Script checks if reboot file holds the last reboot reason.
Arguments:
    -h  show this help message
    \$1 (reboot_reason) : Reboot reason to be verified    : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${dm_setup_file} (see ${dm_setup_file} -h)
    - On DEVICE: Run: ./${tc_name}
Script usage example:
    ./${tc_name} USER
    ./${tc_name} CLOUD
usage_string
}

while getopts h option; do
    case "$option" in
        h)
            usage && exit 1
            ;;
        *)
            echo "Unknown argument" && exit 1
            ;;
    esac
done

check_kconfig_option "CONFIG_OSP_REBOOT_PSTORE" "y" ||
    raise "CONFIG_OSP_REBOOT_PSTORE != y - Testcase not applicable REBOOT PERSISTENT STORAGE not supported" -l "${tc_name}" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
# Fill variables with provided arguments.
reason_to_check=$1

reboot_file_path="/var/run/osp_reboot_reason"

log_title "$tc_name: DM test - Verify last reboot reason matches $reason_to_check"

[ -e "$reboot_file_path" ] &&
    log "$tc_name: reboot file exists in $reboot_file_path" ||
    raise "reboot file is missing - $reboot_file_path" -l "$tc_name" -tc
[ -s "$reboot_file_path" ] &&
    log "$tc_name: reboot file is not empty - $reboot_file_path" ||
    raise "reboot file is empty - $reboot_file_path" -l "$tc_name" -tc

cat $reboot_file_path | grep -q "REBOOT"
if [ $? = 0 ]; then
    log "$tc_name: REBOOT string found in file - $reboot_file_path"
    reason=$(cat $reboot_file_path | awk '{print $2}')
    case "$reason_to_check" in
        "USER" | \
        "CLOUD" | \
        "CRASH")
            if [ $reason = $reason_to_check ]; then
                log "$tc_name: Found reason: $reason"
            else
                raise "FAIL: Could not find $reason_to_check string in file" -l "$tc_name" -tc
            fi
        ;;
        *)
            raise "FAIL: Unknown reason to check: $reason_to_check" -l "$tc_name" -tc
        ;;
    esac
else
    raise "FAIL: Could not find REBOOT string in file - $reboot_file_path" -l "$tc_name" -tc
fi

pass
