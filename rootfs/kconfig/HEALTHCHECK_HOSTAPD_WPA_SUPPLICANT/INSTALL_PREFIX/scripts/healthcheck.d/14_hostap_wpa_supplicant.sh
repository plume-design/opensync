#!/bin/sh
# Checks if hostapd and wpa_supplicant are running and responsive.
# This is done by pinging the hostapd and wpa_supplicant control sockets.


die() { log_warn "$*"; Healthcheck_Fail; }
pidof hostapd || die hostapd not found
pidof wpa_supplicant || die wpa_supplicant not found
timeout 1 wpa_cli -p '' -g /var/run/hostapd/global ping | grep -q PONG || die ping to hostapd failed
timeout 1 wpa_cli -p '' -g /var/run/wpa_supplicantglobal ping | grep -q PONG || die ping to wpa_supplicant failed
Healthcheck_Pass
