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
source "${FUT_TOPDIR}/shell/lib/ltem_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm

manager_setup_file="ltem/ltem_setup.sh"
usage()
{
cat << usage_string
ltem/ltem_validation.sh [-h] arguments
Description:
    - Script will Check Lte is running on Lte interface by checking entries are present in appropriate tables
      like Lte_State, Connection_Manager_Uplink and Wifi_Inet_State tables.
Arguments:
    -h : show this help message
    \$1 (lte_if_name)                 : lte interface name              : (string)(required)
    \$2 (if_type)                     : interface type                  : (string)(required)
    \$3 (access_point_name)           : access point name of SIM        : (string)(required)
    \$4 (has_l2)                      : table attribute has_l2          : (bool)(required)
    \$5 (has_l3)                      : table attribute has_l3          : (bool)(required)
    \$6 (metric)                      : metric value in routing table   : (int)(required)
    \$7 (route_tool_path)             : route tool path in device       : (string)(required))

Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./ltem/ltem_validation.sh lte_if_name if_type access_point_name has_l2 has_l3 metric
Script usage example:
    ./ltem/ltem_validation.sh wwan0 lte data.icore.name true true 100 /sbin/route
    ./ltem/ltem_validation.sh wwan0 lte data.icore.name true false 100 /sbin/route
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

NARGS=7
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s)" -l "ltem/ltem_validation.sh" -arg
lte_if_name=${1}
if_type=${2}
access_point_name=${3}
has_l2=${4}
has_l3=${5}
metric=${6}
route_tool_path=${7}
LTE_ROUTE_TIMEOUT=30

check_ovsdb_table_exist Lte_State &&
    log "ltem/ltem_force_lte.sh: Lte_Config table exists in ovsdb - Success" ||
    raise "FAIL: Lte_Config table does not exist in ovsdb" -l "ltem/ltem_force_lte.sh" -s

wait_ovsdb_entry Lte_State -w if_name "$lte_if_name" \
    -is ipv4_enable "true" \
    -is apn "$access_point_name=" \
    -is lte_failover_enable "true" \
    -is manager_enable "true" \
    -is modem_enable "true"  &&
        log "ltem/ltem_validation.sh: Expected entry in Lte_State table for $lte_if_name interface and $access_point_name of SIM - Success " ||
        raise "FAIL: Expected entry is not present on Lte_State table for $lte_if_name interface and $access_point_name of SIM " -l "ltem/ltem_validation.sh" -tc

wait_ovsdb_entry Connection_Manager_Uplink -w if_name "$lte_if_name" \
    -is if_type "$if_type" \
    -is if_name "$lte_if_name" \
    -is has_L2 "$has_l2" \
    -is has_L3 "$has_l3" &&
    log "ltem/ltem_validation.sh: wait_ovsdb_entry - Entry present for $lte_if_name interface of type $if_type for Connection_Manager_Uplink - Success " ||
    raise "FAIL: Entry not present for $lte_if_name interface of type $if_type for Connection_Manager_Uplink " -l "ltem/ltem_validation.sh" -tc

wait_ovsdb_entry Wifi_Inet_State -w if_name "$lte_if_name" \
    -is if_type "$if_type" \
    -is if_name "$lte_if_name" &&
    log "ltem/ltem_validation.sh: wait_ovsdb_entry - Expected Entry for $lte_if_name interface on Wifi_Inet_State- Success " ||
    raise "FAIL: Expected Entry is not present of $lte_if_name interface on Wifi_Inet_State" -l "ltem/ltem_validation.sh" -tc

wait_for_function_response 0 "check_default_lte_route_gw $lte_if_name $metric $route_tool_path" $LTE_ROUTE_TIMEOUT &&
    log "ltem/ltem_validation - Lte interface was added to route table - Success" ||
    raise "FAIL: Lte interface was not added to route table" -l "ltem/ltem_validation" -tc

pass