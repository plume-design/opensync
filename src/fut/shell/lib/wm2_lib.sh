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
export FUT_WM2_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/wm2_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Wireless Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function starts wireless driver on a device.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   start_wireless_driver
###############################################################################
start_wireless_driver()
{
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:start_wireless_driver" -fc
}

###############################################################################
# DESCRIPTION:
#   Function prepares device for WM tests.  If called with parameters it waits
#   for radio interfaces in Wifi_Radio_State table.
#   Calling it without radio interface names, it skips the step checking the interfaces.
#   Raises exception on fail in any of its steps.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   0   On successful setup.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wm_setup_test_environment
#   wm_setup_test_environment wifi0 wifi1 wifi2
###############################################################################
wm_setup_test_environment()
{
    log -deb "wm2_lib:wm_setup_test_environment - Running WM2 setup"

    device_init &&
        log -deb "wm2_lib:wm_setup_test_environment - Device initialized - Success" ||
        raise "FAIL: device_init - Could not initialize device" -l "wm2_lib:wm_setup_test_environment" -ds

    start_openswitch &&
        log -deb "wm2_lib:wm_setup_test_environment - OpenvSwitch started - Success" ||
        raise "FAIL: start_openswitch - Could not start OpenvSwitch" -l "wm2_lib:wm_setup_test_environment" -ds

    start_wireless_driver &&
        log -deb "wm2_lib:wm_setup_test_environment - Wireless driver started - Success" ||
        raise "FAIL: start_wireless_driver - Could not start wireles driver" -l "wm2_lib:wm_setup_test_environment" -ds

    restart_managers
    log -deb "wm2_lib:wm_setup_test_environment - Executed restart_managers, exit code: $?"

    empty_ovsdb_table AW_Debug &&
        log -deb "wm2_lib:wm_setup_test_environment - AW_Debug table emptied - Success" ||
        raise "FAIL: empty_ovsdb_table AW_Debug - Could not empty AW_Debug table" -l "wm2_lib:wm_setup_test_environment" -ds

    set_manager_log WM TRACE &&
        log -deb "wm2_lib:wm_setup_test_environment - Manager log for WM set to TRACE - Success" ||
        raise "FAIL: set_manager_log WM TRACE - Could not set WM manager log severity" -l "wm2_lib:wm_setup_test_environment" -ds

    vif_clean &&
        log -deb "wm2_lib:wm_setup_test_environment - vif_clean - Success" ||
        raise "FAIL: vif_clean - Could not clean VIFs" -l "wm2_lib:wm_setup_test_environment" -ow

    # Check if radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" &&
            log -deb "wm2_lib:wm_setup_test_environment - Wifi_Radio_State::if_name '$if_name' present - Success" ||
            raise "FAIL: Wifi_Radio_State::if_name for '$if_name' does not exist" -l "wm2_lib:wm_setup_test_environment" -ds
    done

    log -deb "wm2_lib:wm_setup_test_environment - WM setup - end"

    return 0

}
####################### SETUP SECTION - STOP ##################################

####################### VIF SECTION - START ###################################

###############################################################################
# DESCRIPTION:
#   Function empties all VIF interfaces by emptying the Wifi_VIF_Config table.
#   Wait for Wifi_VIF_State table to be empty.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  wait timeout in seconds (int, optional, default=60)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   vif_clean
#   vif_clean 240
###############################################################################
vif_clean()
{
    VIF_CLEAN_TIMEOUT=${1:-60}

    log -deb "wm2_lib:vif_clean - Purging VIFs"

    empty_ovsdb_table Wifi_VIF_Config ||
        raise "FAIL: empty_ovsdb_table - Could not empty table Wifi_VIF_Config" -l "wm2_lib:vif_clean" -oe

    wait_for_empty_ovsdb_table Wifi_VIF_State ${VIF_CLEAN_TIMEOUT} ||
        raise "FAIL: wait_for_empty_ovsdb_table - Could not empty Wifi_VIF_State table" -l "wm2_lib:vif_clean" -ow
}

####################### VIF SECTION - STOP ####################################

###################### RADIO SECTION - START ##################################

###############################################################################
# DESCRIPTION:
#   Function retrieves interface regulatory domain.
# INPUT PARAMETER(S):
#   $1  Physical Radio interface name for which to retrieve regulatory domain
# ECHOES:
#   US
# NOTE:
#   This is a stub function. Provide function for each device in overrides.
#   Function should echo interface regulatory domain
# USAGE EXAMPLE(S):
#   get_iface_regulatory_domain wifi0
###############################################################################
get_iface_regulatory_domain()
{
    log -deb "wm2_lib:get_iface_regulatory_domain - This is a stub function. Override implementation needed for each model. Defaulting to US!"
    echo 'US' && return 0
}

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

    if [ "${check_standard}" == 0 ]; then
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
    wait_for_function_output "cac_completed" "get_radio_channel_state ${state_channel} ${if_name}" ${cac_time} &&
        log -deb "wm2_lib:wait_for_cac_complete - Channel state went to cac_completed. Channel available" ||
        log -err "FAIL: Channel CAC was not completed in given CAC time (${cac_time}s)." -l "wm2_lib:wait_for_cac_complete"

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
}

###############################################################################
# DESCRIPTION:
#   Function configures existing radio interface.
#   After expansion of parameters it checks for mandatory parameter, ti.
#   radio interface name. Prior to configuring the interface it verifies
#   selected channel is allowed for selected radio.
#   Configures radio interface afterwards.
#   Raises an exception if radio interface does not exist, selected channel
#   is not allowed or mandatory parameters are missing.
#   Wait for configuration to reflect to Wifi_Radio_State table.
# INPUT PARAMETER(S):
#   Parameters are fed into function as key-value pairs.
#   Function supports the following keys for parameter values:
#       -if_name, -channel_mode, -fallback_parents, -ht_mode,-hw_mode,
#       -tx_chainmask, -tx_power, -enabled, -country, -channel, -timeout
#   Where mandatory key-value pair is:
#       -if_name <if_name> (string, required)
#   Other parameters are optional. Order of key-value pairs can be random.
#   Refer to USAGE EXAMPLE(S) for details.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   configure_radio_interface -if_name wifi1 -channel 36 -enabled true
###############################################################################
configure_radio_interface()
{
    radio_args=""
    replace="func_arg"
    timeout=""
    disable_cac="false"
    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "${option}" in
            -if_name)
                radio_if_name=${1}
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -channel_mode | \
            -fallback_parents | \
            -ht_mode | \
            -hw_mode | \
            -tx_chainmask | \
            -tx_power | \
            -enabled)
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -country)
                country_arg="${replace} ${option#?} ${1}"
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -channel)
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                channel=$1
                shift
                ;;
            -timeout)
                timeout="-t ${1}"
                shift
                ;;
            -disable_cac)
                disable_cac="true"
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:configure_radio_interface" -arg
                ;;
        esac
    done

    [ -z "${radio_if_name}" ] &&
        raise "FAIL: 'if_name' argument empty" -l "wm2_lib:configure_radio_interface" -arg

    if [ -n "${channel}" ]; then
        # Only check if channel is allowed, not ready for use
        check_is_channel_allowed "$channel" "$radio_if_name" &&
            log -deb "wm2_lib:configure_radio_interface - Channel '$channel' is allowed on '$radio_if_name' - Success" ||
            raise "FAIL: Channel '$channel' is not allowed on '$radio_if_name'" -l "wm2_lib:configure_radio_interface" -ds
    fi

    # Perform action configure Radio
    check_ovsdb_entry Wifi_Radio_Config -w if_name "${radio_if_name}"
    [ $? -eq 0 ] ||
        raise "FAIL: Radio interface does not exists" -l "wm2_lib:configure_radio_interface" -ds

    log -deb "wm2_lib:configure_radio_interface - Configuring radio interface '${radio_if_name}'"
    func_params=${radio_args//${replace}/-u}
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$radio_if_name" $func_params &&
        log -deb "wm2_lib:configure_radio_interface - update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params - Success" ||
        raise "FAIL: Could not update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params" -l "wm2_lib:configure_radio_interface" -oe

    # Validate action configure Radio
    func_params=${radio_args//${replace}/-is}
    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$radio_if_name" $func_params ${timeout} &&
        log -deb "wm2_lib:configure_radio_interface - wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params - Success" ||
        raise "FAIL: wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params" -l "wm2_lib:configure_radio_interface" -ow

    if [ "${disable_cac}" == "false" ]; then
        # Even if the channel is set in Wifi_Radio_State, it is not
        # necessarily available for immediate use if CAC is in progress.
        validate_cac "${wm2_if_name}" &&
            log "wm2_lib:configure_radio_interface - CAC time elapsed or not needed" ||
            raise "FAIL: CAC failed. Channel is not usable" -l "wm2_lib:configure_radio_interface" -ds
    else
        log -deb "wm2_lib:configure_radio_interface - CAC explicitly disabled"
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates VIF interface.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   ...
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
# Backhaul configuration on root node:
#   create_vif_interface \
#   -radio_if_name wifi1 \
#   -if_name bhaul-ap-l50 \
#   -mac_list '["set",["aa:bb:cc:dd:ee:ff"]]' \
#   -mac_list_type whitelist \
#   -mode ap \
#   -security '["map",[["encryption","WPA-PSK"],["key","PSK"],["mode","2"]]]' \
#   -ssid bhaul_ssid \
#   -ssid_broadcast "disabled" \
#   -vif_radio_idx 1 \
#   -enabled true
# Backhaul configuration on leaf node:
#   create_vif_interface \
#   -if_name bhaul-sta-l50 \
#   -security '["map",[["encryption","WPA-PSK"],["key","PSK"],["mode","2"]]]' \
#   -ssid bhaul_ssid
###############################################################################
create_vif_interface()
{
    vif_args_c=""
    vif_args_w=""
    replace="func_arg"

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "${option}" in
            -radio_if_name)
                radio_if_name=${1}
                shift
                ;;
            -if_name)
                vif_if_name=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -ap_bridge | \
            -bridge | \
            -dynamic_beacon | \
            -mac_list_type | \
            -mac_list | \
            -parent | \
            -ssid_broadcast | \
            -vif_radio_idx | \
            -vlan_id | \
            -wpa_oftags)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -wpa | \
            -wpa_key_mgmt | \
            -wpa_psks)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -enabled)
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -ssid)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -mode)
                wm2_mode=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -security)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -credential_configs)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:create_vif_interface" -arg
                ;;
        esac
    done

    [ "$wm2_mode" = "sta" ] &&
        remove_sta_connections "$vif_if_name"

    [ -z "${vif_if_name}" ] &&
        raise "FAIL: Interface name argument empty" -l "wm2_lib:create_vif_interface" -arg

    # Check if entry for if_name already exists in Wifi_VIF_Config table
    # Update if entry exists, insert otherwise
    check_ovsdb_entry Wifi_VIF_Config -w if_name "${vif_if_name}"
    if [ $? -eq 0 ]; then
        log -deb "wm2_lib:create_vif_interface - Updating existing VIF entry"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        log -deb "wm2_lib:create_vif_interface - Creating VIF entry"
        function_to_call="insert_ovsdb_entry"
        function_arg="-i"
    fi

    # Perform action update/insert VIF
    func_params=${vif_args_c//$replace/$function_arg}
    # shellcheck disable=SC2086
    eval $function_to_call Wifi_VIF_Config -w if_name "$vif_if_name" $func_params &&
        log -deb "wm2_lib:create_vif_interface - $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params - Success" ||
        raise "FAIL: $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" -l "wm2_lib:create_vif_interface" -oe

    # Mutate radio entry with VIF uuid
    if [ "${function_to_call}" == "insert_ovsdb_entry" ]; then
        vif_uuid=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$vif_if_name" ) ||
            raise "FAIL: get_ovsdb_entry_value" -l "wm2_lib:create_vif_interface" -oe
        ${OVSH} u Wifi_Radio_Config -w if_name=="${radio_if_name}" vif_configs:ins:'["set",[["uuid","'${vif_uuid//" "/}'"]]]'
    fi

    # Validate action insert/update VIF
    func_params=${vif_args_w//$replace/-is}
    # shellcheck disable=SC2086
    eval wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" $func_params &&
        log -deb "wm2_lib:create_vif_interface - wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params - Success" ||
        raise "FAIL: wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" -l "wm2_lib:create_vif_interface" -ow

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function resets VIF STA interface.
#     This function reset Wifi_VIF_Config table entry for specific STA interface to default values
#
#   Raises exception on fail.
# INPUT PARAMETER(S):
#     - if_name: Wifi_VIF_Config::if_name
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   reset_sta_interface bhaul-sta-l50
###############################################################################
reset_sta_interface()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:reset_sta_interface requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    if_name=${1}
    log -deb "wm2_lib:reset_sta_interface - Resetting STA ${if_name} interface"
    update_ovsdb_entry Wifi_VIF_Config -w if_name "${if_name}" -w mode "sta" \
        -u credential_configs "[\"set\",[]]" \
        -u mac_list "[\"set\",[]]" \
        -u mac_list_type "[\"set\",[]]" \
        -u parent "[\"set\",[]]" \
        -u security "[\"map\",[]]" \
        -u ssid "" \
        -u ssid_broadcast "[\"set\",[]]" &&
            log -deb "wm2_lib:reset_sta_interface - STA VIF-s reset"
    check_ovsdb_table_field_exists Wifi_VIF_Config "wpa"
    if [ "${?}" == "0" ]; then
        log -deb "wm2_lib:reset_sta_interface - Checking and resetting wpa, wpa_key_mgmt, wpa_oftags, wpa_psks for STA VIF-s"
        update_ovsdb_entry Wifi_VIF_Config -w if_name "${if_name}" -w mode "sta" \
            -u wpa "[\"set\",[]]" \
            -u wpa_key_mgmt "[\"set\",[]]" \
            -u wpa_oftags "[\"map\",[]]" \
            -u wpa_psks "[\"map\",[]]" &&
                log -deb "wm2_lib:reset_sta_interface - wpa, wpa_key_mgmt, wpa_oftags, wpa_psks are reset for STA VIF-s"
    else
        log -err "wm2_lib:reset_sta_interface - WPA not implemented for this OS implementation"
        print_tables Wifi_VIF_Config
    fi
    return 0
}
###############################################################################
# DESCRIPTION:
#   Function configures VIF STA interface.
#     This function only configures STA interface based on if_name, ssid and security field, if other parameters are passed
#     it will update those parameters as well but for correct configuring of STA interface, user should pass only if_name, ssid and security
#
#     Function will first try and configure STA interface using Wifi_Credentials_Config and if STA association fails, it
#     will use Wifi_VIF_Config::security field instead
#
#   Raises exception on fail.
# INPUT PARAMETER(S):
#     - if_name: Wifi_VIF_Config::if_name
#     - parent: Wifi_VIF_Config::parent
#     - wpa_oftags: Wifi_VIF_Config::wpa_oftags
#     - wpa_key_mgmt: Wifi_VIF_Config::wpa_key_mgmt
#     - wpa: Wifi_VIF_Config::wpa
#     - wpa_psks: Wifi_VIF_Config::wpa_psks
#     - channel: Used to validate correct channel is set in Wifi_VIF_State::channel after association of STA interface
#     - ssid: Wifi_VIF_Config::ssid
#     - security: Wifi_VIF_Config::security
#     - onboard_type: Wifi_Credentials_Config::onboard_type
#     - clear_wcc: If set to true, function will remove ALL entries in Wifi_Credentials_Config table
#     - wait_ip: If set to true, function will wait for inet_addr in the Wifi_Inet_State table based on if_name to be populated
#     - use_security: If set to true, function will use Wifi_VIF_Config::security field instead of creating and using Wifi_Credentials_Config
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
# Backhaul configuration on leaf node:
#   configure_sta_interface \
#   -if_name bhaul-sta-l50 \
#   -security '["map",[["encryption","WPA-PSK"],["key","PSK"],["mode","2"]]]' \
#   -ssid bhaul_ssid
###############################################################################
configure_sta_interface()
{
    vif_args_c=""
    vif_args_w=""
    security_args_w=""
    wcc_args_c=""
    replace="func_arg"
    clear_wcc="false"
    wait_ip="false"
    is_wpa="false"
    use_security="false"
    call_arguments="$@"
    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "${option}" in
            -if_name)
                vif_if_name=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -parent)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -wpa_oftags)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -wpa_key_mgmt | \
            -wpa)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                is_wpa="true"
                shift
                ;;
            -wpa_psks)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -channel)
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -ssid)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                wcc_args_c="${wcc_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                ssid="${1}"
                shift
                ;;
            -security)
                security_args_c="${security_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                wcc_args_c="${wcc_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                security_args_w="${security_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                security="${1}"
                shift
                ;;
            -onboard_type)
                wcc_args_c="${wcc_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -clear_wcc)
                clear_wcc="true"
                ;;
            -wait_ip)
                wait_ip="true"
                ;;
            -use_security)
                use_security="true"
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:configure_sta_interface" -arg
                ;;
        esac
    done

    [ -z "${vif_if_name}" ] &&
        raise "FAIL: Interface name argument empty" -l "wm2_lib:configure_sta_interface" -arg
    [ -z "${ssid}" ] &&
        raise "FAIL: SSID name argument empty" -l "wm2_lib:configure_sta_interface" -arg
    [ -z "${security}" ] &&
        raise "FAIL: SSID name argument empty" -l "wm2_lib:configure_sta_interface" -arg

    if [ "${clear_wcc}" == "true" ]; then
        log -deb "wm2_lib:configure_sta_interface - Clearing Wifi_Credential_Config table"
        empty_ovsdb_table Wifi_Credential_Config &&
            log -deb "wm2_lib:configure_sta_interface - Wifi_Credential_Config table is cleared" ||
            raise "FAIL: empty_ovsdb_table Wifi_Credential_Config" -l "wm2_lib:configure_sta_interface" -oe
    fi

    if [ "${is_wpa}" == "false" ] && [ -n "${wcc_args_c}" ] && [ "${use_security}" == "false" ]; then
        func_params=${wcc_args_c//$replace/"-i"}
        eval insert_ovsdb_entry Wifi_Credential_Config $func_params &&
            log -deb "wm2_lib:configure_sta_interface - insert_ovsdb_entry Wifi_Credential_Config $func_params - Success" ||
            raise "FAIL: insert_ovsdb_entry Wifi_Credential_Config $func_params" -l "wm2_lib:configure_sta_interface" -oe
        func_params=${wcc_args_c//$replace/"-w"}
        # Issue with different busybox version causes syntax error if we check for security field in where statement
        wcc_uuid=$(get_ovsdb_entry_value Wifi_Credential_Config _uuid -w ssid "${ssid}") &&
            log -deb "wm2_lib:configure_sta_interface - Wifi_Credential_Config uuid is ${wcc_uuid}" ||
            raise "FAIL: get_ovsdb_entry_value Wifi_Credential_Config _uuid $func_params" -l "wm2_lib:configure_sta_interface" -oe
        vif_args_c="${vif_args_c} -m :ins: ${replace} credential_configs '[\"set\",[[\"uuid\",\"${wcc_uuid}\"]]]'"
    elif [ "${is_wpa}" == "true" ]; then
        log -deb "wm2_lib:configure_sta_interface - WPA is used, will not set security field nor Wifi_Credential_Config"
    else
        log -err "wm2_lib:configure_sta_interface - Wifi_Credential_Config is not used. Will use security field instead"
        vif_args_c="${vif_args_c} ${security_args_c}"
        # Do not wait for security field in Wifi_VIF_State due to mode not reflecting in all cases.
        # parent reflection should be enough to validate that the LEAF associated
        # vif_args_w="${vif_args_w} ${security_args_w}"
    fi

    # Check if entry for if_name already exists in Wifi_VIF_Config table
    # Update if entry exists, insert otherwise
    check_ovsdb_entry Wifi_VIF_Config -w if_name "${vif_if_name}"
    if [ $? -eq 0 ]; then
        log -deb "wm2_lib:configure_sta_interface - Updating existing VIF entry"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        raise "FAIL: STA VIF entry does not exist" -l "wm2_lib:configure_sta_interface" -ds
    fi

    # Perform action update/insert VIF
    func_params=${vif_args_c//$replace/$function_arg}
    # shellcheck disable=SC2086
    eval $function_to_call Wifi_VIF_Config -w if_name "$vif_if_name" $func_params &&
        log -deb "wm2_lib:configure_sta_interface - $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params - Success" ||
        raise "FAIL: $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" -l "wm2_lib:configure_sta_interface" -oe

    wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_VIF_State parent -w if_name $vif_if_name" &&
        parent_bssid=0 ||
        parent_bssid=1
    if [ "$parent_bssid" -eq 0 ]; then
        parent_bssid=$(get_ovsdb_entry_value Wifi_VIF_State parent -w if_name "$vif_if_name")
        update_ovsdb_entry Wifi_VIF_Config -w if_name "$vif_if_name" \
            -u parent "$parent_bssid" &&
                log -deb "wm2_lib:configure_sta_interface - VIF_State parent was associated" ||
                raise "FAIL: Failed to update Wifi_VIF_Config with parent MAC address" -l "wm2_lib:configure_sta_interface" -ds
    else
        # If security was already used and VIF did not associate, raise exception
        if [ "${use_security}" == "false" ];then
            log -err "wm2_lib:configure_sta_interface - Failed to associate with GW using Wifi_Credentials. Will re-try using security field instead"
            configure_sta_interface $call_arguments -use_security &&
                log -deb "wm2_lib:configure_sta_interface: STA VIF entry successfully configured" ||
                raise "FAIL: VIF_State parent was not associated - use_security ${use_security}" -l "wm2_lib:configure_sta_interface" -ds
        else
            raise "FAIL: VIF_State parent was not associated - use_security ${use_security}" -l "wm2_lib:configure_sta_interface" -ds
        fi
    fi

    # Validate action insert/update VIF
    func_params=${vif_args_w//$replace/-is}
    # shellcheck disable=SC2086
    eval wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" $func_params &&
        log -deb "wm2_lib:configure_sta_interface - wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params - Success" ||
        raise "FAIL: wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" -l "wm2_lib:configure_sta_interface" -ow

    if [ "${wait_ip}" == "true" ]; then
        log -deb "wm2_lib:configure_sta_interface - Waiting for ${vif_if_name} Wifi_Inet_State address"
        wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_Inet_State inet_addr -w if_name ${vif_if_name}"
        wait_ovsdb_entry Wifi_Inet_State -w if_name "${vif_if_name}" -is_not inet_addr "0.0.0.0" &&
            log -deb "wm2_lib:configure_sta_interface - ${vif_if_name} inet_addr in Wifi_Inet_State is $(get_ovsdb_entry_value Wifi_Inet_State inet_addr -w if_name $vif_if_name)" ||
            raise "FAIL: ${vif_if_name} inet_addr in Wifi_Inet_State is empty" -l "wm2_lib:configure_sta_interface" -oe
    fi
    log -deb "wm2_lib:configure_sta_interface: STA VIF entry successfully configured"
    return 0
}
###############################################################################
# DESCRIPTION:
#   Function creates and configures VIF interface and makes sure required
#   radio interface is created and configured as well.
#   After expansion of parameters it checks for mandatory parameters.
#   Makes sure selected channel is allowed.
#   Configures radio interface.
#   Creates (or makes an update if VIF interface entry already exists) and
#   configures VIF interface.
#   Makes sure all relevant Config tables are reflected to State tables.
# NOTE:
#   This function does not verify that the channel is ready for immediate
#   use, only that the channel was set, which means that DFS channels are
#   likely performing CAC, and a timeout and check needs to be done in
#   the calling function. See function: check_is_channel_ready_for_use()
# INPUT PARAMETER(S):
#   Parameters are fed into function as key-value pairs.
#   Function supports the following keys for parameter values:
#       -if_name, -vif_if_name, -vif_radio_idx, -channel
#       -channel_mode, -ht_mode, -hw_mode, -country, -enabled, -mode,
#       -ssid, -ssid_broadcast, -security, -parent, -mac_list, -mac_list_type,
#       -tx_chainmask, -tx_power, -fallback_parents,
#       -ap_bridge, -ap_bridge, -dynamic_beacon, -vlan_id,
#       -wpa, -wpa_key_mgmt, -wpa_psks, -wpa_oftags
#   Where mandatory key-value pairs are:
#       -if_name <if_name> (string, required)
#       -vif_if_name <vif_if_name> (string, required)
#       -channel <channel> (integer, required)
#   Other parameters are optional. Order of key-value pairs can be random.
#   Optional parameter pair:
#       -timeout <timeout_seconds>: how long to wait for channel change. If
#        empty, use default ovsh wait time.
#   Refer to USAGE EXAMPLE(S) for details.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   create_radio_vif_interface -vif_radio_idx 2 \
#       -if_name wifi2 \
#       -vif_if_name home-ap-u50 \
#       -channel 165 \
#       -channel_mode manual \
#       -ssid FutTestSSID \
#       -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]' \
#       -enabled true
#       -ht_mode HT20 \
#       -hw_mode 11ac \
#       -mode ap \
#       -country US
###############################################################################
create_radio_vif_interface()
{
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    channel_change_timeout=""
    disable_cac="false"
    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -ht_mode)
                radio_ht_mode="$replace ${option#?} ${1}"
                shift
                ;;
            -channel_mode | \
            -hw_mode | \
            -fallback_parents | \
            -tx_power | \
            -tx_chainmask)
                radio_args="$radio_args $replace ${option#?} ${1}"
                shift
                ;;
            -default_oftag | \
            -dpp_cc | \
            -wpa_oftags)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -vif_radio_idx | \
            -ssid_broadcast | \
            -parent | \
            -mac_list_type | \
            -dynamic_beacon | \
            -bridge | \
            -vlan_id | \
            -radius_srv_secret | \
            -radius_srv_addr | \
            -wpa | \
            -wpa_key_mgmt | \
            -wpa_psks)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -mac_list)
                vif_args_c="${vif_args_c} ${replace} mac_list $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} mac_list $(single_quote_arg "$1")"
                shift
                ;;
            -credential_configs)
                vif_args_c="${vif_args_c} ${replace} credential_configs $(single_quote_arg "$1")"
                shift
                ;;
            -ssid)
                vif_args_c="${vif_args_c} ${replace} ssid $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ssid $(single_quote_arg "$1")"
                shift
                ;;
            -ap_bridge)
                vif_args_c="$vif_args_c $replace ap_bridge $1"
                vif_args_w="$vif_args_w $replace ap_bridge $1"
                shift
                ;;
            -security)
                vif_args_c="$vif_args_c $replace security $(single_quote_arg "$1")"
                vif_args_w="$vif_args_w $replace security $(single_quote_arg "$1")"
                shift
                ;;
            -mode)
                vif_args_c="$vif_args_c $replace mode $1"
                vif_args_w="$vif_args_w $replace mode $1"
                wm2_mode=$1
                shift
                ;;
            -enabled)
                radio_args="$radio_args $replace enabled $1"
                vif_args_c="$vif_args_c $replace enabled $1"
                vif_args_w="$vif_args_w $replace enabled $1"
                shift
                ;;
            -country)
                radio_args="$radio_args $replace country $1"
                country_arg="$replace country $1"
                shift
                ;;
            -channel)
                radio_args="$radio_args $replace channel $1"
                vif_args_w="$vif_args_w $replace channel $1"
                channel=$1
                shift
                ;;
            -if_name)
                radio_args="$radio_args $replace if_name $1"
                wm2_if_name=$1
                shift
                ;;
            -vif_if_name)
                vif_args_c="$vif_args_c $replace if_name $1"
                vif_args_w="$vif_args_w $replace if_name $1"
                wm2_vif_if_name=$1
                shift
                ;;
            -timeout)
                channel_change_timeout="-t ${1}"
                shift
                ;;
            -disable_cac)
                disable_cac="true"
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:create_radio_vif_interface" -arg
                ;;
        esac
    done

    # Mandatory parameters
    [ -z "${wm2_if_name}" ] &&
        raise "FAIL: 'if_name' argument empty" -l "wm2_lib:create_radio_vif_interface" -arg
    [ -z "${wm2_vif_if_name}" ] &&
        raise "FAIL: 'vif_if_name' argument empty" -l "wm2_lib:create_radio_vif_interface" -arg
    [ -z "${channel}" ] &&
        raise "FAIL: 'channel' argument empty" -l "wm2_lib:create_radio_vif_interface" -arg

    # Only check if channel is allowed, need not be ready for immediate use
    check_is_channel_allowed "$channel" "$wm2_if_name" &&
        log -deb "wm2_lib:create_radio_vif_interface - Channel '$channel' is allowed on interface '$wm2_if_name'" ||
        raise "FAIL: Channel '$channel' is not allowed on interface '$wm2_if_name'" -l "wm2_lib:create_radio_vif_interface" -ds

    log -deb "wm2_lib:create_radio_vif_interface - Bringing up radio/VIF interface"

    func_params="${radio_args//$replace/-u} ${radio_ht_mode//$replace/-u}"
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params &&
        log -deb "wm2_lib:create_radio_vif_interface - Table Wifi_Radio_Config updated - Success" ||
        raise "FAIL: Could not update Wifi_Radio_Config table" -l "wm2_lib:create_radio_vif_interface" -tc

    if [ "$wm2_mode" = "sta" ]; then
        remove_sta_connections "$wm2_vif_if_name"
    fi

    function_to_call="insert_ovsdb_entry"
    function_arg="-i"

    ${OVSH} s Wifi_VIF_Config -w if_name=="$wm2_vif_if_name" &&
        update=0 ||
        update=1
    if [ "$update" -eq 0 ]; then
        log -deb "wm2_lib:create_radio_vif_interface - VIF entry exists, updating Wifi_VIF_Config instead of inserting"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    fi

    func_params=${vif_args_c//$replace/$function_arg}
    # shellcheck disable=SC2086
    eval $function_to_call Wifi_VIF_Config -w if_name "$wm2_vif_if_name" $func_params &&
        log -deb "wm2_lib:create_radio_vif_interface - $function_to_call Wifi_VIF_Config - Success" ||
        raise "FAIL: Could not $function_to_call to Wifi_VIF_Config" -l "wm2_lib:create_radio_vif_interface" -fc

    # Associate VIF and radio interfaces
    wm2_uuids=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$wm2_vif_if_name") ||
        raise "FAIL: Could not get _uuid for '$wm2_vif_if_name' from Wifi_VIF_Config: get_ovsdb_entry_value" -l "wm2_lib:create_radio_vif_interface" -oe

    wm2_vif_configs_set="[\"set\",[[\"uuid\",\"$wm2_uuids\"]]]"

    func_params=${radio_args//$replace/-u}
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params \
        -u vif_configs "$wm2_vif_configs_set" &&
            log -deb "wm2_lib:create_radio_vif_interface - Table Wifi_Radio_Config updated - Success" ||
            raise "FAIL: Could not update table Wifi_Radio_Config" -l "wm2_lib:create_radio_vif_interface" -oe

    # shellcheck disable=SC2086
    func_params=${vif_args_w//$replace/-is}
    eval wait_ovsdb_entry Wifi_VIF_State -w if_name "$wm2_vif_if_name" $func_params ${channel_change_timeout} &&
        log -deb "wm2_lib:create_radio_vif_interface - Wifi_VIF_Config reflected to Wifi_VIF_State - Success" ||
        raise "FAIL: Could not reflect Wifi_VIF_Config to Wifi_VIF_State" -l "wm2_lib:create_radio_vif_interface" -ow

    if [ "$wm2_mode" = "sta" ]; then
        wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_VIF_State parent -w if_name $wm2_vif_if_name" &&
            parent_mac=0 ||
            parent_mac=1
        if [ "$parent_mac" -eq 0 ]; then
            parent_mac=$(get_ovsdb_entry_value Wifi_VIF_State parent -w if_name "$wm2_vif_if_name")
            update_ovsdb_entry Wifi_VIF_Config -w if_name "$wm2_vif_if_name" \
                -u parent "$parent_mac" &&
                    log -deb "wm2_lib:create_radio_vif_interface - VIF_State parent was associated" ||
                    log -deb "wm2_lib:create_radio_vif_interface - VIF_State parent was not associated"
        fi
    fi

    if [ -n "$country_arg" ]; then
        radio_args=${radio_args//$country_arg/""}
    fi

    func_params="${radio_args//$replace/-is} ${radio_ht_mode//$replace/-is}"

    if [ "$wm2_mode" = "sta" ]; then
        func_params="${radio_args//$replace/-is}"
    fi

    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" $func_params ${channel_change_timeout} &&
        log -deb "wm2_lib:create_radio_vif_interface - Wifi_Radio_Config reflected to Wifi_Radio_State - Success" ||
        raise "FAIL: Could not reflect Wifi_Radio_Config to Wifi_Radio_State" -l "wm2_lib:create_radio_vif_interface" -ow

    if [ "${disable_cac}" == "false" ]; then
        # Even if the channel is set in Wifi_Radio_State, it is not
        # necessarily available for immediate use if CAC is in progress.
        validate_cac "${wm2_if_name}" &&
            log "wm2_lib:create_radio_vif_interface - CAC time elapsed or not needed" ||
            raise "FAIL: CAC failed. Channel is not usable" -l "wm2_lib:create_radio_vif_interface" -ds
    else
        log -deb "wm2_lib:create_radio_vif_interface - CAC explicitly disabled"
    fi

    log -deb "wm2_lib:create_radio_vif_interface - Wireless interface created"
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function deletes VIF interface and removes interface entry for selected
#   VIF interface from Wifi_VIF_Config, and waits for entry to be removed from
#   Wifi_VIF_State.
# INPUT PARAMETER(S):
#   Parameters are fed into function as key-value pairs.
#   Function supports the following keys for parameter values:
#       -if_name, -vif_if_name
#   Where mandatory key-value pairs are:
#       -if_name <if_name> (string, required)
#       -vif_if_name <vif_if_name> (string, required)
#   Other parameters are optional. Order of key-value pairs can be random.
#   Refer to USAGE EXAMPLE(S) for details.
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_vif_interface -if_name wifi2 \
#       -vif_if_name home-ap-u50 \
###############################################################################
remove_vif_interface()
{
    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -if_name)
                wm2_if_name=$1
                shift
                ;;
            -vif_if_name)
                wm2_vif_if_name=$1
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:remove_vif_interface" -arg
                ;;
        esac
    done

    [ -z "${wm2_if_name}" ] &&
        raise "FAIL: 'if_name' argument empty" -l "wm2_lib:remove_vif_interface" -arg
    [ -z "${wm2_vif_if_name}" ] &&
        raise "FAIL: 'vif_if_name' argument empty" -l "wm2_lib:remove_vif_interface" -arg

    log -deb "wm2_lib:remove_vif_interface - Removing VIF interface"

    # shellcheck disable=SC2086
    remove_ovsdb_entry Wifi_VIF_Config -w if_name "$wm2_vif_if_name" &&
        log -deb "wm2_lib:remove_vif_interface - Entry '$wm2_vif_if_name' removed from table Wifi_VIF_Config - Success" ||
        raise "FAIL: Could not remove entry '$wm2_vif_if_name' from table Wifi_VIF_Config" -l "wm2_lib:remove_vif_interface" -fc
    # shellcheck disable=SC2086
    wait_ovsdb_entry_remove Wifi_VIF_State -w if_name "$wm2_vif_if_name" &&
        log -deb "wm2_lib:remove_vif_interface - Wifi_VIF_Config reflected to Wifi_VIF_State for '$wm2_vif_if_name' - Success" ||
        raise "FAIL: Could not reflect Wifi_VIF_Config to Wifi_VIF_State for '$wm2_vif_if_name'" -l "wm2_lib:remove_vif_interface" -ow

    log -deb "wm2_lib:remove_vif_interface - Wireless interface deleted from Wifi_VIF_State"

    return 0
}
###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
check_radio_vif_state()
{
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    retval=0

    log -deb "wm2_lib:check_radio_vif_state - Checking if interface $if_name is up"
    get_vif_interface_is_up "$if_name"
    if [ "$?" -eq 0 ]; then
        log -deb "wm2_lib:check_radio_vif_state - Interface '$if_name' is up"
    else
        log -deb "wm2_lib:check_radio_vif_state - Interface '$if_name' is not up"
        return 1
    fi

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -if_name)
                radio_args="$radio_args $replace if_name $1"
                shift
                ;;
            -vif_if_name)
                vif_args="$vif_args $replace if_name $1"
                wm2_vif_if_name=$1
                shift
                ;;
            -vif_radio_idx)
                vif_args="$vif_args $replace vif_radio_idx $1"
                shift
                ;;
            -ssid)
                vif_args="$vif_args $replace ssid $(single_quote_arg "$1")"
                shift
                ;;
            -channel)
                radio_args="$radio_args $replace channel $1"
                vif_args="$vif_args $replace channel $1"
                shift
                ;;
            -ht_mode)
                radio_args="$radio_args $replace ht_mode $1"
                shift
                ;;
            -hw_mode)
                radio_args="$radio_args $replace hw_mode $1"
                shift
                ;;
            -mode)
                vif_args="$vif_args $replace mode $1"
                shift
                ;;
            -security)
                vif_args="$vif_args $replace security $(single_quote_arg "$1")"
                shift
                ;;
            -country)
                radio_args="$radio_args $replace country $1"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "wm2_lib:check_radio_vif_state" -arg
                ;;
        esac
    done

    func_params=${radio_args//$replace/-w}
    # shellcheck disable=SC2086
    check_ovsdb_entry Wifi_Radio_State $func_params
    if [ $? -eq 0 ]; then
        log -deb "wm2_lib:check_radio_vif_state - Wifi_Radio_State is valid for given configuration"
    else
        log -deb "wm2_lib:check_radio_vif_state - Entry with required radio arguments in Wifi_Radio_State does not exist"
        retval=1
    fi

    func_params=${vif_args//$replace/-w}
    eval check_ovsdb_entry Wifi_VIF_State $func_params
    if [ $? -eq 0 ]; then
        log -deb "wm2_lib:check_radio_vif_state - Wifi_VIF_State is valid for given configuration"
    else
        log -deb "wm2_lib:check_radio_vif_state - Entry with required VIF arguments in Wifi_VIF_State does not exist"
        retval=1
    fi

    return $retval
}

###############################################################################
# DESCRIPTION:
#   Function checks if channel is applied at OS - LEVEL2.
#   Raises exception if actual channel does not match expected value
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  VIF interface name (string, required)
# RETURNS:
#   0  if actual channel matches expected value
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_channel_at_os_level 1 home-ap-24
###############################################################################
check_channel_at_os_level()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_channel_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_channel=$1
    # shellcheck disable=SC2034
    wm2_vif_if_name=$2

    log -deb "wm2_lib:check_channel_at_os_level - Checking channel '$wm2_channel' at OS - LEVEL2"
    wait_for_function_output $wm2_channel "get_channel_from_os $wm2_vif_if_name" &&
        log -deb "wm2_lib:check_channel_at_os_level - channel '$wm2_channel' is set at OS - LEVEL2 - Success" ||
        raise "FAIL: channel '$wm2_channel' is not set at OS - LEVEL2" -l "wm2_lib:check_channel_at_os_level" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns channel set at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  VIF interface name (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_channel_from_os home-ap-24
###############################################################################
get_channel_from_os()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:get_channel_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    log -deb "wm2_lib:get_channel_from_os - Getting channel from OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:get_channel_from_os" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if HT mode for interface on selected channel is
#   applied at OS - LEVEL2.
#   Raises exception if actual HT mode does not match expected value
# INPUT PARAMETER(S):
#   $1  HT mode (string, required)
#   $2  VIF interface name (string, required)
#   $3  Channel (int, required)
# RETURNS:
#   0  if actual HT mode matches expected value
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_ht_mode_at_os_level HT40 home-ap-24 2
#   check_ht_mode_at_os_level HT20 home-ap-50 36
###############################################################################
check_ht_mode_at_os_level()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_ht_mode_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_ht_mode=$1
    wm2_vif_if_name=$2
    wm2_channel=$3

    log -deb "wm2_lib:check_ht_mode_at_os_level - Checking HT mode for channel '$wm2_channel' at OS - LEVEL2"
    wait_for_function_output "$wm2_ht_mode" "get_ht_mode_from_os $wm2_vif_if_name $wm2_channel" &&
        log -deb "wm2_lib:check_ht_mode_at_os_level - HT Mode '$wm2_ht_mode' set at OS - LEVEL2 - Success" ||
        raise "FAIL: HT Mode '$wm2_ht_mode' is not set at OS - LEVEL2" -l "wm2_lib:check_ht_mode_at_os_level" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if Beacon interval is applied at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  Beacon interval (int, required)
#   $2  VIF interface name (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_beacon_interval_at_os_level 600 home-ap-U50
###############################################################################
check_beacon_interval_at_os_level()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_beacon_interval_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_bcn_int=$1
    wm2_vif_if_name=$2

    log -deb "wm2_lib:check_beacon_interval_at_os_level - Checking Beacon interval for interface '$wm2_vif_if_name' at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:check_beacon_interval_at_os_level" -fc
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
#   0   On success.
# USAGE EXAMPLE(S):
###############################################################################
check_radio_mimo_config()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_radio_mimo_config requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_chainmask_max_value=$1
    wm2_if_name=$2

    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" \
        -u tx_chainmask 0 ||
            raise "update_ovsdb_entry" -l "wm2_lib:check_radio_mimo_config" -tc

    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" \
        -is tx_chainmask "$wm2_tx_chainmask_max_value" &&
            log -deb "wm2_lib:check_radio_mimo_config - Max Tx Chainmask value is '$wm2_tx_chainmask_max_value'" ||
            raise "FAIL: '$wm2_tx_chainmask_max_value' is not valid for this radio MIMO" -l "wm2_lib:check_radio_mimo_config" -tc

    mimo=$(get_ovsdb_entry_value Wifi_Radio_State tx_chainmask -w if_name "$wm2_if_name")
    case "$mimo" in
        3)
            log -deb "wm2_lib:check_radio_mimo_config - Radio MIMO config is 2x2"
            ;;
        7)
            log -deb "wm2_lib:check_radio_mimo_config - Radio MIMO config is 3x3"
            ;;
        15)
            log -deb "wm2_lib:check_radio_mimo_config - Radio MIMO config is 4x4"
            ;;
        *)
            raise "FAIL: Wrong mimo provided: $mimo" -l "wm2_lib:check_radio_mimo_config" -arg
            ;;
    esac

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if Tx Chainmask is applied at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  Tx Chainmask (int, required)
#   $2  Interface name (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_tx_chainmask_at_os_level 3 home-ap-U50
###############################################################################
check_tx_chainmask_at_os_level()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_tx_chainmask_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_tx_chainmask=$1
    wm2_if_name=$2

    log -deb "wm2_lib:check_tx_chainmask_at_os_level - Checking Tx Chainmask for interface '$wm2_if_name' at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:check_tx_chainmask_at_os_level" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if Tx Power is applied at OS - LEVEL2.
#   Raises exception if actual Tx Power does not match expected value
# INPUT PARAMETER(S):
#   $1  Tx Power (int, required)
#   $2  VIF interface name (string, required)
#   $3  Radio interface name (string, required)
# RETURNS:
#   0  if actual Tx Power matches expected value
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_tx_power_at_os_level 21 home-ap-24 wifi0
#   check_tx_power_at_os_level 14 wl0.2 wl0
###############################################################################
check_tx_power_at_os_level()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_tx_power_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_tx_power=$1
    # shellcheck disable=SC2034
    wm2_vif_if_name=$2
    # shellcheck disable=SC2034
    wm2_radio_if_name=$3

    log -deb "wm2_lib:check_tx_power_at_os_level - Checking Tx Power for interface '$wm2_radio_if_name' at OS - LEVEL2"
    wait_for_function_output $wm2_tx_power "get_tx_power_from_os $wm2_vif_if_name" &&
        log -deb "wm2_lib:check_tx_power_at_os_level - Tx Power '$wm2_tx_power' is set at OS - LEVEL2 - Success" ||
        raise "FAIL: Tx Power '$wm2_tx_power' is not set at OS - LEVEL2" -l "wm2_lib:check_tx_power_at_os_level" -tc
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns Tx Power set at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  VIF interface name (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_tx_power_from_os home-ap-24
###############################################################################
get_tx_power_from_os()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:get_tx_power_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    log -deb "wm2_lib:check_ht_mode_at_os_level - Getting Tx Power for interface '$wm2_vif_if_name' at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:get_tx_power_from_os" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if country is applied at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  Country (string, required)
#   $2  Interface name (string, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_country_at_os_level US wifi0
###############################################################################
check_country_at_os_level()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_country_at_os_level requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_country=$1
    wm2_if_name=$2

    log -deb "wm2_lib:check_country_at_os_level - Checking 'country' for '$wm2_if_name' at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:check_country_at_os_level" -fc
}

###############################################################################
# DESCRIPTION:
#   Function returns HT mode set at OS - LEVEL2.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  vif_if_name (string, required)
#   $2  channel (int, not used, but still required, do not optimize)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_ht_mode_from_os home-ap-24 1
###############################################################################
get_ht_mode_from_os()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:get_ht_mode_from_os requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2

    log -deb "wm2_lib:check_ht_mode_at_os_level - Getting HT mode for channel '$wm2_channel' at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:get_ht_mode_from_os" -fc
}

###############################################################################
# DESCRIPTION:
#   Function echoes the radio channel state description in channels field of
#   table Wifi_Radio_State.
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  Radio interface name (string, required)
# RETURNS:
#   0   A valid channel state was echoed to stdout.
#   1   Channel is not allowed or state is not recognized.
# ECHOES:
#   (on return 0):
#   "allowed"       : non-dfs channel
#   "nop_finished"  : dfs channel, requires cac finished before usable
#   "cac_completed" : dfs channel, cac completed, usable
#   "nop_started"   : dfs channel, radar was detected and it must not be used
# USAGE EXAMPLE(S):
#   ch_state=$(get_radio_channel_state 2 wifi0)
###############################################################################
get_radio_channel_state()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:get_radio_channel_state requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_radio_if_name=$2

    # Ensure channel is allowed.
    # Redirect output to ensure clean echo to stdout
    check_is_channel_allowed "$wm2_channel" "$wm2_radio_if_name" >/dev/null 2>&1 ||
        return 1

    state_raw=$(get_ovsdb_entry_value Wifi_Radio_State channels -w if_name "$wm2_radio_if_name" -r | tr ']' '\n' | grep "$wm2_channel")
    state="$(echo "${state_raw##*state}" | tr -d ' \":}')"
    if [ "$state" == "allowed" ]; then
        echo "allowed"
    elif [ "$state" == "nop_finished" ]; then
        echo "nop_finished"
    elif [ "$state" == "cac_completed" ]; then
        echo "cac_completed"
    elif [ "$state" == "nop_started" ]; then
        echo "nop_started"
    else
        # Undocumented state, return 1
        echo "${state_raw##*state}" | tr -d '\":}'
        return 1
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if requested channel is available on selected radio interface.
#   It does not check if the channel is available for immediate use.
#   Raises exception if Wifi_Radio_State::allowed_channels is not populated or
#   if selected channel is not found in the list of allowed channels.
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  Radio interface name (string, required)
# RETURNS:
#   0   Channel is available on selected radio interface.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_is_channel_allowed 2 wifi0
#   check_is_channel_allowed 144 wifi2
###############################################################################
check_is_channel_allowed()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_is_channel_allowed requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_radio_if_name=$2

    log -deb "wm2_lib:check_is_channel_allowed - Waiting for Wifi_Radio_State::allowed_channels to be populated"
    wait_for_function_response 'notempty' "get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name ${wm2_radio_if_name}" &&
        log -deb "wm2_lib:check_is_channel_allowed - Wifi_Radio_State::allowed_channels populated - Success" ||
        raise "FAIL: Wifi_Radio_State::allowed_channels not populated" -l "wm2_lib:check_is_channel_allowed" -ds

    log -deb "wm2_lib:check_is_channel_allowed - Checking if channel '$wm2_channel' is allowed for '$wm2_radio_if_name'"
    allowed_channels=$(get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name "$wm2_radio_if_name" -r)
    if [ -z "${allowed_channels}" ]; then
        ${OVSH} s Wifi_Radio_State
        raise "FAIL: Wifi_Radio_State::allowed_channels for '$wm2_radio_if_name' is empty" -l "wm2_lib:check_is_channel_allowed" -ds
    fi
    log -deb "wm2_lib:check_is_channel_allowed - allowed_channels: ${allowed_channels}"
    contains_element "${wm2_channel}" $(echo ${allowed_channels} | sed 's/\[/ /g; s/\]/ /g; s/,/ /g;') &&
        log -deb "wm2_lib:check_is_channel_allowed - Channel '$wm2_channel' is allowed on radio '$wm2_radio_if_name' - Success" ||
        raise "FAIL: Wifi_Radio_State::allowed_channels for '$wm2_radio_if_name' does not contain '$wm2_channel'" -l "wm2_lib:check_is_channel_allowed" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if a radar event was detected on the requested channel for
#   the requested radio interface. Radar event is determined by channel state.
#   If NOP is started for selected channel then it is assumed the radar event
#   occured inside NOP (Non Occupancy Period) for selected channel.
#   Raises exception if NOP not finished, assuming radar was detected within NOP.
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  Radio interface name (string, required)
# RETURNS:
#   0   No radar detected on channel.
#   1   Radar detected on channel.
# USAGE EXAMPLE(S):
#   check_radar_event_on_channel 2 wifi0
###############################################################################
check_radar_event_on_channel()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_radar_event_on_channel requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_radio_if_name=$2

    check_is_channel_allowed "$wm2_channel" "$wm2_radio_if_name" ||
        raise "FAIL: Channel '$wm2_channel' is not allowed on radio '$wm2_radio_if_name'" -l "wm2_lib:check_radar_event_on_channel" -ds

    log -deb "wm2_lib:check_radar_event_on_channel - Checking radar events on channel '$wm2_channel'"
    if [ "$(get_radio_channel_state "$wm2_channel" "$wm2_radio_if_name")" == "nop_started" ]; then
        raise "FAIL: Radar event detected on channel '$wm2_channel'" -f "wm2_lib:check_radar_event_on_channel" -ds
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if requested channel is ready to use on radio interface.
#   Even if the channel changes in Wifi_Radio_State table, this might mean that
#   the channel was DFS and is currently undergoing CAC. The channel is actually
#   ready for use, only once the state is equal to "allowed" or "cac_completed".
# INPUT PARAMETER(S):
#   $1  Channel (required)
#   $2  Radio interface name (required)
# RETURNS:
#   0   Channel is ready use.
#   1   Channel is not ready to use.
# USAGE EXAMPLE(S):
#   check_is_channel_ready_for_use 2 wifi0
###############################################################################
check_is_channel_ready_for_use()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_is_channel_ready_for_use requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2
    is_empty=false

    log -deb "wm2_lib:check_is_channel_ready_for_use - Checking if channel '$wm2_channel' is ready for immediate use"
    wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_Radio_State channels -w if_name $wm2_radio_if_name" || is_empty=true

    if [ "$is_empty" = "true" ]; then
        log -deb "wm2_lib:check_is_channel_ready_for_use - Table Wifi_Radio_State dump"
        ${OVSH} s Wifi_Radio_State || true
        raise "FAIL: Wifi_Radio_State::channels is empty for '$wm2_radio_if_name'" -l "wm2_lib:check_is_channel_ready_for_use" -ds
    fi

    check_is_channel_allowed "$wm2_channel" "$wm2_if_name" &&
        log -deb "wm2_lib:check_is_channel_ready_for_use - channel $wm2_channel is allowed on radio $wm2_radio_if_name" ||
        raise "FAIL: Channel $wm2_channel is not allowed on radio $wm2_radio_if_name" -l "wm2_lib:check_is_channel_ready_for_use" -ds

    state="$(get_radio_channel_state "$wm2_channel" "$wm2_radio_if_name")"
    if [ "$state" == "cac_completed" ] || [ "$state" == "allowed" ]; then
        log -deb "wm2_lib:check_is_channel_ready_for_use - Channel '$wm2_channel' is ready for use - $state"
        return 0
    fi

    log -deb "wm2_lib:check_is_channel_ready_for_use - Channel '$wm2_channel' is not ready for use: $state"
    return 1
}

###############################################################################
# DESCRIPTION:
#   Function checks if CAC (Channel Availability Check) on selected channel and
#   interface is started.
#   State is established by inspecting the Wifi_Radio_State table.
#   Raises exception if CAC is not started.
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  Interface name (string, required)
# RETURNS:
#   0   CAC started for channel.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_is_cac_started 120 wifi2
#   check_is_cac_started 100 wifi2
###############################################################################
check_is_cac_started()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_is_cac_started requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2

    log -deb "wm2_lib:check_is_cac_started - Checking if CAC is started on channel $wm2_channel"
    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -F '["'$wm2_channel'","{\"state\": \"cac_started\"}"]'; then
        log -deb "wm2_lib:check_is_cac_started - CAC started on channel '$wm2_channel'"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "FAIL: CAC is not started on channel '$wm2_channel'" -l "wm2_lib:check_is_cac_started" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks if NOP (No Occupancy Period) on selected channel and
#   interface is finished, indicating the period of 30 minutes since weather
#   radar signal on channel in question has passed, making channel immediately
#   usable.
#   State is established by inspecting the Wifi_Radio_State table.
#   Raises exception if NOP not finished.
# INPUT PARAMETER(S):
#   $1  Channel (int, required)
#   $2  Interface name (string, required)
# RETURNS:
#   0   NOP finished for channel or channel is already allowed.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_is_nop_finished 120 wifi2
###############################################################################
check_is_nop_finished()
{
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_is_nop_finished requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2

    log -deb "wm2_lib:check_is_nop_finished - Checking if NOP finished on channel '$wm2_channel'"
    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -F '["'$wm2_channel'","{\"state\": \"nop_finished\"}"]'; then
        log -deb "wm2_lib:check_is_nop_finished - NOP finished on channel '$wm2_channel'"
        return 0
    elif
        ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -F '["'$wm2_channel'","{\"state\":\"allowed\"}"]'; then
        log -deb "wm2_lib:check_is_nop_finished - Channel '$wm2_channel' is allowed"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "FAIL: NOP is not finished on channel '$wm2_channel'" -l "wm2_lib:check_is_nop_finished" -tc
}

###############################################################################
# DESCRIPTION:
#   Function simulates DFS (Dynamic Frequency Shift) radar event on interface.
#   This function always raises an exception, it is a stub function and needs
#   a function with the same name and usage in platform or device overrides.
# INPUT PARAMETER(S):
#   $1  channel (int, required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   simulate_dfs_radar <IF_NAME>
###############################################################################
simulate_dfs_radar()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:simulate_dfs_radar requires ${NARGS} input argument(s), $# given" -arg
    wm2_if_name=$1

    log -deb "wm2_lib:simulate_dfs_radar - Triggering DFS radar event on '${wm2_if_name}'"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:simulate_dfs_radar" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks for CSA(Channel Switch Announcement) msg on the LEAF device
#   sent by GW on channel change.
# INPUT PARAMETER(S):
#   $1  MAC address of GW (string, required)
#   $2  CSA channel GW switches to (int, required)
#   $3  HT mode of the channel (string, required)
# RETURNS:
#   None.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   check_sta_send_csa_message 1A:2B:3C:4D:5E:6F 6 HT20
###############################################################################
check_sta_send_csa_message()
{
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:check_sta_send_csa_message requires ${NARGS} input argument(s), $# given" -arg
    gw_vif_mac=$1
    gw_csa_channel=$2
    ht_mode=$3

    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "wm2_lib:check_sta_send_csa_message" -fc
}
###################### RADIO SECTION - STOP ###################################

###################### STATION SECTION - START ################################

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
remove_sta_connections()
{
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "wm2_lib:remove_sta_connections requires ${NARGS} input argument(s), $# given" -arg
    wm2_sta_if_name=$1

    log -deb "[DEPRECATED] - Function wm2_lib:remove_sta_connections is deprecated in favor of remove_sta_interfaces_exclude"
    log -deb "wm2_lib:remove_sta_connections - Removing STA connections except $wm2_sta_if_name"
    ${OVSH} d Wifi_VIF_Config -w if_name!="$wm2_sta_if_name" -w mode==sta &&
        log -deb "wm2_lib:remove_sta_connections - STA connections except '$wm2_sta_if_name' removed - Success" ||
        raise "FAIL: Could not remove STA connections" -l "wm2_lib:remove_sta_connections" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function removes all STA interfaces, except explicitly provided one.
#   Waits timeout time for interfaces to be removed.
#   Waits for system to react, or timeouts with error.
#   Raises an exception if interfaces are not removed.
# INPUT PARAMETER(S):
#   $1  Wait timeout in seconds (int, optional, default=DEFAULT_WAIT_TIME)
#   $2  STA interface name, interface to keep from removing (string, optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_sta_interfaces_exclude 60
###############################################################################
remove_sta_interfaces_exclude()
{
    local wait_timeout=${1:-$DEFAULT_WAIT_TIME}
    local wm2_sta_if_name=$2
    local retval=1

    if [ -n "${wm2_sta_if_name}" ]; then
        log -deb "wm2_lib:remove_sta_interfaces_exclude - Removing STA interfaces except '${wm2_sta_if_name}'"
        ovs_cmd="-w mode==sta -w if_name!=${wm2_sta_if_name}"
    else
        log -deb "wm2_lib:remove_sta_interfaces_exclude - Removing all STA interfaces"
        ovs_cmd="-w mode==sta"
    fi

    # shellcheck disable=SC2086
    ${OVSH} d Wifi_VIF_Config ${ovs_cmd} &&
        log -deb "wm2_lib:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_Config - Success" ||
        raise "FAIL: Could not remove STA interfaces from Wifi_VIF_Config" -l "wm2_lib:remove_sta_interfaces_exclude" -oe

    # Verifying Wifi_VIF_Config reflected to Wifi_VIF_State
    wait_time=0
    while [ $wait_time -le "$wait_timeout" ]; do
        wait_time=$((wait_time+1))
        log -deb "wm2_lib:remove_sta_interfaces_exclude - Waiting Wifi_VIF_Config is reflected to Wifi_VIF_State, retry: $wait_time"
        # shellcheck disable=SC2086
        table_select=$(${OVSH} s Wifi_VIF_State ${ovs_cmd}) || true
        if [ -z "$table_select" ]; then
            retval=0
            break
        fi
        sleep 1
    done

    if [ $retval = 0 ]; then
        log -deb "wm2_lib:remove_sta_interfaces_exclude - Removed STA interfaces from Wifi_VIF_State table - Success"
        return $retval
    else
        raise "FAIL: Could not remove STA interfaces from Wifi_VIF_State table" -l "wm2_lib:remove_sta_interfaces_exclude" -oe
    fi
}

############################################ STATION SECTION - STOP ####################################################
