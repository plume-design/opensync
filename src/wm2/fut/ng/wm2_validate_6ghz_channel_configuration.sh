#!/bin/sh -axe

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


## DESCRIPTION ##
# This test validates 6GHz channel configuration.
# Requires single DUT device with 6GHz radio with US regulatory domain.
# For any other regulatory, upper channels may report failure.
## /DESCRIPTION ##

## PARAMETERS ##
# shell ssh access command
test -n "$dut"
# name of virtual interface to be created
test -n "$dut_vif"
# virtual interface index
dut_vif_idx=${dut_vif_idx:-0}
# ap's ssid
ssid=${ssid:-"test-ssid"}
## /PARAMETERS ##

self=$0

step() {
    name=${self}_$(echo "$*" | tr ' ' '_' | tr -dc a-z0-9_)
    if "$@"
    then
        echo "$name PASS" | tee -a "logs/$self/ret"
    else
        echo "$name FAIL" | tee -a "logs/$self/ret"
    fi
}

rm -f "logs/$self/ret"

create_vif_validate_channel() {
    $dut <<. || return $?
        ovsh d Wifi_VIF_Config
        sleep 1

        dut_phy=\$(ovsh -Ur s Wifi_Radio_State \
                   -w freq_band=="6G" \
                   if_name
        )
        test -n "\$dut_phy"

        vif=\$(ovsh -Ur i Wifi_VIF_Config \
            if_name:="$dut_vif" \
            enabled:=true \
            mac_list_type:=none \
            ssid:="$ssid" \
            security:='["map", [["encryption", "WPA-PSK"],["key","12345678"]]]' \
            vif_radio_idx:="$dut_vif_idx" \
            mode:=ap
        )

        ovsh U Wifi_Radio_Config \
            -w if_name=="\$dut_phy" \
            if_name:="\$dut_phy" \
            enabled:=true \
            freq_band:="6G" \
            channel:="$1" \
            vif_configs::"[\"set\",[[\"uuid\",\"\$vif\"]]]"

        ovsh w Wifi_Radio_State -w if_name==\$dut_phy enabled:=true
        ovsh w Wifi_Radio_State -w if_name==\$dut_phy freq_band:="6G"
        ovsh w Wifi_Radio_State -w if_name==\$dut_phy channel:="$1"
        ovsh w Wifi_VIF_State -w if_name==$dut_vif enabled:=true
        ovsh w Wifi_VIF_State -w if_name==$dut_vif ssid:="$ssid"
        ovsh w Wifi_VIF_State -w if_name==$dut_vif channel:="$1"
.
}

CHANNEL_LIST="37 53 69 85 101 117 133 149 165 181 197 213"


for ch in $CHANNEL_LIST
do
    step create_vif_validate_channel $ch
done

cat "logs/$self/ret"
