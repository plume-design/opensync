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

mac_set_local_bit()
{
    local MAC="$1"

    # ${MAC%%:*} - first digit in MAC address
    # ${MAC#*:} - MAC without first digit
    printf "%02X:%s" $(( 0x${MAC%%:*} | 0x2 )) "${MAC#*:}"
}

mac_get()
{
    ifconfig "$1" | grep -o -E '([A-F0-9]{2}:){5}[A-F0-9]{2}'
}

{%- if CONFIG_TARGET_LAN_SET_LOCAL_MAC_BIT %}
ETH_BRIDGE_MAC=$(mac_set_local_bit $(mac_get {{CONFIG_TARGET_ETH_FOR_LAN_BRIDGE}}))
{%- else %}
ETH_BRIDGE_MAC=$(mac_get {{ CONFIG_TARGET_ETH_FOR_LAN_BRIDGE }})
{%- endif %}

echo "Setting up native LAN bridge with MAC address $ETH_BRIDGE_MAC"
brctl addbr {{ CONFIG_TARGET_LAN_BRIDGE_NAME }}
ip link set {{ CONFIG_TARGET_LAN_BRIDGE_NAME }} address "$ETH_BRIDGE_MAC"
ip link set dev {{ CONFIG_TARGET_LAN_BRIDGE_NAME }} up
echo "Enabling bridge netfilter on {{ CONFIG_TARGET_LAN_BRIDGE_NAME }}"
echo 1 > /sys/devices/virtual/net/{{ CONFIG_TARGET_LAN_BRIDGE_NAME }}/bridge/nf_call_iptables
echo 1 > /sys/devices/virtual/net/{{ CONFIG_TARGET_LAN_BRIDGE_NAME }}/bridge/nf_call_ip6tables
