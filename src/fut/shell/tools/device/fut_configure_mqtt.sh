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

default_device_cert_path="/var/certs/"
usage() {
    cat << usage_string
tools/device/fut_configure_mqtt.sh [-h] arguments
Description:
    - Script configures MQTT settings in AWLAN_Node table
Arguments:
    -h  show this help message
    \$1 (hostname)    : MQTT hostname                          : (string)(required)
    \$2 (port)        : MQTT port                              : (string)(required)
    \$3 (location_id) : locationId for AWLAN_Node:mqtt_headers : (string)(required)
    \$4 (node_id)     : nodeId for AWLAN_Node:mqtt_headers     : (string)(required)
    \$5 (topics)      : topics for AWLAN_Node:mqtt_settings    : (string)(required)
    \$6 (dut_ca_path) : Path to certificates folder on DUT     : (string)(optional)(default=${default_device_cert_path})
Script usage example:
    ./tools/device/fut_configure_mqtt.sh 192.168.200.1 65002 1000 100 http
    ./tools/device/fut_configure_mqtt.sh 192.168.200.1 65002 1000 100 http /var/certs/
usage_string
}

trap '
fut_ec=$?
fut_info_dump_line
if [ $fut_ec -ne 0 ]; then 
    print_tables AWLAN_Node SSL
fi
fut_info_dump_line
exit $fut_ec
' EXIT SIGINT SIGTERM

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

# INPUT ARGUMENTS:
NARGS=5
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
# Input arguments specific to GW, required:
hostname=${1}
port=${2}
location_id=${3}
node_id=${4}
topics=${5}
certs_dir=${6:-$default_device_cert_path}
fut_server_cert_path="${FUT_TOPDIR}/shell/tools/server/certs/ca.crt"
fut_client_cert_path="${FUT_TOPDIR}/shell/tools/server/certs/client.crt"
fut_client_key_path="${FUT_TOPDIR}/shell/tools/server/certs/client.key"

cat "${fut_server_cert_path}" > "${certs_dir}/ca.pem"
cat "${fut_client_cert_path}" > "${certs_dir}/client.pem"
cat "${fut_client_key_path}" > "${certs_dir}/client_dec.key"

log "tools/device/fut_configure_mqtt.sh: Configuring MQTT AWLAN_Node settings"
update_ovsdb_entry AWLAN_Node \
    -u mqtt_settings '["map",[["broker","'"${hostname}"'"],["compress","none"],["port","'"${port}"'"],["topics","'"${topics}"'"]]]' \
    -u mqtt_headers '["map",[["locationId","'"${location_id}"'"],["nodeId","'"${node_id}"'"]]]'

log "tools/device/fut_configure_mqtt.sh: Restarting QM manager to instantly reconnects"
killall qm &&
    log "tools/device/fut_configure_mqtt.sh: QM killed" ||
    log "tools/device/fut_configure_mqtt.sh: QM not running, nothing to kill"

start_specific_manager qm &&
    log "tools/device/fut_configure_mqtt.sh: QM killed" ||
    raise "FAIL: to start QM manager" -ds -l "tools/device/fut_configure_mqtt.sh"

set_manager_log QM TRACE ||
    raise "FAIL: set_manager_log QM TRACE" -l "tools/device/fut_configure_mqtt.sh" -ds
