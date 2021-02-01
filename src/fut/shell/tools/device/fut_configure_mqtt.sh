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
source "${FUT_TOPDIR}/shell/lib/nm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
source "${LIB_OVERRIDE_FILE}"

tc_name="fsm/$(basename "$0")"
usage() {
    cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures MQTT settings in AWLAN_Node table
Arguments:
    -h  show this help message
    \$1 (hostname)    : MQTT hostname                          : (string)(required)
    \$2 (port)        : MQTT port                              : (string)(required)
    \$3 (location_id) : locationId for AWLAN_Node:mqtt_headers : (string)(required)
    \$4 (node_id)     : nodeId for AWLAN_Node:mqtt_headers     : (string)(required)
    \$5 (topics)      : topics for AWLAN_Node:mqtt_settings    : (string)(required)
Script usage example:
    ./${tc_name} 192.168.200.1 65002 1000 100 http
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

# INPUT ARGUMENTS:
NARGS=5
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
# Input arguments specific to GW, required:
hostname=${1}
port=${2}
location_id=${3}
node_id=${4}
topics=${5}
fut_ca_path="${FUT_TOPDIR}/shell/tools/device/files/fut_ca.pem"

log "$tc_name: Adding FUT cert to ca.pem"
cat "${fut_ca_path}" >> /var/certs/ca.pem ||
    raise "Failed to add fut cert to ca.pem" -ds -l "${tc_name}"

log "$tc_name: Configuring MQTT AWLAN_Node settings"
update_ovsdb_entry AWLAN_Node \
    -u mqtt_settings '["map",[["broker","'"${hostname}"'"],["compress","none"],["port","'"${port}"'"],["topics","'"${topics}"'"]]]' \
    -u mqtt_headers '["map",[["locationId","'"${location_id}"'"],["nodeId","'"${node_id}"'"]]]'

log "$tc_name: Restarting QM manager to instantly reconnects"
killall qm &&
    log "$tc_name: QM killed" ||
    log "$tc_name: QM not running, nothing to kill"

start_specific_manager qm &&
    log "$tc_name: QM killed" ||
    raise "Failed to start QM manager" -ds -l "${tc_name}"

set_manager_log QM TRACE ||
    raise "set_manager_log QM TRACE" -l "${tc_name}" -ds
