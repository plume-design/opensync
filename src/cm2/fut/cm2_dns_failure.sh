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


# Include basic environment config from default shell file and if any from FUT framework generated /tmp/fut_set_env.sh file
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi

source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/cm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"

trap '
    check_restore_management_access || true
    run_setup_if_crashed cm || true
' EXIT SIGINT SIGTERM

usage="
$(basename "$0") [-h] \$1

where options are:
    -h  show this help message

where arguments are:
    test_type=\$@ -- used as test step - (string)(required)
    manager_addr=\$@ -- used as manager address to be resolved - (string)(required)

this script is dependent on following:
    - running CM manager
    - running NM manager

example of usage:
    /tmp/fut-base/shell/cm2/$(basename "$0") dns_blocked ssl:54.200.0.59:443
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

# Input parameters
if [ $# -lt 2 ]; then
    echo 1>&2 "$0: incorrect number of input arguments"
    echo "$usage"
    exit 2
fi

test_type=$1
manager_addr=$2

tc_name="cm2/$(basename "$0")"
log "$tc_name: CM2 test - DNS Failure"

if [ "$test_type" = "dns_blocked" ]; then
    log "$tc_name: Setting AWLAN_Node manager_addr to '$manager_addr'"
    update_ovsdb_entry AWLAN_Node -u manager_addr "$manager_addr" &&
        log "$tc_name: update_ovsdb_entry - AWLAN_Node table updated - manager_addr $manager_addr" ||
        raise "update_ovsdb_entry - Failed to update AWLAN_Node table - manager_addr $manager_addr" -l "$tc_name" -tc

    log "$tc_name: Waiting for Cloud status to go to BACKOFF"
    wait_cloud_state BACKOFF &&
        log "$tc_name: wait_cloud_state - Cloud set to BACKOFF" ||
        raise "Failed to set cloud to BACKOFF" -l "$tc_name" -tc

    log "$tc_name: Waiting for Cloud status not to become ACTIVE"
    wait_cloud_state_not ACTIVE &&
        log "$tc_name: wait_cloud_state - Cloud stayed in BACKOFF" ||
        raise "Cloud set to ACTIVE - but it should not be" -l "$tc_name" -tc
elif [ "$test_type" = "dns_recovered" ]; then
    log "$tc_name: Setting AWLAN_Node manager_addr to '$manager_addr'"
    update_ovsdb_entry AWLAN_Node -u manager_addr "$manager_addr" &&
        log "$tc_name: update_ovsdb_entry - AWLAN_Node table updated - manager_addr $manager_addr" ||
        raise "update_ovsdb_entry - Failed to update AWLAN_Node table - manager_addr $manager_addr" -l "$tc_name" -tc

    log "$tc_name: Waiting for Cloud status to go to BACKOFF"
    wait_cloud_state BACKOFF &&
        log "$tc_name: wait_cloud_state - Cloud set to BACKOFF" ||
        raise "Failed to set cloud to BACKOFF" -l "$tc_name" -tc

    log "$tc_name: Waiting for Cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log "$tc_name: wait_cloud_state - Cloud set to ACTIVE" ||
        raise "Failed to set cloud to ACTIVE" -l "$tc_name" -tc
else
    raise "Wrong test type option" -l "$tc_name" -arg
fi

pass
