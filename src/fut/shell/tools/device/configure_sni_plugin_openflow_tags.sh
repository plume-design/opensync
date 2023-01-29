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
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

usage() {
    cat << usage_string
configure_sni_plugin_openflow_tags.sh [-h] arguments
Description:
    - Script configures FSM tag settings to the OVSDB Openflow_Tag table.
    \$1 (mac_dut)          : DUT MAC address         : (string)(required)
    \$2 (mac_client)       : Client MAC address      : (string)(required)
    \$3 (mac_rpi_server)   : RPI server MAC address  : (string)(required)
Arguments:
    -h  show this help message
Script usage example:
    ./configure_sni_plugin_openflow_tags.sh <MAC_DUT> <MAC_CLIENT> <MAC_RPI_SERVER>
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
print_tables Openflow_Tag
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=3
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "tools/device/configure_sni_plugin_openflow_tags.sh" -arg
mac_dut=${1}
mac_client=${2}
mac_rpi_server=${3}

insert_ovsdb_entry Openflow_Tag \
    -i name all_gateways \
    -i cloud_value ["set",["$mac_dut","$mac_rpi_server"]] &&
        log "configure_sni_plugin_openflow_tags.sh: 'all_gateways' inserted to Openflow_Tag - Success" ||
        raise "FAIL: Failed to insert 'all_gateways' to Openflow_Tag" -l "configure_sni_plugin_openflow_tags.sh" -oe

insert_ovsdb_entry Openflow_Tag \
    -i name dpi-devices \
    -i cloud_value ["set",["$mac_client"]] &&
        log "configure_sni_plugin_openflow_tags.sh: 'dpi-devices' inserted to Openflow_Tag - Success" ||
        raise "FAIL: Failed to insert 'dpi-devices' to Openflow_Tag" -l "configure_sni_plugin_openflow_tags.sh" -oe

insert_ovsdb_entry Openflow_Tag \
    -i name gateways \
    -i cloud_value ["set",["$mac_rpi_server"]] &&
        log "configure_sni_plugin_openflow_tags.sh: 'gateways' inserted to Openflow_Tag - Success" ||
        raise "FAIL: Failed to insert 'gateways' to Openflow_Tag" -l "configure_sni_plugin_openflow_tags.sh" -oe

insert_ovsdb_entry Openflow_Tag \
    -i name walleye_sni_attrs \
    -i cloud_value '["set",["http.host","http.url","tls.sni"]]' &&
        log "configure_sni_plugin_openflow_tags.sh: 'walleye_sni_attrs' inserted to Openflow_Tag - Success" ||
        raise "FAIL: Failed to insert 'walleye_sni_attrs' to Openflow_Tag" -l "configure_sni_plugin_openflow_tags.sh" -oe

pass
