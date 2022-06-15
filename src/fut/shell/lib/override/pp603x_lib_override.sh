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
#   PP603X libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/pp603x_lib_override.sh sourced"

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
    log -deb "pp603x_lib_override:disable_watchdog - Disabling watchdog"
    ${OPENSYNC_ROOTDIR}/bin/wpd --set-auto
    sleep 1
    # shellcheck disable=SC2034
    PID=$(pidof wpd) || raise "wpd not running" -l "pp603x_lib_override:disable_watchdog" -ds
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
        log -deb "pp603x_lib_override:device_init - Watchdog disabled - Success" ||
        raise "FAIL: device_init - Could not disable watchdog" -l "pp603x_lib_override:device_init" -ds

    stop_managers &&
        log -deb "pp603x_lib_override:device_init - Managers stopped - Success" ||
        raise "FAIL: stop_managers - Could not stop managers" -l "pp603x_lib_override:device_init" -ds

    stop_healthcheck &&
        log -deb "pp603x_lib_override:device_init - Healthcheck stopped - Success" ||
        raise "FAIL: stop_healthcheck - Could not stop healthcheck" -l "pp603x_lib_override:device_init" -ds

    disable_fatal_state_cm &&
        log -deb "pp603x_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: disable_fatal_state_cm - Could not disable CM fatal state" -l "pp603x_lib_override:device_init" -ds

    return $?
}

###############################################################################
# DESCRIPTION:
#   Function returns channel set at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  VIF interface name (string, required)
# RETURNS:
#   Echoes channel set for interface
# USAGE EXAMPLE(S):
#   get_channel_from_os home-ap-24
###############################################################################
get_channel_from_os()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "qca_platform_override:get_channel_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    iw $wm2_vif_if_name info | grep -F channel | awk '{ print $2 }'
}

###############################################################################
# DESCRIPTION:
#   Function returns HT mode set at OS - LEVEL2.
# INPUT PARAMETER(S):
#   $1  VIF interface name (string, required)
#   $2  channel (int, not used, but still required, do not optimize)
# RETURNS:
#   Echoes HT mode set for interface
# USAGE EXAMPLE(S):
#   get_ht_mode_from_os home-ap-24 1
###############################################################################
get_ht_mode_from_os()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "qca_platform_override:get_ht_mode_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2

    iwpriv $wm2_vif_if_name get_mode | sed 's/HE/ HT/g' | sed 's/PLUS$//' | sed 's/MINUS$//' | awk '{ print $3 }'
}

###############################################################################
# DESCRIPTION:
#   Function checks existence of neighbor report messages.
#   Supported radio bands: 2.4G, 5G, 6G
#   Supported survey types: on-chan, off-chan
#   Supported report type: raw
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  radio band (string, required)
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
#   inspect_neighbor_report 5G 1 on-chan 10 5 raw <neighbor SSID> <neighbor MAC>
###############################################################################
inspect_neighbor_report()
{
    local NARGS=8
    [ $# -ne ${NARGS} ] &&
        raise "pp603x_lib_override:inspect_neighbor_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_band=$1
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
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "pp603x_lib_override:inspect_neighbor_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_band" \
        "$sm_channel_list" \
        "survey" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "pp603x_lib_override:inspect_neighbor_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "pp603x_lib_override:inspect_neighbor_report" -oe

    insert_wifi_stats_config \
        "$sm_radio_band" \
        "$sm_channel_list" \
        "neighbor" \
        "$sm_survey_type" \
        "$sm_reporting_interval" \
        "$sm_sampling_interval" \
        "$sm_report_type" &&
            log -deb "pp603x_lib_override:inspect_neighbor_report - Wifi_Stats_Config inserted - Success" ||
            raise "FAIL: Could not insert Wifi_Stats_Config: insert_wifi_stats_config" -l "pp603x_lib_override:inspect_neighbor_report" -oe

    check_neighbor_report_log "$sm_radio_band" "$sm_channel" "$sm_survey_type" adding_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"
    check_neighbor_report_log "$sm_radio_band" "$sm_channel" "$sm_survey_type" sending_neighbor "$sm_neighbor_mac" "$sm_neighbor_ssid"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty Wifi_Stats_Config table" -l "pp603x_lib_override:inspect_neighbor_report" -oe

    return 0
}
