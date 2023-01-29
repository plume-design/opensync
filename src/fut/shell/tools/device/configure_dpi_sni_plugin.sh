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
# shellcheck disable=SC1091,SC2016
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage() {
    cat << usage_string
configure_dpi_sni_plugin.sh [-h] arguments
Description:
    Script configures FSM settings for:
        - Walleye
        - DPI SNI client
        - Gatekeeper and
        - dispatcher to Flow_Service_Manager_Config.
Arguments:
    -h  show this help message
    \$1 (location_id)  : DUT Location ID  : (string)(required)
    \$2 (node_id)      : DUT Node Id      : (string)(required)
Script usage example:
    ./configure_dpi_sni_plugin.sh  <LOCATION_ID> <NODE_ID>
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
print_tables Flow_Service_Manager_Config
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=2
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
location_id=${1}
node_id=${2}

# Configure Walleye DPI
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler walleye_dpi \
    -i type dpi_plugin \
    -i other_config "[\"map\",[[\"dpi_dispatcher\",\"core_dpi_dispatch\"], [\"dso_init\",\"walleye_dpi_plugin_init\"], [\"mqtt_v\",\"dev-test/dev_dpi_walleye/${node_id}/${location_id}\"]]]" &&
        log "configure_dpi_sni_plugin.sh: Walleye DPI configuration inserted to Flow_Service_Manager_Config - Success" ||
        raise "FAIL: Failed to insert Walleye DPI configuration to Flow_Service_Manager_Config" -l "configure_dpi_sni_plugin.sh" -oe

# Configure DPI SNI client
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler dpi_sni \
    -i type dpi_client \
    -i other_config '["map",[["dpi_plugin","walleye_dpi"],["dso_init","dpi_sni_plugin_init"],["flow_attributes","${walleye_sni_attrs}"],["policy_table","gatekeeper"],["provider_plugin","gatekeeper"]]]' &&
        log "configure_dpi_sni_plugin.sh: DPI SNI client configuration inserted to Flow_Service_Manager_Config - Success" ||
        raise "FAIL: Failed to insert DPI SNI client configuration to Flow_Service_Manager_Config" -l "configure_dpi_sni_plugin.sh" -oe

# Configure Gatekeeper (Controller based Gatekeeper)
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler gatekeeper \
    -i type web_cat_provider \
    -i other_config '["map",[["gk_url","https://fut.opensync.io:8443/gatekeeper"],["dso_init","gatekeeper_plugin_init"],["cacert","'${FUT_TOPDIR}'/shell/tools/server/certs/ca.pem"]]]' &&
        log "configure_dpi_sni_plugin.sh: Gatekeeper configuration inserted to Flow_Service_Manager_Config - Success" ||
        raise "FAIL: Failed to insert Gatekeeper configuration to Flow_Service_Manager_Config" -l "configure_dpi_sni_plugin.sh" -oe

# Configure dispatcher
insert_ovsdb_entry Flow_Service_Manager_Config \
    -i handler core_dpi_dispatch \
    -i type dpi_dispatcher \
    -i if_name "br-home.devdpi" \
    -i other_config '["map",[["excluded_devices","${all_gateways}"]]]' &&
        log "configure_dpi_sni_plugin.sh: Dispatcher configuration inserted to Flow_Service_Manager_Config - Success" ||
        raise "FAIL: Failed to insert Dispatcher configuration to Flow_Service_Manager_Config" -l "configure_dpi_sni_plugin.sh" -oe

pass
