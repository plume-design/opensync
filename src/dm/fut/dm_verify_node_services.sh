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
source "${FUT_TOPDIR}/shell/lib/dm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="dm/dm_setup.sh"
usage()
{
cat << usage_string
dm/dm_verify_node_services.sh [-h] arguments
Description:
    - Verify Node_Services table is correctly populated
    - Check if Node_Services table contains service from test case config
    - Check if Node_Services table having enable field for service is set
      to true and service is running
Arguments:
    -h  show this help message
    \$1 (service)     : service to verify : (string)(required)
    \$2 (kconfig_val) : kconfig value used to check service is supported on device or not : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./dm/dm_verify_node_services.sh <service> <KCONFIG-VALUE>
Script usage example:
    ./dm/dm_verify_node_services.sh wm CONFIG_MANAGER_WM
    ./dm/dm_verify_node_services.sh blem CONFIG_MANAGER_BLEM
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

NARGS=2
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input arguments" -l "dm/dm_verify_node_services.sh" -arg
service=${1}
kconfig_val=${2}

trap '
fut_info_dump_line
print_tables Node_Services
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "dm/dm_verify_node_services.sh: DM test - Verify Node_Services table contains given service, respective enable field is set to true and is running"

check_kconfig_option "$kconfig_val" "y" &&
    log "dm/dm_verify_node_services.sh: $kconfig_val = y - KCONFIG exists on the device - Success" ||
    raise "FAIL: $kconfig_val - KCONFIG is not supported on the device" -l "dm/dm_verify_node_services.sh" -s

check_ovsdb_entry Node_Services -w service "$service" &&
    log "dm/dm_verify_node_services.sh: Node_Services table contains $service - Success" ||
    raise "FAIL: Node_Services table does not contain $service" -l "dm/dm_verify_node_services.sh" -tc

if [ $(get_ovsdb_entry_value Node_Services enable -w service $service) == "true" ]; then
    log "dm/dm_verify_node_services.sh: $service from Node_Services table that have enable field set to true"
    if [ -n $($(get_process_cmd) | grep /usr/opensync/bin/$service | grep -v 'grep' | wc -l) ]; then
        log "dm/dm_verify_node_services.sh: $service from Node_Services table is running - Success"
    else
        raise "FAIL: $service from Node_Services table is not running" -l "dm/dm_verify_node_services.sh" -tc
    fi
else
    raise "FAIL: $service from Node_Services table that have enable field not set to true"
fi

pass
