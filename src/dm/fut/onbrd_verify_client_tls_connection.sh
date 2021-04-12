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
source "${FUT_TOPDIR}/shell/lib/onbrd_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="onbrd/$(basename "$0")"
manager_setup_file="onbrd/onbrd_setup.sh"
haproxy_cfg_path="tools/rpi/files/haproxy.cfg"
fut_cloud_start_path="tools/rpi/start_cloud_simulation.sh"
usage()
{
cat << usage_string
${tc_name} [-h]
Description:
    - Validate CM connecting to specific Cloud TLS version
Arguments:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
    - On RPI SERVER:
        - Edit ${haproxy_cfg_path} to change TLS version
            Look for:
              ssl-default-bind-options force-tlsv<TLS-VERSION> ssl-max-ver TLSv<TLS-VERSION> ssl-min-ver TLSv<TLS-VERSION>
              ssl-default-server-options force-tlsv<TLS-VERSION> ssl-max-ver TLSv<TLS-VERSION> ssl-min-ver TLSv<TLS-VERSION>
            Change <TLS-VERSION> to one of following: 1.0, 1.1. 1.2
        - Run ./${fut_cloud_start_path} -r
    - On DEVICE: Run: ./${tc_name}
Script usage example:
   ./${tc_name}
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

log_title "${tc_name}: ONBRD test - Verify client TLS connection"

log "${tc_name}: Setting CM log level to TRACE"
set_manager_log CM TRACE

log "${tc_name}: Saving current state of AWLAN_Node/SSL ovsdb tables for revert after"
# connect_to_fut_cloud() updates only SSL::ca_cert, save original for later use
ssl_ca_cert_org="$(get_ovsdb_entry_value SSL ca_cert)"
# connect_to_fut_cloud() updates AWLAN_Node::redirector_addr and Manager::inactivity_probe::min_backoff::max_backoff, save original for later use
an_redirector_addr_org="$(get_ovsdb_entry_value AWLAN_Node redirector_addr)"
m_inactivity_probe_org="$(get_ovsdb_entry_value Manager inactivity_probe)"
m_min_backoff_org="$(get_ovsdb_entry_value AWLAN_Node min_backoff)"
m_max_backoff_org="$(get_ovsdb_entry_value AWLAN_Node max_backoff)"
log -deb "${tc_name}: Backup values"
log -deb "   SSL        :: ca_cert          := ${ssl_ca_cert_org}"
log -deb "   AWLAN_Node :: redirector_addr  := ${an_redirector_addr_org}"
log -deb "   Manager    :: inactivity_probe := ${m_inactivity_probe_org}"
log -deb "   AWLAN_Node :: min_backoff      := ${m_min_backoff_org}"
log -deb "   AWLAN_Node :: max_backoff      := ${m_max_backoff_org}"

trap '
    fut_info_dump_line
    print_tables SSL Manager AWLAN_Node
    fut_info_dump_line
    [ -n "${ssl_ca_cert_org}" ] && update_ovsdb_entry SSL -u ca_cert "${ssl_ca_cert_org}" || true
    [ -n "${an_redirector_addr_org}" ] && update_ovsdb_entry AWLAN_Node -u redirector_addr "${an_redirector_addr_org}" || true
    [ -n "${m_inactivity_probe_org}" ] && update_ovsdb_entry Manager -u inactivity_probe "${m_inactivity_probe_org}" || true
    [ -n "${m_min_backoff_org}" ] && update_ovsdb_entry AWLAN_Node -u min_backoff "${m_min_backoff_org}" || true
    [ -n "${m_max_backoff_org}" ] && update_ovsdb_entry AWLAN_Node -u max_backoff "${m_max_backoff_org}" || true
' EXIT SIGINT SIGTERM

connect_to_fut_cloud &&
    log "${tc_name}: Device connected to FUT cloud. Start test case execution" ||
    raise "Failed to connect device to FUT cloud. Terminate test" -l "${tc_name}" -tc

# Check if connection is maintained for 60s
log "${tc_name}: Checking if connection is maintained and stable"
for interval in $(seq 1 3); do
    log "${tc_name}: Sleeping for 20 seconds"
    sleep 20
    log "${tc_name}: Check connection status in Manager table is ACTIVE, check num: $interval"
    ${OVSH} s Manager status -r | grep "ACTIVE" &&
        log "${tc_name}: wait_cloud_state - Connection state is ACTIVE, check num: $interval" ||
        raise "wait_cloud_state - FAILED: Connection state is NOT ACTIVE, check num: $interval, connection should be maintained" -l "${tc_name}" -tc
done

pass
