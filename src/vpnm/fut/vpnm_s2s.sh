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

manager_setup_file="vpnm/vpnm_setup.sh"
usage()
{
cat << usage_string
vpnm/vpnm_s2s.sh [-h] arguments
Description:
    - The goal of this testcase is to verify that VPNM can succesfully establish a
      site to site IPSec tunnel and to verify that the tunnel status is correctly
      updated in the `VPN_Tunnel` OVSDB table and that the `IPSec_State` fields are
      correctly updated.

Arguments:
    -h : show this help message

Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./vpnm/vpnm_s2s.sh
Script usage example:
    ./vpnm/vpnm_s2s.sh
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "vpnm/vpnm_s2s.sh" -arg

# Execute on EXIT signal.
trap '
fut_info_dump_line
check_restore_ovsdb_server
fut_info_dump_line
' EXIT SIGINT SIGTERM

check_ovsdb_table_exist VPN_Tunnel &&
    log "vpnm/vpnm_s2s.sh: VPN_Tunnel table exists in ovsdb - Success" ||
    raise "FAIL: VPN_Tunnel table does not exist in ovsdb" -l "vpnm/vpnm_s2s.sh" -s

empty_ovsdb_table VPN_Tunnel
empty_ovsdb_table IPSec_Config
empty_ovsdb_table IPSec_State
empty_ovsdb_table Tunnel_Interface

log "vpnm/vpnm_s2s.sh: Inserting a VPN_Tunnel row..."
insert_ovsdb_entry VPN_Tunnel \
    -i name "primary" \
    -i enable true \
    -i healthcheck_enable false &&
        log "vpnm/vpnm_s2s.sh: insert_ovsdb_entry - VPN_Tunnel - Success" ||
        raise "FAIL: insert_ovsdb_entry - VPN_Tunnel" -l "vpnm/vpnm_s2s.sh" -oe

log "vpnm/vpnm_s2s.sh: Inserting an IPSec_Config row..."
insert_ovsdb_entry IPSec_Config \
    -i tunnel_name "primary" \
    -i remote_endpoint_id "gateway.site-to-site.test" \
    -i remote_endpoint "192.168.7.211" \
    -i local_endpoint_id "node@site-to-site.test" \
    -i local_subnets "0.0.0.0/0" \
    -i remote_subnets "0.0.0.0/0" \
    -i local_auth_mode "psk" \
    -i remote_auth_mode "psk" \
    -i psk "site-to-site-node" \
    -i key_exchange "ikev2" &&
        log "vpnm/vpnm_s2s.sh: insert_ovsdb_entry - IPSec_Config - Success" ||
        raise "FAIL: insert_ovsdb_entry - IPSec_Config" -l "vpnm/vpnm_s2s.sh" -oe

log "vpnm/vpnm_s2s.sh: Checking if a site to site IPsec tunnel has been established"

wait_ovsdb_entry VPN_Tunnel \
    -w name "primary" \
    -is tunnel_status "up" &&
        log "vpnm/vpnm_s2s.sh: IPSec VPN tunnel established - Success" ||
        raise "FAIL: IPSec s2s VPN tunnel failed to establish" -l "vpnm/vpnm_s2s.sh" -oe

wait_for_function_response 'notempty' "get_ovsdb_entry_value IPSec_State local_subnets -w tunnel_name primary" &&
    log "vpnm/vpnm_s2s.sh: IPSec s2s: local subnets set in IPSec_State - Success" ||
    raise "FAIL: IPSec s2s: local subnets not set in IPSec_State" -l "vpnm/vpnm_s2s.sh" -oe

wait_for_function_response 'notempty' "get_ovsdb_entry_value IPSec_State remote_subnets -w tunnel_name primary" &&
    log "vpnm/vpnm_s2s.sh: IPSec s2s: remote subnets set in IPSec_State - Success" ||
    raise "FAIL: IPSec s2s: remote subnets not set in IPSec_State" -l "vpnm/vpnm_s2s.sh" -oe

pass
