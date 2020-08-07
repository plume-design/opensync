#!/usr/bin/env bash

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


current_dir=$(dirname "$(realpath "$BASH_SOURCE")")
fut_topdir="$(realpath "$current_dir"/../..)"
source "$fut_topdir/lib/rpi_lib.sh"

usage="$(basename "$0") [-h] \$1 \$2 \$3

where arguments are:
    interface=\$1 --  interface name which to use to connect to network (string)(required)
    network_ssid=\$2 --  network ssid to which rpi client will try and connect (string)(required)
    network_bssid=\$3 --  network bssid (MAC) to which rpi client will try and connect (string)(required)
    network_pass=\$4 -- network password which rpi client will use to try and connect to network (string)(required)
    network_key_mgmt=\$5 -- used as key_mgmt value in wpa_supplicant.conf file
    enable_dhcp=\$6 -- enable or disable DHCPDISCOVER on network !!! PARTIALLY IMPLEMENTED !!!
        - possibilities:
            - on (string) - enable dhcp discover for interface
            - off (string) - disable dhcp discover for interface - (default value)
                    - killing dhclient process after 5 seconds - workaround
    msg_prefix=\$7 -- used as message prefix for log messages (string)(optional)
Script is used to connect RPI client to WPA2 network

Script does following:
    - bring down interface wlan0 -> ifdown wlan0
    - clear wpa_supplicant.conf file
    - create new wpa_supplicant.conf using wpa_passphrase and other configuration
    - bring up interface wlan0 -> ifup wlan0
    - check if RPI client is connected to netowork -> ip link show wlan0

Example of usage:
    $(basename "$0") wlan0 wm_dut_24g_network 72:a7:56:f0:0d:72 WifiPassword123 WPA-PSK on
        - connect to wm_dut_24g_network ssid with bssid 72:a7:56:f0:0d:72 using wlan0 interface key_mgmnt mode of
          WPA-PSK using network password WifiPassword123 and keeping dhcp discover on
"

while getopts h option; do
    case "$option" in
        h)
            echo "$usage"
            exit 1
            ;;
    esac
done

if [[ $# -lt 6 ]]; then
    echo 1>&2 "$0: not enough arguments"
    echo "$usage"
    exit 2
fi

interface=$1
network_ssid=$2
network_bssid=$3
network_pass=$4
network_key_mgmt=$5
enable_dhcp=$6
msg_prefix=$7

network_connect_to_wpa2 $interface $network_ssid $network_bssid $network_pass $network_key_mgmt $enable_dhcp $msg_prefix