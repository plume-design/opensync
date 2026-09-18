#!/bin/sh
# {# jinja-parse #}

. "$LOGPULL_LIB"

BACKEND_STRONGSWAN="{{CONFIG_OSN_BACKEND_IPSEC_LINUX_STRONGSWAN}}"

collect_ipsec()
{
    if [ -n "$BACKEND_STRONGSWAN" ]; then
        collect_cmd ipsec statusall
        collect_file /etc/ipsec.conf
    fi
}

collect_ipsec

