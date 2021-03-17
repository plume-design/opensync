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

#
# Collect hostap
#
. "$LOGPULL_LIB"

collect_hostap()
{
    if [ -e /usr/sbin/hostapd ]; then
        # Collect hostapd and supplicant config files
        for FN in /var/run/*.config /var/run/*.pskfile; do
            collect_cmd cat $FN
        done

        for sockdir in $(find /var/run/hostapd-* -type d); do
            for ifname in $(ls $sockdir/); do
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname status
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname all_sta
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname get_config
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname show_neighbor
            done
        done

        for sockdir in $(find /var/run/wpa_supplicant-* -type d); do
            for ifname in $(ls $sockdir/); do
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname status
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname list_n
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname scan_r
            done
        done
    fi
}

collect_hostap
