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
addr_internet_man_file="tools/server/cm/address_internet_man.sh"
step_1_name="dns_blocked"
step_2_name="dns_recovered"
usage()
{
cat << usage_string
cm2/cm2_dns_failure.sh [-h] arguments
Description:
    - Script checks if CM properly sets Manager 'status' field in case of DNS being blocked-unblocked
      DNS block fail cases:
          CM fails to set Manager 'status' to BACKOFF
          CM fails to remain in BACKOFF
      DNS unblock fail cases:
          CM fails to set Manager 'status' to BACKOFF
          CM fails to set Manager 'status' to ACTIVE
Arguments:
    -h : show this help message
    \$1 (test_step)    : used as test step                      : (string)(required) : (${step_1_name}, ${step_2_name})
Testcase procedure:
    - On DEVICE: Run: ${cm_setup_file} (see ${cm_setup_file} -h)
    - On RPI SERVER: Run: ${addr_internet_man_file} <WAN-IP-ADDRESS> block
    - On DEVICE: Run: cm2/cm2_dns_failure.sh ${step_1_name}
    - On RPI SERVER: Run: ${addr_internet_man_file} <WAN-IP-ADDRESS> unblock
    - On DEVICE: Run: cm2/cm2_dns_failure.sh ${step_2_name}
Script usage example:
    ./cm2/cm2_dns_failure.sh ${step_1_name}
    ./cm2/cm2_dns_failure.sh ${step_2_name}
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
    raise "TARGET_CAP_EXTENDER != y - Testcase applicable only for EXTENDER-s" -l "cm2/cm2_dns_failure.sh" -s

NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "cm2/cm2_dns_failure.sh" -arg
test_step=${1}

trap '
fut_info_dump_line
print_tables AWLAN_Node Manager
check_restore_management_access || true
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "cm2/cm2_dns_failure.sh: CM2 test - DNS Failure - $test_step"

if [ "$test_step" = "${step_1_name}" ]; then
    redirector_addr=$(get_ovsdb_entry_value AWLAN_Node redirector_addr)
    none_redirector_addr='ssl:none:443'

    clear_dns_cache &&
        log "cm2/cm2_dns_failure.sh - DNS cache clear - Success" ||
        raise "FAIL: DNS cache clear failed" -l "cm2/cm2_dns_failure.sh" -s

    log "cm2/cm2_dns_failure.sh: Setting AWLAN_Node::redirector_addr to $none_redirector_addr to initiate CM reconnection (resolving)"
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${none_redirector_addr}"
        log "cm2/cm2_dns_failure.sh - AWLAN_Node::redirector_addr set to ${none_redirector_addr} - Success" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ${none_redirector_addr}" -l "cm2/cm2_dns_failure.sh" -oe

    log "cm2/cm2_dns_failure.sh: Waiting for Cloud status to go to BACKOFF"
    wait_cloud_state BACKOFF &&
        log "cm2/cm2_dns_failure.sh: wait_cloud_state - Detected Cloud status BACKOFF - Success" ||
        raise "FAIL: wait_cloud_state - Failed to detect Cloud status BACKOFF" -l "cm2/cm2_dns_failure.sh" -tc

    log "cm2/cm2_dns_failure.sh: Setting AWLAN_Node::redirector_addr to $redirector_addr to initiate CM reconnection (resolving)"
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr}"
        log "cm2/cm2_dns_failure.sh - AWLAN_Node::redirector_addr set to ${redirector_addr} - Success" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ${redirector_addr}" -l "cm2/cm2_dns_failure.sh" -oe

    log "cm2/cm2_dns_failure.sh: Waiting for Cloud status to stay BACKOFF"
    wait_cloud_state BACKOFF &&
        log "cm2/cm2_dns_failure.sh: wait_cloud_state - Detected Cloud status BACKOFF - Success" ||
        raise "FAIL: wait_cloud_state - Failed to detect Cloud status BACKOFF" -l "cm2/cm2_dns_failure.sh" -tc

    log "cm2/cm2_dns_failure.sh: Making sure Cloud status does not become ACTIVE"
    wait_cloud_state_not ACTIVE 120 &&
        log "cm2/cm2_dns_failure.sh: wait_cloud_state - Cloud stayed in BACKOFF - Success" ||
        raise "FAIL: Cloud set to ACTIVE - but it should not be" -l "cm2/cm2_dns_failure.sh" -tc
elif [ "$test_step" = "${step_2_name}" ]; then
    log "cm2/cm2_dns_failure.sh: Waiting for Cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log "cm2/cm2_dns_failure.sh: wait_cloud_state - Detected Cloud status ACTIVE - Success" ||
        raise "FAIL: wait_cloud_state - Failed to detect Cloud status ACTIVE" -l "cm2/cm2_dns_failure.sh" -tc
else
    raise "FAIL: Wrong test type option" -l "cm2/cm2_dns_failure.sh" -arg
fi

pass
