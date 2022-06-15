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

# {# jinja-parse #}
#
# This script is currently used for 2 things:
#  - To implement event-driven ipsec tunnel status monitoring.
#    It works via the charon updown plugin.
#  - For the parameters that are difficult (if not impossible) to parse
#    from "ipsec status" or "ipsec statusall", for instance virtual IPs assigned.
#    This script extracts such parameters from PLUTO_xyz vars and
#    writes them to a dedicated directory/file available to OpenSync.
#

OSN_IPSEC_STATUS_DIR="{{CONFIG_OSN_IPSEC_TMPFS_STATUS_DIR}}"

OSN_IPSEC_STATUS_FILE="${OSN_IPSEC_STATUS_DIR}/${PLUTO_CONNECTION}"

DISABLE_POLICY_LIST="{{CONFIG_OSN_IPSEC_DISABLE_POLICY_IFACE_LIST}}"

VIRT_IP_MAX=8

# get all assigned IPv4 virtual IPs into variable VIRT_IP4
get_virt_ips()
{
    i=1
    VIRT_IP4=""
    while [ $i -le $VIRT_IP_MAX ]; do
        eval IP="\$PLUTO_MY_SOURCEIP4_$i"

        if [ -z "$IP" ]; then
            break;
        fi

        [ $i -gt 1 ] && VIRT_IP4="${VIRT_IP4} "
        VIRT_IP4="${VIRT_IP4}${IP}"
        let "i=$i+1"
    done
}

# In order to use an IPsec route-based tunnel effectively, we need to make
# some /proc/sys settings
set_proc_sys_route_based()
{
    # Disable crypto transformations on the physical interface:
    echo '1' > /proc/sys/net/ipv4/conf/${PLUTO_INTERFACE}/disable_xfrm

    # Disable IPsec policy (SPD) for the physical interface
    echo '1' > /proc/sys/net/ipv4/conf/${PLUTO_INTERFACE}/disable_policy

    # Disable IPsec policy for any system interfaces statically defined
    # for the platform that need this setting:
    for IFACE in ${DISABLE_POLICY_LIST}; do
        echo '1' > /proc/sys/net/ipv4/conf/${IFACE}/disable_policy
    done
}

mkdir -p "${OSN_IPSEC_STATUS_DIR}"

case "${PLUTO_VERB}" in
    up-client)
        get_virt_ips

        if [ -n "$VIRT_IP4" ]; then
            echo "VIRT_IP4 $VIRT_IP4" > "${OSN_IPSEC_STATUS_FILE}"
        else
            echo "" > "${OSN_IPSEC_STATUS_FILE}"
        fi

        # Apply /proc/sys tweaks for route-based tunnels:
        #
        # Note: We currently don't have any specific info whether this
        # particular tunnel is policy-based or route-based. However if
        # a mark is set on the tunnel we can fairly assume this is a
        # route-based option and in that case we need to set:

        if [ -n "${PLUTO_MARK_IN}" -o -n "${PLUTO_MARK_OUT}" ]; then
            set_proc_sys_route_based
        fi

        ;;

    down-client)
        rm -f "${OSN_IPSEC_STATUS_FILE}"
        ;;
esac
