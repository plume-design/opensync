#!/bin/sh

echo -n 'Starting Open vSwitch ...'
# start openvswitch
/etc/init.d/openvswitch start

# Wait until ovsdb is ready
while ! ovsdb-client list-dbs > /dev/null 2>&1
do
    sleep 1
    echo -n .
done

