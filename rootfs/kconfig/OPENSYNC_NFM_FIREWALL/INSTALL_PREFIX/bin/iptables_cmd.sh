#!/bin/sh -e

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

MANAGER_WANO_IFACE_LIST="{{ CONFIG_MANAGER_WANO_IFACE_LIST }}"

log()
{
    echo "$@"
}

to_syslog()
{
    logger -s -t firewall 2>&1
}

iptables_boot()
{
    log "Setting default IPv4 policies"
    # Set default policies
    iptables -P INPUT DROP
    iptables -P FORWARD DROP
    # OUTPUT should be gradually moved to the DROP policy as we tighten the
    # security. For now leave them open, we don't want to lock pods out
    iptables -P OUTPUT ACCEPT

    # Flush all other rules
    iptables -F
    iptables -X
    iptables -t nat -F
    iptables -t nat -X
    iptables -t mangle -F
    iptables -t mangle -X

    log "Installing emergency rules"
    iptables -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT
    iptables -A FORWARD -m state --state RELATED,ESTABLISHED -j ACCEPT
    iptables -A OUTPUT -m state --state RELATED,ESTABLISHED -j ACCEPT

    # Always enable the local interface
    iptables -A INPUT -i lo -j ACCEPT
    log "Enabling management network interfaces '$MANAGER_WANO_IFACE_LIST' unconditionally"
    for iface in $MANAGER_WANO_IFACE_LIST
    do
        iptables -A INPUT -i "${iface}.4" -j ACCEPT
    done

    log "Enabling ICMP protocol on all interfaces, blocking timestamp request and reply"
    iptables -A INPUT -p icmp --icmp-type timestamp-request -j DROP
    iptables -A INPUT -p icmp -j ACCEPT
    iptables -A OUTPUT -p icmp --icmp-type timestamp-reply -j DROP

    log "Explicitly allow DHCP responses"
    iptables -A INPUT -p udp --sport 67 -j ACCEPT
}

ip6tables_boot()
{
    log "Setting default IPv6 policies"
    # Set default policies
    ip6tables -P INPUT DROP
    ip6tables -P FORWARD DROP
    ip6tables -P OUTPUT ACCEPT

    # Flush all other rules
    ip6tables -F
    ip6tables -X
    ip6tables -t mangle -F
    ip6tables -t mangle -X

    log "Installing emergency rules"
    ip6tables -A INPUT   -m state --state RELATED,ESTABLISHED -j ACCEPT
    ip6tables -A FORWARD -m state --state RELATED,ESTABLISHED -j ACCEPT
    ip6tables -A OUTPUT  -m state --state RELATED,ESTABLISHED -j ACCEPT

    # Always enable the local interface
    ip6tables -A INPUT -i lo -j ACCEPT
    log "Enabling management network interfaces '$MANAGER_WANO_IFACE_LIST' unconditionally"
    for iface in $MANAGER_WANO_IFACE_LIST
    do
        ip6tables -A INPUT -i "${iface}.4" -j ACCEPT
    done

    log "Enabling ICMPv6 protocol on all interfaces"
    ip6tables -A INPUT -p icmpv6 --icmpv6-type router-advertisement -m limit --limit 20/min --limit-burst 5 -j ACCEPT
    ip6tables -A INPUT -p icmpv6 --icmpv6-type router-advertisement -j DROP
    ip6tables -A INPUT -p icmpv6 -j ACCEPT

    log "Enabling DHCPv6 protocol on all interfaces"
    ip6tables -A INPUT -m state --state NEW -m udp -p udp --dport 546 -d fe80::/64 -j ACCEPT
}

case "$1" in
    "boot")
        # Initialize iptables, set default policies etc. Must be called once per boot.
        iptables_boot 2>&1 | to_syslog
        ip6tables_boot 2>&1 | to_syslog
        ;;

    *)
        log "Unknown command: $@"
        exit 1
        ;;
esac
