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
ookla_timeout=90
usage()
{
cat << usage_string
othr/othr_verify_ookla_speedtest_bind_options.sh [-h] arguments
Description:
    - Script verifies ookla speedtest bind options.
Arguments:
    -h  show this help message
    \$1  (testid)       : Wifi_Speedtest_Config::testid     : (int)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./othr/othr_verify_ookla_speedtest_bind_options.sh <TESTID>
Script usage example:
    ./othr/othr_verify_ookla_speedtest_bind_options.sh 110
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
    print_tables Wifi_Speedtest_Config Wifi_Speedtest_Status
    $(get_process_cmd)
    check_restore_ovsdb_server
    fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -arg

testid=${1}

log_title "othr/othr_verify_ookla_speedtest_bind_options.sh: OTHR test - Verify Ookla Speedtest Bind Options Specification"

check_kconfig_option "CONFIG_3RDPARTY_OOKLA" "y" ||
    check_kconfig_option "CONFIG_SPEEDTEST_OOKLA" "y" ||
        raise "OOKLA not present on device" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -s

# ookla_path is hardcoded during compile time
ookla_path="${OPENSYNC_ROOTDIR}/bin/ookla"
[ -e "$ookla_path" ] &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: ookla speedtest binary is present on system - Success" ||
    raise "SKIP: Ookla speedtest binary is not present on system" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -s

# Get default ip address and network interface for uplink connections
FUT_ST_BIND_TP=$(ip route get 1.1.1.1)
FUT_ST_BIND_IP=$(echo $FUT_ST_BIND_TP | awk -F"src " 'NF>1 { sub(/ .*/, "", $2); print $2; }')
FUT_ST_BIND_IF=$(echo $FUT_ST_BIND_TP | awk -F"dev " 'NF>1 { sub(/ .*/, "", $2); print $2; }')

# Check that a bind iterface can be specified

empty_ovsdb_table Wifi_Speedtest_Config ||
    raise "FAIL: Could not empty Wifi_Speedtest_Config: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

empty_ovsdb_table Wifi_Speedtest_Status ||
    raise "FAIL: Could not empty Wifi_Speedtest_Status: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

insert_ovsdb_entry Wifi_Speedtest_Config -i test_type "OOKLA" -i testid "$testid" -i st_bind_interface "$FUT_ST_BIND_IF" &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: insert_ovsdb_entry - Wifi_Speedtest_Config::test_type - Success" ||
    raise "FAIL: insert_ovsdb_entry - Failed to insert Wifi_Speedtest_Config::test_type" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

sleep 1

pid_of_ookla=$(get_pid "$ookla_path")
[ -n "$pid_of_ookla" ] &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: Speedtest process started with pid $pid_of_ookla - Success" ||
    raise "FAIL: Speedtest process not started" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

wait_ovsdb_entry Wifi_Speedtest_Status -w testid "$testid" -is status "0" -t ${ookla_timeout} &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: wait_ovsdb_entry - Wifi_Speedtest_Status::status is 0 - Success" ||
    raise "FAIL: wait_ovsdb_entry - Wifi_Speedtest_Status::status is not 0" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

# Check that a bind address can be specified

empty_ovsdb_table Wifi_Speedtest_Config ||
    raise "FAIL: Could not empty Wifi_Speedtest_Config: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

empty_ovsdb_table Wifi_Speedtest_Status ||
    raise "FAIL: Could not empty Wifi_Speedtest_Status: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

insert_ovsdb_entry Wifi_Speedtest_Config -i test_type "OOKLA" -i testid "$testid" -i st_bind_address "$FUT_ST_BIND_IP" &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: insert_ovsdb_entry - Wifi_Speedtest_Config::test_type - Success" ||
    raise "FAIL: insert_ovsdb_entry - Failed to insert Wifi_Speedtest_Config::test_type" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

sleep 1

pid_of_ookla=$(get_pid "$ookla_path")
[ -n "$pid_of_ookla" ] &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: Speedtest process started with pid $pid_of_ookla - Success" ||
    raise "FAIL: Speedtest process not started" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

wait_ovsdb_entry Wifi_Speedtest_Status -w testid "$testid" -is status "0" -t ${ookla_timeout} &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: wait_ovsdb_entry - Wifi_Speedtest_Status::status is 0 - Success" ||
    raise "FAIL: wait_ovsdb_entry - Wifi_Speedtest_Status::status is not 0" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

# Check that both bind interface and bind address can be specified

empty_ovsdb_table Wifi_Speedtest_Config ||
    raise "FAIL: Could not empty Wifi_Speedtest_Config: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

empty_ovsdb_table Wifi_Speedtest_Status ||
    raise "FAIL: Could not empty Wifi_Speedtest_Status: empty_ovsdb_table" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

insert_ovsdb_entry Wifi_Speedtest_Config -i test_type "OOKLA" -i testid "$testid" -i st_bind_interface "$FUT_ST_BIND_IF" -i st_bind_address "$FUT_ST_BIND_IP" &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: insert_ovsdb_entry - Wifi_Speedtest_Config::test_type - Success" ||
    raise "FAIL: insert_ovsdb_entry - Failed to insert Wifi_Speedtest_Config::test_type" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -ds

sleep 1

pid_of_ookla=$(get_pid "$ookla_path")
[ -n "$pid_of_ookla" ] &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: Speedtest process started with pid $pid_of_ookla - Success" ||
    raise "FAIL: Speedtest process not started" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

wait_ovsdb_entry Wifi_Speedtest_Status -w testid "$testid" -is status "0" -t ${ookla_timeout} &&
    log "othr/othr_verify_ookla_speedtest_bind_options.sh: wait_ovsdb_entry - Wifi_Speedtest_Status::status is 0 - Success" ||
    raise "FAIL: wait_ovsdb_entry - Wifi_Speedtest_Status::status is not 0" -l "othr/othr_verify_ookla_speedtest_bind_options.sh" -tc

pass
