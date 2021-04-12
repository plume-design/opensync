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
source "${FUT_TOPDIR}/shell/lib/fsm_lib.sh"
[ -e "${LIB_OVERRIDE_FILE}" ] && source "${LIB_OVERRIDE_FILE}" || raise "" -olfm

tc_name="fsm/$(basename "$0")"

usage() {
    cat << usage_string
${tc_name} [-h] arguments
Description:
    - Script configures FSM settings to Flow_Service_Manager_Config
Arguments:
    -h  show this help message
    \$1 (lan_bridge_if) : used as bridge interface name         : (string)(required)
    \$2 (postfix)       : used as postfix on tap interface name : (string)(required)
    \$3 (handler)       : used as handler at fsm tables         : (string)(required)
    \$4 (plugin)        : used as plugin at fsm tables          : (string)(required)
Script usage example:
    ./${tc_name} br-home tdns dev_dns /usr/plume/lib/libfsm_dns.so
    ./${tc_name} br-home thttp dev_http /usr/opensync/lib/libfsm_http.so
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

trap '
fut_info_dump_line
print_tables Openflow_Config
print_tables Flow_Service_Manager_Config FSM_Policy
fut_info_dump_line
' EXIT SIGINT SIGTERM

# INPUT ARGUMENTS:
NARGS=4
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
lan_bridge_if=${1}
tap_name_postfix=${2}
handler=${3}
plugin=${4}

# Construct from input arguments
tap_if="${lan_bridge_if}.${tap_name_postfix}"

log_title "$tc_name: FSM test - Configuring FSM tables required for FSM testing"

log "$tc_name: Cleaning FSM OVSDB Config tables"
empty_ovsdb_table Openflow_Config
empty_ovsdb_table Flow_Service_Manager_Config
empty_ovsdb_table FSM_Policy

insert_ovsdb_entry Flow_Service_Manager_Config \
    -i if_name "${tap_if}" \
    -i handler "$handler" \
    -i plugin "$plugin" &&
        log "$tc_name: Flow_Service_Manager_Config entry added" ||
        raise "Failed to insert Flow_Service_Manager_Config entry" -l "$tc_name" -oe

pass
