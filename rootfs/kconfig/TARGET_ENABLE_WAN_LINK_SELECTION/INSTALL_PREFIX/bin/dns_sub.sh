#!/bin/sh

#
# Support routines for assigning DNS entries for several devices.
# This module adds DNS support for scripts.
#

RESOLV_FILE="/tmp/resolv.conf"
DNS_TMP="/tmp/dns"

dns_reset()
{
    local iface="$1"; shift
    local resolv="${DNS_TMP}/${iface}.resolv"

    mkdir -p "${DNS_TMP}"

    # Touch temporary resolv file
    echo -n > "${resolv}.$$"
}

dns_add()
{
    local iface="$1" ; shift
    local resolv="${DNS_TMP}/${iface}.resolv"

    echo "$@" >> "${resolv}.$$"
}

# Filter input and show only unique lines
unique()
{
    awk '!($0 in uniqa) { print($0); uniqa[$0]=1 }'
}

dns_apply()
{
    local iface="$1" ; shift
    local resolv="${DNS_TMP}/${iface}.resolv"

    [ -e "${resolv}.$$" ] && {
        mv -f "${resolv}.$$" "${resolv}"
    }

    # Run all entries in the various resolv files through `unique` so duplicate
    # lines are filtered out. Some setups don't work well with repeated
    # nameserver entries, also sort files to make the order stable.
    find "${DNS_TMP}" -name '*.resolv' | sort | xargs cat | unique > "${RESOLV_FILE}.$$"

    # The timeouts need to be shorter than FSM resolving thread,
    # otherwise connectivity issues may be encountered
    echo "options timeout:3" >> "${RESOLV_FILE}.$$"

    # if there are no changes compared to existing config do nothing
    if cmp -s "${RESOLV_FILE}.$$" "${RESOLV_FILE}" 2>/dev/null; then
        rm -f "${RESOLV_FILE}.$$"
        return
    fi

    # Some services may set umask to 0077 for security reasons. This might make
    # the /tmp/resolv.conf file unreadable for dnsmasq on platforms where it is
    # running as a non-root user (BCM, for example). Force a permission change
    # below
    chmod 0644 "${RESOLV_FILE}.$$"
    mv "${RESOLV_FILE}.$$" "${RESOLV_FILE}"

}
