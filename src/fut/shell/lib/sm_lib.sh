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
if [ -e "/tmp/fut_set_env.sh" ]; then
    source /tmp/fut_set_env.sh
else
    source "${FUT_TOPDIR}/shell/config/default_shell.sh"
fi
source "${FUT_TOPDIR}/shell/lib/wm2_lib.sh"
source "${LIB_OVERRIDE_FILE}"


############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common Stats Manager functions
#
############################################ INFORMATION SECTION - STOP ################################################

############################################ SETUP SECTION - START #####################################################

sm_log_test_pass_msg="---------------------------------- OK ----------------------------------"

sm_setup_test_environment()
{
    fn_name="sm_lib:sm_setup_test_environment"
    wm_setup_test_environment "$@" &&
        log -deb "$fn_name - wm_setup_test_environment - Success" ||
        raise "- wm_setup_test_environment - Failed" -l "$fn_name" -ds

    # Check if LM can be started, if not try starting PM.
    # If it fails raise an exception.
    start_specific_manager lm
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - start_specific_manager lm - Success"
    else
        log -deb "$fn_name - start_specific_manager lm - Failed. Trying to start pm instead"
        start_specific_manager pm
        if [ $? -eq 0 ]; then
            log -deb "$fn_name - start_specific_manager pm - Success"
        else
            raise "Both start_specific_manager lm, start_specific_manager pm - Failed" -l "$fn_name" -ds
        fi
    fi

    # QM start for report queue handling
    start_specific_manager qm &&
        log -deb "$fn_name - start_specific_manager qm - Success" ||
        raise "start_specific_manager qm - Failed" -l "$fn_name" -ds

    start_specific_manager sm &&
        log -deb "$fn_name - start_specific_manager sm - Success" ||
        raise "start_specific_manager sm - Failed" -l "$fn_name" -ds

    empty_ovsdb_table AW_Debug
    set_manager_log SM TRACE
}

insert_ws_config()
{
    fn_name="sm_lib:insert_ws_config"
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
            raise "Failed insert_ovsdb_entry" -l "$fn_name" -oe

    log -deb "$fn_name - Wifi_Stats_Config config Inserted"
}

############################################ SETUP SECTION - STOP #####################################################


############################################ TEST CASE SECTION - START #################################################

check_survey_report_log()
{
    fn_name="sm_lib:check_survey_report_log"
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4

    case $sm_log_type in
        *processing_survey*)
            sm_log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting processing survey"
            sm_die_msg="No survey processing done on $sm_radio_type $sm_survey_type-chan on channel $sm_channel"
            match_pattern_for_log_inspecting="Processing $sm_radio_type .* $sm_survey_type $sm_channel"
        ;;
        *scheduled_scan*)
            sm_log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting scheduling survey"
            sm_die_msg="No survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Scheduled $sm_radio_type $sm_survey_type $sm_channel scan"
        ;;
        *fetched_survey*)
            sm_log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting fetched survey"
            sm_die_msg="No survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Fetched $sm_radio_type $sm_survey_type $sm_channel survey"
        ;;
        *sending_survey_report*)
            sm_log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting sending survey"
            sm_die_msg="No survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* $sm_survey_type $sm_channel survey report"
        ;;
        *)
            raise "Incorrect sm_log_type provided" -l "$fn_name" -arg
    esac

    log "$fn_name - $sm_log_msg"
    wait_for_function_response 0 "$LOGREAD | tail -250 | grep -q \"$match_pattern_for_log_inspecting\"" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "$sm_die_msg" -l "$fn_name" -tc
}

inspect_survey_report()
{
    fn_name="sm_lib:inspect_survey_report"
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_stats_type="survey"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -tc

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
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -tc

    return 0
}

check_neighbor_report_log()
{
    fn_name="sm_lib:check_neighbor_report_log"
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4
    sm_neighbor_mac=$5
    sm_neighbor_ssid=$6

    case $sm_log_type in
        *add_neighbor*)
            sm_log_msg="Checking for $sm_radio_type neighbor adding for $sm_neighbor_mac"
            sm_die_msg="No neighbor $sm_neighbor_mac was added"
            match_pattern_for_log_inspecting="Adding $sm_radio_type .* $sm_survey_type neighbor {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid' .* chan=$sm_channel}"
        ;;
        *parsed_neighbor_bssid*)
            sm_log_msg="Checking for $sm_radio_type neighbor parsing of bssid $sm_neighbor_mac"
            sm_die_msg="No neighbor $sm_neighbor_mac was parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type BSSID $sm_neighbor_mac"
        ;;
        *parsed_neighbor_ssid*)
            sm_log_msg="Checking for $sm_radio_type neighbor parsing of ssid $sm_neighbor_ssid"
            sm_die_msg="No neighbor $sm_neighbor_ssid was parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type SSID $sm_neighbor_ssid"
        ;;
        *sending_neighbor*)
            sm_log_msg="Checking for $sm_radio_type neighbor sending of $sm_neighbor_mac"
            sm_die_msg="No neighbor $sm_neighbor_mac was added"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* $sm_survey_type neighbors {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid' .* chan=$sm_channel}"
        ;;
        *)
            raise "Incorrect sm_log_type provided" -l "$fn_name" -arg
    esac

    log "$fn_name - $sm_log_msg"
    wait_for_function_response 0 "$LOGREAD | tail -250 | grep -q \"$match_pattern_for_log_inspecting\"" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "$sm_die_msg" -l "$fn_name" -tc
}

inspect_neighbor_report()
{
    fn_name="sm_lib:inspect_neighbor_report"
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_neighbor_ssid=$7
    sm_neighbor_mac=$(echo "$8" | tr a-z A-Z)

    if [ -z "$sm_neighbor_mac" ] || [ -z "$sm_neighbor_ssid" ]; then
        raise "Empty neighbor MAC address, or neighbor ssid" -l "$fn_name" -arg
    fi

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -tc

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
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -tc
    return 0
}

check_leaf_report_log()
{
    fn_name="sm_lib:check_leaf_report_log"
    sm_radio_type=$1
    sm_client_mac_address=$(echo "$2" | tr a-z A-Z)
    sm_log_type=$3

    case $sm_log_type in
        *connected*)
            sm_log_msg="Checking logs for leaf reporting radio $sm_radio_type connection established"
            sm_die_msg="No client $sm_client_mac_address connected for reporting"
            match_pattern_for_log_inspecting="Marked $sm_radio_type .* client $sm_client_mac_address connected"
        ;;
        *client_parsing*)
            sm_log_msg="Checking logs for leaf parsing $sm_client_mac_address"
            sm_die_msg="No client $sm_client_mac_address parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type client MAC $sm_client_mac_address"
        ;;
        *client_update*)
            sm_log_msg="Checking logs for leaf entry update $sm_client_mac_address"
            sm_die_msg="No client $sm_client_mac_address updated"
            match_pattern_for_log_inspecting="Updating $sm_radio_type .* client $sm_client_mac_address entry"
        ;;
        *sending*)
            sm_log_msg="Checking logs for leaf $sm_client_mac_address $sm_radio_type sample sending"
            sm_die_msg="No client $sm_client_mac_address $sm_radio_type sample sending initiated"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* client $sm_client_mac_address stats"
        ;;
        *)
            raise "Incorrect sm_log_type provided" -l "$fn_name" -arg
    esac
    log -deb "$fn_name - $sm_log_msg"

    wait_for_function_response 0 "$LOGREAD | tail -250 | grep -q \"$match_pattern_for_log_inspecting\"" &&
        log -deb "$fn_name - $sm_log_test_pass_msg" ||
        raise "$sm_die_msg" -l "$fn_name" -tc
}

inspect_leaf_report()
{
    fn_name="sm_lib:inspect_leaf_report"
    sm_radio_type=$1
    sm_reporting_interval=$2
    sm_sampling_interval=$3
    sm_report_type=$4
    sm_leaf_mac=$5

    if [ -z "$sm_leaf_mac" ]; then
        raise "Empty leaf MAC address" -l "$fn_name" -arg
    fi

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -oe

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
        raise "Failed empty_ovsdb_table Wifi_Stats_Config" -l "$fn_name" -oe

    return 0
}

############################################ TEST CASE SECTION - STOP ##################################################
