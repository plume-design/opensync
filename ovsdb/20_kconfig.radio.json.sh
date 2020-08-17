
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


bhaul_ssid=$BACKHAUL_SSID
bhaul_psk=$BACKHAUL_PASS
bhaul_multi="$MULTI_BACKHAUL_CREDS"

test "$CONFIG_OVSDB_BOOTSTRAP" = y || bhaul_multi=

cat <<OVS
[
    "Open_vSwitch",
$(echo "$bhaul_multi" | tr ': ' ' \n' | while read ssid psk
do
cat <<CRED
{
    "op": "insert",
    "table": "Wifi_Credential_Config",
    "uuid-name": "cred_$(echo "$ssid $psk" | md5sum | awk '{print $1}')",
    "row": {
        "onboard_type": "gre",
        "ssid": "$ssid",
        "security": ["map", [
            ["encryption", "WPA-PSK"],
            ["key", "$psk"]
        ]]
    }
},
CRED
done)
$(for i in $CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST
do
phy=$(echo "$i" | cut -d: -f1)
vif=$(echo "$i" | cut -d: -f2)
cat <<VIF
{
    "op": "insert",
    "table": "Wifi_VIF_Config",
    "uuid-name": "vif_$(echo "$vif" | md5sum | awk '{print $1}')",
    "row": {
        "enabled": true,
        "vif_dbg_lvl": 0,
        "if_name": "$vif",
        "bridge": "",
        "mode": "sta",
        "wds": false,
        "vif_radio_idx": 0,
        "multi_ap": "none",
        "ssid": "$bhaul_ssid",
        "security": ["map", [
            ["encryption", "WPA-PSK"],
            ["key", "$bhaul_psk"]
        ]],
        "credential_configs": ["set", [
$(echo "$bhaul_multi" | tr ': ' ' \n' | while read ssid psk
do
    printf ',["named-uuid", "cred_%s"]\n' "$(echo "$ssid $psk" | md5sum | awk '{print $1}')"
done | dd bs=1 skip=1 2>/dev/null)
        ]]
    }
},
VIF
done)
$(for i in $CONFIG_OVSDB_BOOTSTRAP_WIFI_PHY_LIST
do
phy=$(echo "$i" | cut -d: -f1)
chainmask=$(echo "$i" | cut -d: -f2)
htmode=$(echo "$i" | cut -d: -f3)
hwmode=$(echo "$i" | cut -d: -f4)
freqband=$(echo "$i" | cut -d: -f5)
hwtype=$(echo "$i" | cut -d: -f6)
args=$(echo "$i" | cut -d: -f7-)
cat <<PHY
{
    "op": "insert",
    "table": "Wifi_Radio_Config",
    "row": {
        "enabled": true,
        "if_name": "$phy",
        "freq_band": "$freqband",
        "channel_mode": "cloud",
        "channel_sync": 0,
        "hw_type": "$hwtype",
        "hw_config": ["map", [
$(echo -n "$args" \
    | tr ',' '\0' \
    | xargs -r0n1 sh -c '
        k=$(echo "$0" | cut -d= -f1)
        v=$(echo "$0" | cut -d= -f2-)
        echo ",[\"$k\", $v]"
' | dd bs=1 skip=1 2>/dev/null)
        ]],
        "ht_mode": "$htmode",
        "hw_mode": "$hwmode",
        "tx_chainmask": $chainmask,
        "vif_configs": ["set", [
$(for j in $CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST
do
_phy=$(echo "$j" | cut -d: -f1)
_vif=$(echo "$j" | cut -d: -f2)
test "$phy" = "$_phy" && printf ',["named-uuid", "vif_%s"]\n' "$(echo "$_vif" | md5sum | awk '{print $1}')"
done | dd bs=1 skip=1 2>/dev/null)
        ]]
    }
},
PHY
done)
    { "op": "comment", "comment": "" }
]
OVS
