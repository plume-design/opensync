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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
onbrd/onbrd_verify_home_vaps_on_radios.sh [-h] arguments
Description:
    - Validate home VAPs on radios exist
Arguments:
    -h  show this help message
    \$1 (if_name) : used as interface name to check : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_verify_home_vaps_on_radios.sh <IF-NAME>
Script usage example:
   ./onbrd/onbrd_verify_home_vaps_on_radios.sh home-ap-24
   ./onbrd/onbrd_verify_home_vaps_on_radios.sh home-ap-l50
   ./onbrd/onbrd_verify_home_vaps_on_radios.sh home-ap-u50
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
print_tables Wifi_VIF_Config Wifi_VIF_State
ifconfig | grep -qwE "$if_name"
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "onbrd/onbrd_verify_home_vaps_on_radios.sh" -arg
if_name=$1

log_title "onbrd/onbrd_verify_home_vaps_on_radios.sh: ONBRD test - Verify home VAPs on all radios, check interface '${if_name}'"

wait_for_function_response 0 "check_ovsdb_entry Wifi_VIF_State -w if_name $if_name" &&
    log "onbrd/onbrd_verify_home_vaps_on_radios.sh: Interface $if_name exists - Success" ||
    raise "FAIL: Interface $if_name does not exist" -l "onbrd/onbrd_verify_home_vaps_on_radios.sh" -tc

wait_for_function_response 0 "check_interface_exists $if_name" &&
    log "onbrd/onbrd_verify_home_vaps_on_radios.sh: Interface $if_name exists on system - Success" ||
    raise "FAIL: Interface $if_name does not exist on system" -l "onbrd/onbrd_verify_home_vaps_on_radios.sh" -tc

log "onbrd/onbrd_verify_home_vaps_on_radios.sh: Clean created interfaces after test"
vif_clean

pass
