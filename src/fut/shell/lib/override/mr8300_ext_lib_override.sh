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
#   MR8300-EXT libraries overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/mr8300_ext_lib_override.sh sourced"

###############################################################################
# DESCRIPTION:
#   Function returns tx_power set at OS level – LEVEL2.
#   Uses iw to get tx_power info from VIF interface.
# INPUT PARAMETER(S):
#   $1  VIF interface name (required)
# RETURNS:
#   0   on successful tx_power retrieval, fails otherwise
# ECHOES:
#   tx_power from OS
# USAGE EXAMPLE(S):
#   get_tx_power_from_os home-ap-24
###############################################################################
get_tx_power_from_os()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
       raise "mr8300_ext_lib_override:get_tx_power_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    iw $wm2_vif_if_name info | grep txpower | awk -F '.' '{print $1}'| awk -F ' ' '{print $2}'
}

###############################################################################
# DESCRIPTION:
#   Function starts wireless driver on a device.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_wireless_driver
###############################################################################
start_wireless_driver()
{
    log "mr8300_ext_lib_override:start_wireless_driver - Starting wireless driver"

    # service wpad restart
    /etc/init.d/wpad restart ||
        raise "FAIL: Could not start wireless manager" -l "mr8300_ext_lib_override:start_wireless_driver" -ds
    sleep 2
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
        raise "mr8300_ext_lib_override:check_neighbor_report_log requires ${NARGS} input argument(s), $# given" -arg
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
            match_pattern_for_log_inspecting="Adding $sm_radio_type .* $sm_survey_type neighbor {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid' .* chan=$sm_channel}"
            ;;
        *parsed_neighbor_bssid*)
            log_msg="Checking for $sm_radio_type neighbor parsing of bssid $sm_neighbor_mac"
            die_msg="No neighbor $sm_neighbor_mac was parsed"
            sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type BSSID $sm_neighbor_mac"
            ;;
        *parsed_neighbor_ssid*)
            log_msg="Checking for $sm_radio_type neighbor parsing of ssid $sm_neighbor_ssid"
            die_msg="No neighbor $sm_neighbor_ssid was parsed"
            sm_log_test_pass_msg="Neighbor $sm_neighbor_ssid was parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type SSID $sm_neighbor_ssid"
            ;;
        *sending_neighbor*)
            log_msg="Checking for $sm_radio_type neighbor sending of $sm_neighbor_mac"
            die_msg="No neighbor $sm_neighbor_mac was added"
            sm_log_test_pass_msg="Neighbor $sm_neighbor_mac was added"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* $sm_survey_type neighbors {bssid='$sm_neighbor_mac' ssid='$sm_neighbor_ssid' .* chan=$sm_channel}"
            ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "mr8300_ext_lib_override:check_neighbor_report_log" -arg
            ;;
    esac

    log "mr8300_ext_lib_override:check_neighbor_report_log - $log_msg"
    wait_for_function_response 0 "$LOGREAD | tail -1000 | grep -i \"$match_pattern_for_log_inspecting\"" &&
        log -deb "mr8300_ext_lib_override:check_neighbor_report_log - $sm_log_test_pass_msg - Success" ||
        raise "FAIL: $die_msg" -l "mr8300_ext_lib_override:check_neighbor_report_log" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks existance of neighbor report messages.
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
    local NARGS=8
    [ $# -ne ${NARGS} ] &&
        raise "mr8300_ext_lib_override:inspect_neighbor_report requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_reporting_interval=$4
    sm_sampling_interval=$5
    sm_report_type=$6
    sm_neighbor_ssid=$7
    sm_neighbor_mac=$(echo "$8")
    log -deb "Not changing the case of the string MAC of the sm_neighbor_mac is $sm_neighbor_mac"

    sm_channel_list="[\"set\",[$sm_channel]]"

    empty_ovsdb_table Wifi_Stats_Config ||
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "mr8300_ext_lib_override:inspect_neighbor_report" -oe

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
        raise "FAIL: Could not empty Wifi_Stats_Config: empty_ovsdb_table" -l "mr8300_ext_lib_override:inspect_neighbor_report" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if beacon interval is applied at system level.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  beacon interval (required)
#   $2  interface name (required)
# RETURNS:
#   0   Beacon interval is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_beacon_interval_at_os_level 600 home-ap-U50
###############################################################################
check_beacon_interval_at_os_level()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "mr8300_ext_lib_override:check_beacon_interval_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    wm2_bcn_int=$1
    wm2_vif_if_name=$2
    local beacon_variable=0

    log "mr8300_ext_lib_override:check_beacon_interval_at_os_level - Checking Beacon Interval at OS - LEVEL2"

    case "$wm2_vif_if_name" in
        "home-ap-24")
            if_name=wlan1
        ;;
        "home-ap-l50")
            if_name=wlan2
        ;;
        "home-ap-u50")
            if_name=wlan0
        ;;
        *)
            raise "FAIL: Incorrect home-ap provided" -l "mr8300_ext_lib_override:check_beacon_interval_at_os_level" -arg
        ;;
    esac

    sleep 10
    beacon_variable=$("hostapd_cli -p /var/run/hostapd-${if_name} -i $wm2_vif_if_name status | grep beacon_int | awk -F '=' '{print $2}'")
    if [ $beacon_variable -eq $wm2_bcn_int ]; then
        log -deb "mr8300_ext_lib_override:check_beacon_interval_at_os_level - Beacon Interval $wm2_bcn_int for $wm2_vif_if_name is set at OS - LEVEL2 - Success"
    else
        raise "FAIL: Beacon interval $wm2_bcn_int for $wm2_vif_if_name is not set at OS - LEVEL2" -l "mr8300_ext_lib_override:check_beacon_interval_at_os_level" -tc
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function connects device to simulated FUT cloud.
#   Procedure:
#       - test if certificate file in provided folder exists
#       - update SSL table with certificate location and file name
#       - remove redirector address so it will not interfere
#       - set inactivity probe to 30s
#       - set manager address to FUT cloud
#       - set target in Manager table (it should be resolved by CM)
#       - wait for cloud state to become ACTIVE
#   Raises an exception on fail.
# INPUT PARAMETER(S):
#   $1  cloud IP (optional, defaults to 192.168.200.1)
#   $2  port (optional, defaults to 443)
#   $3  certificates folder (optional, defaults to $FUT_TOPDIR/utility/files)
#   $4  certificate file (optional, defaults to fut_ca.pem)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   connect_to_fut_cloud
#   connect_to_fut_cloud 192.168.200.1 65000 fut-base/utility/files fut_ca.pem
###############################################################################
connect_to_fut_cloud()
{
    target=${1:-"192.168.200.1"}
    port=${2:-"65000"}
    cert_dir=${3:-"$FUT_TOPDIR/shell/tools/device/files"}
    ca_fname=${4:-"fut_ca.pem"}
    inactivity_probe=30000

    log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - Configure certificates, check if file exists"
    test -f "$cert_dir/$ca_fname" ||
        raise "FAIL: File $cert_dir/$ca_fname not found" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    update_ovsdb_entry SSL -u ca_cert "$cert_dir/$ca_fname"
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - SSL ca_cert set to $cert_dir/$ca_fname - Success" ||
        raise "FAIL: SSL ca_cert not set to $cert_dir/$ca_fname" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    # Remove redirector, to not interfere with the flow
    update_ovsdb_entry AWLAN_Node -u redirector_addr ''
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - AWLAN_Node redirector_addr set to '' - Success" ||
        raise "FAIL: AWLAN_Node::redirector_addr not set to ''" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    # Inactivity probe sets the timing of keepalive packets
    update_ovsdb_entry Manager -u inactivity_probe $inactivity_probe &&
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - Manager inactivity_probe set to $inactivity_probe - Success" ||
        raise "FAIL: Manager::inactivity_probe not set to $inactivity_probe" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    # AWLAN_Node::manager_addr is the controller address, provided by redirector
    update_ovsdb_entry AWLAN_Node -u manager_addr "ssl:$target:$port" &&
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - AWLAN_Node manager_addr set to ssl:$target:$port - Success" ||
        raise "FAIL: AWLAN_Node::manager_addr not set to ssl:$target:$port" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    # CM should ideally fill in Manager::target itself
    update_ovsdb_entry Manager -u target "ssl:$target:$port"
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - Manager target set to ssl:$target:$port - Success" ||
        raise "FAIL: Manager::target not set to ssl:$target:$port" -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds

    log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - Waiting for FUT cloud status to go to ACTIVE"
    wait_cloud_state ACTIVE &&
        log -deb "mr8300_ext_lib_override:connect_to_fut_cloud - Manager::status is set to ACTIVE. Connected to FUT cloud - Success" ||
        raise "FAIL: Manager::status is not ACTIVE. Not connected to FUT cloud." -l "mr8300_ext_lib_override:connect_to_fut_cloud" -ds
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
        raise "mr8300_ext_lib_override:check_leaf_report_log requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    # shellcheck disable=SC2018,SC2019
    sm_client_mac_address=$(echo "$2" | tr a-z A-Z)
    sm_log_type=$3

    case $sm_log_type in
        *connected*)
            log_msg="Checking logs for leaf reporting radio $sm_radio_type connection established"
            die_msg="No client $sm_client_mac_address connected for reporting"
            sm_log_test_pass_msg="Client $sm_client_mac_address connected for reporting"
            match_pattern_for_log_inspecting="Marked $sm_radio_type .* client $sm_client_mac_address connected"
        ;;
        *client_parsing*)
            log_msg="Checking logs for leaf parsing $sm_client_mac_address"
            die_msg="No client $sm_client_mac_address parsed"
            sm_log_test_pass_msg="Client $sm_client_mac_address parsed"
            match_pattern_for_log_inspecting="Parsed $sm_radio_type client MAC $sm_client_mac_address"
        ;;
        *client_update*)
            log_msg="Checking logs for leaf entry update $sm_client_mac_address"
            die_msg="No client $sm_client_mac_address updated"
            sm_log_test_pass_msg="Client $sm_client_mac_address updated"
            match_pattern_for_log_inspecting="Updating $sm_radio_type .* client $sm_client_mac_address entry"
        ;;
        *sending*)
            log_msg="Checking logs for leaf $sm_client_mac_address $sm_radio_type sample sending"
            die_msg="No client $sm_client_mac_address $sm_radio_type sample sending initiated"
            sm_log_test_pass_msg="client $sm_client_mac_address $sm_radio_type sample sending initiated"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* client $sm_client_mac_address stats"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "mr8300_ext_lib_override:check_leaf_report_log" -arg
        ;;
    esac

    log -deb "mr8300_ext_lib_override:check_leaf_report_log - $log_msg"
    wait_for_function_response 0 "$LOGREAD | tail -1000 | grep -i \"$match_pattern_for_log_inspecting\"" &&
        log -deb "mr8300_ext_lib_override:check_leaf_report_log - $sm_log_test_pass_msg - Success" ||
        raise "FAIL: $die_msg" -l "mr8300_ext_lib_override:check_leaf_report_log" -tc

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
        raise "mr8300_ext_lib_override:check_survey_report_log requires ${NARGS} input argument(s), $# given" -arg
    sm_radio_type=$1
    sm_channel=$2
    sm_survey_type=$3
    sm_log_type=$4

    case $sm_log_type in
        *processing_survey*)
            log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting processing survey"
            die_msg="No survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            sm_log_test_pass_msg="Survey processing done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Processing $sm_radio_type .* $sm_survey_type $sm_channel"
        ;;
        *scheduled_scan*)
            log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting scheduling survey"
            die_msg="No survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            sm_log_test_pass_msg="Survey scheduling done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Scheduled $sm_radio_type $sm_survey_type $sm_channel scan"
        ;;
        *fetched_survey*)
            log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting fetched survey"
            die_msg="No survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            sm_log_test_pass_msg="Survey fetching done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Fetched $sm_radio_type $sm_survey_type $sm_channel survey"
        ;;
        *sending_survey_report*)
            log_msg="Checking logs for survey $sm_radio_type channel $sm_channel reporting sending survey"
            die_msg="No survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            sm_log_test_pass_msg="Survey sending done on $sm_radio_type $sm_survey_type on channel $sm_channel"
            match_pattern_for_log_inspecting="Sending $sm_radio_type .* $sm_survey_type $sm_channel survey report"
        ;;
        *)
            raise "FAIL: Incorrect log type provided" -l "mr8300_ext_lib_override:check_survey_report_log" -arg
        ;;
    esac

    log "mr8300_ext_lib_override:check_survey_report_log - $log_msg"
    wait_for_function_response 0 "$LOGREAD | tail -1000 | grep -i \"$match_pattern_for_log_inspecting\"" &&
        log -deb "mr8300_ext_lib_override:check_survey_report_log - $sm_log_test_pass_msg" ||
        raise "FAIL: $die_msg" -l "mr8300_ext_lib_override:check_survey_report_log" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function initializes device for use in FUT.
#   Instead of performing "stop_managers", it only does "stop_openswitch", to
#   avoid removal of certificates in /var/run/openvswitch/
#   It calls a function that instructs CM to prevent the device from rebooting.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   Last exit status.
# USAGE EXAMPLE(S):
#   device_init
###############################################################################
device_init()
{
    # 'stop_managers' is removed, because the certificates are deleted in
    # /var/run/openvswitch/ in combination with managers script
    # stop_managers

    stop_openswitch &&
        log -deb "mr8300_ext_lib_override:device_init - stopped OpenvSwitch - Success" ||
        raise "FAIL: Could not stop OpenvSwitch: stop_openswitch" -l "mr8300_ext_lib_override:device_init" -ds
    cm_disable_fatal_state &&
        log -deb "mr8300_ext_lib_override:device_init - CM fatal state disabled - Success" ||
        raise "FAIL: Could not disable CM fatal state" -l "mr8300_ext_lib_override:device_init" -ds
    return $?
}

restart_managers()
{
    /usr/opensync/bin/restart.sh &&
        log -deb "mr8300_ext_lib_override:restart_managers - Success" ||
        log -err "mr8300_ext_lib_override:restart_managers - Failure"
}

###############################################################################
# DESCRIPTION:
#   Function returns HT mode set at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  vif_if_name (required)
#   $2  channel (not used, but still required, do not optimize)
# RETURNS:
#   0   on successful channel retrieval, fails otherwise
# ECHOES:
#   HT mode from OS in format: HT20, HT40 (examples)
# USAGE EXAMPLE(S):
#   get_ht_mode_from_os home-ap-24 1
###############################################################################
get_ht_mode_from_os()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "mr8300_ext_lib_override:get_ht_mode_from_os - requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2
    local ht_mode_val=0
    local ht_mode_check=0

    ht_mode_val=$(iw $wm2_vif_if_name info | grep  channel | (awk -F ':' '{print $2}'| awk -F 'M' '{print $1}'))
    ht_mode_check="HT$ht_mode_val"
    echo "${ht_mode_check}"
}

###############################################################################
# DESCRIPTION:
#   Function returns channel set at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  vif interface name (required)
# RETURNS:
#   0   on successful channel retrieval, fails otherwise
# ECHOES:
#   Channel from OS
# USAGE EXAMPLE(S):
#   get_channel_from_os home-ap-24
###############################################################################
get_channel_from_os()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "mr8300_ext_lib_override:get_channel_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    iw $wm2_vif_if_name info | grep  channel | awk -F ',' '{print $1}' | awk -F ' ' '{print $2}'
}


###############################################################################
# DESCRIPTION:
#   Function checks for CSA(Channel Switch Announcement) msg on the LEAF device
#   sent by GW on channel change.
# INPUT PARAMETER(S):
#   $1  mac address of GW (string, required)
#   $2  CSA channel GW switches to (int, required)
#   $3  HT mode of the channel (string, required)
# RETURNS:
#   0   CSA message is found in LEAF device var logs, fail otherwise.
# USAGE EXAMPLE(S):
#   check_sta_send_csa_message 1A:2B:3C:4D:5E:6F 6 HT20
###############################################################################
check_sta_send_csa_message()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "mr8300_ext_lib_override:check_sta_send_csa_message requires ${NARGS} input argument(s), $# given" -arg
    gw_vif_mac=$1
    gw_csa_channel=$2
    ht_mode=$3

    # Example log:
    # Tue Jan 19 14:01:07 2021 daemon.notice wpa_supplicant[4900]: bhaul-sta-24: CTRL-EVENT-STARTED-CHANNEL-SWITCH freq=2437 ht_enabled=1 ch_offset=0 ch_width=20 MHz cf1=2437 cf2=0
    wm_csa_log_grep="$LOGREAD | grep -i 'wpa_supplicant' | grep -i 'CTRL-EVENT-STARTED-CHANNEL-SWITCH'"
    wait_for_function_response 0 "${wm_csa_log_grep}" 30 &&
        log "mr8300_ext_lib_override:check_sta_send_csa_message : 'csa completed' message found in logs for channel:${gw_csa_channel} with HT mode: ${ht_mode} - Success" ||
        raise "FAIL: Failed to find 'csa completed' message in logs for channel: ${gw_csa_channel} with HT mode: ${ht_mode}" -l "mr8300_ext_lib_override:check_sta_send_csa_message" -tc
    return 0
}
