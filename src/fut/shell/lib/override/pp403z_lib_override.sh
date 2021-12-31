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
#   PP403Z libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/pp403z_lib_override.sh sourced"

####################### UNIT OVERRIDE SECTION - START #########################

###############################################################################
# DESCRIPTION:
#   Function disables watchdog.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   disable_watchdog
###############################################################################
disable_watchdog()
{
    log -deb "pp403z_lib_override:disable_watchdog - Disabling watchdog"
    ${OPENSYNC_ROOTDIR}/bin/wpd --set-auto
    sleep 1
    # shellcheck disable=SC2034
    PID=$(pidof wpd) || raise "wpd not running" -l "pp403z_lib_override:disable_watchdog" -ds
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
        log -deb "pp403z_lib_override:stop_healthcheck - Disabling healthcheck."
        /etc/init.d/healthcheck stop || true

        log -deb "pp403z_lib_override:stop_healthcheck - Check if healthcheck is disabled"
        wait_for_function_response 1 "$(get_process_cmd) | grep -e 'healthcheck' | grep -v 'grep'"
        if [ "$?" -ne 0 ]; then
            log -deb "pp403z_lib_override:stop_healthcheck - Healthcheck is NOT disabled ! PID: $(get_pid "healthcheck")"
            return 1
        else
            log -deb "pp403z_lib_override:stop_healthcheck - Healthcheck is disabled."
        fi
    else
        log -deb "pp403z_lib_override:stop_healthcheck - Healthcheck is already disabled."
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
        log -deb "pp403z_lib_override:device_init - Watchdog disabled - Success" ||
        raise "FAIL: disable_watchdog - Could not disable watchdog" -l "pp403z_lib_override:device_init" -ds

    stop_managers &&
        log -deb "pp403z_lib_override:device_init - Managers stopped - Success" ||
        raise "FAIL: stop_managers - Could not stop managers" -l "pp403z_lib_override:device_init" -ds

    stop_healthcheck &&
        log -deb "pp403z_lib_override:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: stop_healthcheck - Could not stop healthcheck" -l "pp403z_lib_override:device_init" -ds

    cm_disable_fatal_state &&
        log -deb "pp403z_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: cm_disable_fatal_state - Could not disable CM fatal state" -l "pp403z_lib_override:device_init" -ds

    return $?
}

####################### UNIT OVERRIDE SECTION - STOP ##########################


####################### WM OVERRIDE SECTION - START ###########################

###############################################################################
# DESCRIPTION:
#   Function removes all STA interfaces, except explicitly provided ones.
#   Waits timeout time for interfaces to be removed.
#   Waits for system to react, or timeouts with error.
# INPUT PARAMETER(S):
#   $1  wait timeout in seconds (int, optional, default=DEFAULT_WAIT_TIME)
#   $2  STA interface name, interface to keep from removing (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_sta_interfaces_exclude
#   remove_sta_interfaces_exclude 60
###############################################################################
remove_sta_interfaces_exclude()
{
    # shellcheck disable=SC2034
    local wait_timeout=${1:-$DEFAULT_WAIT_TIME}
    local wm2_sta_if_name=$2

    if [ -n "${wm2_sta_if_name}" ]; then
        log -deb "pp403z_lib_override:remove_sta_interfaces_exclude - Removing STA interfaces except ${wm2_sta_if_name}"
        ovs_cmd="-w mode==sta -w if_name!=${wm2_sta_if_name}"
    else
        log -deb "pp403z_lib_override:remove_sta_interfaces_exclude - Removing all STA interfaces"
        ovs_cmd="-w mode==sta"
    fi
    ${OVSH} d Wifi_VIF_Config ${ovs_cmd} &&
        log -deb "pp403z_lib_override:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_Config - Success" ||
        raise "FAIL: Could not remove STA interfaces from Wifi_VIF_Config table" -l "pp403z_lib_override:remove_sta_interfaces_exclude" -oe
    sleep 15
    print_tables Wifi_VIF_Config Wifi_VIF_State

    log -deb "pp403z_lib_override:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_Config - Success"
}
####################### WM OVERRIDE SECTION - STOP ############################


####################### SM OVERRIDE SECTION - START ###########################

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
        raise "pp403z_lib_override:inspect_leaf_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_reporting_interval=$2
    sm_sampling_interval=$3
    sm_report_type=$4
    sm_leaf_mac=$5

    if [[ -z $sm_leaf_mac ]]; then
        raise "FAIL: Empty leaf MAC address" -l "pp403z_lib_override:inspect_leaf_report" -ow
    fi

    empty_ovsdb_table Wifi_Stats_Config

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

    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" init_client_rep
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" parsed_mac
    check_leaf_report_log "$sm_radio_type" "$sm_leaf_mac" marked_connected

    log -deb "pp403z_lib_override:inspect_leaf_report - Emptying Wifi_Stats_Config table"
    empty_ovsdb_table Wifi_Stats_Config &&
        log -deb "pp403z_lib_override:inspect_leaf_report - Wifi_Stats_Config table emptied - Success" ||
        raise "FAIL: Could not empty Wifi_Stats_Config table" -l "pp403z_lib_override:inspect_leaf_report" -tc

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
        raise "pp403z_lib_override:check_leaf_report_log requires ${NARGS} input argument(s), $# given" -arg
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
            sm_log_grep="$LOGREAD | tail -250 | grep -i 'Parsed ${radio_mode} client MAC ${leaf_mac_address}'"
        ;;
        *marked_connected*)
            log_msg="Checking logs for leaf $leaf_mac_address marked connected"
            die_msg="No leaf $leaf_mac_address was marked connected"
            sm_log_grep="$LOGREAD | tail -250 | grep -i 'Marked ${radio_mode}' | grep -i 'client ${leaf_mac_address} connected'"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "pp403z_lib_override:check_leaf_report_log" -arg
        ;;
    esac

    log -deb "pp403z_lib_override:check_leaf_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" 30 &&
        log -deb "pp403z_lib_override:check_leaf_report_log - Success" ||
        raise "FAIL: $die_msg" -l "pp403z_lib_override:check_leaf_report_log" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function inspects existence of all survey report messages.
#   Supported radio types: 2.4G, 5GL, 5GU
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception on failing to empty table Wifi_Stats_Config.
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
        raise "pp403z_lib_override:inspect_survey_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_stats_type="survey"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config
    insert_ws_config \
        "$sm_radio_type" \
        "$sm_channel_list" \
        "$sm_stats_type" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type"

    check_survey_report_log "$sm_radio_type" "$sm_channel" "$sm_survey_type" processing_survey

    log -deb "pp403z_lib_override:inspect_survey_report - Emptying Wifi_Stats_Config table"
    empty_ovsdb_table Wifi_Stats_Config &&
        log -deb "pp403z_lib_override:inspect_survey_report - Wifi_Stats_Config table emptied - Success" ||
        raise "FAIL: Could not empty Wifi_Stats_Config table" -l "pp403z_lib_override:inspect_survey_report" -tc

    return 0
}

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
        raise "pp403z_lib_override:check_survey_report_log requires ${NARGS} input argument(s), $# given" -arg
    report_radio=$1
    channel=$2
    type=$3
    log_type=$4

    echo "$report_radio $channel $type $log_type"

    case $log_type in
        *processing_survey*)
            log_msg="Checking logs for survey $report_radio channel $channel reporting processing survey"
            die_msg="No survey processing done on $report_radio $type-chan on channel $channel"
            sm_log_grep="$LOGREAD | tail -250 | grep -i 'Updated "${radio_mode}"' | grep -i '${type} survey chan ${channel}'"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "pp403z_lib_override:check_survey_report_log" -arg
        ;;
    esac

    log -deb "pp403z_lib_override:check_survey_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" 30 &&
        log -deb "pp403z_lib_override:check_survey_report_log - Success" ||
        raise "FAIL: $die_msg" -l "pp403z_lib_override:check_survey_report_log" -tc

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
#   $1  radio type (string, required)
#   $2  channel (int, required)
#   $3  survey type (string, required)
#   $4  log type (string, required)
#   $5  neighbor mac (string, required)
#   $6  neighbor ssid (string, required)
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
        raise "pp403z_lib_override:check_neighbor_report_log requires ${NARGS} input argument(s), $# given" -arg
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
            sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Adding $radio_mode' |  grep -i \"$type neighbor {bssid='$neighbor_mac_lowercase' ssid='$neighbor_ssid'\" | grep -i 'chan=$channel}'"
        ;;
        *parsed_neighbor_bssid*)
            log_msg="Checking for $radio_mode neighbor parsing of bssid $neighbor_mac_lowercase"
            die_msg="No neighbor $neighbor_mac_lowercase was parsed"
            sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Parsed $radio_mode BSSID $neighbor_mac'"
        ;;
        *parsed_neighbor_ssid*)
            log_msg="Checking for $radio_mode neighbor parsing of ssid $neighbor_ssid"
            die_msg="No neighbor $neighbor_ssid was parsed"
            sm_log_grep="$LOGREAD | tail -1000 | grep -i 'Parsed $radio_mode SSID $neighbor_ssid'"
        ;;
        *sending_neighbor*)
            log_msg="Checking for $radio_mode neighbor sending of $neighbor_mac_lowercase"
            die_msg="No neighbor $neighbor_mac_lowercase was added"
            sm_log_grep="$LOGREAD | tail -2000 | grep -i 'Sending $radio_mode' | grep -i \"$type neighbors {bssid='$neighbor_mac_lowercase' ssid='$neighbor_ssid'\" | grep -i 'chan=$channel}'"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "pp403z_lib_override:check_neighbor_report_log" -arg
        ;;
    esac

    log -deb "pp403z_lib_override:check_neighbor_report_log - $log_msg"
    wait_for_function_response 0 "${sm_log_grep}" &&
        log -deb "pp403z_lib_override:check_neighbor_report_log - Success" ||
        raise "FAIL: $die_msg" -l "pp403z_lib_override:check_neighbor_report_log" -tc

    return 0
}

####################### SM OVERRIDE SECTION - STOP ############################

####################### NM OVERRIDE SECTION - START ###########################

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
#   $1  internal interface name (string, required)
#   $2  external interface name (string, required)
# RETURNS:
# USAGE EXAMPLE(S):
#   N/A
###############################################################################
check_upnp_configuration_valid()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "pp403z_lib_override:check_upnp_configuration_valid requires ${NARGS} input argument(s), $# given" -arg
    nm2_internal_if=$1
    nm2_external_if=$2

    log -deb "pp403z_lib_override:check_upnp_configuration_valid - LEVEL2 - Checking if $nm2_internal_if set as internal interface"
    grep -q "listening_ip=$nm2_internal_if" /var/tmp/miniupnpd/miniupnpd.conf ||
        raise "FAIL: UPnP configuration not valid for internal interface" -l "pp403z_lib_override:check_upnp_configuration_valid" -tc

    log -deb "pp403z_lib_override:check_upnp_configuration_valid - LEVEL2 - Checking if $nm2_external_if set as external interface"
    grep -q "ext_ifname=$nm2_external_if" /var/tmp/miniupnpd/miniupnpd.conf ||
        raise "FAIL: UPnP configuration not valid for external interface" -l "pp403z_lib_override:check_upnp_configuration_valid" -tc
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
    log -deb "pp403z_lib_override:vif_clean - Purging VIF"
    empty_ovsdb_table Wifi_VIF_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_VIF_Config table" -l "pp403z_lib_override:vif_clean" -oe
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
    log -deb "pp403z_lib_override:onbrd_setup_test_environment - Running ONBRD setup"

    device_init &&
        log -deb "pp403z_lib_override:onbrd_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "pp403z_lib_override:onbrd_setup_test_environment" -ds

    start_openswitch &&
        log -deb "pp403z_lib_override:onbrd_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "pp403z_lib_override:onbrd_setup_test_environment" -ds

    restart_managers
    log -deb "pp403z_lib_override:onbrd_setup_test_environment: Executed restart_managers, exit code: $?"

    sleep 15

    log -deb "pp403z_lib_override:onbrd_setup_test_environment - ONBRD setup - end"

    return 0
}

####################### ONBRD OVERRIDE SECTION - STOP #########################
