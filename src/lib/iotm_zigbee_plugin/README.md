# Zigbee Handler

This is the default plugin for managing rules that interface with the Zigbee
target layer. All target layer methods are availible as commands through this
plugin, and all target layer events will be surfaced up to the IoT manager.

## Purpose

This is the base plugin to be loaded into OpenSync IoT for handling Zigbee events.
This enables standard rules to fire, such as connecting to a device or
sending a cluster command. 

## How to enable

The shared library must be loaded onto the platform and then the manager must
be told about the location of the shared library. For example, if the shared
library is kept in `/usr/plume/lib` then the following row would result in the
manager loading the BLE Plugin:

```
ovsh U IOT_Manager_Config -w handler=="dev_zigbee_default" \
    handler:="dev_zigbee_default" \
    plugin:="/usr/plume/lib/libiotm_zigbee.so" \
    other_config:='["map",[["dso_init","iotm_zigbee_handler_init"]]]' 
```

## Parameter Keys, Commands, and Events

The following is the list of supported parameter keys (mac, cluster_id, etc.),
commands, and events that may be used to craft Zigbee Rules. The rules should use
the below values to form rules matching the examples above.

### Parameter Keys

Parameters are used to either filter out events to fire a rule, or as values
that are passed to a command. For example a rule may have the filter
["cluster_id", "0x01"] and this will only fire the rule if an event matches
that cluster.

| Key | Description | Example |
| --- | ----------- | ------- |
| enable_pairing_start_epoch | epoch timestamp when this rule should run | "1597958865" |
| mac | mac of the device | "DE:AD:BE:EF" |
| input_cluster | a uuid for a cluster that recieves commands | "0x0001" |
| output_cluster | a uuid for a cluster that has data | "0x0001" |
| cluster_id | a generic cluster id | "0x0001" |
| device_id | 16-bit Zigbee Profile ID | "0x0100" |
| profile_id | id of profile that is supported on an endpoint  | "0x0001" |
| node_address | 16-bit PAN ID | "0x2136" |
| attribute_id | 16-bit Attribute ID | "0x0000" |
| attribute_start_id | 16-bit Attribute ID | "0x0000" |
| attribute_data_type | ZCL data type of attribute | "0x01" |
| max_attributes | maximum number of attributes to discover | "0xFF" |
| command_id | 8-bit Command ID | "0x00" |
| start_command_id | command id to begin discovery | "0x00" |
| max_commands | maximum number of commands to discover | "0xFF" |
| endpoint | id of endpoint  | "0x00" |
| endpoint_filter | endpoints we are interested in (can repeat) | "0x00" |
| status_code | status code of zigbee write command | "0x00" |
| param_data | generic byte array to be passed to radio for op | "0xabe01" |
| decode_type | method for decoding data | "hex" |
| data | generic byte array recieved from an op | "0xabe01" |
| data_len | length of byte array that was recieved | 10 |
| is_report | indicates if result of read or notification | "true" "false" |
| is_reporting_configured | checks if reporting is configured for  | "true" "false" |
| min_report_interval | Minimum reporting interval in seconds  | "60" |
| max_report_interval | Maximum reporting interval in seconds  | "120" |
| timeout_period | Maximum expected time, in seconds, between received reports  | "360" |
| error | an error value if present | "error connecting" |



### Events

These are the events that rules in the table `IOT_Rule_Config` can bind to as a
result of this plugin.

| Event | Description | Caused By |
| ------- | --------- | --------- |
| zigbee_error | zigbee radio reported an error | any call |
| zigbee_device_annced | a new device has joined the network | zigbee_enable_pairing |
| zigbee_ep_discovered | endpoint reported by a device | zigbee_discover_endpoints |
| zigbee_attr_discovered | an attribute has been discovered | zigbee_discover_attributes |
| zigbee_comm_recv_discovered | a new command received has been discovered | zigbee_discover_commands_received |
| zigbee_comm_gen_discovered | a new command generated has been discovered | zigbee_discover_commands_generated |
| zigbee_attr_value_received | a new attribute value was received | zigbee_read_attributes |
| zigbee_attr_write_success | status of write command issued to remote device | zigbee_write_attributes |
| zigbee_report_configed_success | status of configure reporting request | zigbee_configure_reporting |
| zigbee_report_config_received | a new reporting configuration was received | zigbee_configure_reporting |
| zigbee_default_response | a new default response has been received. provides status of commands issued to a remote device  | zigbee_send_cluster_specific_command |

### Commands

Commands may run after an event has fired, most commands follow a specific
event, although they may also be chained together. For example, a
zigbee_send_cluster_specific_command must follow a zigbee_device_annced, but it may
also follow a zigbee_attr_value_received.

| Command | Description | In Response To |
| ------- | ----------- | -------------- |
| zigbee_enable_pairing | use enable_pairing_start_epoch to tell gateway to allow devices on | entrypoint |
| zigbee_configure_reporting | subscribe to updates for device values | zigbee_device_annced |
| zigbee_discover_attributes | see what attributes a device has | zigbee_device_annced |
| zigbee_discover_commands_generated | discover commands generated on a cluster | zigbee_device_annced |
| zigbee_discover_commands_received | discover commands received on a cluster | zigbee_device_annced |
| zigbee_discover_endpoints | discover device's active endpoints, and their clusters | zigbee_device_annced |
| zigbee_read_attributes | read values of attributes | zigbee_device_annced |
| zigbee_read_reporting_configuration | read reporting configuration for attributes | zigbee_device_annced |
| zigbee_send_cluster_specific_command | Send cluster specific command to device | zigbee_device_annced |
| zigbee_send_network_leave | send network leave request to device | zigbee_device_annced |
| zigbee_write_attributes | write values to attributes | zigbee_device_annced |
