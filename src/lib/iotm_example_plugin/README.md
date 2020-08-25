# Example Plugin

## Purpose

This plugin is designed to provide a starting point for building a new IoT
Manager plugin. The plugin accepts events and logs the key/value contents.

## How to enable

The shared library must be loaded onto the platform and then the manager must
be told about the location of the shared library. For example, if the shared
library is kept in `/usr/plume/lib` then the following row would result in the
manager loading the Example Plugin:

```
ovsh U IOT_Manager_Config -w handler=="dev_example_plugin" \
    handler:="dev_example_plugin" \
    plugin:="/usr/plume/lib/libiotm_example.so" \
    other_config:='["map",[["dso_init","iotm_example_plugin_init"]]]' 
```

## Events

To send an event to the example plugin, a rule must be installed that routes to
the plugin. Below is an example for logging 'ble_advertised' events using the
example plugin:

```
ovsh U IOT_Rule_Config -w name=="log_adverts_pulseox" \
    name:="log_adverts_pulseox" \
    event:="ble_advertised" \
    filter:='["map",[["mac","${dev_ble_mac}"]]]' \
    actions:='["map",[["dev_example_plugin","ble_advertised"]]]'
```
