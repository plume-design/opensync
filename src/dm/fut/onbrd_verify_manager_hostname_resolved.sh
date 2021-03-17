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
usage()
{
cat << usage_string
${tc_name} [-h] arguments
Description:
    - Validate AWLAN_Node manager_addr being resolved in Manager target
Arguments:
    -h  show this help message
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name}
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

log_title "$tc_name: ONBRD test - Verify if AWLAN_Node manager address hostname is resolved"

# Restart managers to start every config resolution from the begining
restart_managers
# Give time to managers to bring up tables
sleep 30

redirector_addr_none="ssl:none:443"
wait_for_function_response 'notempty' "get_ovsdb_entry_value AWLAN_Node redirector_addr" &&
    redirector_addr=$(get_ovsdb_entry_value AWLAN_Node redirector_addr) ||
    raise "AWLAN_Node::redirector_addr is not set" -l "${tc_name}" -tc

log "$tc_name: Setting AWLAN_Node redirector_addr to ${redirector_addr_none}"
update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr_none}" &&
    log "$tc_name: AWLAN_Node::redirector_addr updated" ||
    raise "Could not update AWLAN_Node::redirector_addr" -l "$tc_name" -tc

log "${tc_name}: Wait Manager target to clear"
wait_for_function_response 'empty' "get_ovsdb_entry_value Manager target" &&
    log "${tc_name}: Manager::target is cleared" ||
    raise "Manager::target is not cleared" -l "${tc_name}" -tc

log "$tc_name: Setting AWLAN_Node redirector_addr to ${redirector_addr}"
update_ovsdb_entry AWLAN_Node -u redirector_addr "${redirector_addr}" &&
    log "$tc_name: AWLAN_Node::redirector_addr updated" ||
    raise "Could not update AWLAN_Node::redirector_addr" -l "$tc_name" -tc

log "${tc_name}: Wait Manager target to resolve to address"
wait_for_function_response 'notempty' "get_ovsdb_entry_value Manager target" &&
    log "${tc_name}: Manager::target is set" ||
    raise "Manager::target is not set" -l "${tc_name}" -tc

print_tables Manager
pass
