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


# Script used to configure and validate Linux Traffic Control rules when device is running in Native Bridge Configuration

# FUT environment loading
# shellcheck disable=SC1091
# shellcheck disable=SC3046
source /tmp/fut-base/shell/config/default_shell.sh
[ -e "/tmp/fut-base/fut_set_env.sh" ] && source /tmp/fut-base/fut_set_env.sh
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
[ -e "${PLATFORM_OVERRIDE_FILE}" ] && source "${PLATFORM_OVERRIDE_FILE}" || raise "${PLATFORM_OVERRIDE_FILE}" -ofm
[ -e "${MODEL_OVERRIDE_FILE}" ] && source "${MODEL_OVERRIDE_FILE}" || raise "${MODEL_OVERRIDE_FILE}" -ofm


# Test case name
manager_setup_file="nm2/nm2_setup.sh"
usage()
{
cat << usage_string
nm2/nm2_verify_linux_traffic_control_rules.sh [-h] arguments
Description:
    - The script configures and validates Traffic Control (TC) rules on the pod by configuring
    Interface_Classifier and IP_Interface tables.
    - It also validates modification and deletion of the rules.
Arguments:
    -h  show this help message
    \$1 (if_name)                            : used as if_name in Wifi_Inet_Config table                : (string)(required)
    \$2 (ingress_match)                      : used as ingress match in Interface_Classifier table      : (string)(required)
    \$3 (ingress_action)                     : used as ingress action in Interface_Classifier table     : (string)(required)
    \$4 (ingress_expected_str)               : expected string to verify if the ingress rule is applied : (string)(required)
    \$5 (egress_match)                       : used as egress match in Interface_Classifier table       : (string)(required)
    \$6 (egress_action)                      : used as egress action in Interface_Classifier table      : (string)(required)
    \$7 (egress_expected_str)                : expected string to verify if the egress rule is applied  : (string)(required)
    \$8 (priority)                           : priority for Ingress and Egress Classifier               : (required)
    \$9 (ingress_updated_match)              : updated ingress classifier configuration                 : (string)(required)
    \$10 (ingress_expected_str_after_update) : expected output for checking updated ingress classifier  : (string)(required)

Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_verify_linux_traffic_control_rules.sh <IF_NAME> <IG_MATCH> <IG_ACTION> <IG_CHECK_STR> \
                      <IG_CHECK_STR> <EG_MATCH> <EG_ACTION> <EG_CHECK_STR> <PRIORITY> \
                      <IG_MODIFIED_MATCH> <IG_CHECK_MODIFIED_STR>
Script usage example:
   ./nm2/nm2_verify_linux_traffic_control_rules.sh dummy-intf "ip flower ip_proto tcp src_port 80" \
                "action mirred egress mirror dev br-home" "80" "ip flower ip_proto udp src_port 67" \
                "action mirred egress redirect dev br-home" "67" 11 \
                "ip flower ip_proto tcp src_port 8080" "8080"
usage_string
}

# options
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

NARGS=10
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s), provided $#" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -arg

# Fill variables with provided arguments.
if_name=$1
ingress_match=$2
ingress_action=$3
ingress_expected_str=$4
egress_match=$5
egress_action=$6
egress_expected_str=$7
priority=$8
ingress_updated_match=$9
ingress_expected_str_after_update=${10}

ic_egress_token="dev_eg_${if_name}"
ip_intf_name="dev_ip_intf"
ic_ingress_token="dev_ig_${if_name}"

trap '
    fut_info_dump_line
    print_tables Openflow_Tag IP_Interface Interface_Classifier
    reset_inet_entry $if_name || true
    run_setup_if_crashed nfm || true
    check_restore_management_access || true
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_verify_linux_traffic_control_rules.sh: Configuring and validating Linux Traffic Control rules"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating Wifi_Inet_Config entry for: $if_name"
create_inet_entry \
    -if_name "${if_name}" \
    -if_type "tap" \
    -NAT false \
    -ip_assign_scheme "none" \
    -dhcp_sniff "false" \
    -network true \
    -enabled true &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating Wifi_Inet_Config entry for: $if_name - Success" ||
        raise "FAIL: Failed to create interface $if_name" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# create Interface classifier for ingress rule
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating ingress interface classifier (token:'$ic_ingress_token', match:'$ingress_match')"
insert_ovsdb_entry Interface_Classifier \
    -i token "$ic_ingress_token" \
    -i priority "$priority" \
    -i match "$ingress_match" \
    -i action "$ingress_action" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating ingress interface classifier (token:'$ic_ingress_token', match:'$ingress_match') - Success" ||
        raise "FAIL: Failed Creating ingress interface classifier (token:'$ic_ingress_token', match:'$ingress_match')" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# create Interface classifier for egress rule
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating egress interface classifier (token:'$ic_egress_token', match:'$egress_match')"
insert_ovsdb_entry Interface_Classifier \
    -i token "$ic_egress_token" \
    -i priority "$priority" \
    -i match "$egress_match" \
    -i action "$egress_action" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating egress interface classifier (token:'$ic_egress_token', match:'$egress_match') - Success" ||
        raise "FAIL: Failed Creating egress interface classifier (token:'$ic_egress_token', match:'$egress_match')" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Getting uuid for Interface classifier '$ic_ingress_token'"
ic_ingress_uuid=$(get_ovsdb_entry_value Interface_Classifier _uuid -w "token" "$ic_ingress_token") ||
    raise "FAIL: failed to get uuid for Interface Classifier '$ic_ingress_token'" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Getting uuid for egress interface classifier '$ic_egress_token'"
ic_egress_uuid=$(get_ovsdb_entry_value Interface_Classifier _uuid -w "token" "$ic_egress_token") ||
    raise "FAIL: failed to get uuid for egress interface classifier '$ic_egress_token'" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# create IP_Interface and set the ingress rule
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating IP_Interface '$ip_intf_name' with ingress rule"
insert_ovsdb_entry IP_Interface \
    -i if_name "${if_name}" \
    -i name "$ip_intf_name" \
    -i ingress_classifier '["set",[["uuid","'${ic_ingress_uuid}'"]]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating IP_Interface '$ip_intf_name' with ingress rule - Success" ||
        raise "FAIL: Failed to create IP_Interface '$ip_intf_name' with ingress rule" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# verify if the ingress configuration is applied correctly in the system
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Validating if the ingress Traffic Control rule is configured on the device"
nb_is_tc_rule_configured "${if_name}" "${ingress_expected_str}" "ingress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Validating if the ingress Traffic Control rule is configured on the device - Success" ||
    raise "FAIL: Failed, ingress Traffic Control rule is not configured on the device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# modify Interface classifier for ingress with new value
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating interface classifier table with new ingress rule: '$ingress_updated_match'"
update_ovsdb_entry Interface_Classifier \
    -w token "${ic_ingress_token}" \
    -u match "$ingress_updated_match" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating interface classifier table with new ingress rule: '$ingress_updated_match' - Success" ||
        raise "FAIL: Failed to update interface classifier table with new ingress rule: '$ingress_updated_match'" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# verify if the ingress configuration is applied correctly in the system
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the updated ingress match rule is configured on the device"
nb_is_tc_rule_configured "${if_name}" "${ingress_expected_str_after_update}" "ingress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the updated ingress match rule is configured on the device - Success" ||
    raise "FAIL: Failed ingress match rule is not configured on the device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface table with egress rule"
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u egress_classifier '["set",[["uuid","'${ic_egress_uuid}'"]]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface table with egress rule - Success" ||
        raise "FAIL: Failed updating IP_Interface table with egress rule" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# verify if the egress classifier rule is applied in the system (L2 check)
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the egress match rule is configured on the device"
nb_is_tc_rule_configured "${if_name}" "${egress_expected_str}" "egress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the egress match rule is configured on the device - Success" ||
    raise "FAIL: Failed egress match rule is not configured on the device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# Remove the ingress Rule and check if it is removed.
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing ingress rule from IP_Interface '$ip_intf_name'"
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u ingress_classifier '["set", ['']]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing ingress rule from IP_Interface '$ip_intf_name' - Success" ||
        raise "FAIL: Failed to remove ingress rule from IP_Interface $ip_intf_name failed" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the ingress rule is removed from the device"
nb_is_tc_rule_removed "${if_name}" "${ingress_expected_str}" "ingress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Ingress rule removed from the device - Success" ||
    raise "Fail: Failed to remove ingress rule from from the device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# Remove the egress Rule and check if it is removed.
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing egress rule from IP_Interface"
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u egress_classifier '["set", ['']]'

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Checking if the egress rule is removed from the device"
nb_is_tc_rule_removed "${if_name}" "${egress_expected_str}" "egress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Egress rule removed from the device - Success" ||
    raise "Fail: Failed to remove egress rule from from the device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up ingress rule from Interface_Classifier table"
remove_ovsdb_entry Interface_Classifier -w token "${ic_ingress_token}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up ingress rule from Interface_Classifier table - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove ingress rule from Interface_Classifier table"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up egress rule from Interface_Classifier table"
remove_ovsdb_entry Interface_Classifier -w token "${ic_egress_token}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up egress rule from Interface_Classifier table - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove egress rule from Interface_Classifier table"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up IP_Interface table"
remove_ovsdb_entry IP_Interface -w name "${ip_intf_name}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Cleaning up IP_Interface table - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove IP_Interface table"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing interface $if_name"
delete_inet_interface "$if_name" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing interface $if_name - Success" ||
    raise "FAIL: Failed to removing interface $if_name" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -tc

pass
