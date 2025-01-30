#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


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
