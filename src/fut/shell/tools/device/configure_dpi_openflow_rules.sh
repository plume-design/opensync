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

usage()
{
cat << usage_string
fsm/configure_dpi_openflow_rules.sh [-h] arguments
Description:
    Script inserts rules to the OVSDB Openflow_Config table.
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
print_tables Openflow_Config Openflow_State
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

NARGS=1
[ $# -lt ${NARGS} ] && raise "Requires at least '${NARGS}' input argument(s)" -arg
bridge=${1}

log_title "fsm/fsm_configure_of_rules.sh: FSM test - Configure Openflow rules"

log "configure_dpi_openflow_rules.sh: Cleaning Openflow_Config table"
empty_ovsdb_table Openflow_Config

dev_token="dev_test"
of_port=20001

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 0 \
    -i priority 0 \
    -i bridge "${bridge}" \
    -i action normal &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 1 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 0 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i action "resubmit(,7)" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 2 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 0 \
    -i bridge "${bridge}" \
    -i action normal &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 3 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i rule "ct_state=-trk,ip" \
    -i action "ct(table=7,zone=1)" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 4 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i rule "ct_state=+trk,ct_mark=0,ip" \
    -i action "ct(commit,zone=1,exec(load:0x1->NXM_NX_CT_MARK[])),NORMAL,output:${of_port}" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 5 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i rule "ct_zone=1,ct_state=+trk,ct_mark=1,ip" \
    -i action "NORMAL,output:${of_port}" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 6 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i rule "ct_zone=1,ct_state=+trk,ct_mark=2,ip" \
    -i action "NORMAL" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 7 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

insert_ovsdb_entry Openflow_Config \
    -i token "${dev_token}" \
    -i table 7 \
    -i priority 200 \
    -i bridge "${bridge}" \
    -i rule "ct_state=+trk,ct_mark=3,ip" \
    -i action "DROP" &&
        log "configure_dpi_openflow_rules.sh: Openflow rule 8 inserted - Success" ||
        raise "FAIL: Failed to insert Openflow rule" -l "configure_dpi_openflow_rules.sh" -oe

pass
