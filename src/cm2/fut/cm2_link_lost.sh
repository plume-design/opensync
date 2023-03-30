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

cm_setup_file="cm2/cm2_setup.sh"
usage()
{
cat << usage_string
cm2/cm2_link_lost.sh [-h] arguments
Description:
    - Test script validates Connection_Manager_Uplink:has_L2 proper behaviour
Arguments:
    -h : show this help message
    \$1 (if_name) : used as L2 interface : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${cm_setup_file} (see ${cm_setup_file} -h)
                 Run: ./cm2/cm2_link_lost.sh <IF-NAME-L2>
Script usage example:
    ./cm2/cm2_link_lost.sh eth0
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

check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "cm2/cm2_link_lost.sh" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "cm2/cm2_link_lost.sh" -arg
if_name=$1

trap '
fut_info_dump_line
print_tables Connection_Manager_Uplink
set_interface_up "$if_name" || true
check_restore_management_access || true
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "cm2/cm2_link_lost.sh: CM2 test - has_L2 validation"

log "cm2/cm2_link_lost.sh: Dropping interface $if_name"
set_interface_down "$if_name" &&
    log "cm2/cm2_link_lost.sh: Interface $if_name is down - Success" ||
    raise "FAIL: Could not bring down interface $if_name" -l "cm2/cm2_link_lost.sh" -ds

log "cm2/cm2_link_lost.sh: Waiting for Connection_Manager_Uplink::has_L2 is false on $if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 false &&
    log "cm2/cm2_link_lost.sh: wait_ovsdb_entry - Interface $if_name has_L2 is false - Success" ||
    raise "FAIL: Connection_Manager_Uplink::has_L2 is not false" -l "cm2/cm2_link_lost.sh" -ow

log "cm2/cm2_link_lost.sh: Bringing up interface $if_name"
set_interface_up "$if_name" &&
    log "cm2/cm2_link_lost.sh: Interface $if_name is up - Success" ||
    raise "FAIL: Could not bring up interface $if_name" -l "cm2/cm2_link_lost.sh" -ds

log "cm2/cm2_link_lost.sh: Waiting for Connection_Manager_Uplink::has_L2 -> true for if_name==$if_name"
wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$if_name" -is has_L2 true &&
    log "cm2/cm2_link_lost.sh: wait_ovsdb_entry - Interface $if_name has_L2 is true - Success" ||
    raise "FAIL: Connection_Manager_Uplink::has_L2 is not true" -l "cm2/cm2_link_lost.sh" -ow

pass
