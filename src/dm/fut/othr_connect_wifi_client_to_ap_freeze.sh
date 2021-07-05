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
source "${FUT_TOPDIR}/shell/lib/othr_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

tc_name="othr/$(basename "$0")"
manager_setup_file="othr/othr_setup.sh"
bridge="br-home"
freeze_src_token=device_freeze_src
freeze_dst_token=device_freeze_dst

usage() {
cat <<usage_string
${tc_name} [-h] arguments
Description:
    - Script adds Openflow rules to freeze the client.
Arguments:
    -h  show this help message
    \$1  (client_mac)     : MAC address of client connected to ap: (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./${tc_name} <CLIENT-MAC-ADDR>
Script usage example:
    ./${tc_name} a1:b2:c3:d4:e5:f6
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
print_tables Openflow_Tag Openflow_Config
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -ne ${NARGS} ] && raise "Requires exactly '${NARGS}' input argument" -arg
client_mac=${1}

log_title "$tc_name: OTHR test - Adding Openflow rules to tables to freeze client"

log "$tc_name: Inserting rules into Openflow tables to freeze the client: $client_mac"
${OVSH} i Openflow_Tag name:=frozen cloud_value:='["set",['$(printf '"%s"' "${client_mac}")']]' &&
    log "$tc_name: Entry inserted to Openflow_Tag for name 'frozen', client MAC '$client_mac' - Success" ||
    raise "FAIL: Failed to add Openflow rules for client $client_mac to Openflow_Tag table" -l "$tc_name" -oe

${OVSH} i Openflow_Config action:=drop bridge:="${bridge}" priority:=200 rule:='dl_src=${frozen}' table:=0 token:=${freeze_src_token} &&
    log "$tc_name: Entry inserted to Openflow_Config for action 'drop' for dl_src rule - Success" ||
    raise "FAIL: Failed to insert to Openflow_Config for action 'drop' for dl_src rule" -l "$tc_name" -oe

${OVSH} i Openflow_Config  action:=drop bridge:="${bridge}" priority:=200 rule:='dl_dst=${frozen}' table:=0 token:=${freeze_dst_token} &&
    log "$tc_name: Entry inserted to Openflow_Config for action 'drop' for dl_dst rule - Success" ||
    raise "FAIL: Failed to insert to Openflow_Config for action 'drop' for dl_dst rule" -l "$tc_name" -oe

pass
