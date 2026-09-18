#! /bin/sh

ovsh u Node_Services enable:=false -w service==cpm
ovsh d Captive_Portal
killall tinyproxy
rm /tmp/tinyproxy/tinyproxy.*.conf
rm /var/run/tinyproxy.*.pid
