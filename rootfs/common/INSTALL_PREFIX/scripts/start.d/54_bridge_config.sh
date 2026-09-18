#!/bin/sh
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
