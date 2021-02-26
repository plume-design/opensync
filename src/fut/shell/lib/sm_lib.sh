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


# Include basic environment config
export FUT_SM_LIB_SRC=true
[ "${FUT_WM2_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/sm_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Stats Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

sm_log_test_pass_msg="---------------------------------- OK ----------------------------------"

###############################################################################
# DESCRIPTION:
#   Function prepares device for SM tests.
#   If called with parametes it makes wm2 setup.
#   Raises exception on fail of:
#       - start pm or lm manager,
#       - start of qm,
#       - start of sm.
# INPUT PARAMETER(S):
#   $@  wm setup parameters (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   sm_setup_test_environment
###############################################################################
sm_setup_test_environment()
{
    fn_name="sm_lib:sm_setup_test_environment"
    wm_setup_test_environment "$@" &&
        log -deb "$fn_name - wm_setup_test_environment - Success" ||
        raise "FAIL: wm_setup_test_environment" -l "$fn_name" -ds

    log "$fn_name - Running SM setup"

    # Check if LM can be started, if not try starting PM.
    # If it fails raises an exception.
    start_specific_manager lm
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - start_specific_manager lm - Success"
    else
        log -deb "$fn_name - start_specific_manager lm failed. Trying to start pm instead"
        start_specific_manager pm
        if [ $? -eq 0 ]; then
            log -deb "$fn_name - start_specific_manager pm - Success"
        else
            raise "FAIL: Both start_specific_manager lm, start_specific_manager pm failed" -l "$fn_name" -ds
        fi
    fi

    # QM start for report queue handling
    start_specific_manager qm &&
        log -deb "$fn_name - start_specific_manager qm - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager qm" -l "$fn_name" -ds

    start_specific_manager sm &&
        log -deb "$fn_name - start_specific_manager sm - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager sm" -l "$fn_name" -ds

    empty_ovsdb_table AW_Debug &&
        log -deb "$fn_name - AW_Debug table emptied - Success" ||
        raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "$fn_name" -ds

    set_manager_log SM TRACE &&
        log -deb "$fn_name - Manager log for SM set to TRACE - Success" ||
        raise "FAIL: Could not set manager log severity: set_manager_log SM TRACE" -l "$fn_name" -ds

    log "$fn_name - SM setup - end"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function inserts wifi stats config to Wifi_Stats_Config table.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan, undefined
#   Supported stats types: survey, neighbor
#   Supported report type: raw
#   Raises exception on failing to configure Wifi_Stats_Config table.
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  channel list (required)
#   $3  stats type (required)
#   $4  survey type (required)
#   $5  reporting interval (required)
#   $6  sampling interval (required)
#   $6  report type (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   insert_ws_config 5GL "[\"set\",[$sm_channel]]" survey on-chan 10 5 raw
#   insert_ws_config 5GL "[\"set\",[$sm_channel]]" survey "[\"set\",[]]" 10 5 raw
###############################################################################
insert_ws_config()
{
    fn_name="sm_lib:insert_ws_config"
    local NARGS=7
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel_list=$2
    sm_stats_type=$3
    sm_survey_type=$4
    sm_reporting_interval=$5
    sm_sampling_interval=$6
    sm_report_type=$7

    log -deb "$fn_name - Inserting Wifi_Stats_Config config"

    if [ -z "$sm_survey_type" ]; then
        sm_survey_type="[\"set\",[]]"
    fi

    insert_ovsdb_entry Wifi_Stats_Config \
        -i radio_type "$sm_radio_type" \
        -i channel_list "$sm_channel_list" \
        -i stats_type "$sm_stats_type" \
        -i survey_type "$sm_survey_type" \
        -i reporting_interval "$sm_reporting_interval" \
        -i sampling_interval "$sm_sampling_interval" \
        -i report_type "$sm_report_type" ||
        raise "FAIL: Could not insert to Wifi_Stats_Config: insert_ovsdb_entry" -l "$fn_name" -oe

    log -deb "$fn_name - Wifi_Stats_Config config inserted"
}

####################### SETUP SECTION - STOP ##################################

####################### TEST CASE SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function checks existance of survey report log messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported log types: processing_survey, scheduled_scan, fetched_survey, sending_survey_report
#   Raises exception on fail:
#       - incorrect log type provided
#       - logs not found
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  channel (required)
#   $3  survey type (required)
#   $4  log type (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_survey_report_log 5GL 1 on-chan processing_survey
#   check_survey_report_log 5GL 1 on-chan scheduled_scan
###############################################################################
check_survey_report_log()
{
    fn_name="sm_lib:check_survey_report_log"
    local NARGS=4
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4

    case $sm_log_type in
    *processing_survey*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting processing survey"
        die_msg="No survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Processing $sm_radio_type' | grep -i '$sm_survey_type $sm_channel'"
        ;;
    *scheduled_scan*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting scheduling survey"
        die_msg="No survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Scheduled $sm_radio_type $sm_survey_type $sm_channel scan'"
        ;;
    *fetched_survey*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting fetched survey"
        die_msg="No survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Fetched $sm_radio_type $sm_survey_type $sm_channel survey'"
        ;;
    *sending_survey_report*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting sending survey"
        die_msg="No survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Sending $sm_radio_type' | grep -i '$sm_survey_type $sm_channel survey report'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "$fn_name" -arg
        ;;
    esac

    log "$fn_name - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "FAIL: $die_msg" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function inspects existance of all survey report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception on failing to empty table Wifi_Stats_Config.
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  channel (required)
#   $3  survey type (required)
#   $4  reporting interval (required)
#   $5  sampling interval (required)
#   $6  report type (required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_survey_report_log 5GL 1 on-chan 10 5 raw
###############################################################################
inspect_survey_report()
{
    fn_name="sm_lib:inspect_survey_report"
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_stats_type="survey"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    insert_ws_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "$sm_stats_type" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" processing_survey
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" scheduled_scan
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" fetched_survey
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" sending_survey_report

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks neighbor report log messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported log types: add_neighbor, parsed_neighbor_bssid, parsed_neighbor_ssid, sending_neighbor
#   Raises exception on fail:
#       - incorrect log type provided
#       - logs not found
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  channel (required)
#   $3  survey type (required)
#   $4  log type (required)
#   $5  neighbor mac (required)
#   $6  neighbor ssid (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_neighbor_report_log 5GL 1 on-chan add_neighbor <neighbor MAC> <neighbor SSID>
###############################################################################
check_neighbor_report_log()
{
    fn_name="sm_lib:check_neighbor_report_log"
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4
    sm_neighbor_mac=$5
    sm_neighbor_ssid=$6

    case $sm_log_type in
    *add_neighbor*)
        log_msg="Checking for $sm_radio_type neighbor adding for $sm_neighbor_mac"
        die_msg="No neighbor $sm_neighbor_mac was added"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was added"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Adding $sm_radio_type' | grep -i \"$sm_survey_type neighbor {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid'\" | grep -i 'chan=$sm_channel'"
        ;;
    *parsed_neighbor_bssid*)
        log_msg="Checking for $sm_radio_type neighbor parsing of bssid $sm_neighbor_mac"
        die_msg="No neighbor $sm_neighbor_mac was parsed"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was parsed"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed $sm_radio_type' | grep -i 'BSSID $sm_neighbor_mac'"
        ;;
    *parsed_neighbor_ssid*)
        log_msg="Checking for $sm_radio_type neighbor parsing of ssid $sm_neighbor_ssid"
        die_msg="No neighbor $sm_neighbor_ssid was parsed"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_ssid was parsed"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed $sm_radio_type' | grep -i 'SSID $sm_neighbor_ssid'"
        ;;
    *sending_neighbor*)
        log_msg="Checking for $sm_radio_type neighbor sending of $sm_neighbor_mac"
        die_msg="No neighbor $sm_neighbor_mac was added"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was added"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Sending $sm_radio_type' | grep -i \"$sm_survey_type neighbors {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid'\" | grep -i 'chan=$sm_channel'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "$fn_name" -arg
        ;;
    esac

    log "$fn_name - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "FAIL: $die_msg" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks existance of neigbor report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  channel (required)
#   $3  survey type (required)
#   $4  reporting interval (required)
#   $5  sampling interval (required)
#   $6  report type (required)
#   $7  neighbor ssid (required)
#   $8  neighbor MAC address (required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   inspect_neighbor_report 5GL 1 on-chan 10 5 raw <neighbor SSID> <neighbor MAC>
###############################################################################
inspect_neighbor_report()
{
    fn_name="sm_lib:inspect_neighbor_report"
    local NARGS=8
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_neighbor_ssid=$7
    # shellcheck disable=SC2018,SC2019
    sm_neighbor_mac=$(echo "$8" | tr a-z A-Z)

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    insert_ws_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "survey" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    insert_ws_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "neighbor" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" add_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" parsed_neighbor_bssid "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" parsed_neighbor_ssid "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" sending_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks leaf report log messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported log types: connected, client_parsing, client_update, sending.
#   Raises exception on fail:
#       - incorrect log type provided
#       - logs not found
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  client mac (required)
#   $3  log type (required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_neighbor_report_log 5GL <client MAC> connected
#   check_neighbor_report_log 5GL <client MAC> client_parsing
###############################################################################
check_leaf_report_log()
{
    fn_name="sm_lib:check_leaf_report_log"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    # shellcheck disable=SC2018,SC2019
    sm_client_mac_address=$(echo "$2" | tr a-z A-Z)
    sm_log_type=$3

    case $sm_log_type in
    *connected*)
        log_msg="Checking logs for leaf reporting radio $sm_radio_type connection established"
        die_msg="No client $sm_client_mac_address connected for reporting"
        sm_log_test_pass_msg="Client $sm_client_mac_address connected for reporting"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Marked $sm_radio_type' | grep -i 'client $sm_client_mac_address connected'"
        ;;
    *client_parsing*)
        log_msg="Checking logs for leaf parsing $sm_client_mac_address"
        die_msg="No client $sm_client_mac_address parsed"
        sm_log_test_pass_msg="Client $sm_client_mac_address parsed"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed $sm_radio_type client MAC $sm_client_mac_address'"
        ;;
    *client_update*)
        log_msg="Checking logs for leaf entry update $sm_client_mac_address"
        die_msg="No client $sm_client_mac_address updated"
        sm_log_test_pass_msg="Client $sm_client_mac_address updated"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Updating $sm_radio_type' | grep -i 'client $sm_client_mac_address entry'"
        ;;
    *sending*)
        log_msg="Checking logs for leaf $sm_client_mac_address $sm_radio_type sample sending"
        die_msg="No client $sm_client_mac_address $sm_radio_type sample sending initiated"
        sm_log_test_pass_msg="client $sm_client_mac_address $sm_radio_type sample sending initiated"
        sm_log_grep="$LOGREAD | tail -500 | grep -i 'Sending $sm_radio_type' | grep -i 'client $sm_client_mac_address stats'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "$fn_name" -arg
        ;;
    esac
    log -deb "$fn_name - $log_msg"

    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "FAIL: $die_msg" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks existance of leaf report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported report type: raw
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  radio type (required)
#   $2  reporting interval (required)
#   $3  sampling interval (required)
#   $4  report type (required)
#   $5  leaf mac (required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   inspect_leaf_report 5GL 10 5 raw <leaf MAC>
###############################################################################
inspect_leaf_report()
{
    fn_name="sm_lib:inspect_leaf_report"
    local NARGS=5
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_reporting_interval=$2
    sm_sampling_interval=$3
    sm_report_type=$4
    sm_leaf_mac=$5

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    insert_ws_config \
        "$sm_radio_type" \
        "[\"set\",[]]" \
        "survey" \
        "[\"set\",[]]" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    insert_ws_config \
        "$sm_radio_type" \
        "[\"set\",[]]" \
        "client" \
        "[\"set\",[]]" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" connected
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" client_parsing
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" client_update
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" sending

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "$fn_name" -oe

    return 0
}

####################### TEST CASE SECTION - STOP #############################
