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

manager_setup_file="onbrd/onbrd_setup.sh"
usage()
{
cat << usage_string
onbrd/onbrd_verify_manager_hostname_resolved.sh [-h] arguments
Description:
    - Validate AWLAN_Node manager_addr being resolved in Manager target
Arguments:
    -h  show this help message
    \$1 is_extender : 'true' - device is an extender, 'false' - device is a residential_gateway : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./onbrd/onbrd_verify_manager_hostname_resolved.sh <true/false>
Script usage example:
   ./onbrd/onbrd_verify_manager_hostname_resolved.sh true    #if device is a extender
   ./onbrd/onbrd_verify_manager_hostname_resolved.sh false   #if device is a gateway
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
print_tables AWLAN_Node Manager
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly ${NARGS} input argument" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -arg
is_extender=${1}

if [ $is_extender == "true" ]; then
    check_kconfig_option "TARGET_CAP_EXTENDER" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Device is not EXTENDER capable" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -s
elif [ $is_extender == "false" ]; then
    check_kconfig_option "TARGET_CAP_GATEWAY" "y" ||
    raise "TARGET_CAP_EXTENDER != y - Device is not a Gateway" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -s
else
    raise "Wrong option" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -s
fi

log_title "onbrd/onbrd_verify_manager_hostname_resolved.sh: ONBRD test - Verify if AWLAN_Node manager address hostname is resolved"

# Restart managers to start every config resolution from the begining
restart_managers
# Give time to managers to bring up tables
sleep 30

redirector_addr_none="ssl:none:443"
wait_for_function_response 'notempty' "get_ovsdb_entry_value AWLAN_Node redirector_addr" &&
    redirector_addr=$(get_ovsdb_entry_value AWLAN_Node redirector_addr) ||
    raise "FAIL: AWLAN_Node::redirector_addr is not set" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -tc

if [ $is_extender == "true" ]; then
    log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Setting AWLAN_Node redirector_addr to ${redirector_addr_none}"
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr_none}" &&
        log "onbrd/onbrd_verify_manager_hostname_resolved.sh: AWLAN_Node::redirector_addr updated - Success" ||
        raise "FAIL: Could not update AWLAN_Node::redirector_addr" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -oe

    log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Wait Manager target to clear"
    wait_for_function_response 'empty' "get_ovsdb_entry_value Manager target" &&
        log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Manager::target is cleared - Success" ||
        raise "FAIL: Manager::target is not cleared" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -tc

    log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Setting AWLAN_Node redirector_addr to ${redirector_addr}"
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr}" &&
        log "onbrd/onbrd_verify_manager_hostname_resolved.sh: AWLAN_Node::redirector_addr updated - Success" ||
        raise "FAIL: Could not update AWLAN_Node::redirector_addr" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -oe
fi

log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Wait Manager target to resolve to address"
wait_for_function_response 'notempty' "get_ovsdb_entry_value Manager target" &&
    log "onbrd/onbrd_verify_manager_hostname_resolved.sh: Manager::target is set - Success" ||
    raise "FAIL: Manager::target is not set" -l "onbrd/onbrd_verify_manager_hostname_resolved.sh" -tc

print_tables Manager
pass
