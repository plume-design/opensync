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
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Validate dhcp dry run was successful.
    - Connection_Manager_Uplink::has_L3 is true indicating the OFFER message from DHCP server was received.
Arguments:
    -h  show this help message
    \$1 (if_name) : Used to define WAN interface : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <IF-NAME>
Script usage example:
   ./${tc_name} eth0
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
print_tables Connection_Manager_Uplink
fut_info_dump_line
interface_bring_up "$if_name" || true
check_restore_management_access || true
run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "${tc_name}" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg
if_name=$1

log_title "$tc_name: ONBRD test - Verify DHCP dry run success"

log "$tc_name: Checking if WANO is enabled, if yes, skip..."
check_kconfig_option "CONFIG_MANAGER_WANO" "y" &&
    raise "WANO manager is enabled, skipping test!" -l "${tc_name}" -s

# Toggling the uplink interface DOWN/UP
log "$tc_name: Dropping interface $if_name"
interface_bring_down "$if_name" &&
    log "$tc_name: Interface $if_name is down - Success" ||
    raise "FAIL: Could not bring down interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L2 is false on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 false &&
    log "$tc_name: wait_ovsdb_entry - Interface $if_name has_L2 is false - Success" ||
    raise "FAIL: wait_ovsdb_entry - Connection_Manager_Uplink::has_L2 is not false" -l "$tc_name" -tc

log "$tc_name: Bringing up interface $if_name"
interface_bring_up "$if_name" &&
    log "$tc_name: Interface $if_name is up - Success" ||
    raise "FAIL: Could not bring up interface $if_name" -l "$tc_name" -ds

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L2 is true on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 true &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink::has_L2 is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Connection_Manager_Uplink::has_L2 is not 'true'" -l "$tc_name" -tc

log "$tc_name: Waiting for Connection_Manager_Uplink::has_L3 is true on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L3 true &&
    log "$tc_name: wait_ovsdb_entry - Connection_Manager_Uplink::has_L3 is 'true' - Success" ||
    raise "FAIL: wait_ovsdb_entry - Connection_Manager_Uplink::has_L3 is not 'true'" -l "$tc_name" -tc

pass
