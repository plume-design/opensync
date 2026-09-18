#!/bin/sh
#
# Collect ospkg status
#
. "$LOGPULL_LIB"

collect_ospkg_status()
{
    collect_cmd $CONFIG_INSTALL_PREFIX/tools/ospkg status -v
}

collect_ospkg_status

