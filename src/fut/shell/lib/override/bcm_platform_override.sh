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
#   Broadcomm(BCM) platform overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/bcm_platform_override.sh sourced"

####################### Broadcomm(BCM) PLATFORM OVERRIDE SECTION - START #########################

###############################################################################
# DESCRIPTION:
#   Function starts wireless driver on a device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   start_wireless_driver
###############################################################################
start_wireless_driver()
{
    /etc/init.d/bcm-wlan-drivers.sh start
}

###############################################################################
# DESCRIPTION:
#   Function stops wireless driver on a device.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   stop_wireless_driver
###############################################################################
stop_wireless_driver()
{
    /etc/init.d/bcm-wlan-drivers.sh stop
}

###############################################################################
# DESCRIPTION:
#   Function checks if channel is applied at system level.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  interface name (required)
# RETURNS:
#   0   Channel is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_channel_at_os_level 36 home-ap-50 36
###############################################################################
check_channel_at_os_level()
{
    fn_name="bcm_platform_override:check_channel_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking channel at OS"

    wait_for_function_response 0 "wl -a $wm2_vif_if_name channel | grep -F \"current mac channel\" | grep -qF \"$wm2_channel\""

    if [[ $? == 0 ]]; then
        log -deb "$fn_name - Channel is set to $wm2_channel at OS level"
        return 0
    fi

    raise "Channel is NOT set to $wm2_channel" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks if country is applied at system level.
#   Uses iwpriv to get tx power info.
#   Provide override function if iwpriv not available on device.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  country (required)
#   $2  interface name (required)
# RETURNS:
#   0   Country is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   N/A, not used, awaits removal.
###############################################################################
check_country_at_os_level()
{
    fn_name="bcm_platform_override:check_country_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_country=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking COUNTRY at OS level"

    wait_for_function_response 0 "wl -a $wm2_if_name country | grep -qF $wm2_country" &&
        log -deb "$fn_name - 'country' $wm2_country is set at OS level - LEVEL2" ||
        raise "FAIL: 'country' $wm2_country is not set at OS level - LEVEL2" -l "$fn_name" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function simulates DFS (Dynamic Frequency Shift) radar event on interface.
# INPUT PARAMETER(S):
#   $1  channel (required)
# RETURNS:
#   0   Simulation was a success.
# USAGE EXAMPLE(S):
#   N/A
###############################################################################
simulate_dfs_radar()
{
    fn_name="bcm_platform_override:simulate_dfs_radar"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_if_name=$1

    log -deb "$fn_name - Trigering DFS radar event on wm2_if_name"

    wait_for_function_response 0 "wl -i $wm2_if_name radar 2" &&
        log -deb "$fn_name - DFS event: $wm2_if_name simulation was SUCCESSFUL" ||
        log -err "$fn_name - DFS event: $wm2_if_name simulation was UNSUCCESSFUL"

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if tx power is applied at system level.
#   Uses wl command txpwr_target_max to get tx power info from radio interface.
#   VIF interface is not used for this model.
# INPUT PARAMETER(S):
#   $1  tx power (required)
#   $2  VIF interface name (not used, but still required, do not optimize)
#   $3  radio interface name (required)
# RETURNS:
#   0   Tx power is not as expected.
#   1   Tx power is as expected.
# USAGE EXAMPLE(S):
#   check_tx_power_at_os_level 21 home-ap-24 wifi0
#   check_tx_power_at_os_level 21 wl0.2 wl0
###############################################################################
check_tx_power_at_os_level()
{
    fn_name="bcm_platform_override:check_tx_power_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_power=$1
    # shellcheck disable=SC2034
    wm2_vif_if_name=$2
    wm2_radio_if_name=$3

    log -deb "$fn_name - Checking Tx-Power at OS level"
    wait_for_function_response 0 "wl -i $wm2_radio_if_name txpwr_target_max | grep -q 'Maximum Tx Power Target .* $wm2_tx_power.00'" &&
        log -deb "$fn_name - Tx-Power: $wm2_tx_power is set at OS level" ||
            (
                wl -a "$wm2_radio_if_name" curpower
                return 1
            ) || raise "Tx-Power: $wm2_tx_power is NOT set at OS level" -l "$fn_name" -tc
    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns tx_power set at OS level – LEVEL2.
#   Uses wl to get tx_power info from VIF interface.
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
    fn_name="bcm_platform_override:get_tx_power_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    wl -i $wm2_vif_if_name txpwr | awk '{print $1}' | awk -F '.' '{print $1}'
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
check_tx_chainmask_at_os_level()
{
    fn_name="bcm_platform_override:check_tx_chainmask_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_chainmask=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking TX CHAINMASK at OS level"

    wait_for_function_response 0 "wl -a $wm2_if_name txchain | grep -qF $wm2_tx_chainmask" &&
        log -deb "$fn_name - TX CHAINMASK: $wm2_tx_chainmask is SET at OS level" ||
        raise "TX CHAINMASK: $wm2_tx_chainmask is NOT set at OS level" -l "$fn_name" -tc

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
    fn_name="bcm_platform_override:check_beacon_interval_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_bcn_int=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking BEACON INTERVAL at OS level"

    wait_for_function_response 0 "wl -a $wm2_vif_if_name bi | grep -qF $wm2_bcn_int" &&
        log -deb "$fn_name - BEACON INTERVAL: $wm2_bcn_int is SET at OS level" ||
        raise "BEACON INTERVAL: $wm2_bcn_int is NOT set at OS level" -l "$fn_name" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if HT mode for interface on selected channel is
#   applied at system level.
#   Raises exception if HT mode is not set at OS level.
# INPUT PARAMETER(S):
#   $1  HT mode (required)
#   $2  interface name (required)
#   $3  channel (required)
# RETURNS:
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_ht_mode_at_os_level HT40 home-ap-24 2
#   check_ht_mode_at_os_level HT20 home-ap-50 36
###############################################################################
check_ht_mode_at_os_level()
{
    fn_name="bcm_platform_override:check_ht_mode_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_ht_mode=$1
    wm2_vif_if_name=$2
    wm2_channel=$3

    ht_mode_numerical=${wm2_ht_mode:2}

    if [[ $wm2_channel -gt 14 ]]; then
        if [[ $ht_mode_numerical == 40 ]]; then
            channel_modulo=$((wm2_channel%8))
            if [[ $channel_modulo == 0 ]]; then
                driver_ht_mode=$wm2_channel'u'
            elif [[ $wm2_channel -eq 153 ]]; then
                driver_ht_mode=$wm2_channel'u'
            elif [[ $wm2_channel -eq 161 ]]; then
                driver_ht_mode=$wm2_channel'u'
            else
                driver_ht_mode=$wm2_channel'l'
            fi
        elif [[ $ht_mode_numerical == 80 ]]; then
            driver_ht_mode="$wm2_channel/$ht_mode_numerical"
        else
            driver_ht_mode=$wm2_channel
        fi
    else
        driver_ht_mode=$wm2_channel
    fi

    log -deb "$fn_name - Checking HT MODE at OS level"

    wait_for_function_response 0 "wl -a $wm2_vif_if_name chanspec | grep -qF $driver_ht_mode" &&
        log -deb "$fn_name - HT MODE: $wm2_ht_mode is SET at OS level" ||
        raise "HT MODE: $wm2_ht_mode is NOT set at OS level" -l "$fn_name" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if channel is applied at system level.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  interface name (required)
# RETURNS:
#   0   Channel is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_channel_at_os_level 36 home-ap-50 36
###############################################################################
check_channel_at_os_level()
{
    fn_name="bcm_platform_override:check_channel_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking channel at OS - LEVEL2"

    wait_for_function_response 0 "wl -a $wm2_vif_if_name channel | grep -F \"current mac channel\" | grep -qF \"$wm2_channel\""
    # shellcheck disable=SC2181
    if [[ $? == 0 ]]; then
        log -deb "$fn_name - Channel is set to $wm2_channel at OS - LEVEL2"
        return 0
    fi

    raise "FAIL: Channel is not set to $wm2_channel at OS - LEVEL2" -l "$fn_name" -tc
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
#   get_channel_from_os wl0
###############################################################################
get_channel_from_os()
{
    fn_name="bcm_platform_override:get_channel_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wl -a $wm2_vif_if_name channel | grep -F "current mac channel" | cut -f2
}

###############################################################################
# DESCRIPTION:
#   Function returns HT mode set at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  vif_interface_name (required)
#   $2  channel (required)
# RETURNS:
#   0   on successful channel retrieval, fails otherwise
# ECHOES:
#   HT mode from OS in format: HT20, HT40 (examples)
# USAGE EXAMPLE(S):
#   get_ht_mode_from_os wl1.2 1
###############################################################################
get_ht_mode_from_os()
{
    fn_name="bcm_platform_override:get_ht_mode_from_os"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2

    chanspec_str=$(wl -a "$wm2_vif_if_name" chanspec | cut -d' ' -f1)
    echo $chanspec_str | grep -q "/160"
    if [ $? -eq 0 ]; then
        echo "HT160"
        exit 0
    fi
    echo $chanspec_str | grep -q "/80"
    if [ $? -eq 0 ]; then
        echo "HT80"
        exit 0
    fi
    echo $chanspec_str | grep -q "[lu]"
    if [ $? -eq 0 ]; then
        echo "HT40"
        exit 0
    fi
    echo $chanspec_str | grep -qw "$wm2_channel"
    if [ $? -eq 0 ]; then
        echo "HT20"
        exit 0
    fi
    exit 1
}

####################### Broadcomm(BCM) PLATFORM OVERRIDE SECTION - STOP #########################

###############################################################################
# DESCRIPTION:
#   Function echoes upgrade manager's numerical code of identifier.
#   Raises exception if identifier not found.
# INPUT PARAMETER(S):
#   $1  upgrade_identifier (string) (required)
# RETURNS:
#   Echoes code.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   get_um_code UPG_ERR_DL_FW
#   get_um_code UPG_STS_FW_DL_END
###############################################################################
get_um_code()
{
    fn_name="um_lib:get_um_code"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    upgrade_identifier=$1

    case "$upgrade_identifier" in
        "UPG_ERR_ARGS")
            echo  "-1"
            ;;
        "UPG_ERR_URL")
            echo  "-3"
            ;;
        "UPG_ERR_DL_FW")
            echo  "-4"
            ;;
        "UPG_ERR_DL_MD5")
            echo  "-5"
            ;;
        "UPG_ERR_MD5_FAIL")
            echo  "-6"
            ;;
        "UPG_ERR_IMG_FAIL")
            echo  "-7"
            ;;
        "UPG_ERR_FL_ERASE")
            echo  "-8"
            ;;
        "UPG_ERR_FL_WRITE")
            echo  "-9"
            ;;
        "UPG_ERR_FL_CHECK")
            echo  "-10"
            ;;
        "UPG_ERR_BC_SET")
            echo  "-11"
            ;;
        "UPG_ERR_APPLY")
            echo  "-12"
            ;;
        "UPG_ERR_BC_ERASE")
            echo  "-14"
            ;;
        "UPG_ERR_SU_RUN ")
            echo  "-15"
            ;;
        "UPG_ERR_DL_NOFREE")
            echo  "-16"
            ;;
        "UPG_STS_FW_DL_START")
            echo  "10"
            ;;
        "UPG_STS_FW_DL_END")
            echo  "11"
            ;;
        "UPG_STS_FW_WR_START")
            echo  "20"
            ;;
        "UPG_STS_FW_WR_END")
            echo  "21"
            ;;
        "UPG_STS_FW_BC_START")
            echo  "30"
            ;;
        "UPG_STS_FW_BC_END")
            echo  "31"
            ;;
        *)
            raise "FAIL: Unknown upgrade_identifier {given:=$upgrade_identifier}" -l "$fn_name" -arg
            ;;
    esac
}