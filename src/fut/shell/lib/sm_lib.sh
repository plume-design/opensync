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
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
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
# INPUT PARAMETER(S):
#   $@  interfaces (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   sm_setup_test_environment
###############################################################################
sm_setup_test_environment()
{
    log "sm_lib:sm_setup_test_environment - Running SM setup"

    wm_setup_test_environment "$@" &&
        log -deb "sm_lib:sm_setup_test_environment - wm_setup_test_environment - Success" ||
        raise "FAIL: wm_setup_test_environment" -l "sm_lib:sm_setup_test_environment" -ds

    restart_managers
    log -deb "sm_lib:sm_setup_test_environment - Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "sm_lib:sm_setup_test_environment - AW_Debug table emptied - Success" ||
        raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty table" -l "sm_lib:sm_setup_test_environment" -ds

    set_manager_log SM TRACE &&
        log -deb "sm_lib:sm_setup_test_environment - Manager log for SM set to TRACE - Success" ||
        raise "FAIL: set_manager_log SM TRACE - Could not set SM manager log severity" -l "sm_lib:sm_setup_test_environment" -ds

    log -deb "sm_lib:sm_setup_test_environment - SM setup - end"

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
#   $1  radio type (string, required)
#   $2  channel list (string, required)
#   $3  stats type (string, required)
#   $4  survey type (string, required)
#   $5  reporting interval (int, required)
#   $6  sampling interval (int, required)
#   $6  report type (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   insert_wifi_stats_config 5GL "[\"set\",[$sm_channel]]" survey on-chan 10 5 raw
#   insert_wifi_stats_config 5GL "[\"set\",[$sm_channel]]" survey "[\"set\",[]]" 10 5 raw
###############################################################################
insert_wifi_stats_config()
{
    local NARGS=7
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:insert_wifi_stats_config requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel_list=$2
    sm_stats_type=$3
    sm_survey_type=$4
    sm_reporting_interval=$5
    sm_sampling_interval=$6
    sm_report_type=$7

    log "sm_lib:insert_wifi_stats_config - Inserting configuration to Wifi_Stats_Config "

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
        -i report_type "$sm_report_type" &&
            log -deb "sm_lib:insert_wifi_stats_config - Configuration inserted to Wifi_Stats_Config table - Success" ||
            raise "FAIL: insert_ovsdb_entry - Could not insert to Wifi_Stats_Config" -l "sm_lib:insert_wifi_stats_config" -oe

    return 0
}

####################### SETUP SECTION - STOP ##################################

####################### TEST CASE SECTION - START #############################

###############################################################################
# DESCRIPTION:
#   Function checks existence of survey report log messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported log types: processing_survey, scheduled_scan, fetched_survey, sending_survey_report
#   Raises exception on fail:
#       - incorrect log type provided
#       - logs not found
# INPUT PARAMETER(S):
#   $1  radio type (string, required)
#   $2  channel (int, required)
#   $3  survey type (string, required)
#   $4  log type (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_survey_report_log 5GL 1 on-chan processing_survey
#   check_survey_report_log 5GL 1 on-chan scheduled_scan
###############################################################################
check_survey_report_log()
{
    local NARGS=4
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:check_survey_report_log requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4

    case $sm_log_type in
    *processing_survey*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting processing survey"
        die_msg="No survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Processing $sm_radio_type' | grep -i '$sm_survey_type $sm_channel'"
        ;;
    *scheduled_scan*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting scheduling survey"
        die_msg="No survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Scheduled $sm_radio_type $sm_survey_type $sm_channel scan'"
        ;;
    *fetched_survey*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting fetched survey"
        die_msg="No survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Fetched $sm_radio_type $sm_survey_type $sm_channel survey'"
        ;;
    *sending_survey_report*)
        log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting sending survey"
        die_msg="No survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_test_pass_msg="Survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Sending $sm_radio_type' | grep -i '$sm_survey_type $sm_channel survey report'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "sm_lib:check_survey_report_log" -arg
        ;;
    esac

    log "sm_lib:check_survey_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "sm_lib:check_survey_report_log - Found $sm_log_test_pass_msg - Success" ||
        raise "FAIL: $die_msg" -l "sm_lib:check_survey_report_log - Log not found" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function inspects existence of all survey report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception if fails to empty table Wifi_Stats_Config.
# INPUT PARAMETER(S):
#   $1  radio type (string, required)
#   $2  channel (int, required)
#   $3  survey type (string, required)
#   $4  reporting interval (int, required)
#   $5  sampling interval (int, required)
#   $6  report type (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_survey_report_log 5GL 1 on-chan 10 5 raw
###############################################################################
inspect_survey_report()
{
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:inspect_survey_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_stats_type="survey"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_Stats_Config" -l "sm_lib:inspect_survey_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "$sm_stats_type" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "sm_lib:inspect_survey_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "sm_lib:inspect_survey_report" -oe

    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" processing_survey
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" scheduled_scan
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" fetched_survey
    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" sending_survey_report

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_Stats_Config table" -l "sm_lib:inspect_survey_report" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks neighbor report log messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported log types: add_neighbor, parsed_neighbor_bssid,
#                        parsed_neighbor_ssid, sending_neighbor
#   Raises exception on fail:
#       - incorrect log type provided
#       - log not found
# INPUT PARAMETER(S):
#   $1  radio type (string, required)
#   $2  channel (int, required)
#   $3  survey type (string, required)
#   $4  log type (string, required)
#   $5  neighbor mac (string, required)
#   $6  neighbor ssid (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_neighbor_report_log 5GL 1 on-chan add_neighbor <neighbor MAC> <neighbor SSID>
###############################################################################
check_neighbor_report_log()
{
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:check_neighbor_report_log requires ${NARGS} input argument(s), $# given" -arg
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
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Adding $sm_radio_type' | grep -i \"$sm_survey_type neighbor {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid'\" | grep -i 'chan=$sm_channel'"
        ;;
    *parsed_neighbor_bssid*)
        log_msg="Checking for $sm_radio_type neighbor parsing of bssid $sm_neighbor_mac"
        die_msg="No neighbor $sm_neighbor_mac was parsed"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was parsed"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Parsed $sm_radio_type' | grep -i 'BSSID $sm_neighbor_mac'"
        ;;
    *parsed_neighbor_ssid*)
        log_msg="Checking for $sm_radio_type neighbor parsing of ssid $sm_neighbor_ssid"
        die_msg="No neighbor $sm_neighbor_ssid was parsed"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_ssid was parsed"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Parsed $sm_radio_type' | grep -i 'SSID $sm_neighbor_ssid'"
        ;;
    *sending_neighbor*)
        log_msg="Checking for $sm_radio_type neighbor sending of $sm_neighbor_mac"
        die_msg="No neighbor $sm_neighbor_mac was sent"
        sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was sent"
        sm_log_grep="$LOGREAD | tail -5000 | grep -i 'Sending $sm_radio_type' | grep -i \"$sm_survey_type neighbors {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid'\" | grep -i 'chan=$sm_channel'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "sm_lib:check_neighbor_report_log" -arg
        ;;
    esac

    log "sm_lib:check_neighbor_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "sm_lib:check_neighbor_report_log - Found $sm_log_test_pass_msg - Success" ||
        raise "FAIL: $die_msg" -l "sm_lib:check_neighbor_report_log - Log not found" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks existence of neighbor report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  radio type (string, required)
#   $2  channel (int, required)
#   $3  survey type (string, required)
#   $4  reporting interval (int, required)
#   $5  sampling interval (int, required)
#   $6  report type (string, required)
#   $7  neighbor ssid (string, required)
#   $8  neighbor MAC address (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   inspect_neighbor_report 5GL 1 on-chan 10 5 raw <neighbor SSID> <neighbor MAC>
###############################################################################
inspect_neighbor_report()
{
    local NARGS=8
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:inspect_neighbor_report requires ${NARGS} input argument(s), $# given" -arg
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
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "sm_lib:inspect_neighbor_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "survey" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "sm_lib:inspect_neighbor_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "sm_lib:inspect_neighbor_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "neighbor" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "sm_lib:inspect_neighbor_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "sm_lib:inspect_neighbor_report" -oe

    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" add_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" parsed_neighbor_bssid "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" parsed_neighbor_ssid "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" sending_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_Stats_Config table" -l "sm_lib:inspect_neighbor_report" -oe

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
#   $1  radio type (string, required)
#   $2  client mac (string, required)
#   $3  log type (string, required)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_neighbor_report_log 5GL <client MAC> connected
#   check_neighbor_report_log 5GL <client MAC> client_parsing
###############################################################################
check_leaf_report_log()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:check_leaf_report_log requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    # shellcheck disable=SC2018,SC2019
    sm_client_mac_address=$(echo "$2" | tr a-z A-Z)
    sm_log_type=$3

    case $sm_log_type in
    *connected*)
        log_msg="Checking logs for leaf reporting radio $sm_radio_type connection established"
        die_msg="No client $sm_client_mac_address connected for reporting"
        sm_log_test_pass_msg="Client $sm_client_mac_address connected for reporting"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Marked $sm_radio_type' | grep -i 'client $sm_client_mac_address connected'"
        ;;
    *client_parsing*)
        log_msg="Checking logs for leaf parsing $sm_client_mac_address"
        die_msg="No client $sm_client_mac_address parsed"
        sm_log_test_pass_msg="Client $sm_client_mac_address parsed"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Parsed $sm_radio_type client MAC $sm_client_mac_address'"
        ;;
    *client_update*)
        log_msg="Checking logs for leaf entry update $sm_client_mac_address"
        die_msg="No client $sm_client_mac_address updated"
        sm_log_test_pass_msg="Client $sm_client_mac_address updated"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Updating $sm_radio_type' | grep -i 'client $sm_client_mac_address entry'"
        ;;
    *sending*)
        log_msg="Checking logs for leaf $sm_client_mac_address $sm_radio_type sample sending"
        die_msg="No client $sm_client_mac_address $sm_radio_type sample sending initiated"
        sm_log_test_pass_msg="client $sm_client_mac_address $sm_radio_type sample sending initiated"
        sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Sending $sm_radio_type' | grep -i 'client $sm_client_mac_address stats'"
        ;;
    *)
        raise "FAIL: Incorrect log type provided" -l "sm_lib:check_leaf_report_log" -arg
        ;;
    esac

    log "sm_lib:check_leaf_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "sm_lib:check_leaf_report_log - Log $sm_log_test_pass_msg found - Success" ||
        raise "FAIL: $die_msg" -l "sm_lib:check_leaf_report_log" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks existence of leaf report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported report type: raw
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  radio type (string, required)
#   $2  reporting interval (int, required)
#   $3  sampling interval (int, required)
#   $4  report type (string, required)
#   $5  leaf mac (string, required)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   inspect_leaf_report 5GL 10 5 raw <leaf MAC>
###############################################################################
inspect_leaf_report()
{
    local NARGS=5
    [ $# -ne ${NARGS} ] &&
        raise "sm_lib:inspect_leaf_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_reporting_interval=$2
    sm_sampling_interval=$3
    sm_report_type=$4
    sm_leaf_mac=$5

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "sm_lib:inspect_leaf_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_type" \
        "[\"set\",[]]" \
        "survey" \
        "[\"set\",[]]" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "sm_lib:inspect_leaf_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "sm_lib:inspect_leaf_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_type" \
        "[\"set\",[]]" \
        "client" \
        "[\"set\",[]]" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "sm_lib:inspect_leaf_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "sm_lib:inspect_leaf_report" -oe

    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" connected
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" client_parsing
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" client_update
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" sending

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_Stats_Config table" -l "sm_lib:inspect_leaf_report" -oe

    return 0
}

####################### TEST CASE SECTION - STOP #############################
