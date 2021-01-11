# Connected Devices Plugin

This plugin is used to track any devices that are currently connected to the
IoT Manager. This plugin will update and remove from the iot_connected_devices
tag in Openflow_Tags. 

Any rule that is installed should be run against any currently connected
devices. The IOT manager tracks any devices with the connected device tag. If a
rule is installed and it's filter matches a device with that tag, the rule's
action will be ran.

## Install the plugin

```
ovsh U IOT_Manager_Config -w handler=="dev_connected_devices" \
    handler:="dev_connected_devices" \
    plugin:="/usr/plume/lib/libiotm_connected.so" \
    other_config:='["map",[["dso_init","iotm_connected_devices_init"]]]'
```
