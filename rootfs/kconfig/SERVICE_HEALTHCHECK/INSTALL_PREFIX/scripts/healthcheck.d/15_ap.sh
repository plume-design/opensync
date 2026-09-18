#!/bin/sh
OVSH=$CONFIG_INSTALL_PREFIX/tools/ovsh

# The device eventually is expected to have at least one AP
# interface created by the external ovs controller for
# active (enabled) radio. Not having any AP interfaces is considered
# an error and may need a recovery.

# Skip healtcheck when all radios are disbaled
[ -z "$($OVSH -r s Wifi_Radio_Config -w enabled==true if_name)" ] && Healthcheck_Pass

for vif_uuid in $($OVSH -rU s Wifi_VIF_Config _uuid -w enabled==true -w mode==ap)
do
    $OVSH -rU s Wifi_Radio_Config -w enabled==true vif_configs \
        | grep -q -F -- "$vif_uuid" && Healthcheck_Pass
done
Healthcheck_Fail
