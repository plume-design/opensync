#!/bin/sh -e
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
