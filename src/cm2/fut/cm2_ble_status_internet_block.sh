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
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

cm_setup_file="cm2/cm2_setup.sh"
adr_internet_man_file="tools/server/cm/address_internet_man.sh"
step_1_name="internet_blocked"
step_1_bit_process="15"
step_2_name="internet_recovered"
step_2_bit_process="75"
usage()
{
cat << usage_string
cm2/cm2_ble_status_internet_block.sh [-h] arguments
Description:
    - Script observes AW_Bluetooth_Config table field 'payload' during internet reconnection
      If AW_Bluetooth_Config payload field fails to change in given sequence test fails
Arguments:
    -h : show this help message
    \$1 (test_step) : used as test step : (string)(required) : (${step_1_name}, ${step_2_name})
Testcase procedure:
    - On DEVICE: Run: ${cm_setup_file} (see ${cm_setup_file} -h)
                 Run: cm2/cm2_ble_status_internet_block.sh ${step_1_name}
    - On RPI SERVER: Run: ${adr_internet_man_file} <WAN-IP-ADDRESS> block
    - On DEVICE: Run: cm2/cm2_ble_status_internet_block.sh ${step_2_name}
    - On RPI SERVER: Run: ${adr_internet_man_file} <WAN-IP-ADDRESS> unblock
Script usage example:
   ./cm2/cm2_ble_status_internet_block.sh ${step_1_name}
   ./cm2/cm2_ble_status_internet_block.sh ${step_2_name}
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

check_kconfig_option "CONFIG_MANAGER_BLEM" "y" ||
    raise "CONFIG_MANAGER_BLEM != y - BLE not present on device" -l "cm2/cm2_ble_status_internet_block.sh" -s

check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "cm2/cm2_ble_status_internet_block.sh" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "cm2/cm2_ble_status_internet_block.sh" -arg
test_step=${1}

trap '
fut_info_dump_line
print_tables AW_Bluetooth_Config
check_restore_management_access || true
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "cm2/cm2_ble_status_internet_block.sh: CM2 test - Observe BLE Status - $test_step"

case $test_step in
    ${step_1_name})
        bit_process=${step_1_bit_process}
        for bit in $bit_process; do
            log "cm2/cm2_ble_status_internet_block.sh: Checking AW_Bluetooth_Config::payload for $bit:00:00:00:00:00"
            wait_ovsdb_entry AW_Bluetooth_Config -is payload "$bit:00:00:00:00:00" &&
                log "cm2/cm2_ble_status_internet_block.sh: wait_ovsdb_entry - AW_Bluetooth_Config::payload changed to $bit:00:00:00:00:00 - Success" ||
                raise "FAIL: AW_Bluetooth_Config::payload failed to change to $bit:00:00:00:00:00" -l "cm2/cm2_ble_status_internet_block.sh" -tc
        done
    ;;
    ${step_2_name})
        bit_process=${step_2_bit_process}
        for bit in $bit_process; do
            log "cm2/cm2_ble_status_internet_block.sh: Checking AW_Bluetooth_Config::payload for $bit:00:00:00:00:00"
            wait_ovsdb_entry AW_Bluetooth_Config -is payload "$bit:00:00:00:00:00" &&
                log "cm2/cm2_ble_status_internet_block.sh: wait_ovsdb_entry - AW_Bluetooth_Config::payload changed to $bit:00:00:00:00:00 - Success" ||
                raise "FAIL: AW_Bluetooth_Config::payload failed to change to $bit:00:00:00:00:00" -l "cm2/cm2_ble_status_internet_block.sh" -tc
        done
    ;;
    *)
        raise "FAIL: Incorrect test_step provided" -l "cm2/cm2_ble_status_internet_block.sh" -arg
esac

pass
