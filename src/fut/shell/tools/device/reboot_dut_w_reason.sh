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

dm_setup_file="dm/dm_setup.sh"
usage()
{
cat << usage_string
tools/device/reboot_dut_w_reason.sh [-h]
Description:
    Script reboots device in a configured manner:
      - USER intervention and
      - CLOUD reboot currently supported.
Arguments:
    -h  show this help message
    \$1 (reboot_reason) : Reboot trigger type             : (string)(required)
    Optional argument (If reboot reason is 'CLOUD'):
    \$2 (opensync_path) : Path to Opensync root directory : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${dm_setup_file} (see ${dm_setup_file} -h)
    - On DEVICE: Run: .tools/device/reboot_dut_w_reason.sh <REBOOT_REASON>
Script usage example:
    .tools/device/reboot_dut_w_reason.sh USER
    .tools/device/reboot_dut_w_reason.sh CLOUD /usr/opensync
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

check_kconfig_option "CONFIG_OSP_REBOOT_PSTORE" "y" ||
    raise "CONFIG_OSP_REBOOT_PSTORE != y - Testcase not applicable REBOOT PERSISTENT STORAGE not supported" -l "tools/device/reboot_dut_w_reason.sh" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/device/reboot_dut_w_reason.sh" -arg
# Fill variables with provided arguments.
reboot_reason=$1
if [ $reboot_reason == "CLOUD" ]; then
    NARGS=2
    [ $# -eq ${NARGS} ] && opensync_path=$2 ||
    raise "Invalid/missing argument - path to opensync root directory" -l "tools/device/reboot_dut_w_reason.sh" -arg
fi

log_title "tools/device/reboot_dut_w_reason.sh: DM test - Reboot DUT with reason - $reboot_reason"

log "tools/device/reboot_dut_w_reason.sh - Simulating $reboot_reason reboot"
case "$reboot_reason" in
    "USER")
        reboot
    ;;
    "CLOUD")
        trigger_cloud_reboot ${opensync_path}
        print_tables Wifi_Test_Config
    ;;
    *)
        raise "FAIL: Unknown reason to check: $reboot_reason" -l "tools/device/reboot_dut_w_reason.sh" -arg
    ;;
esac

pass
