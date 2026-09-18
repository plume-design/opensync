#!/bin/sh
#
# Collect common Open vSwitch database info
#
. "$LOGPULL_LIB"

collect_ovsdb()
{
    OVSDB_PID=`pidof ovsdb-server`
    OVSDB_DB=`cat /proc/$OVSDB_PID/cmdline | tr "\0" "\n" | grep conf.db`

    collect_cmd ovsdb-client dump
    collect_cmd ovsdb-client -f json dump
    collect_cmd ovsdb-tool -mm show-log $OVSDB_DB
}

collect_ovsdb
