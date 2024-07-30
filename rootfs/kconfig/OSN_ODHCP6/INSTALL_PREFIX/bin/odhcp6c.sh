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
INSTALL_PREFIX={{INSTALL_PREFIX}}
LAN_INTF="{{CONFIG_TARGET_LAN_BRIDGE_NAME}}"
MODE_PREFIX_NO_ADDRESS={{CONFIG_OSN_ODHCP6_MODE_PREFIX_NO_ADDRESS}}

[ -z "$2" ] && echo "Error: should be run by odhcpc6c" && exit 1

. ${INSTALL_PREFIX}/bin/dns_sub.sh

OPTS_FILE=/var/run/odhcp6c_$1.opts
RAND_ADDR_FILE=/var/run/odhcp6c_$1.rand_addr
IP_UNNUMBERED_MODE_FILE=/var/run/odhcp6c_$1.ip_unnumbered

#
# Remove the "prefix" part from a `ip -6 route` command. For example:
#
#     "fe80::/64 dev br-home proto kernel metric 256"
#       returns
#     "dev br-home proto kernel metric 256"
#
#     "unreachable fd36:a253:1d8e::/48 dev lo proto static metric 2147483647 error -113"
#       returns
#     "dev lo proto static metric 2147483647 error -113"
#
ip6route_no_prefix()
{
    case "$1" in
        unreach*)
            shift
    esac
    shift

    echo "$@"
}

#
# Take a `ip -6 route` command and return the `SELECTOR` (ip -6 route help) part
# of the command line. In essence, this command takes the arguments and strips
# them according to a white list.
#
ip6route_get_select()
{
    # The first argument to an "ip route" command can be "unreachable" -- this
    # keyword cannot be used as a route selector
    case "$1" in
        unreach*)
            shift
    esac

    # The first argument is the route prefix
    sel="$1"
    shift

    while [ -n "$1" ]
    do
        case "$1" in
            via|table|dev|proto|type|scope)
                sel="${sel} $1 $2"
                shift
                ;;
        esac
        shift
    done

    echo "$sel"
}

#
# Map pairs of arguments to key=value pairs
#
ip6route_to_keyvalue()
{
    while [ -n "$1" ]
    do
        printf "%s=%s " "$1" "$2"
        shift 2 || break
    done
}

ip6route_check()
{
    sel=$(ip6route_get_select "$@")
    route=$(ip6route_no_prefix $(ip -6 route show $sel | head -1))

    # `ip -6 route SELECTION` will show only fields that are not already present
    # in SELECTION so pass both `sel` and `route` to ip6route_to_keyvalue().
    # Both the `sel` and `route` variables start with the prefix address, strip
    # it.
    COPT="$(ip6route_to_keyvalue $(ip6route_no_prefix $sel) $route)"

    for O in $(ip6route_to_keyvalue $(ip6route_no_prefix "$@"))
    do
        if ! echo "$COPT" | grep -qE "(^| )$O($| )"
        then
            return 1
        fi
    done
    return 0
}

# ignore route if env other_config_accept_ra_defrtr=false and route is default (::/0)
ip6route_check_accept_ra_defrtr()
{
    if [ "$other_config_accept_ra_defrtr" = "false" ]; then
        if [ "$1" = "::/0" ]; then
            return 0
        fi
    fi
    return 1
}

# Check if a similar route already exist, if it does, do nothing
ip6route_replace()
{
    ip6route_check "$@" && return 0
    ip6route_check_accept_ra_defrtr "$@" && return 0
    ip -6 route replace "$@"
}

update_resolv()
{
    # ignore DNS if env other_config_accept_ra_rdnss=false
    if [ "$other_config_accept_ra_rdnss" = "false" ]; then
        return 0
    fi
    dns_reset "$1_ipv6"
    [ -n "$2" ] && dns_add "$1_ipv6" "nameserver $2"
    dns_apply "$1_ipv6"
}

log_dhcp6_time_event()
{
    logger="${INSTALL_PREFIX}"/tools/telog
    if [ -x "$logger" ]; then
        $logger -n "telog" -c "DHCP6_CLIENT" -s "$1" -t "$2" "addr=${3:-null}"
    fi
}

#
# From the delegated prefix generate a random IPv6 address.
#
rand_addr_gen_from_prefix()
{
    local prefix=${1%::*}
    local prefix_len=$2

    local addr=$prefix
    [ $prefix_len -le 48 ] && addr="$addr:"

    local i=0
    while [ $i -lt 4 ]
    do
        local hex_16bit=$(cat /dev/urandom | tr -dc 0123456789abcdef | head -c 4)
        [ "$hex_16bit" == "0000" ] && continue

        addr="${addr}:${hex_16bit}"
        let "i=$i+1"
    done
    echo "${addr}"
}

#
# Generate a random address from the prefix and
# assign it to the interface.
#
rand_addr_assign_from_prefix()
{
    local device=$1
    local prefix=$2
    local rand_addr=""

    local paddr="${prefix%%,*}"
    local plen="${paddr#*/}"    # prefix length
    local pref="${paddr%/*}"    # prefix

    if [ -e "$RAND_ADDR_FILE" ]; then
        rand_addr=$(cat "$RAND_ADDR_FILE")
    else
        rand_addr=$(rand_addr_gen_from_prefix "$pref" "$plen")
        echo "$rand_addr" > $RAND_ADDR_FILE
    fi

    ip -6 addr replace "${rand_addr}/128" dev "$device"
    ip -6 neigh add proxy "$rand_addr" dev "$LAN_INTF"
}

#
# Generate a ::1/64 address from prefix
# and assign it to the LAN interface.
#
ip_unnumbered_assign_addr_from_prefix()
{
    local device=$1
    local prefix=$2
    local addr=""

    local paddr="${prefix%%,*}"
    local pref="${paddr%/*}"

    addr="${pref}1/64"

    ip -6 addr replace "$addr" dev "$LAN_INTF"
    ip link set up dev "$LAN_INTF"  # LAN interface may be down, bring it up

    # Mark the interface in IP unnumbered mode:
    touch "$IP_UNNUMBERED_MODE_FILE"
}

setup_interface()
{
    local device="$1"
    event_time="$(date -r $OPTS_FILE "+%m-%d-%Y %H:%M")"

    # Merge RA-DNS
    for radns in $RA_DNS; do
        local duplicate=0
        for dns in $RDNSS; do
            [ "$radns" = "$dns" ] && duplicate=1
        done
        [ "$duplicate" = 0 ] && RDNSS="$RDNSS $radns"
    done

    local dnspart=""
    for dns in $RDNSS; do
        if [ -z "$dnspart" ]; then
            dnspart="\"$dns\""
        else
            dnspart="$dnspart, \"$dns\""
        fi
    done

    update_resolv "$device" "$dns"

    local prefixpart=""
    for entry in $PREFIXES; do
        local addr="${entry%%,*}"
                entry="${entry#*,}"
                local preferred="${entry%%,*}"
                entry="${entry#*,}"
                local valid="${entry%%,*}"
                entry="${entry#*,}"
        [ "$entry" = "$valid" ] && entry=

        local class=""
        local excluded=""

        while [ -n "$entry" ]; do
            local key="${entry%%=*}"
                    entry="${entry#*=}"
            local val="${entry%%,*}"
                    entry="${entry#*,}"
            [ "$entry" = "$val" ] && entry=

            if [ "$key" = "class" ]; then
                class=", \"class\": $val"
            elif [ "$key" = "excluded" ]; then
                excluded=", \"excluded\": \"$val\""
            fi
        done

        local prefix="{\"address\": \"$addr\", \"preferred\": $preferred, \"valid\": $valid $class $excluded}"

        if [ -z "$prefixpart" ]; then
            prefixpart="$prefix"
        else
            prefixpart="$prefixpart, $prefix"
        fi

        # TODO: delete this somehow when the prefix disappears
        ip6route_replace unreachable "$addr" proto ra expires "$valid"
    done

    # Merge addresses
    for entry in $RA_ADDRESSES; do
        local duplicate=0
        local addr="${entry%%/*}"
        for dentry in $ADDRESSES; do
            local daddr="${dentry%%/*}"
            [ "$addr" = "$daddr" ] && duplicate=1
        done
        [ "$duplicate" = "0" ] && ADDRESSES="$ADDRESSES $entry"
    done

    for entry in $ADDRESSES; do
        local addr="${entry%%,*}"
        entry="${entry#*,}"
        local preferred="${entry%%,*}"
        entry="${entry#*,}"
        local valid="${entry%%,*}"

        ip -6 address replace "$addr" dev "$device" preferred_lft "$preferred" valid_lft "$valid"
        address=$addr
    done

    for entry in $RA_ROUTES; do
        local addr="${entry%%,*}"
        entry="${entry#*,}"
        local gw="${entry%%,*}"
        entry="${entry#*,}"
        local valid="${entry%%,*}"
        entry="${entry#*,}"
        local metric="${entry%%,*}"

        if [ -n "$gw" ]; then
            ip6route_replace "$addr" via "$gw" metric "$metric" dev "$device" proto ra expires "$valid"
        else
            ip6route_replace "$addr" metric "$metric" dev "$device" proto ra expires "$valid"
        fi

        for prefix in $PREFIXES; do
            local paddr="${prefix%%,*}"
            [ -n "$gw" ] && ip6route_replace "$addr" via "$gw" metric "$metric" dev "$device" from "$paddr" proto ra expires "$valid"
        done
    done

    # Apply hop-limit
    [ -n "$RA_HOPLIMIT" -a "$RA_HOPLIMIT" != "0" -a "/proc/sys/net/ipv6/conf/$device/hop_limit" ] && {
        echo "$RA_HOPLIMIT" > "/proc/sys/net/ipv6/conf/$device/hop_limit"
    }

    # Before exporting PREFIXES, filter out prefixes with invalid/expired lifetimes
    _PREFIXES=""
    for prefix in $PREFIXES
    do
        lft=${prefix#*,}
        [ "$lft" == "0,0" ] && continue
        _PREFIXES="${_PREFIXES}${_PREFIXES:+ }${prefix}"
    done
    PREFIXES="${_PREFIXES}"

    #
    # We didn't get any IA_NA, but got IA_PD: we could still
    # be able to establish connectivity:
    #
    if [ -z "$ADDRESSES" ] && [ -n "$PREFIXES" ]; then

        local prefix=$(echo "$PREFIXES" | cut -d ' ' -f 1)

        if [ "$MODE_PREFIX_NO_ADDRESS" == "RAND_ADDRESS" ]; then
            # Take (the first) prefix, generate a random address from it
            # and assign it to the interface to allow connectivity:
            rand_addr_assign_from_prefix "$device" "$prefix"

        elif [ "$MODE_PREFIX_NO_ADDRESS" == "IP_UNNUMBERED" ]; then
            # IP unnumbered: The interface "borrows" an IP address
            # from another interface.
            #
            # Take (the first) prefix, generate a ::1/64 address from it
            # and assign it to the LAN interface to allow connectivity:
            ip_unnumbered_assign_addr_from_prefix "$device" "$prefix"
        fi
    fi

    export > "${OPTS_FILE}.$$"
    mv "${OPTS_FILE}.$$" "${OPTS_FILE}"

    curr_time="$(date "+%m-%d-%Y %H:%M")"
    if [ "$2" != "ra-updated" ] || [ "$event_time" != "$curr_time" ]
    then
        log_dhcp6_time_event "$1" "$2" "$address"
    fi
}

teardown_interface()
{
    rm -f "$OPTS_FILE"
    local device="$1"
    ip -6 route flush dev "$device" proto ra
    ip -6 address flush dev "$device" scope global
    update_resolv "$device" ""
}

ip_unnumbered_teardown()
{
    local device=$1

    if [ -e "$IP_UNNUMBERED_MODE_FILE" ]; then
        ip -6 address flush dev "$LAN_INTF" scope global

        rm -f "$IP_UNNUMBERED_MODE_FILE"
    fi
}

(
    case "$2" in
        bound|rebound)
            teardown_interface "$1"
            setup_interface "$1" "$2"
        ;;
        informed|updated|ra-updated)
            setup_interface "$1" "$2"
        ;;
        stopped|unbound)
            teardown_interface "$1"
            ip_unnumbered_teardown "$1"
        ;;
        started)
            teardown_interface "$1"
            ip_unnumbered_teardown "$1"
        ;;
    esac

    # user rules
    [ -f /etc/odhcp6c.user ] && . /etc/odhcp6c.user
) 9>/tmp/odhcp6c.lock.$1
rm -f /tmp/odhcp6c.lock.$1
