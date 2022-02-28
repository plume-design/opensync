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


####################### INFORMATION SECTION - START ###########################
#
#   BCM947622DVT libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/bcm947622dvt_lib_override.sh sourced"

####################### UNIT OVERRIDE SECTION - START #########################

###############################################################################
# DESCRIPTION:
#   Function returns filename of the script manipulating OpenSync managers.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Path to managers script.
# USAGE EXAMPLE(S):
#   get_managers_script
###############################################################################
get_managers_script()
{
    echo "/etc/init.d/manager"
}

###############################################################################
# DESCRIPTION:
#   Function stops healthcheck process and disables it.
#   Checks if healthcheck already stopped, does nothing if already stopped.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   1   healthcheck process is not stopped.
#   0   healthcheck process is stopped.
# USAGE EXAMPLE(S):
#   stop_healthcheck
###############################################################################
stop_healthcheck()
{
    if [ -n "$(get_pid "healthcheck")" ]; then
        log "bcm947622dvt_lib_override:stop_healthcheck - Disabling healthcheck."
        /etc/init.d/healthcheck stop || true

        log -deb "bcm947622dvt_lib_override:stop_healthcheck - Check if healthcheck is disabled"
        wait_for_function_response 1 "$(get_process_cmd) | grep -e 'healthcheck' | grep -v 'grep'"
        if [ "$?" -ne 0 ]; then
            log -deb "bcm947622dvt_lib_override:stop_healthcheck - Healthcheck is NOT disabled ! PID: $(get_pid "healthcheck")"
            return 1
        else
            log -deb "bcm947622dvt_lib_override:stop_healthcheck - Healthcheck is disabled."
        fi
    else
        log -deb "bcm947622dvt_lib_override:stop_healthcheck - Healthcheck is already disabled."
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function initializes device for use in FUT.
#   It disables watchdog to prevent the device from rebooting.
#   It stops healthcheck service to prevent the device from rebooting.
#   It calls a function that instructs CM to prevent the device from rebooting.
#   It stops all managers.
#   It ensures that the "/etc" folder is writable.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   device_init
###############################################################################
device_init()
{
    disable_watchdog &&
        log -deb "bcm947622dvt_lib_override:device_init - Watchdog disabled - Success" ||
        raise "FAIL: Could not disable watchdog" -l "bcm947622dvt_lib_override:device_init" -ds
    stop_managers &&
        log -deb "bcm947622dvt_lib_override:device_init - Managers stopped - Success" ||
        raise "FAIL: Could not stop managers" -l "bcm947622dvt_lib_override:device_init" -ds
    stop_healthcheck &&
        log -deb "bcm947622dvt_lib_override:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: Could not stop healthcheck" -l "bcm947622dvt_lib_override:device_init" -ds
    disable_fatal_state_cm &&
        log -deb "bcm947622dvt_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: Could not disable CM fatal state" -l "bcm947622dvt_lib_override:device_init" -ds
    log_state_value="$(get_kconfig_option_value "TARGET_PATH_LOG_STATE")"
    log_state_file=$(echo ${log_state_value} | tr -d '"')
    log_dir="${log_state_file%/*}"
    [ -n "${log_dir}" ] || raise "Kconfig option TARGET_PATH_LOG_STATE value empty" -l "bcm947622dvt_lib_override:device_init" -ds
    set_dir_to_writable "${log_dir}" &&
        log -deb "bcm947622dvt_lib_override:device_init - ${log_dir} is writable - Success" ||
        raise "FAIL: ${log_dir} is not writable" -l "bcm947622dvt_lib_override:device_init" -ds
}

####################### UNIT OVERRIDE SECTION - STOP ##########################


####################### WM OVERRIDE SECTION - START ###########################

###############################################################################
# DESCRIPTION:
#   Function validates that CAC was completed in set CAC time for specific channel which is set in Wifi_Radio_State
# INPUT PARAMETER(S):
#   $1  Physical Radio interface name for which to validate CAC if needed
# RETURNS:
#   0 - In following cases:
#       - shell/config/regulatory.txt file was not found
#       - No associated, enabled and AP VIF found for specified Phy Radio interface
#       - Channel, ht_mode or freq_band is not set in Wifi_Radio_State for specified Phy Radio interface
#       - Channel which is set in Wifi_Radio_State is not DFS nor DFS-WEATHER channel
#       - CAC was at cac_completed state in Wifi_Radio_State for specific channel which is set in Wifi_Radio_State
#   1 - In following cases:
#       - Invalid number of arguments specified
#       - Failure to acquire vif_states uuid-s from Wifi_Radio_State for specified Phy Radio name
#       - CAC was not completed in given CAC time for specific channel which is set in Wifi_Radio_State
# NOTE:
# - CAC times are hardcoded inside this function and their values are following:
#   - NON-DFS : 0s (CAC is not required for NON-DFS channels)
#   - DFS     : 60s
#   - WEATHER : 600s
# USAGE EXAMPLE(S):
#   validate_cac wifi0
###############################################################################
validate_cac()
{
    # First validate presence of regulatory.txt file
    regulatory_file_path="${FUT_TOPDIR}/shell/config/regulatory.txt"
    if [ ! -f "${regulatory_file_path}" ]; then
        log -deb "Regulatory file ${regulatory_file_path} does not exist, nothing to do."
        return 0
    fi

    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:validate_cac requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    if_name="${1}"
    # Check if Radio interface is associated to VIF ap
    vif_states=$(get_ovsdb_entry_value Wifi_Radio_State vif_states -w if_name "${if_name}")
    if [ "${vif_states}" == "[\"set\",[]]" ]; then
        log -deb "wm2_lib:validate_cac - Radio interfaces is not associated to any VIF, nothing to do."
        return 0
    fi
    # Check if channel is set in Wifi_Radio_State
    state_channel=$(get_ovsdb_entry_value Wifi_Radio_State channel -w if_name "${if_name}")
    if [ "${state_channel}" == "[\"set\",[]]" ]; then
        log -deb "wm2_lib:validate_cac - Channel is not set in Wifi_Radio_State, nothing to do."
        return 0
    fi
    state_ht_mode=$(get_ovsdb_entry_value Wifi_Radio_State ht_mode -w if_name "${if_name}")
    if [ "${state_ht_mode}" == "[\"set\",[]]" ]; then
        log -deb "wm2_lib:validate_cac - ht_mode is not set in Wifi_Radio_State, nothing to do."
        return 0
    fi
    state_freq_band=$(get_ovsdb_entry_value Wifi_Radio_State freq_band -w if_name "${if_name}" | tr '[A-Z]' '[a-z]')
    if [ "${state_freq_band}" == "[\"set\",[]]" ]; then
        log -deb "wm2_lib:validate_cac - freq_band is not set in Wifi_Radio_State, nothing to do."
        return 0
    fi

    # Retrieve device regulatory domain
    state_country=$(get_iface_regulatory_domain "${if_name}")
    echo "${state_country}"
    state_country=$(echo "${state_country}" | tail -1)

    # Check channel type if it requires CAC
    log -deb "wm2_lib:validate_cac - Country: ${state_country} | Channel: ${state_channel} | Freq band: ${state_freq_band} | HT mode: ${state_ht_mode}"
    reg_dfs_standard_match=$(cat "${regulatory_file_path}" | grep -i "${state_country}_dfs_standard_${state_freq_band}_${state_ht_mode}")
    check_standard=$(contains_element "${state_channel}" ${reg_dfs_standard_match})

    reg_dfs_weather_match=$(cat "${regulatory_file_path}" | grep -i "${state_country}_dfs_weather_${state_freq_band}_${state_ht_mode}")
    check_weather=$(contains_element "${state_channel}" ${reg_dfs_weather_match})

    if [ ${state_ht_mode} = "HT160" ]; then
        cac_time=60
        HT160_match=0
        non_dfs_channel_list="36 40 44 48"
        standard_dfs_channel_list="52 56 60 64"
    elif [ "${check_standard}" == 0 ]; then
        # channel_type='standard-dfs'
        cac_time=60
    elif [ "${check_weather}" == 0 ]; then
        # channel_type='weather-dfs'
        cac_time=600
    else
        log -deb "wm2_lib:validate_cac - Channel ${state_channel} is not DFS nor WEATHER channel so CAC wait is not required."
        return 0
    fi

    # Check if Radio is associated to any AP VIF (ignore STA vif-s)
    vif_states_uuids="$(get_ovsdb_entry_value Wifi_Radio_State vif_states -w if_name "${if_name}" -json_value uuid)"
    if [ "$?" != 0 ]; then
        raise "wm2_lib:validate_cac - Failed to acquire vif_states uuid-s for ${if_name}" -ds
    fi
    # Check if there is AP VIF and is enabled for specific Radio
    vif_found=1
    for i in ${vif_states_uuids}; do
        check_ovsdb_entry Wifi_VIF_State -w _uuid '["uuid",'$i']' -w mode ap -w enabled true
        if [ "${?}" == 0 ]; then
            log -deb "wm2_lib:validate_cac - Enabled and associated AP VIF found for Radio ${if_name} - Success"
            vif_found=0
            break
        fi
    done
    if [ "${vif_found}" == 1 ]; then
        raise "FAIL: Radio interfaces is not associated to any AP enabled VIF" -l "wm2_lib:validate_cac" -ds
    fi

    # HT160 spans the entire radio band, therefore all channels need to be checked i.e. non DFS channel state must
    # be allowed and DFS channel state must be cac_completed
    if [ "${HT160_match}" == 0 ]; then
        for channel in $non_dfs_channel_list
        do
            wait_for_function_output "allowed" "get_radio_channel_state ${channel} ${if_name}" &&
                log -deb "wm2_lib:validate_cac - Channel ${channel} is not DFS nor WEATHER channel so CAC wait is not required." ||
                log -err "FAIL: Channel CAC was not completed." -l "wm2_lib:validate_cac"
        done
        for channel in $standard_dfs_channel_list
        do
            wait_for_function_output "cac_completed" "get_radio_channel_state ${channel} ${if_name}" ${cac_time} &&
                log -deb "wm2_lib:validate_cac - Channel state went to cac_completed. Channel available" ||
                log -err "FAIL: Channel CAC was not completed in given CAC time (${cac_time}s)." -l "wm2_lib:validate_cac"
        done
        return 0
    elif [ "${check_standard}" == 0 ] || [ "${check_weather}" == 0 ]; then
        wait_for_function_output "cac_completed" "get_radio_channel_state ${state_channel} ${if_name}" ${cac_time} &&
            log -deb "wm2_lib:validate_cac - Channel state went to cac_completed. Channel available" ||
            log -err "FAIL: Channel CAC was not completed in given CAC time (${cac_time}s)." -l "wm2_lib:validate_cac"

        log -deb "wm2_lib:validate_cac - Checking interface ${if_name} channel ${state_channel} status"
        channel_status="$(get_radio_channel_state "${state_channel}" "${if_name}")"
        log -deb "wm2_lib:validate_cac - Channel status is: ${channel_status}"
        if [ "${channel_status}" == 'cac_completed' ]; then
            log -deb "wm2_lib:validate_cac -  CAC completed for channel ${state_channel} - Success"
            return 0
        else
            print_tables Wifi_Radio_State || true
            raise "FAIL: CAC was not completed for channel ${state_channel}" -l "wm2_lib/validate_cac" -ds
        fi
    fi
}

###############################################################################
# DESCRIPTION:
#   Function removes all STA interfaces, except explicitly provided ones.
#   Waits timeout time for interfaces to be removed.
#   Waits for system to react, or timeouts with error.
# INPUT PARAMETER(S):
#   $1  wait timeout in seconds (int, optional, default=DEFAULT_WAIT_TIME)
#   $2  sta interface name, interface to keep from removing (optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_sta_interfaces_exclude 60
###############################################################################
remove_sta_interfaces_exclude()
{
    # shellcheck disable=SC2034
    local wait_timeout=${1:-$DEFAULT_WAIT_TIME}
    local wm2_sta_if_name=$2

    if [ -n "${wm2_sta_if_name}" ]; then
        log "bcm947622dvt_lib_override:remove_sta_interfaces_exclude - Removing STA interfaces except ${wm2_sta_if_name}"
        ovs_cmd="-w mode==sta -w if_name!=${wm2_sta_if_name}"
    else
        log "bcm947622dvt_lib_override:remove_sta_interfaces_exclude - Removing all STA interfaces"
        ovs_cmd="-w mode==sta"
    fi
    ${OVSH} d Wifi_VIF_Config ${ovs_cmd} &&
        log -deb "bcm947622dvt_lib_override:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_Config" ||
        raise "Failed to remove STA interfaces from Wifi_VIF_Config" -l "bcm947622dvt_lib_override:remove_sta_interfaces_exclude" -oe
    sleep 15
    print_tables Wifi_VIF_Config Wifi_VIF_State
    log -deb "bcm947622dvt_lib_override:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_State"
}
####################### WM OVERRIDE SECTION - STOP ############################


####################### SM OVERRIDE SECTION - START ###########################

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
    local NARGS=5
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:inspect_leaf_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_reporting_interval=$2
    sm_sampling_interval=$3
    sm_report_type=$4
    sm_leaf_mac=$5

    if [[ -z $sm_leaf_mac ]]; then
        raise "Failed: Empty leaf MAC address" -l "bcm947622dvt_lib_override:inspect_leaf_report" -ow
    fi

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "bcm947622dvt_lib_override:inspect_leaf_report" -oe

    insert_wifi_stats_config \
        $sm_radio_type \
        "[\"set\",[]]" \
        "survey" \
        "[\"set\",[]]" \
        $sm_reporting_interval \
        $sm_sampling_interval \
        $sm_report_type

    insert_wifi_stats_config \
        $sm_radio_type \
        "[\"set\",[]]" \
        "client" \
        "[\"set\",[]]" \
        $sm_reporting_interval \
        $sm_sampling_interval \
        $sm_report_type

    check_leaf_report_log $sm_radio_type $sm_leaf_mac init_client_rep
    check_leaf_report_log $sm_radio_type $sm_leaf_mac parsed_mac
    check_leaf_report_log $sm_radio_type $sm_leaf_mac marked_connected

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "bcm947622dvt_lib_override:inspect_leaf_report" -oe

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
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:check_leaf_report_log requires ${NARGS} input argument(s), $# given" -arg
    radio_mode=$1
    leaf_mac_address=$(echo "$2" | tr a-z A-Z)
    log_type=$3

    case $log_type in
        *init_client_rep*)
            log_msg="Checking logs for client reporting initializing"
            die_msg="No client reporting was initialized"
            sm_log_grep="$LOGREAD | grep -i 'Initializing ${radio_mode}' | grep -i 'client reporting'"
        ;;
        *parsed_mac*)
            log_msg="Checking logs for leaf $leaf_mac_address MAC stats parsing"
            die_msg="No MAC stats parsed for leaf $leaf_mac_address"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed ${radio_mode} client MAC ${leaf_mac_address}'"
        ;;
        *marked_connected*)
            log_msg="Checking logs for leaf $leaf_mac_address marked connected"
            die_msg="No leaf $leaf_mac_address was marked connected"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Marked ${radio_mode}' | grep -i 'client ${leaf_mac_address} connected'"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "bcm947622dvt_lib_override:check_leaf_report_log" -arg
        ;;
    esac
    log "bcm947622dvt_lib_override:check_leaf_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" 30 &&
        log -deb "bcm947622dvt_lib_override:check_leaf_report_log - OK" ||
        raise "FAIL: $die_msg" -l "bcm947622dvt_lib_override:check_leaf_report_log" -tc

    return 0
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
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:inspect_survey_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_stats_type="survey"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "bcm947622dvt_lib_override:inspect_survey_report" -oe

    insert_wifi_stats_config \
        $sm_radio_type \
        $sm_channel_list \
        $sm_stats_type \
        $sm_survey_type \
        $sm_reporting_interval \
        $sm_sampling_interval \
        $sm_report_type

    check_survey_report_log $sm_radio_type $sm_channel $sm_survey_type processing_survey
    check_survey_report_log $sm_radio_type $sm_channel $sm_survey_type sending_survey_report_at

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "bcm947622dvt_lib_override:inspect_survey_report" -oe

    return 0
}

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
    local NARGS=4
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:check_survey_report_log requires ${NARGS} input argument(s), $# given" -arg
    report_radio=$1
    channel=$2
    type=$3
    log_type=$4

    echo "$report_radio $channel $type $log_type"

    case $log_type in
        *processing_survey*)
            log_msg="Checking logs for survey $report_radio channel $channel reporting processing survey"
            die_msg="No survey processing done on $report_radio $type-chan on channel $channel"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Processing $report_radio' | grep -i '$type $channel'"
        ;;
        *sending_survey_report_at*)
            log_msg="Checking logs for survey $report_radio channel $channel reporting send survey"
            die_msg="No survey was sent for $report_radio $type on channel $channel"
            # shellcheck disable=SC2034
            sm_log_test_pass_msg="Survey was sent for $report_radio $type on channel $channel"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Sending $report_radio' | grep -i '$type survey report at'"
            ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "bcm947622dvt_lib_override:check_survey_report_log" -arg
        ;;
    esac
    log "bcm947622dvt_lib_override:check_survey_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" 30 &&
        log -deb "bcm947622dvt_lib_override:check_survey_report_log - OK" ||
        raise "FAIL: $die_msg" -l "bcm947622dvt_lib_override:check_survey_report_log" -tc

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
    local NARGS=6
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:check_neighbor_report_log requires ${NARGS} input argument(s), $# given" -arg
    radio_mode=$1
    channel=$2
    type=$3
    log_type=$4
    neighbor_mac=$5
    neighbor_ssid=$6
    neighbor_mac_lowercase=$(echo "$5" | tr A-Z a-z)

    case $log_type in
        *add_neighbor*)
            log_msg="Checking for $radio_mode neighbor adding for $neighbor_mac_lowercase"
            die_msg="No neighbor $neighbor_mac_lowercase was added"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Adding $radio_mode' | grep -i \"$type neighbor {bssid='$neighbor_mac_lowercase' ssid='$neighbor_ssid'\" | grep -i 'chan=$channel}'"
        ;;
        *parsed_neighbor_bssid*)
            log_msg="Checking for $radio_mode neighbor parsing of bssid $neighbor_mac_lowercase"
            die_msg="No neighbor $neighbor_mac_lowercase was parsed"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed $radio_mode BSSID $neighbor_mac'"
        ;;
        *parsed_neighbor_ssid*)
            log_msg="Checking for $radio_mode neighbor parsing of ssid $neighbor_ssid"
            die_msg="No neighbor $neighbor_ssid was parsed"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Parsed $radio_mode SSID $neighbor_ssid'"
        ;;
        *sending_neighbor*)
            log_msg="Checking for $radio_mode neighbor sending of $neighbor_mac_lowercase"
            die_msg="No neighbor $neighbor_mac_lowercase was added"
            sm_log_grep="$LOGREAD | tail -500 | grep -i 'Sending $radio_mode' | grep -i \"$type neighbors {bssid='$neighbor_mac_lowercase' ssid='$neighbor_ssid'\" | grep -i 'chan=$channel}'"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "bcm947622dvt_lib_override:check_neighbor_report_log" -arg
        ;;
    esac
    log "bcm947622dvt_lib_override:check_neighbor_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" 30 &&
        log -deb "bcm947622dvt_lib_override:check_neighbor_report_log - OK" ||
        raise "FAIL: $die_msg" -l "bcm947622dvt_lib_override:check_neighbor_report_log" -tc

    return 0
}

####################### SM OVERRIDE SECTION - STOP ############################

####################### NM OVERRIDE SECTION - START ###########################

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
#   N/A
###############################################################################
check_upnp_configuration_valid()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "bcm947622dvt_lib_override:check_upnp_configuration_valid requires ${NARGS} input argument(s), $# given" -arg
    nm2_internal_if=$1
    nm2_external_if=$2

    log "bcm947622dvt_lib_override:check_upnp_configuration_valid - LEVEL 2 - Checking if $nm2_internal_if set as internal interface"
    grep -q "listening_ip=$nm2_internal_if" /var/tmp/miniupnpd/miniupnpd.conf ||
        raise "UPnP configuration NOT VALID for internal interface" -l "bcm947622dvt_lib_override:check_upnp_configuration_valid" -tc

    log "bcm947622dvt_lib_override:check_upnp_configuration_valid - LEVEL 2 - Checking if $nm2_external_if set as external interface"
    grep -q "ext_ifname=$nm2_external_if" /var/tmp/miniupnpd/miniupnpd.conf &&
        log -deb "bcm947622dvt_lib_override:check_upnp_configuration_valid - UPnP configuration valid for external interface" ||
        raise "UPnP configuration NOT VALID for external interface" -l "bcm947622dvt_lib_override:check_upnp_configuration_valid" -tc
}

####################### NM OVERRIDE SECTION - STOP ############################

####################### WM OVERRIDE SECTION - START ###########################

###############################################################################
# DESCRIPTION:
#   Function empties all VIF interfaces.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   vif_clean
###############################################################################
vif_clean()
{
    log "bcm947622dvt_lib_override:vif_clean - Purging VIF"
    empty_ovsdb_table Wifi_VIF_Config ||
        raise "FAIL: Could not empty table Wifi_VIF_Config: empty_ovsdb_table" -l "bcm947622dvt_lib_override:vif_clean" -oe
    sleep 5
}

####################### WM OVERRIDE SECTION - STOP ############################

####################### ONBRD OVERRIDE SECTION - START ########################

###############################################################################
# DESCRIPTION:
#   Function prepares device for ONBRD tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   onbrd_setup_test_environment
###############################################################################
onbrd_setup_test_environment()
{
    log "bcm947622dvt_lib_override:onbrd_setup_test_environment - Running ONBRD setup"

    device_init &&
        log -deb "bcm947622dvt_lib_override:onbrd_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "bcm947622dvt_lib_override:onbrd_setup_test_environment" -ds

    start_openswitch &&
        log -deb "bcm947622dvt_lib_override:onbrd_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "bcm947622dvt_lib_override:onbrd_setup_test_environment" -ds

    restart_managers
    log -deb "bcm947622dvt_lib_override:onbrd_setup_test_environment: Executed restart_managers, exit code: $?"

    sleep 15

    log -deb "bcm947622dvt_lib_override:onbrd_setup_test_environment - ONBRD setup - end"

    return 0
}

####################### ONBRD OVERRIDE SECTION - STOP #########################
