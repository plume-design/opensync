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

# Checks if BSS is up by execution of platform-specific scripts,
# but there are multiple exceptions to such simple check; this script handles
# those exceptions using platform-agnostic tools.
#
# BSS may be down, when:
#   1. All channels are in NOP state
#   2. Radio is misconfigured (e.g. channel is not set)
#   3. Other VIF in the same MLD group is up


OVSH=$CONFIG_INSTALL_PREFIX/tools/ovsh
BSS_IS_UP_D=$CONFIG_INSTALL_PREFIX/scripts/healthcheck.bss.d
RET_CODE_OK=0
RET_CODE_FAIL=1
RET_CODE_CHANNELS_MISMATCHED=1
RET_CODE_ALL_CHANNELS_NOP=2

run_parts()
{
    dir=$1
    shift
    for i in $(ls $dir/*)
    do
        if ! ( . "$i" "$@" ); then
            # Exiting with fail if script from BSS_IS_UP_D returned 1 (fail)
            return 1
        fi
    done
    return 0
}

check_radio()
{
    radio_name=$1

    # If the amount of channels is equal to the number of nop_started statuses
    # then return pass
    # else continue with healtcheck
    N_STATES=$($OVSH -jU s Wifi_Radio_State -w if_name==$radio_name channels \
        | grep -E '"(1|2)?[0-9]?[0-9]",' \
        | wc -l)
    N_NOP_STARTED=$($OVSH -jU s Wifi_Radio_State -w if_name==$radio_name channels \
        | grep -E '\{\\"state\\": \\"nop_started\\"\}' \
        | wc -l)
    if [ $N_STATES -eq $N_NOP_STARTED ]; then
        log_warn "$radio_name: All channels are nop_started"
        return $RET_CODE_ALL_CHANNELS_NOP
    fi

    # The channel mismatch check doesn't fit well into the 10_bss.sh check itself,
    # however, it needs to factor in the "all channels are nop".
    # If the channel mismatch check was to be placed in a different script
    # (e.g. 11_chan_mismatch.sh) it would need to repeat the same check.
    # That would waste a ton of resources—notably, a dozen extra fork+exec.
    if ! $OVSH -jUr s Wifi_Radio_State -w if_name==$radio_name channel | grep -oEq '[0-9]+'; then
        log_warn "$radio_name: channel is not set"
        return $RET_CODE_CHANNELS_MISMATCHED
    fi

    if ! $OVSH -jUr s Wifi_Radio_State -w if_name==$radio_name ht_mode | grep -oEq 'HT[0-9]+'; then
        log_warn "$radio_name: ht_mode is not set"
        return $RET_CODE_CHANNELS_MISMATCHED
    fi
    return $RET_CODE_OK
}


is_mld_group_up()
{
    # Check if mld group of the vif is up
    ifname=$1
    mld_addr=$($OVSH -r s Wifi_VIF_State mld_addr -w if_name=="$ifname" -w mld_addr!='["set",[]]')

    # if mld_addr is empty, vif is not in mld group
    if [ -z $mld_addr ]; then
        return $RET_CODE_FAIL
    fi

    other_vifs_in_mld=$($OVSH -r s Wifi_VIF_State if_name -w mld_addr=="$mld_addr" -w mode==sta -w if_name!="$ifname" -w enabled==true)

    for vif in $other_vifs_in_mld; do
        if run_parts "$BSS_IS_UP_D" "$vif"; then
            return $RET_CODE_OK
        fi
    done
    return $RET_CODE_FAIL
}

for radio_name in $($OVSH -r s Wifi_Radio_Config if_name -w enabled==true)
do
    # ? Shouldn't the check below be executed only for 5G radios ?
    check_radio $radio_name
    case $? in
        $RET_CODE_ALL_CHANNELS_NOP)
            continue
            ;;
        $RET_CODE_CHANNELS_MISMATCHED)
            exit 1
            ;;
        $RET_CODE_OK)
            ;;
    esac

    radio_vif_configs=$($OVSH -rU s Wifi_Radio_Config -w enabled==true -w if_name==$radio_name vif_configs)
    for ifname in $($OVSH -r s Wifi_VIF_Config if_name -w enabled==true)
    do
        vif_uuid=$($OVSH -rU s Wifi_VIF_Config _uuid -w if_name=="$ifname")
        if echo "$radio_vif_configs" | grep -q -F -- "$vif_uuid"; then
            if run_parts "$BSS_IS_UP_D" "$ifname"; then
                continue
            elif $OVSH -r s Wifi_VIF_State mode -w if_name=="$ifname" | grep -q sta; then
                is_mld_group_up $ifname
                case $? in
                    $RET_CODE_OK)
                        continue
                        ;;
                    $RET_CODE_FAIL)
                        log_warn "$ifname: mld bss is not up"
                        exit 1
                        ;;
                esac
            else
                log_warn "$ifname: bss is not up"
                exit 1
            fi
        fi
    done
done
