#!/bin/sh
#
# Collect hostap
#
. "$LOGPULL_LIB"

collect_hostap()
{
    if [ -e /usr/sbin/hostapd ]; then
        # Collect hostapd and supplicant config files
        for FN in /var/run/*.config /var/run/*.pskfile /var/run/*.rxkh; do
            collect_cmd cat $FN
        done

        for sockdir in $(find /var/run/hostapd* -type d); do
            for ifname in $(ls $sockdir/); do
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname status
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname all_sta
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname get_config
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname show_neighbor
                collect_cmd timeout 1 hostapd_cli -p $sockdir -i $ifname get_rxkhs
            done
        done

        for sockdir in $(find /var/run/wpa_supplicant* -type d); do
            for ifname in $(ls $sockdir/); do
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname status
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname list_n
                collect_cmd timeout 1 wpa_cli -p $sockdir -i $ifname scan_r
            done
        done
    fi
}

collect_hostap
