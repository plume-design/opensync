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


# Script used to configure and validate Traffic Control rules with templates when running in Native Bridge Mode

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
nm2/nm2_verify_linux_traffic_control_rules_template_rules.sh [-h] arguments
Description:
    - Script configures Interface_Classifier and IP_Interface tables, through which the linux Traffic Control template rules
    are applied on the device.  Also verifies if the linux Traffic Control configuration is applied on the system.
    - Modify and delete operations is also validated.
Arguments:
    -h  show this help message
    \$1 (if_name)           : Interface name                                         : (string)(required)
    \$2 (ig_match)          : Ingress match string                                   : (string)(required)
    \$3 (ig_action)         : Ingress action string                                  : (string)(required)
    \$4 (ig_tag_name)       : Tag name used in Ingress match string                  : (string)(required)
    \$5 (eg_match)          : Egress match string                                    : (string)(required)
    \$6 (eg_action)         : Egress action string                                   : (string)(required)
    \$7 (eg_match_with_tag) : Egress match string with tag value                     : (string)(required)
    \$8 (eg_expected_str)   : Expected output for checking if egress rule is applied : (string)(required)
Testcase procedure:
    - On DEVICE: Run: ./${manager_setup_file} (see ${manager_setup_file} -h)
                 Run: ./nm2/nm2_verify_linux_traffic_control_rules_template_rules.sh <IF_NAME> <IG_MATCH> <IG_ACTION> <IG_TAG_NAME>
                      <EG_MATCH> <EG_ACTION> <EG_MATCH_WITH_TAG> <EG_EXPECTED_STR>
Script usage example:
   ./nm2/nm2_verify_linux_traffic_control_rules_template_rules.sh dummy-intf "ip flower dst_mac \${devices_tag}" \
                "action mirred egress mirror dev br-home" "devices_tag" \
                "ip flower ip_proto udp src_port 67" "action mirred egress redirect dev br-home" \
                "ip flower dst_mac \${devices_tag}" "67"
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

NARGS=8
[ $# -lt ${NARGS} ] && usage && raise "Requires at least '${NARGS}' input argument(s)" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -arg

# Fill variables with provided arguments.
if_name=$1

ingress_match_with_tag=$2
ingress_action=$3
tag_name=$4
egress_match=$5
egress_action=$6
egress_match_with_tag=$7
egress_expected_str=$8

ic_egress_token="dev_eg_${if_name}"
ic_egress_token_with_tag="dev_eg_${if_name}_tag"
ic_ingress_token="dev_ig_${if_name}"
ip_intf_name="dev_ip_intf"

# mac address used for template tag
mac1="11:11:11:11:11:11"
mac2="22:22:22:22:22:22"

trap '
    fut_info_dump_line
    print_tables Openflow_Tag IP_Interface Interface_Classifier
    reset_inet_entry $if_name || true
    run_setup_if_crashed nbm || true
    check_restore_management_access || true
    fut_info_dump_line
' EXIT SIGINT SIGTERM

log_title "nm2/nm2_verify_linux_traffic_control_rules.sh:  Configuring and Validating Linux Traffic Control template Rule"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating Wifi_Inet_Config entries for $if_name (if_type:tap)"
create_inet_entry \
    -if_name "${if_name}" \
    -if_type "tap" \
    -NAT false \
    -ip_assign_scheme "none" \
    -dhcp_sniff "false" \
    -network true \
    -enabled true &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Interface $if_name created - Success" ||
        raise "FAIL: Failed to create interface $if_name" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# create Interface classifier for ingress
log "nm2/nm2_verify_linux_traffic_control_rules.sh: - Creating Interface_Classifier '$ic_ingress_token' (match:'$ingress_match_with_tag')"
insert_ovsdb_entry Interface_Classifier \
    -i token "$ic_ingress_token" \
    -i priority 2 \
    -i match "$ingress_match_with_tag" \
    -i action "$ingress_action" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Interface classifier $ic_ingress_token created - Success" ||
        raise "FAIL: Failed to create interface classifier $ic_ingress_token" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# create Interface classifier with template match for egress
log "nm2/nm2_verify_linux_traffic_control_rules.sh: - Creating Interface_Classifier '$ic_ingress_token' (match:'$egress_match_with_tag')"
insert_ovsdb_entry Interface_Classifier \
    -i token "$ic_egress_token_with_tag" \
    -i priority 2 \
    -i match "$egress_match_with_tag" \
    -i action "$ingress_action" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Interface classifier $ic_egress_token_with_tag created - Success" ||
        raise "FAIL: Failed to create interface classifier $ic_egress_token_with_tag" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# create Interface classifier for egress
log "nm2/nm2_verify_linux_traffic_control_rules.sh: - Creating Interface_Classifier '$ic_egress_token' (match:'$egress_match')"
insert_ovsdb_entry Interface_Classifier \
    -i token "$ic_egress_token" \
    -i priority 2 \
    -i match "$egress_match" \
    -i action "$egress_action" &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Interface classifier $ic_ingress_token created - Success" ||
        raise "FAIL: Failed to create interface classifier $ic_ingress_token" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Getting uuid for Interface classifier '$ic_ingress_token'"
ic_ingress_uuid=$(get_ovsdb_entry_value Interface_Classifier _uuid -w "token" "$ic_ingress_token") ||
    raise "FAIL: Failed to get uuid for Interface Classifier" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Getting uuid for Interface classifier '$ic_egress_token'"
ic_egress_uuid=$(get_ovsdb_entry_value Interface_Classifier _uuid -w "token" "$ic_egress_token") ||
    raise "FAIL: Failed to get uuid for Interface Classifier" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Getting uuid for Interface classifier '$ic_egress_token_with_tag'"
ic_egress_with_tag_uuid=$(get_ovsdb_entry_value Interface_Classifier _uuid -w "token" "$ic_egress_token_with_tag") ||
    raise "FAIL: Failed to get uuid for Interface Classifier" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# create IP_Interface and set the ingress rule
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Creating IP_Interface '$ip_intf_name' with ingress rule"
insert_ovsdb_entry IP_Interface \
    -i if_name "${if_name}" \
    -i name "$ip_intf_name" \
    -i ingress_classifier '["set",[["uuid","'${ic_ingress_uuid}'"]]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: IP_Interface $ip_intf_name created - Success" ||
        raise "FAIL: Failed to create IP_Interface $ip_intf_name" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# creating openflow_Tag after the tag is referenced.  The rule should be
# updated with the tag value.
insert_ovsdb_entry Openflow_Tag \
    -i name "${tag_name}" \
    -i cloud_value '["set",["'${mac1}'","'${mac2}'"]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Entry inserted to Openflow_Tag for name '$tag_name'  - Success" ||
        raise "FAIL: Failed to insert $tag_name in Openflow_Tag table" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -oe

# verify if the ingress configuration with tag value is applied correctly in the system
nb_is_tc_rule_configured "${if_name}" ${mac1} "ingress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Ingress Traffic Control rule with ${mac1} is configured on device - Success" ||
    raise "FAIL: failed Traffic Control rule with ${mac1} is configured on device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

nb_is_tc_rule_configured "${if_name}" ${mac2} "ingress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Ingress Traffic Control rule with ${mac2} is configured on device - Success" ||
    raise "FAIL: failed Traffic Control rule with ${mac2} is configured on device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface '$ip_intf_name' with egress rule "
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u egress_classifier '["set",[["uuid","'${ic_egress_uuid}'"]]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface $ip_intf_name with egress rule - Success" ||
        raise "FAIL: Updating IP_Interface $ip_intf_name with egress rule" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# verify if the egress classifier rule is applied in the system (L2 check)
nb_is_tc_rule_configured ${if_name} ${egress_expected_str} "egress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Egress Traffic Control rule with ${egress_expected_str} is configured on device - Success" ||
    raise "FAIL: failed Egress Traffic Control rule with ${egress_expected_str} is configured on device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface '$ip_intf_name' with egress tag rule "
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u egress_classifier '["set",[["uuid","'${ic_egress_with_tag_uuid}'"]]]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Updating IP_Interface $ip_intf_name with egress rule - Success" ||
        raise "FAIL: Updating IP_Interface $ip_intf_name with egress rule" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

# verify if the egress configuration is applied correctly in the system
nb_is_tc_rule_configured ${if_name} ${mac1} "egress" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: Egress Traffic Control rule with ${mac1} is configured on device - Success" ||
    raise "FAIL: Failed Egress Traffic Control rule with ${mac1} is configured on device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh:" -oe

# Delete Interface Classifiers and IP_Interface
log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing ingress and egress classifiers from '$ip_intf_name'"
update_ovsdb_entry IP_Interface \
    -w if_name "${if_name}" \
    -u egress_classifier '["set", ['']]' \
    -u ingress_classifier '["set", ['']]' &&
        log "nm2/nm2_verify_linux_traffic_control_rules.sh: Removing ingress and egress classifiers from '$ip_intf_name' - Success" ||
        raise "FAIL: Removing ingress and egress classifiers from '$ip_intf_name'" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -ds

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Clean up ingress Interface Classifier ${ic_ingress_token}"
remove_ovsdb_entry Interface_Classifier -w token "${ic_ingress_token}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: OVSDB entry from Interface_Classifier removed for $ic_ingress_token - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove OVSDB entry from Interface_Classifier for $ic_ingress_token"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Clean up ingress Interface Classifier ${ic_egress_token}"
remove_ovsdb_entry Interface_Classifier -w token "${ic_egress_token}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: OVSDB entry from Interface_Classifier removed for $ic_egress_token - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove OVSDB entry from Interface_Classifier for $ic_egress_token"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Clean up ingress Interface Classifier ${ic_egress_token_with_tag}"
remove_ovsdb_entry Interface_Classifier -w token "${ic_egress_token_with_tag}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: OVSDB entry from Interface_Classifier removed for $ic_egress_token_with_tag - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove OVSDB entry from Interface_Classifier for $ic_egress_token_with_tag"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: Clean up IP_Interface ${ip_intf_name}"
remove_ovsdb_entry IP_Interface -w name "${ip_intf_name}" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: OVSDB entry from IP_Interface removed for $ip_intf_name - Success" ||
    log -err "nm2/nm2_verify_linux_traffic_control_rules.sh: Failed to remove OVSDB entry from IP_Interface for $ip_intf_name"

log "nm2/nm2_verify_linux_traffic_control_rules.sh: clean up interface $if_name"
delete_inet_interface "$if_name" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: interface $if_name removed from device - Success" ||
    raise "FAIL: interface $if_name not removed from device" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -tc

# Check if manager survived.
manager_pid_file="${OPENSYNC_ROOTDIR}/bin/qosm"
wait_for_function_response 0 "check_manager_alive $manager_pid_file" &&
    log "nm2/nm2_verify_linux_traffic_control_rules.sh: QOSM is running - Success" ||
    raise "FAIL: QOSM not running/crashed" -l "nm2/nm2_verify_linux_traffic_control_rules.sh" -tc

pass
