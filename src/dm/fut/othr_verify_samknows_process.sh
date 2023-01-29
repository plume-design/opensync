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
source "${FUT_TOPDIR}/shell/lib/othr_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="dm/othr_setup.sh"
usage()
{
cat << usage_string
othr/othr_verify_samknows_process.sh [-h] arguments
Description:
    - Script verifies if samknows feature is triggered on DUT.
Arguments:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./othr/othr_verify_samknows_process.sh
Script usage example:
    ./othr/othr_verify_samknows_process.sh
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
    print_tables Node_Config Node_State
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

check_kconfig_option "3RDPARTY_SAMKNOWS" "y" ||
    raise "'Samknows' feature not supported on this device" -l "othr/othr_verify_samknows_process.sh" -s

log_title "othr/othr_verify_samknows_process.sh: OTHR test - Verify Samknows process is triggered on DUT"

check_ovsdb_table_exist Node_Config ||
    raise "FAIL: Node_Config table does not exist in ovsdb" -l "othr/othr_verify_samknows_process.sh" -s

${OVSH} U Node_Config -w module==samknows value:=\"true\" key:=enable &&
    log "othr/othr_verify_samknows_process.sh: Upsert - Node_Config::value and Node_Config::key are inserted - Success" ||
    raise "FAIL: Upsert - Node_Config::value and Node_Config::key failed to insert" -l "othr/othr_verify_samknows_process.sh" -oe

wait_ovsdb_entry Node_State -w module "samknows" -is value \"true\" -is key enable &&
    log "othr/othr_verify_samknows_process.sh: wait_ovsdb_entry - Node_State::value and Node_State::key are updated - Success" ||
    raise "FAIL: wait_ovsdb_entry - Node_State::value and Node_State::key failed to update" -l "othr/othr_verify_samknows_process.sh" -tc

pgrep "samknows" &> /dev/null &&
    log "othr/othr_verify_samknows_process.sh: Samknows process is running on the DUT - Success" ||
    raise "FAIL: Samknows process failed to run on DUT" -l "othr/othr_verify_samknows_process.sh" -tc

remove_ovsdb_entry Node_Config -w module "samknows" &&
    log "othr/othr_verify_samknows_process.sh: remove_ovsdb_entry - Removed entry for 'samknows' from Node_Config table - Success" ||
    raise "FAIL: remove_ovsdb_entry - Failed to remove entry for 'samknows' from Node_Config table" -l "othr/othr_verify_samknows_process.sh" -tc

pass
