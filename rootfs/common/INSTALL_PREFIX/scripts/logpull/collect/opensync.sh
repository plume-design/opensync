#!/bin/sh
#
# Collect common OpenSync info
#
. "$LOGPULL_LIB"

collect_osync()
{
    collect_cmd  $CONFIG_INSTALL_PREFIX/bin/dm --show-info
    collect_file $CONFIG_INSTALL_PREFIX/etc/kconfig
    if [ -e $CONFIG_INSTALL_PREFIX/.version ]; then
        collect_file $CONFIG_INSTALL_PREFIX/.version
    fi
    if [ -e $CONFIG_INSTALL_PREFIX/.versions ]; then
        collect_file $CONFIG_INSTALL_PREFIX/.versions
    fi
    # Collect quilt patch series info file which should be added during SDK buildtime
    if [ -e /etc/quilt-series.txt ]; then
        collect_file /etc/quilt-series.txt
    fi
}

collect_owm()
{
    # This will put dbg events in syslog
    killall -s SIGUSR1 owm
    sleep 1
    eval "$($CONFIG_INSTALL_PREFIX/bin/owm export sh)"
    collect_file $OSW_DIAG_DBG_FILE && rm -f $OSW_DIAG_DBG_FILE
}

collect_osync
collect_owm
