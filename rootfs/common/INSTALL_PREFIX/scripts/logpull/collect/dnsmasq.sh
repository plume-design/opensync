#!/bin/sh
#
# Collect dnsmasq
#
. "$LOGPULL_LIB"

collect_dnsmasq()
{
    collect_file $CONFIG_OSN_DNSMASQ_ETC_PATH
    collect_file $CONFIG_OSN_DNSMASQ_LEASE_PATH
    collect_file /etc/resolv.conf
    collect_file /tmp/resolv.conf.auto
    collect_file /tmp/resolv.conf
}

collect_dnsmasq
