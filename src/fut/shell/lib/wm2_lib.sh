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
source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
source "${LIB_OVERRIDE_FILE}"

############################################ INFORMATION SECTION - START ###############################################
#
#   Base library of common Wireless Manager functions
#
############################################ INFORMATION SECTION - STOP ################################################


############################################ SETUP SECTION - START #####################################################

start_qca_hostapd()
{
    fn_name="wm2_lib:start_qca_hostapd"
    log -deb "$fn_name - Starting qca-hostapd"
    /etc/init.d/qca-hostapd boot
    sleep 2
}

start_qca_wpa_supplicant()
{
    fn_name="wm2_lib:start_qca_wpa_supplicant"
    log -deb "$fn_name - Starting qca-wpa-supplicant"
    /etc/init.d/qca-wpa-supplicant boot
    sleep 2
}

start_wireless_driver()
{
    fn_name="wm2_lib:start_wireless_driver"
    start_qca_hostapd ||
        raise "start_qca_hostapd" -l "$fn_name" -ds
    start_qca_wpa_supplicant ||
        raise "start_qca_wpa_supplicant" -l "$fn_name" -ds
}

wm_setup_test_environment()
{
    fn_name="wm2_lib:wm_setup_test_environment"
    log -deb "$fn_name - WM2 SETUP"

    device_init ||
        raise "device_init" -l "$fn_name" -ds

    start_openswitch ||
        raise "start_openswitch" -l "$fn_name" -ds

    start_wireless_driver ||
        raise "start_wireless_driver" -l "$fn_name" -ds

    start_specific_manager wm ||
        raise "start_specific_manager wm" -l "$fn_name" -ds

    empty_ovsdb_table AW_Debug ||
        raise "empty_ovsdb_table AW_Debug" -l "$fn_name" -ds

    set_manager_log WM TRACE ||
        raise "set_manager_log WM TRACE" -l "$fn_name" -ds

    vif_clean ||
        raise "vif_clean" -l "$fn_name" -ow

    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" ||
            raise "wait_ovsdb_entry" -l "$fn_name" -tc
    done

}
############################################ SETUP SECTION - STOP ######################################################

############################################ VIF SECTION - START #######################################################

# Clear all existing VIF interfaces
vif_clean()
{
    fn_name="wm2_lib:vif_clean"
    VIF_CLEAN_TIMEOUT=60
    log -deb "$fn_name - Purging VIF"

    empty_ovsdb_table Wifi_VIF_Config ||
        raise "empty_ovsdb_table" -l "$fn_name" -oe

    wait_for_empty_ovsdb_table Wifi_VIF_State ${VIF_CLEAN_TIMEOUT} ||
        raise "wait_for_empty_ovsdb_table" -l "$fn_name" -ow
}

############################################ VIF SECTION - STOP #######################################################


############################################ RADIO SECTION - START #####################################################

configure_radio_interface()
{
    radio_args=""
    replace="func_arg"
    fn_name="wm2_lib:configure_radio_interface"
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
                channel_arg="${replace} ${option#?} ${1}"
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                check_is_channel_ready_for_use "${1}" "$radio_if_name" &&
                    log "$fn_name - Channel ${1} is ready for use on $radio_if_name" ||
                    raise "Channel ${1} is not ready for use on $radio_if_name" -l "$fn_name" -ds
                shift
                ;;
        esac
    done

    # Perform action configure Radio
    [ -z ${radio_if_name} ] && raise "Radio interface name argument empty" -l "${fn_name}" -arg
    check_ovsdb_entry Wifi_Radio_Config -w if_name "${radio_if_name}"
    [ $? -eq 0 ] || raise "Radio interface does not exits" -l "${fn_name}" -ds
    log -deb "$fn_name - Configuring radio interface"
    func_params=${radio_args//${replace}/-u}
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$radio_if_name" $func_params &&
        log -deb "$fn_name - Success update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params" ||
        raise "Failure update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params" -l "$fn_name" -oe

    # Do not check channel, as we do not know if VAPS exist on radio
    func_params=${radio_args//$channel_arg/""}
    # WAR: country does not show up in state, remove from verification args
    func_params=${func_params//$country_arg/""}  # WAR: remove asap
    # Validate action configure Radio
    func_params=${func_params//$replace/-is}
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$radio_if_name" $func_params &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params" ||
        raise "Failure wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params" -l "$fn_name" -ow
}

create_vif_interface()
{
    vif_args_c=""
    vif_args_w=""
    replace="func_arg"
    fn_name="wm2_lib:create_vif_interface"
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
                vif_args_c="${vif_args_c} ${replace} ${option#?} "${1}
                vif_args_w="${vif_args_w} ${replace} ${option#?} "${1}
                shift
                ;;
            -ap_bridge | \
            -bridge | \
            -dynamic_beacon | \
            -mac_list_type | \
            -mac_list | \
            -parent | \
            -ssid_broadcast | \
            -ssid | \
            -vif_radio_idx | \
            -vlan_id | \
            -enabled)
                vif_args_c="${vif_args_c} ${replace} ${option#?} "${1}
                vif_args_w="${vif_args_w} ${replace} ${option#?} "${1}
                shift
                ;;
            -mode)
                wm2_mode=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} "${1}
                vif_args_w="${vif_args_w} ${replace} ${option#?} "${1}
                shift
                ;;
            -security)
                vif_args_c="${vif_args_c} ${replace} ${option#?} "${1}
                vif_args_w="${vif_args_w} -is_not ${option#?} [\"map\",[]]"
                shift
                ;;
            -credential_configs)
                vif_args_c="${vif_args_c} ${replace} ${option#?} "${1}
                shift
                ;;
            -channel)
                vif_args_w="${vif_args_w} ${replace} ${option#?} "${1}
                check_is_channel_ready_for_use "${1}" "$radio_if_name" &&
                    log "$fn_name - Channel ${1} is ready for use on $radio_if_name" ||
                    raise "Channel ${1} is not ready for use on $radio_if_name" -l "$fn_name" -ds
                shift
                ;;
        esac
    done

    [ "$wm2_mode" = "sta" ] && remove_sta_connections "$vif_if_name"

    [ -z ${vif_if_name} ] && raise "Interface name argument empty" -l "${fn_name}" -arg
    check_ovsdb_entry Wifi_VIF_Config -w if_name "${vif_if_name}"
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Updating existing VIF entry"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        log -deb "$fn_name - Creating VIF entry"
        function_to_call="insert_ovsdb_entry2"
        function_arg="-i"
    fi

    # Perform action insert/update VIF
    func_params=${vif_args_c//$replace/$function_arg}
    $function_to_call Wifi_VIF_Config -w if_name "$vif_if_name" $func_params &&
        log -deb "$fn_name - Success $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" ||
        raise "Failure Success $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" -l "$fn_name" -oe

    # Mutate radio entry with VIF uuid
    if [ "${function_to_call}" == "insert_ovsdb_entry2" ]; then
        vif_uuid=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$vif_if_name" ) ||
            raise "get_ovsdb_entry_value" -l "$fn_name" -oe
        ${OVSH} u Wifi_Radio_Config -w if_name==${radio_if_name} vif_configs:ins:'["set",[["uuid","'${vif_uuid//" "/}'"]]]'
    fi

    # Validate action insert/update VIF
    func_params=${vif_args_w//$replace/-is}
    wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" $func_params &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" ||
        raise "Failure wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" -l "$fn_name" -ow
}

check_sta_associated()
{
    local fn_name="wm2_lib:check_sta_associated"
    local vif_if_name=${1}
    local fnc_retry_count=${2:-"5"}
    local fnc_retry_sleep=${3:-"3"}
    log -deb "$fn_name - Checking if $vif_if_name is associated"
    # Makes sense if VIF mode==sta, search for "parent" field
    fnc_str="get_ovsdb_entry_value Wifi_VIF_State parent -w if_name $vif_if_name -raw"
    wait_for_function_output "notempty" "${fnc_str}" ${fnc_retry_count} ${fnc_retry_sleep}
    if [ $? -eq 0 ]; then
        parent_mac="$($fnc_str)"
        if false; then  # do not retrofit Wifi_VIF_Config with parent_mac
            update_ovsdb_entry Wifi_VIF_Config -w if_name "$vif_if_name" -u parent "$parent_mac"
        fi
        log -deb "$fn_name - VIF ${vif_if_name} associated to parent MAC ${parent_mac}"
        return 0
    else
        log -deb "$fn_name - VIF ${vif_if_name} not associated, no parent MAC"
        return 1
    fi
}

create_radio_vif_interface()
{
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    fn_name="wm2_lib:create_radio_vif_interface"
    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
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
            -vif_radio_idx)
                vif_args_c="$vif_args_c $replace vif_radio_idx $1"
                vif_args_w="$vif_args_w $replace vif_radio_idx $1"
                shift
                ;;
            -channel)
                radio_args="$radio_args $replace channel $1"
                vif_args_w="$vif_args_w $replace channel $1"
                log "wm2/$(basename "$0"): Checking if channel $1 is ready for use?"
                check_is_channel_ready_for_use "$1" "$wm2_if_name" &&
                    log "wm2/$(basename "$0"): check_is_channel_ready_for_use - Channel $1 is ready for use"
                shift
                ;;
            -channel_mode)
                radio_args="$radio_args $replace channel_mode $1"
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
            -country)
                radio_args="$radio_args $replace country $1"
                country_arg="$replace country $1"
                shift
                ;;
            -enabled)
                radio_args="$radio_args $replace enabled $1"
                vif_args_c="$vif_args_c $replace enabled $1"
                vif_args_w="$vif_args_w $replace enabled $1"
                shift
                ;;
            -mode)
                vif_args_c="$vif_args_c $replace mode $1"
                vif_args_w="$vif_args_w $replace mode $1"
                wm2_mode=$1
                shift
                ;;
            -ssid)
                vif_args_c="$vif_args_c $replace ssid $1"
                vif_args_w="$vif_args_w $replace ssid $1"
                shift
                ;;
            -ssid_broadcast)
                vif_args_c="$vif_args_c $replace ssid_broadcast $1"
                vif_args_w="$vif_args_w $replace ssid_broadcast $1"
                shift
                ;;
            -security)
                vif_args_c="$vif_args_c $replace security $1"
                vif_args_w="$vif_args_w -is_not security [\"map\",[]]"
                shift
                ;;
            -parent)
                vif_args_c="$vif_args_c $replace parent $1"
                vif_args_w="$vif_args_w $replace parent $1"
                shift
                ;;
            -mac_list)
                vif_args_c="$vif_args_c $replace mac_list $1"
                vif_args_w="$vif_args_w $replace mac_list $1"
                shift
                ;;
            -mac_list_type)
                vif_args_c="$vif_args_c $replace mac_list_type $1"
                vif_args_w="$vif_args_w $replace mac_list_type $1"
                shift
                ;;
            -tx_chainmask)
                radio_args="$radio_args $replace tx_chainmask $1"
                shift
                ;;
            -tx_power)
                radio_args="$radio_args $replace tx_power $1"
                shift
                ;;
            -fallback_parents)
                radio_args="$radio_args $replace fallback_parents $1"
                shift
                ;;
            -ap_bridge)
                vif_args_c="$vif_args_c $replace ap_bridge $1"
                vif_args_w="$vif_args_w $replace if_name $1"
                shift
                ;;
            -bridge)
                vif_args_c="$vif_args_c $replace bridge $1"
                vif_args_w="$vif_args_w $replace bridge $1"
                shift
                ;;
            -dynamic_beacon)
                vif_args_c="$vif_args_c $replace dynamic_beacon $1"
                vif_args_w="$vif_args_w $replace dynamic_beacon $1"
                shift
                ;;
            -vlan_id)
                vif_args_c="$vif_args_c $replace vlan_id $1"
                vif_args_w="$vif_args_w $replace vlan_id $1"
                shift
                ;;
        esac
    done

    log -deb "$fn_name - Bringing up radio/vif interface"

    func_params=${radio_args//$replace/-u}
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params &&
       log -deb "$fn_name - Table Wifi_Radio_Config updated" ||
       raise "Wifi_Radio_Config -> update" -l "$fn_name" -tc

    if [ "$wm2_mode" = "sta" ]; then
        remove_sta_connections "$wm2_vif_if_name"
    fi

    function_to_call="insert_ovsdb_entry"
    function_arg="-i"

    ${OVSH} s Wifi_VIF_Config -w if_name=="$wm2_vif_if_name" && update=0 || update=1
    if [ "$update" -eq 0 ]; then
        log -deb "$fn_name - VIF entry exists, updating instead"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    fi

    func_params=${vif_args_c//$replace/$function_arg}
    $function_to_call Wifi_VIF_Config -w if_name "$wm2_vif_if_name" $func_params &&
            log -deb "$fn_name - $function_to_call Wifi_VIF_Config" ||
            raise "Failed $function_to_call" -l "$fn_name" -fc

    wm2_uuids=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$wm2_vif_if_name") ||
        raise "get_ovsdb_entry_value" -l "$fn_name" -oe

    wm2_vif_configs_set="[\"set\",[[\"uuid\",\"$wm2_uuids\"]]]"

    func_params=${radio_args//$replace/-u}
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params \
        -u vif_configs "$wm2_vif_configs_set" &&
            log -deb "$fn_name - Table Wifi_Radio_Config updated" ||
            raise "Wifi_Radio_Config -> update" -l "$fn_name" -oe

    func_params=${vif_args_w//$replace/-is}
    wait_ovsdb_entry Wifi_VIF_State -w if_name "$wm2_vif_if_name" $func_params &&
        log -deb "$fn_name - Wifi_Radio/VIF_Config reflected to Wifi_VIF_State" ||
        raise "Wifi_Radio_Config -> Wifi_VIF_State" -l "$fn_name" -ow

    if [ -n "$country_arg" ]; then
        radio_args=${radio_args//$country_arg/""}
    fi

    func_params=${radio_args//$replace/-is}
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" $func_params &&
        log -deb "$fn_name - Wifi_Radio_Config reflected to Wifi_Radio_State" ||
        raise "Wifi_Radio_Config -> Wifi_Radio_State" -l "$fn_name" -ow

    if [ "$wm2_mode" = "sta" ]; then
        wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_VIF_State parent -w if_name $wm2_vif_if_name" && parent_mac=0 || parent_mac=1
        if [ "$parent_mac" -eq 0 ]; then
            parent_mac=$(get_ovsdb_entry_value Wifi_VIF_State parent -w if_name "$wm2_vif_if_name")
            update_ovsdb_entry Wifi_VIF_Config -w if_name "$wm2_vif_if_name" \
                -u parent "$parent_mac" &&
                    log -deb "$fn_name - VIF_State parent was associated" ||
                    log -deb "$fn_name - VIF_State parent was not associated"
        fi
    fi

    log -deb "$fn_name - Wireless interface created"
}

check_radio_vif_state()
{
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    fn_name="wm2_lib:check_radio_vif_state"

    interface_is_up "$if_name"

    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Interface $if_name is up"
    else
        log -deb "$fn_name - Interface $if_name is not up"
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
                vif_args="$vif_args $replace ssid $1"
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
        esac
    done

    func_params=${radio_args//$replace/-w}

    check_ovsdb_entry Wifi_Radio_State $func_params &&
        log -deb "$fn_name - Wifi_Radio_State is valid for given configuration" ||
        (
            log -deb "$fn_name - VIF_State does not exist" &&
            return 1
        )

    func_params=${vif_args//$replace/-w}
    check_ovsdb_entry Wifi_VIF_State $func_params &&
        log -deb "$fn_name - Wifi_Radio_State is valid for given configuration" ||
        (
            log -deb "$fn_name - VIF_State does not exist" &&
            return 1
        )
}

check_channel_type()
{
    wm2_channel=$1
    wm2_if_name=$2
    wm2_channel_type=$3
    fn_name="wm2_lib:check_channel_type"

    log -deb "$fn_name - Checking if $wm2_channel is valid"
    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF "\"$wm2_channel\""; then
        log -deb "$fn_name - Channel $wm2_channel is VALID"
        log -deb "$fn_name - Checking if channel $wm2_channel is DFS or NON DFS channel"
        if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'$wm2_channel'","{\"state\":\"allowed\"}"]'; then
            if [ "$wm2_channel_type" = "NON_DFS" ]; then
                log -deb "$fn_name - Channel $wm2_channel is NON DFS - PROCEEDING to next step"
                return 0
            fi
            log -deb "$fn_name - Channel $wm2_channel is NON DFS - $wm2_channel_type expected - EXITING"
            return 1
        else
            if [ $wm2_channel_type = "DFS" ]; then
                log -deb "$fn_name - Channel $wm2_channel is DFS - PROCEEDING to next step"
                return 0
            fi
            log -deb "$fn_name - Channel $wm2_channel is DFS - $wm2_channel_type expected - EXITING"
            return 1
        fi
    else
        raise "Channel $wm2_channel NOT $wm2_channel_type" -l "$fn_name" -tc
    fi
}

check_channel_at_os_level()
{
    wm2_channel=$1
    wm2_vif_if_name=$2
    fn_name="wm2_lib:check_channel_at_os_level"

    log -deb "$fn_name - Checking channel at OS"

    wait_for_function_response 0 "iwlist $wm2_vif_if_name channel | grep -F \"Current\" | grep -qF \"(Channel $wm2_channel)\""

    if [ $? = 0 ]; then
        log -deb "$fn_name - Channel is set to $wm2_channel at OS level"
        return 0
    fi

    raise "Channel is NOT set to $wm2_channel" -l "$fn_name" -tc
}

check_ht_mode_at_os_level()
{
    wm2_ht_mode=$1
    wm2_vif_if_name=$2
    fn_name="wm2_lib:check_ht_mode_at_os_level"

    log -deb "$fn_name - Checking HT MODE at OS level"

    wait_for_function_response 0 "iwpriv $wm2_vif_if_name get_mode | grep -qF $wm2_ht_mode"

    if [ $? = 0 ]; then
        log -deb "$fn_name - HT MODE: $wm2_ht_mode is SET at OS level"
        return 0
    else
        raise "HT MODE: $wm2_ht_mode is NOT set at OS" -l "$fn_name" -tc
    fi

}

check_beacon_interval_at_os_level()
{
    wm2_bcn_int=$1
    wm2_vif_if_name=$2
    fn_name="wm2_lib:check_beacon_interval_at_os_level"

    log -deb "$fn_name - Checking BEACON INTERVAL at OS level"

    wait_for_function_response 0 "iwpriv $wm2_vif_if_name get_bintval | grep -qF get_bintval:$wm2_bcn_int"
    if [ $? = 0 ]; then
        log -deb "$fn_name - BEACON INTERVAL: $wm2_bcn_int is SET at OS level"
        return 0
    else
        raise "BEACON INTERVAL: $wm2_bcn_int is NOT set at OS" -l "$fn_name" -tc
    fi

}

check_radio_mimo_config()
{
    wm2_tx_chainmask_max_value=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_radio_mimo_config"

    update_ovsdb_entry Wifi_Radio_Config -w if_name $wm2_if_name \
        -u tx_chainmask 0 ||
            raise "update_ovsdb_entry" -l "$fn_name" -tc

    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" \
        -is tx_chainmask "$wm2_tx_chainmask_max_value" &&
            log -deb "$fn_name - Max TX_CHAINMASK value is $wm2_tx_chainmask_max_value" ||
            raise "$wm2_tx_chainmask_max_value is not valid for this radio MIMO" -l "$fn_name" -tc

    mimo=$(get_ovsdb_entry_value Wifi_Radio_State tx_chainmask -w if_name "$wm2_if_name")

    case "$mimo" in
    3)
        log -deb "$fn_name - Radio MIMO config is 2x2"
        ;;
    7)
        log -deb "$fn_name - Radio MIMO config is 3x3"
        ;;
    15)
        log -deb "$fn_name - Radio MIMO config is 4x4"
        ;;
    esac
}

check_tx_chainmask_at_os_level()
{
    wm2_tx_chainmask=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_tx_chainmask_at_os_level"

    log -deb "$fn_name - Checking TX CHAINMASK at OS level"

    wait_for_function_response 0 "iwpriv $wm2_if_name get_txchainsoft | grep -qF get_txchainsoft:$wm2_tx_chainmask"
    if [ $? = 0 ]; then
        log -deb "$fn_name - TX CHAINMASK: $wm2_tx_chainmask is SET at OS level"
        return 0
    else
        wait_for_function_response 0 "iwpriv $wm2_if_name get_txchainmask | grep -qF get_txchainmask:$wm2_tx_chainmask"
        if [ $? = 0 ]; then
            log -deb "$fn_name - TX CHAINMASK: $wm2_tx_chainmask is SET at OS level"
            return 0
        else
            raise "TX CHAINMASK: $wm2_tx_chainmask is NOT set at OS" -l "$fn_name" -tc
        fi
    fi

}

check_tx_power_at_os_level()
{
    wm2_tx_power=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_tx_power_at_os_level"

    log -deb "$fn_name - Checking Tx-Power at OS level"

    wait_for_function_response 0 "iwconfig $wm2_if_name | grep -qE Tx-Power[:=]$wm2_tx_power" &&
        log -deb "$fn_name - Tx-Power: $wm2_tx_power is set at OS level" ||
        (
            iwconfig "$wm2_if_name"
            return 1
        ) || raise "Tx-Power: $wm2_tx_power is NOT set at OS" -l "$fn_name" -tc
}

check_country_at_os_level()
{
    wm2_country=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_country_at_os_level"

    log -deb "$fn_name - Checking COUNTRY at OS level"

    wait_for_function_response 0 "iwpriv $wm2_if_name getCountryID | grep -qF getCountryID:$wm2_country"
    if [ $? = 0 ]; then
        log -deb "$fn_name - COUNTRY: $wm2_country is SET at OS level"
        return 0
    else
        raise "COUNTRY: $wm2_country is NOT set at OS" -l "$fn_name" -tc
    fi

}

check_is_channel_ready_for_use()
{
    wm2_channel=$1
    wm2_if_name=$2
    is_empty=false
    fn_name="wm2_lib:check_is_channel_ready_for_use"
    log -deb "$fn_name - Checking is CHANNEL $wm2_channel ready for IMMEDIATE use"

    wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_Radio_State channels -w if_name $wm2_if_name" || is_empty=true

        if [ "$is_empty" = "true" ]; then
            log -deb "$fn_name - Table Wifi_Radio_State dump"
            ${OVSH} s Wifi_Radio_State || true
            raise "Field channels is empty in Wifi_Radio_State for $if_name" -l "$fn_name" -ds
        fi

    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'"$wm2_channel"'","{\"state\": \"cac_completed\"}"]'; then
        log -deb "$fn_name - CHANNEL $wm2_channel is ready for USE - cac_finished"
        return 0
    elif ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'"$wm2_channel"'","{\"state\": \"nop_finished\"}"]'; then
        log -deb "$fn_name - CHANNEL $wm2_channel is ready for USE - nop_finished"
        return 0
    elif ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'"$wm2_channel"'","{\"state\":\"allowed\"}"]'; then
        log -deb "$fn_name - CHANNEL $wm2_channel is ready for USE - allowed"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "CHANNEL $wm2_channel is NOT ready for USE" -l "$fn_name" -s
}

check_is_cac_started()
{
    wm2_channel=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_is_cac_started"

    log -deb "$fn_name - Checking is CAC STARTED on CHANNEL $wm2_channel"

    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'$wm2_channel'","{\"state\": \"cac_started\"}"]'; then
        log -deb "$fn_name - CAC STARTED on CHANNEL $wm2_channel"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "CAC is NOT STARTED on CHANNEL" -l "$fn_name" -tc
}

check_is_nop_finished()
{
    wm2_channel=$1
    wm2_if_name=$2
    fn_name="wm2_lib:check_is_nop_finished"
    log -deb "$fn_name - Checking is NOP FINISHED on CHANNEL $wm2_channel"

    if ${OVSH} s Wifi_Radio_State channels -w if_name==$wm2_if_name -r | grep -qF '["'$wm2_channel'","{\"state\": \"nop_finished\"}"]'; then
        log -deb "$fn_name - NOP FINISHED on CHANNEL $wm2_channel"
        return 0
    elif
        ${OVSH} s Wifi_Radio_State channels -w if_name==$wm2_if_name -r | grep -qF '["'$wm2_channel'","{\"state\":\"allowed\"}"]'; then
        log -deb "$fn_name - CHANNEL $wm2_channel is ALLOWED"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name==$wm2_if_name || true
    raise "NOP is NOT FINISHED on CHANNEL" -l "$fn_name" -tc
}

simulate_DFS_radar()
{
    wm2_if_name=$1
    fn_name="wm2_lib:simulate_DFS_radar"
    log -deb "$fn_name - Triggering DFS radar event on wm2_if_name"

    wait_for_function_response 0 "radartool -i $wm2_if_name bangradar"

    if [ $? = 0 ]; then
        log -deb "$fn_name - DFS event: $wm2_if_name simulation was SUCCESSFUL"
        return 0
    else
        log -err "$fn_name - DFS event: $wm2_if_name simulation was UNSUCCESSFUL"
    fi
}
############################################ RADIO SECTION - STOP ######################################################


############################################ STATION SECTION - START ###################################################

remove_sta_connections()
{
    wm2_sta_if_name=$1
    fn_name="wm2_lib:remove_sta_connections"
    log -deb "[DEPRECATED] - Function ${fn_name} is deprecated in favor of remove_sta_interfaces"
    log -deb "$fn_name - Removing sta connections except $wm2_sta_if_name"
    ${OVSH} d Wifi_VIF_Config -w if_name!="$wm2_sta_if_name" -w mode==sta ||
        raise "Failed to remove sta" -l "$fn_name" -oe
}

remove_sta_interfaces()
{
    # Removes all STA interfaces, except for explicitly provided ones.
    # Waits for system to react, or timeouts with error.
    # Input arguments:
    #   wait_timeout: how long to wait for system to react (int, optional, default=DEFAULT_WAIT_TIME)
    #   wm2_sta_if_name: interface name to keep from removing (str, optional)
    local wait_timeout=${1:-$DEFAULT_WAIT_TIME}
    local wm2_sta_if_name=$2

    local fn_name="wm2_lib:remove_sta_interfaces"

    if [ -n "${wm2_sta_if_name}" ]; then
        log -deb "$fn_name - Removing STA interfaces except ${wm2_sta_if_name}"
        ovs_cmd="-w mode==sta -w if_name!=${wm2_sta_if_name}"
    else
        log -deb "$fn_name - Removing all STA interfaces"
        ovs_cmd="-w mode==sta"
    fi

    ${OVSH} d Wifi_VIF_Config ${ovs_cmd} &&
        log -deb "$fn_name - Removed STA interfaces from Wifi_VIF_Config" ||
        raise "Failed to remove STA interfaces from Wifi_VIF_Config" -l "$fn_name" -oe

    wait_time=0
    while true ; do
        log -deb "$fn_name - Waiting for Wifi_VIF_State table, retry $wait_time"
        table_select=$(${OVSH} s Wifi_VIF_State ${ovs_cmd}) || true
        if [ -z "$table_select" ]; then
            log -deb "$fn_name - Removed STA interfaces from Wifi_VIF_State"
            return 0
        fi
        if [ $wait_time -gt $wait_timeout ]; then
            raise "Failed to remove STA interfaces from Wifi_VIF_State" -l "$fn_name" -oe
        fi
        wait_time=$((wait_time+1))
        sleep 1
    done
}

############################################ STATION SECTION - STOP ####################################################


############################################ DHCP SECTION - START ######################################################

############################################ DHCP SECTION - STOP #######################################################
