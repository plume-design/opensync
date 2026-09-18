#!/bin/sh
#
# Check NTP to see if it's sync'd or not.  Dumb check, but seems to work...

OVSH=$CONFIG_INSTALL_PREFIX/tools/ovsh

# Skip NTP check if offline config applied:
$OVSH -r s Node_State -w module==PM -w key==gw_offline_status value | grep -F active && Healthcheck_Pass

let cur_tm="$(date +"%s")"
[ $cur_tm -gt 1000000 ] && Healthcheck_Pass

Healthcheck_Fail
