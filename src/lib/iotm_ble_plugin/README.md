# BLE Handler

## Purpose

This is the base plugin to be loaded into OpenSync IoT for handling BLE events.
This enables standard rules to fire, such as connecting to a device or
recieveing advertising data. 

## How to enable

The shared library must be loaded onto the platform and then the manager must
be told about the location of the shared library. For example, if the shared
library is kept in `/usr/plume/lib` then the following row would result in the
manager loading the BLE Plugin:

```
ovsh U IOT_Manager_Config -w handler=="dev_ble_default" \
    handler:="dev_ble_default" \
    plugin:="/usr/plume/lib/libiotm_ble.so" \
    other_config:='["map",[["dso_init","iotm_ble_handler_init"]]]' 
```

## Events and Commands

This plugin will enable rules to control any BLE Target layer function, and
will emit an event any time the target layer emits an event. It provides
the foundational components to the BLE stack for the IoT manager. A complete
list of the parameters, commands, and events may be found at the bottom of this
document.

## Map Functions

The plugin implements several maps and correlated getter functions. This allows
for a magic OVSDB string to be referenced as an enum. Some of the maps also
include routing functions when necessary, to allow for the proper target layer
methods to be called.

## IoTM API Integration

### Rule Update

The plugin registers callbacks for when rules update so it can bind to updates
for 'ble_advertised' events. When rules are added or removed, the plugin will
check to see if this change impacts the parameters used for a ble scan. If the
scan parameters have updated, the plugin will restart the target layer scan.

### Handler

The plugin implements a handler for IoT events and routes them to target layer
commands. There is a mapping function commands_map that relates to the commands
the handler knows how to process. 

### Emitter

The plugin registers for callbacks from the target layer when BLE events are
emitted. This builds an encapsulated iot event, and then calls emit which is
implemented by IoTM. This allows the event to be processed correctly.

## Parameter Keys, Commands, and Events

The following is the list of supported parameter keys (mac, char_uuid, etc.),
commands, and events that may be used to craft BLE Rules. The rules should use
the below values to form rules matching the examples above.

### Parameter Keys

Parameters are used to either filter out events to fire a rule, or as values
that are passed to a command. For example a rule may have the filter
["char_uuid", "0x0001"] and this will only fire the rule if a device is
advertising that characteristic. 

| Key | Description | Example |
| --- | ----------- | ------- |
| mac | MAC of the device | "*", "DE:AD:BE:EF" |
| name | Name of device, for user to view | "Hue Light" |
| connection_status | Whether device is connected | "success" |
| c_flag | Flag associated with characteristic update | "ble_char_read" |
| is_notification | whether an update is a notification | "true" "false" |
| decode_type | type of decoding to use on string | "hex" |
| data | byte array of data to send | "0xab01" |
| data_len | Length of data array | "4" |
| is_public_addr | whether the advertising address is public | "true" "false" |
| is_primary | get if services in list are primary | "true" "false" |
| serv_uuid | uuid of service | "0x0231" |
| char_uuid | uuid of characteristic to interact with | "0x2803" |
| desc_uuid | uuid of a ble descriptor |"0x2803" |
| status_code | BLE code for an interaction | "0x00" |

### Events

These are the events that rules in the table `IOT_Rule_Config` can bind to as a
result of this plugin.

| Event | Description | Caused By |
| ------- | --------- | --------- |
| ble_error | error emitted from radio or device | any |
| ble_advertised | advertising packet was recieved | advertising packet recieved matching a rule filter |
| ble_connected | a device has connected | ble_connect_device |
| ble_disconnected | a device has disconnected | ble_disconnect_device |
| ble_serv_discovered | a service was discovered upon request | ble_discover_services |
| ble_char_discovered | a char was discovered upon request | ble_discover_characteristics |
| ble_desc_discovered | a desc was discovered upon request | ble_discover_descriptors |
| ble_char_updated | a char with notifications has updated | ble_enable_characteristic_notifications OR ble_read_characteristic |
| ble_desc_updated | a descriptor read request succeeded | ble_read_descriptor |
| ble_char_write_success | a command to write to a char succeeded | ble_write_characteristic |
| ble_desc_write_success | a command to write to a desc has succeeded | ble_write_descriptor |
| ble_char_notify_success | a command to enable notifications has succeeded | ble_enable_characteristic_notifications |

### Commands

Commands may run after an event has fired, most commands follow a specific
event, although they may also be chained together. For example, a
ble_write_characteristic must follow a ble_connected, but it may
also follow a ble_read_characteristic.

| Command | Description | In Response To |
| ------- | ----------- | -------------- |
| ble_connect_device | issue a connect request to a device | ble_advertised |
| ble_disable_characteristic_notifications | turn off char notifications | ble_connected |
| ble_disconnect_device | disconnect from a device | ble_connected |
| ble_discover_characteristics | get a list of chars for a service | ble_serv_discovered |
| ble_discover_descriptors | get list of descriptors for a char | ble_char_discovered |
| ble_discover_services | see what services a device offers | ble_connected |
| ble_enable_characteristic_notifications | when a char value changes, emit event | ble_connected |
| ble_read_characteristic | get the current value of a characteristic | ble_connected |
| ble_read_descriptor | get the current value of a descriptor | ble_connected |
| ble_write_characteristic | set a characteristic value | ble_connected |
| ble_write_descriptor | set a descriptor value | ble_connected |

