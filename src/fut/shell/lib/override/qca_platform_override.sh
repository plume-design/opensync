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
#   Qualcomm(QCA) platform overrides
#
####################### INFORMATION SECTION - STOP ############################

echo "${FUT_TOPDIR}/shell/lib/override/qca_platform_override.sh sourced"

####################### Qualcomm(QCA) PLATFORM OVERRIDE SECTION - START #########################

###############################################################################
# DESCRIPTION:
#   Function starts qca-hostapd.
#   Uses qca-hostapd
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   start_qca_hostapd
###############################################################################
start_qca_hostapd()
{
    fn_name="qca_platform_override:start_qca_hostapd"
    log -deb "$fn_name - Starting qca-hostapd"
    /etc/init.d/qca-hostapd boot
    sleep 2
}

###############################################################################
# DESCRIPTION:
#   Function starts qca-wpa-supplicant.
#   Uses qca-wpa-supplicant
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
# USAGE EXAMPLE(S):
#   start_qca_wpa_supplicant
###############################################################################
start_qca_wpa_supplicant()
{
    fn_name="qca_platform_override:start_qca_wpa_supplicant"
    log -deb "$fn_name - Starting qca-wpa-supplicant"
    /etc/init.d/qca-wpa-supplicant boot
    sleep 2
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
    fn_name="qca_platform_override:start_wireless_driver"
    start_qca_hostapd &&
        log -deb "$fn_name - start_qca_hostapd - Success" ||
        raise "FAIL: Could not start qca host: start_qca_hostapd" -l "$fn_name" -ds
    start_qca_wpa_supplicant &&
        log -deb "$fn_name - start_qca_wpa_supplicant - Success" ||
        raise "FAIL: Could not start wpa supplicant: start_qca_wpa_supplicant" -l "$fn_name" -ds
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
    fn_name="qca_platform_override:check_channel_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking channel at OS"

    wait_for_function_response 0 "iwlist $wm2_vif_if_name channel | grep -F \"Current\" | grep -qF \"(Channel $wm2_channel)\""
    ret_val=$?
    if [ $ret_val -eq 0 ]; then
        log -deb "$fn_name - Channel is set to $wm2_channel at OS - LEVEL2"
    else
        log -err "$fn_name - Channel is not set to $wm2_channel at OS - LEVEL2"
    fi
    return $ret_val
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
    fn_name="qca_platform_override:check_country_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_country=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking 'country' at OS level - LEVEL2"
    wait_for_function_response 0 "iwpriv $wm2_if_name getCountryID | grep -qF getCountryID:$wm2_country"
    if [ $? = 0 ]; then
        log -deb "$fn_name - 'country' $wm2_country is set at OS level - LEVEL2"
        return 0
    else
        raise "FAIL: 'country' $wm2_country is not set at OS level - LEVEL2" -l "$fn_name" -tc
    fi
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
    fn_name="qca_platform_override:simulate_dfs_radar"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_if_name=$1

    log -deb "$fn_name - Triggering DFS radar event on ${wm2_if_name}"
    wait_for_function_response 0 "radartool -i $wm2_if_name bangradar"
    if [ $? = 0 ]; then
        log -deb "$fn_name - DFS event: $wm2_if_name simulation was SUCCESSFUL"
        return 0
    else
        log -err "$fn_name - DFS event: $wm2_if_name simulation was UNSUCCESSFUL"
    fi
}

###############################################################################
# DESCRIPTION:
#   Function checks if tx power is applied at system level.
#   Uses iwconfig to get tx power info from VIF interface.
#   Radio interface is not used for this model.
# INPUT PARAMETER(S):
#   $1  tx power (required)
#   $2  VIF interface name (required)
#   $3  radio interface name (not used, but still required, do not optimize)
# RETURNS:
#   0   Tx power is not as expected.
#   1   Tx power is as expected.
# USAGE EXAMPLE(S):
#   check_tx_power_at_os_level 21 home-ap-24 wifi0
#   check_tx_power_at_os_level 21 wl0.2 wl0
###############################################################################
check_tx_power_at_os_level()
{
    fn_name="qca_platform_override:check_tx_power_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_power=$1
    wm2_vif_if_name=$2
    # shellcheck disable=SC2034
    wm2_radio_if_name=$3

    log -deb "$fn_name - Checking Tx power at OS level - LEVEL2"
    wait_for_function_response 0 "iwconfig $wm2_vif_if_name | grep -qE Tx-Power[:=]$wm2_tx_power" &&
        log -deb "$fn_name - Tx power $wm2_tx_power is set at OS level" ||
        (
            iwconfig "$wm2_vif_if_name"
            return 1
        ) ||
            raise "FAIL: Tx power $wm2_tx_power is not set at OS level - LEVEL2" -l "$fn_name" -tc

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function returns tx_power set at OS level – LEVEL2.
#   Uses iwconfig to get tx_power info from VIF interface.
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
    fn_name="qca_platform_override:get_tx_power_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    iwconfig $wm2_vif_if_name | grep "Tx-Power" | awk '{print $4}' | awk -F '=' '{print $2}'
}

###############################################################################
# DESCRIPTION:
#   Function checks if tx chainmask is applied at system level.
#   Uses iwconfig to get tx chainmask info.
# INPUT PARAMETER(S):
#   $1  tx chainmask (required)
#   $2  Interface name (required)
# RETURNS:
#   0   Tx chainmask is as expected.
# USAGE EXAMPLE(S):
#   check_tx_chainmask_at_os_level 5 wifi0
###############################################################################
check_tx_chainmask_at_os_level()
{
    fn_name="qca_platform_override:check_tx_chainmask_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_chainmask=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking Tx Chainmask at OS level"
    wait_for_function_response 0 "iwpriv $wm2_if_name get_txchainsoft | grep -qF get_txchainsoft:$wm2_tx_chainmask"
    if [ $? = 0 ]; then
        log -deb "$fn_name - Tx Chainmask $wm2_tx_chainmask is set at OS level - LEVEL2"
        return 0
    else
        wait_for_function_response 0 "iwpriv $wm2_if_name get_txchainmask | grep -qF get_txchainmask:$wm2_tx_chainmask"
        if [ $? = 0 ]; then
            log -deb "$fn_name - Tx Chainmask $wm2_tx_chainmask is set at OS level - LEVEL2"
            return 0
        else
            raise "FAIL: Tx Chainmask $wm2_tx_chainmask is not set at OS level - LEVEL2" -l "$fn_name" -tc
        fi
    fi
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
    fn_name="qca_platform_override:check_beacon_interval_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_bcn_int=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking Beacon Interval at OS - LEVEL2"
    wait_for_function_response 0 "iwpriv $wm2_vif_if_name get_bintval | grep -qF get_bintval:$wm2_bcn_int"
    if [ $? = 0 ]; then
        log -deb "$fn_name - Beacon Interval $wm2_bcn_int for $wm2_vif_if_name is set at OS - LEVEL2"
        return 0
    else
        raise "FAIL: Beacon Interval $wm2_bcn_int for $wm2_vif_if_name is not set at OS - LEVEL2" -l "$fn_name" -tc
    fi
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
#   0   HT mode is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_ht_mode_at_os_level HT40 home-ap-24 2
#   check_ht_mode_at_os_level HT20 home-ap-50 36
###############################################################################
check_ht_mode_at_os_level()
{
    fn_name="qca_platform_override:check_ht_mode_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_ht_mode=$1
    wm2_vif_if_name=$2
    channel=$3

    log -deb "$fn_name - Checking HT MODE for channel $channel at OS level"
    wait_for_function_response 0 "iwpriv $wm2_vif_if_name get_mode | grep -qF $wm2_ht_mode"
    if [ $? = 0 ]; then
        log -deb "$fn_name - HT mode $wm2_ht_mode for channel $channel is set at OS level"
        return 0
    else
        raise "FAIL: HT mode $wm2_ht_mode for channel $channel is not set at OS level" -l "$fn_name" -tc
    fi
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
    fn_name="qca_platform_override:get_channel_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    iwlist $wm2_vif_if_name channel | grep -F "Current" | grep -F "Channel" | sed 's/)//g' | awk '{ print $5 }'
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
    fn_name="qca_platform_override:get_ht_mode_from_os"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2
    iwpriv $wm2_vif_if_name get_mode | sed 's/HT/ HT/g' | sed 's/PLUS$//' | awk '{ print $3 }'
}

####################### Qualcomm(QCA) PLATFORM OVERRIDE SECTION - STOP #########################
