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

usage()
{
cat << usage_string
nfm/nfm_native_ebtable_check.sh [-h] arguments
Description:
    - This script will create ebtables rules by configuring rules in the Netfilter table.
      The script also validates that the rules are configured on the device.
Arguments:
    -h : show this help message
    \$1 (netfilter_table_name)  : Netfilter table name                                          : (string)(required)
    \$2 (chain_name)            : chain to use (eg. INPUT, FORWARD etc.)                        : (string)(required)
    \$3 (table_name)            : table to use (filter, nat or broute)                          : (string)(required)
    \$4 (ebtable_rule)          : condition to be matched                                       : (string)(required)
    \$5 (ebtable_target)        : action to take when the rule match (ACCEPT, DROP, CONTINUE)   : (string)(required)
    \$6 (ebtable_priority)      : rule priority                                                 : (string)(required)
    \$7 (update_target)         : updated target value                                          : (string)(required)
Script usage example:
    ./nfm/nfm_setup.sh
    ./nfm/nfm_native_ebtable_check.sh sample_filter BROUTING broute "-d 60:b4:f7:fc:2d:44" DROP 1 ACCEPT
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
[ $# -ne ${NARGS} ] && usage && raise "Requires exactly '${NARGS}' input argument(s), $# given" -l "/nfm/nfm_native_ebtable_check.sh" -arg

netfilter_table_name="${1}"
chain_name="${2}"
table_name="${3}"
ebtable_rule="${4}"
ebtable_target="${5}"
ebtable_priority="${6}"
update_target="${7}"

log_title "/nfm/nfm_native_ebtable_check.sh: Configuring and validating ebtables rules"

log "/nfm/nfm_native_ebtable_check.sh: Configuring Netfilter table entry (name:${netfilter_table_name}, rule:"${ebtable_rule}", target:"${ebtable_target}")"
insert_ovsdb_entry Netfilter -i name "${netfilter_table_name}" \
    -i chain "${chain_name}" \
    -i enable "true" \
    -i protocol "eth" \
    -i table "${table_name}" \
    -i rule "${ebtable_rule}" \
    -i target "${ebtable_target}" \
    -i priority "${ebtable_priority}" &&
        log "/nfm/nfm_native_ebtable_check.sh: Configuring Netfilter table (name:${netfilter_table_name}, rule:"${ebtable_rule}", target:"${ebtable_target}") - Success" ||
        raise "FAIL: Failed to configuring Netfilter table (name: ${netfilter_table_name}, rule:"${ebtable_rule}", target:"${ebtable_target}")" -l "/nfm/nfm_native_ebtable_check.sh" -tc

log "/nfm/nfm_native_ebtable_check.sh: Checking ebtables rule is configured on the device"
is_ebtables_rule_configured "${table_name}" "${chain_name}" "${ebtable_rule}" "${ebtable_target}"
    log "/nfm/nfm_native_ebtable_check.sh: ebtables rule is configured on the device - Success" ||
    raise "FAIL: ebtables rule is not configured on the device" -l "/nfm/nfm_native_ebtable_check.sh" -tc

#updating the added rule in Netfilter Table
log "/nfm/nfm_native_ebtable_check.sh: Updating Netfilter table from target:"${ebtable_target}" to "${update_target}""
update_ovsdb_entry Netfilter -w name "${netfilter_table_name}" -u target "${update_target}" &&
    log "/nfm/nfm_native_ebtable_check.sh: Updating Netfilter table from target:"${ebtable_target}" to "${update_target}" - Success" ||
    raise "FAIL: Failed updating Netfilter table from target:"${ebtable_target}" to "${update_target}"" -l "/nfm/nfm_native_ebtable_check.sh" -tc

log "/nfm/nfm_native_ebtable_check.sh: Checking ebtables rule is configured on the device"
is_ebtables_rule_configured "${table_name}" "${chain_name}" "${ebtable_rule}" "${update_target}" &&
    log "/nfm/nfm_native_ebtable_check.sh: ebtables rule is configured on the device - Success" ||
    raise "FAIL: ebtables rule is not configured on the device" -l "/nfm/nfm_native_ebtable_check.sh" -tc

log "/nfm/nfm_native_ebtable_check.sh: Deleting Netfilter table entry (name:${netfilter_table_name})"
remove_ovsdb_entry Netfilter -w name "${netfilter_table_name}" &&
    log "/nfm/nfm_native_ebtable_check.sh: Deleting Netfilter table entry (name:${netfilter_table_name}) - Success" ||
    raise "FAIL: Failed deleting Netfilter table entry (name:${netfilter_table_name})" -l "/nfm/nfm_native_ebtable_check.sh" -tc

log "/nfm/nfm_native_ebtable_check.sh: Checking ebtables rule is removed from the device"
is_ebtables_rule_removed "${table_name}" "${chain_name}" "${ebtable_rule}" "${update_target}" &&
    log "/nfm/nfm_native_ebtable_check.sh: Checking ebtables rule is removed from the device - Success" ||
    raise "FAIL: Failed, ebtables rule is not removed from the device" -l "/nfm/nfm_native_ebtable_check.sh" -tc

# Check if manager survived.
check_manager_alive "${OPENSYNC_ROOTDIR}/bin/nfm" &&
    log "/nfm/nfm_native_ebtable_check.sh: NFM is running - Success" ||
    raise "FAIL: NFM not running/crashed" -l "/nfm/nfm_native_ebtable_check.sh" -tc

pass
