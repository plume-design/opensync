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

UPNP_BIN="/usr/sbin/miniupnpd"
UPNP_DIR="/tmp/miniupnpd"
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

    log "Installing permanent rules"
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

    # NM MSS clamping jump rule
    iptables -t filter -N NM_MSS_CLAMP
    iptables -t filter -A FORWARD -j NM_MSS_CLAMP

    # NFM jump rules
    log "Installing IPv4 NFM jump rules"
    for CHAIN in INPUT OUTPUT FORWARD
    do
        iptables -t filter -N "NFM_$CHAIN"
        iptables -t filter -A "$CHAIN" -j "NFM_$CHAIN"
    done

    for CHAIN in OUTPUT PREROUTING POSTROUTING
    do
        iptables -t nat -N "NFM_$CHAIN"
        iptables -t nat -A "$CHAIN" -j "NFM_$CHAIN"
    done

    for CHAIN in INPUT OUTPUT FORWARD PREROUTING POSTROUTING
    do
        iptables -t mangle -N "NFM_$CHAIN"
        iptables -t mangle -A "$CHAIN" -j "NFM_$CHAIN"
    done

    log "Enabling ICMP protocol on all interfaces, blocking timestamp request and reply"
    iptables -A INPUT -p icmp --icmp-type timestamp-request -j DROP
    iptables -A INPUT -p icmp -j ACCEPT
    iptables -A OUTPUT -p icmp --icmp-type timestamp-reply -j DROP

    log "Adding NM wavering chains"

    # NM filter chains
    iptables -t filter -N NM_INPUT
    iptables -t filter -A INPUT -j NM_INPUT

    # NM nat chains
    iptables -t nat -N NM_NAT
    iptables -t nat -A POSTROUTING -j NM_NAT

    log "Allow IGMP"
    iptables -A INPUT -p igmp -j ACCEPT
    log "Allow Multicast"
    iptables -A FORWARD -p udp -d 224.0.0.0/4 -j ACCEPT

    # NM forwarding chains
    iptables -t filter -N NM_FORWARD
    iptables -t filter -A FORWARD -j NM_FORWARD

    log "Adding MINIUPNPD chains"
    iptables -t filter -N MINIUPNPD
    iptables -t nat -N MINIUPNPD

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

    log "Installing permanent rules"
    ip6tables -A INPUT   -m state --state RELATED,ESTABLISHED -j ACCEPT
    ip6tables -A FORWARD -m state --state RELATED,ESTABLISHED -j ACCEPT
    ip6tables -A OUTPUT  -m state --state RELATED,ESTABLISHED -j ACCEPT

    # NM MSS clamping jump rule
    ip6tables -t filter -N NM_MSS_CLAMP
    ip6tables -t filter -A FORWARD -j NM_MSS_CLAMP

    # NFM jump rules
    log "Installing IPv6 NFM jump rules"
    for CHAIN in INPUT OUTPUT FORWARD
    do
        ip6tables -t filter -N "NFM_$CHAIN"
        ip6tables -t filter -A "$CHAIN" -j "NFM_$CHAIN"
    done

    for CHAIN in OUTPUT PREROUTING POSTROUTING
    do
        ip6tables -t nat -N "NFM_$CHAIN"
        ip6tables -t nat -A "$CHAIN" -j "NFM_$CHAIN"
    done

    for CHAIN in INPUT OUTPUT FORWARD PREROUTING POSTROUTING
    do
        ip6tables -t mangle -N "NFM_$CHAIN"
        ip6tables -t mangle -A "$CHAIN" -j "NFM_$CHAIN"
    done

    log "Enabling ICMP protocol on all interfaces"
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

    log "Adding NM wavering chains"

    # NM allow input on interfaces
    ip6tables -t filter -N NM_INPUT
    ip6tables -t filter -A INPUT -j NM_INPUT

    # NM forwarding chains
    ip6tables -t filter -N NM_FORWARD
    ip6tables -t filter -A FORWARD -j NM_FORWARD

    # no NAT on IPv6

    log "Adding MINIUPNPD chains"
    ip6tables -t filter -N MINIUPNPD
}

#
# - Clear all wavering rules
# - Stop the upnp daemon and flush its rules
#
iptables_flush()
{
    # Flush out NM rules
    log "Flushing NM rules: NM_NAT NM_FORWARD NM_PORT_FORWARD NM_INPUT NM_MSS_CLAMP"
    iptables  -t nat    -F NM_NAT
    iptables  -t nat    -F NM_PORT_FORWARD
    iptables  -t filter -F NM_PORT_FORWARD
    ip6tables -t filter -F NM_INPUT
    ip6tables -t filter -F NM_FORWARD
    iptables  -t filter -F NM_MSS_CLAMP
    ip6tables -t filter -F NM_MSS_CLAMP

    # Stop miniupnpd daemon
    upnpd_stop

    log "Flushing MINIUPNPND rules"
    iptables  -t filter -F MINIUPNPD
    ip6tables -t filter -F MINIUPNPD
    iptables  -t nat    -F MINIUPNPD
}

#
# Flag the interface as being on LAN -- we should enable all ports in such case to allow
# DHCP and SSH to go through
#
iptables_lan()
{
    local ifname="$1"   # LAN interface

    [ -z "$ifname" ] && {
        log "lan command requires an interface as argument"
        exit 1
    }

    echo "Enabling IPv4 LAN access on $ifname"

    # Accept all incoming packets
    iptables -A NM_INPUT -i "$ifname" -j ACCEPT
    # Accept forwarded packets from local interfaces
    iptables -A NM_FORWARD -i "$ifname" -j ACCEPT
}

ip6tables_lan()
{
    local ifname="$1"   # LAN interface

    [ -z "$ifname" ] && {
        log "lan command requires an interface as argument"
        exit 1
    }

    echo "Enabling IPv6 LAN access on $ifname"

    # Accept all incoming packets
    ip6tables -A NM_INPUT -i "$ifname" -j ACCEPT
    # Accept forwarded packets from local interfaces
    ip6tables -A NM_FORWARD -i "$ifname" -j ACCEPT
}

#
# Enable NAT on specific interface -- this is only for outgoing traffic
#
iptables_nat()
{
    local ifname="$1"   # WAN interface

    [ -z "$ifname" ] && {
        log "nat command requires an interface as argument"
        exit 1
    }

    echo "Enabling NAT on $ifname"

    iptables -t nat -A NM_NAT -o "$ifname" -j MASQUERADE
}

iptables_forward()
{
    local proto="$1"    # protocol "tcp" or "udp"
    local ifname="$2"   # Interface to perform port-forwarding on
    local sport="$3"    # local port (from the pods perspective)
    local dhost="$4"    # destination host:port
    local dport="$5"    # destination host:port

    [ -z "$proto" -o -z "$ifname" -o -z "$sport" -o -z "$dhost" -o -z "$dport" ] && {
        log "Not enough arguments: $@"
        exit 1
    }

    #
    # Sanity checks
    #
    case "$proto" in
        tcp|udp)
            ;;
        *)
            log "Invalid protocol: $proto"
            exit 2
            ;;
    esac

    echo "$ifname" | grep -q -E "^[a-z0-9.-]+$" || {
        log "Invalid interface: $ifname"
        exit 2
    }

    [ "$sport" -gt 1 -a "$sport" -lt 65536 ] || {
        log "Invalid source port: $sport"
        exit 2
    }

    [ "$dport" -gt 1 -a "$dport" -lt 65536 ] || {
        log "Invalid destination port: $dport"
        exit 2
    }

    echo "$dhost" | grep -q -E '^([0-9]{1,3}\.){3}[0-9]{1,3}' || {
        log "Invalid IP address: $dhost"
        exit 2
    }

    log "Adding port forward $proto $ifname:$sport -> $dhost:$dport"

    iptables -t nat -A NM_PORT_FORWARD -i "$ifname" -p "$proto" --dport "$sport" -j DNAT --to-destination "$dhost:$dport"
}

#
# Dump current firewall rules
#
iptables_dump()
{
    echo "========== iptables =========="
    for TABLE in filter nat
    do
        echo "===== $TABLE ====="
        iptables -t $TABLE --list -n -v | sed -e 's/	/        /g'
    done

    echo "========== ip6tables  =========="
    for TABLE in filter
    do
        echo "===== $TABLE ====="
        ip6tables -t $TABLE --list -n -v | sed -e 's/	/        /g'
    done
}

#
# miniupnpd support functions
#
upnpd_stop()
{
    start-stop-daemon -b -K -x "$UPNP_BIN"
}

upnpd_start()
{
    local ext_if=$1     # External interface
    local int_if=$2     # Internal interface

    [ -z "$1" -o -z "$2" ] && {
        log "UPnP requires 2 arguments"
        exit 1
    }

    [ -d "$UPNP_DIR" ] || mkdir -p "$UPNP_DIR" || {
        log "Unable to create temporary UPNP DIR: $UPNP_DIR"
        exit 1
    }

    touch "${UPNP_DIR}/miniupnpd.conf" || {
        log "Unable to create the UPnPD config file: ${UPNP_DIR}/miniupnpd.conf"
        exit 1
    }

    # Generate the miniupnpd config file
    cat > ${UPNP_DIR}/miniupnpd.conf <<-EOF
		ext_ifname=${ext_if}
		listening_ip=${int_if}
		enable_natpmp=yes
		enable_upnp=yes
		secure_mode=yes
		system_uptime=yes
		lease_file=${UPNP_DIR}/upnpd.leases
		allow 1024-65535 0.0.0.0/0 1024-65535
		deny 0-65535 0.0.0.0/0 0-65535
	EOF

    echo "Enabling UPnP on ext:$ext_if to int:$int_if"

    start-stop-daemon -b -S -x "$UPNP_BIN" -- -d -f "${UPNP_DIR}/miniupnpd.conf" || {
        log "Error starting miniupnpd"
        exit 1
    }
}

case "$1" in
    "boot")
        # Initialize iptables, set default policies etc. Must be called once per boot.
        iptables_boot 2>&1 | to_syslog
        ip6tables_boot 2>&1 | to_syslog
        ;;

    "flush")
        # Reset firewall and UPnP status
        iptables_flush 2>&1 | to_syslog
        ;;

    "lan")
        # Flag interface as lan (locally all input connection)
        iptables_lan "$2" 2>&1 | to_syslog
        ip6tables_lan "$2" 2>&1 | to_syslog
        ;;

    "nat")
        # Enable NAT on interface
        iptables_nat "$2" 2>&1 | to_syslog
        ;;

    "forward")
        # Forward a port
        iptables_forward "$2" "$3" "$4" "$5" "$6" 2>&1 | to_syslog
        ;;
    "upnp")
        # UPnP service
        upnpd_start "$2" "$3" 2>&1 | to_syslog
        ;;

    "dump")
        iptables_dump
        ;;
    *)
        log "Unknown command: $@"
        exit 1
        ;;
esac
