#!/bin/sh
# {# jinja-parse #}

# Stop cloud connection
echo "Removing manager"
ovsdb-client transact '
["Open_vSwitch", {
    "op": "delete",
    "table": "Manager",
    "where": []
}, {
    "op" : "update",
    "table" : "Open_vSwitch",
    "where" : [],
    "row": {
        "manager_options": ["set",[]]
    }
}]'

# Remove bridge interfaces from the system
for BRIDGE in $(echo /sys/class/net/*/bridge | tr ' ' '\n' | cut -d '/' -f 5); do
    echo "Removing $BRIDGE"
    ip link set dev $BRIDGE down
    brctl delbr $BRIDGE
done

# Stop openvswitch
/etc/init.d/openvswitch stop
echo "openvswitch stop"
