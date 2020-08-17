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


if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source /tmp/fut-base/shell/config/default_shell.sh
fi
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
source "${LIB_OVERRIDE_FILE}"

usage="
$(basename "$0") [-h] \$1 \$2

where options are:
    -h  show this help message

where arguments are:
    manager_addr=\$1 -- used as manager address - (string)(required)
    target=\$2 -- used to check manager address resolved - (string)(required)

this script is dependent on following:
    - running DM manager

example of usage:
   /tmp/fut-base/shell/onbrd/$(basename "$0") ssl:ec2-54-200-0-59.us-west-2.compute.amazonaws.com:443 ssl:54.200.0.59:443
   /tmp/fut-base/shell/onbrd/$(basename "$0") ssl:54.200.0.59:443 ssl:54.200.0.59:443
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [ $# -lt 2 ]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

manager_addr=$1

# Restart managers to start every config resolution from the begining
restart_managers

# Give time to managers to bring up tables
sleep 30

tc_name="onbrd/$(basename "$0")"

log "$tc_name: Setting AWLAN_Node manager_addr to '$manager_addr'"
update_ovsdb_entry AWLAN_Node -u manager_addr "$manager_addr" &&
    log "$tc_name: update_ovsdb_entry - AWLAN_Node table updated - manager_addr '$manager_addr'" ||
    raise "update_ovsdb_entry - Failed to update AWLAN_Node table - manager_addr '$manager_addr'" -l "$tc_name" -tc

log "$tc_name: ONBRD Verify manager hostname resolved, waiting for Manager is_connected true"
wait_ovsdb_entry Manager -is is_connected true &&
    log "$tc_name: wait_ovsdb_entry - Manager is_connected is true" ||
    raise "wait_ovsdb_entry - Manager is_connected is NOT true" -l "$tc_name" -tc

shift
targets=$@

# shellcheck disable=SC2034
for i in $(seq 1 $#); do

    target=$1
    shift

    wait_ovsdb_entry Manager -is target "$target" &&
        { log "$tc_name: wait_ovsdb_entry - Manager target is '$target'"; pass; } ||
        log "$tc_name: wait_ovsdb_entry - Manager target is NOT '$target'"

done

die "onbrd/$(basename "$0"): wait_ovsdb_entry - Manager target NOT resolved"
