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
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="cm2/$(basename "$0")"
cm_setup_file="cm2/cm2_setup.sh"
adr_internet_man_file="tools/rpi/cm/address_internet_man.sh"
step_1_name="dns_blocked"
step_2_name="dns_recovered"
usage()
{
cat << usage_string
${tc_name} [-h] arguments
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
    - On RPI SERVER: Run: ${adr_internet_man_file} <WAN-IP-ADDRESS> block
    - On DEVICE: Run: ${tc_name} ${step_1_name}
    - On RPI SERVER: Run: ${adr_internet_man_file} <WAN-IP-ADDRESS> unblock
    - On DEVICE: Run: ${tc_name} ${step_2_name}
Script usage example:
    ./${tc_name} ${step_1_name}
    ./${tc_name} ${step_2_name}
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
NARGS=1
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "${tc_name}" -arg

trap '
check_restore_management_access || true
run_setup_if_crashed cm || true' EXIT SIGINT SIGTERM

test_step=${1}

log_title "$tc_name: CM2 test - DNS Failure"

if [ "$test_step" = "${step_1_name}" ]; then
    redirector_addr=$(get_ovsdb_entry_value AWLAN_Node redirector_addr)
    none_redirector_addr='ssl:none:443'
    log "$tc_name: Re-setting AWLAN_Node::redirector_addr to initiate CM reconnection (resolving)"
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${none_redirector_addr}"
        log -deb "$tc_name - AWLAN_Node redirector_addr set to ${none_redirector_addr}" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ${none_redirector_addr}" -l "$tc_name" -ds
    wait_cloud_state BACKOFF &&
        log "$tc_name: wait_cloud_state - Cloud set to BACKOFF" ||
        raise "Failed to set cloud to BACKOFF" -l "$tc_name" -tc
    update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr}"
        log -deb "$tc_name - AWLAN_Node redirector_addr set to ${redirector_addr}" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ${redirector_addr}" -l "$tc_name" -ds
    log "$tc_name: Waiting for Cloud status to go to BACKOFF"
    wait_cloud_state BACKOFF &&
        log "$tc_name: wait_cloud_state - Cloud set to BACKOFF" ||
        raise "Failed to set cloud to BACKOFF" -l "$tc_name" -tc
    log "$tc_name: Waiting for Cloud status not to become ACTIVE"
    wait_cloud_state_not ACTIVE 120 &&
        log "$tc_name: wait_cloud_state - Cloud stayed in BACKOFF" ||
        raise "Cloud set to ACTIVE - but it should not be" -l "$tc_name" -tc
elif [ "$test_step" = "${step_2_name}" ]; then
    log "$tc_name: Waiting for Cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log "$tc_name: wait_cloud_state - Cloud set to ACTIVE" ||
        raise "Failed to set cloud to ACTIVE" -l "$tc_name" -tc
else
    raise "Wrong test type option" -l "$tc_name" -arg
fi
pass
