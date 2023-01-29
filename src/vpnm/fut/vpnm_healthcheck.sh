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
source "${FUT_TOPDIR}/shell/lib/vpnm_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="vpnm/vpnm_setup.sh"
usage()
{
cat << usage_string
vpnm/vpnm_healthcheck.sh [-h] arguments
Description:
    - The goal of this testcase is to verify if VPN healthcheck is functional.

Arguments:
    -h : show this help message

Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./vpnm/vpnm_healthcheck.sh
Script usage example:
    ./vpnm/vpnm_healthcheck.sh
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

NARGS=0
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "vpnm/vpnm_healthcheck.sh" -arg

# Execute on EXIT signal.
trap '
fut_info_dump_line
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

check_ovsdb_table_exist VPN_Tunnel &&
    log "vpnm/vpnm_healthcheck.sh: VPN_Tunnel table exists in ovsdb - Success" ||
    raise "FAIL: VPN_Tunnel table does not exist in ovsdb" -l "vpnm/vpnm_healthcheck.sh" -s

empty_ovsdb_table VPN_Tunnel
empty_ovsdb_table IPSec_Config
empty_ovsdb_table IPSec_State
empty_ovsdb_table Tunnel_Interface

log "vpnm/vpnm_healthcheck.sh: Inserting a VPN_Tunnel row with dummy healthcheck config"
insert_ovsdb_entry VPN_Tunnel \
    -i name "verify-healthcheck" \
    -i enable true \
    -i healthcheck_enable true \
    -i healthcheck_ip 8.8.8.8 \
    -i healthcheck_interval 5 \
    -i healthcheck_timeout 30 &&
        log "vpnm/vpnm_healthcheck.sh: insert_ovsdb_entry - VPN_Tunnel - Success" ||
        raise "FAIL: insert_ovsdb_entry - VPN_Tunnel" -l "vpnm/vpnm_healthcheck.sh" -oe

log "vpnm/vpnm_healthcheck.sh: Checking if VPN Healthcheck up and running"

wait_ovsdb_entry VPN_Tunnel \
    -w name "verify-healthcheck" \
    -is healthcheck_status "ok" &&
        log "vpnm/vpnm_healthcheck.sh: VPN Healthcheck running - Success" ||
        raise "FAIL: VPN Healthcheck NOT initiated" -l "vpnm/vpnm_healthcheck.sh" -oe

pass
