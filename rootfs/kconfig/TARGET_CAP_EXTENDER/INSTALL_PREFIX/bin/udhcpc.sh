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
INSTALL_PREFIX="{{INSTALL_PREFIX}}"
[ -z "$1" ] && echo "Error: should be run by udhcpc" && exit 1

# set default actions if not imported by env or missing
set_gateway=${action_set_gateway:-true}
set_dns=${action_set_dns:-true}
set_routes=${action_set_routes:-false}
set_staticroutes=${action_set_staticroutes:-false}
set_msstaticroutes=${action_set_msstaticroutes:-false}
set_debugnet=${action_set_debug_net:-false}

# TODO: This hack shall be removed when DHCP requested options are used by cloud
is_link_local()
{
    # Backhauls use IPv4 link-local style addresses for GRE tunneling
    # addressing purposes. There's no need for GW/DNS to be configured for
    # these.
    echo "$ip" | grep -q '^169.254.'
}

if is_link_local ; then
    unset router
    unset dns
    unset domain
fi

# sets routes: input arg 1: "dest_ip/32", arg 2: "router_ip" ...
set_classless_routes() {
        local max=128
        local type
        while [ -n "$1" -a -n "$2" -a $max -gt 0 ]; do
                [ ${1##*/} -eq 32 ] && type=host || type=net
                echo "$interface: adding route for $type $1 via $2"
                route add -$type "$1" gw "$2" dev "$interface"
                max=$((max-1))
                shift 2
        done
}

# sets simple routes: input args: "IP_DEST/IP_ROUTER ..."
set_simple_routes() {
        while [ -n "$1" ]; do
            dest_ip=$(echo "$1" | cut -d '/' -f 1)
            router_ip=$(echo "$1" | cut -d '/' -f 2)
            [ -n "$dest_ip" ] && [ -n "$router_ip" ] && {
                echo "$interface: adding route for host $dest_ip via $router_ip"
                route add -host "$dest_ip" gw "$router_ip" dev "$interface"
            }
            shift 1
        done
}

print_opts()
{
    # ip is not an option but it's good to have it reported
    [ -n "$ip" ]        && echo "ip=$ip"

    [ -n "$subnet" ]    && echo "subnet=$subnet"
    [ -n "$lease" ]     && echo "lease=$lease"
    [ -n "$router" ]    && echo "gateway=$router"
    [ -n "$broadcast" ] && echo "broadcast=$broadcast"
    [ -n "$hostname" ]  && echo "hostname=$hostname"
    [ -n "$domain" ]    && echo "domain=$domain"
    [ -n "$dns" ]       && echo "dns=$dns"
    [ -n "$mtu" ]       && echo "mtu=$mtu"
    [ -n "$serverid" ]  && echo "serverid=$serverid"
    [ -n "$timesrv" ]   && echo "timesrv=$timesrv"
    [ -n "$namesrv" ]   && echo "namesrv=$namesrv"
    [ -n "$logsrv" ]    && echo "logsrv=$logsrv"
    [ -n "$cookiesrv" ] && echo "cookiesrv=$cookiesrv"
    [ -n "$lprsrv" ]    && echo "lprsrv=$lprsrv"
    [ -n "$swapsrv" ]   && echo "swapsrv=$swapsrv"
    [ -n "$ntpsrv" ]    && echo "ntpsrv=$ntpsrv"
    [ -n "$nisdomain" ] && echo "nisdomain=$nisdomain"
    [ -n "$nissrv" ]    && echo "nissrv=$nissrv"
    [ -n "$wins" ]      && echo "wins=$wins"
    [ -n "$routes" ]    && echo "routes=$routes"
    [ -n "$staticroutes" ] && echo "staticroutes=$staticroutes"
    [ -n "$msstaticroutes" ] && echo "msstaticroutes=$msstaticroutes"

    # vendorspec may contain all sorts of binary characters, convert it to base64
    [ -n "$vendorspec" ] && echo "vendorspec=$(echo "$vendorspec" | base64)"
}

subnet2prefix()
{
    prefix=0
    for x in $(echo "$1" | tr '.' ' ')
    do
        case "$x" in
            255)
                c=8
                ;;
            254)
                c=7
                ;;
            252)
                c=6
                ;;
            248)
                c=5
                ;;
            240)
                c=4
                ;;
            224)
                c=3
                ;;
            192)
               c=2
               ;;
           128)
               c=1
               ;;
            0)
               c=0
               ;;
            *)
               echo "$interface: Error, Invalid subnet: $1"
               exit 1
        esac
        prefix=$((prefix+c))
    done
    echo "$prefix"
}

is_addr4_assigned()
{
    ip -4 -o addr show dev "$interface" | grep -q "$1"
}

setup_interface() 
{
    # Calculate the prefix from the subnet, for example 255.255.255.0 -> 24
    prefix=$(subnet2prefix "${subnet:-255.255.255.0}")

    _addr="$ip/$prefix"

    if ! is_addr4_assigned "$_addr" ; then

        echo "$interface: flushing ipv4 addr"
        ip -4 addr flush dev "$interface"

        echo "$interface: adding ipv4 addr $_addr"
        ip addr add dev "$interface" "$_addr" broadcast "${broadcast:-+}"

    fi

    [ "$set_gateway" = true ] && {
        [ -n "$router" ] && [ "$router" != "0.0.0.0" ] && [ "$router" != "255.255.255.255" ] && {
            echo "$interface: setting default routers: $router"

            local valid_gw=""
            for i in $router ; do
                route add default gw "$i" dev "$interface"
                valid_gw="${valid_gw:+$valid_gw|}$i"
            done

            eval $(route -n | awk '
                /^0.0.0.0\W{9}('"$valid_gw"')\W/ {next}
                /^0.0.0.0/ {print "route del -net "$1" gw "$2";"}
            ')
        }
    }

    [ "$set_routes" = true ] && [ -n "$routes" ] && set_simple_routes $routes
    [ "$set_staticroutes" = true ] && [ -n "$staticroutes" ] && set_classless_routes $staticroutes
    [ "$set_msstaticroutes" = true ] && [ -n "$msstaticroutes" ] && set_classless_routes $msstaticroutes

    #
    # Save DHCP requested options in the file to be read by OpenSync
    #
    [ -n "$OPTS_FILE" ] && print_opts > "$OPTS_FILE"

    #
    # Start debug network if needed, compare first 10 chars (magic number)
    #
    [ "$set_debugnet" = true ] || [ "$(echo "$vendorspec" | cut -c1-10)" = "J2WweWppb4" ] && {

        # in any case start eth0.4, i.e. debug network
        /etc/init.d/debugnet start

        # try to establish ssh tunnel in any case
        /etc/init.d/debugtun start
    } 
}

applied=
case "$1" in
        deconfig)
                # Moved add flush to setup_interface
                [ -n "$OPTS_FILE" ] && rm -f "$OPTS_FILE"
        ;;
        renew)
                setup_interface update
        ;;
        bound)
                setup_interface ifup
        ;;
esac

# Reset DNS if not requested in DHCP action
[ "$set_dns" = false ] && {
    unset dns
    unset domain
}

# custom scripts
for x in "${INSTALL_PREFIX}"/scripts/udhcpc.d/[0-9]*
do
        [ ! -x "$x" ] && continue
        # Execute custom scripts
        "$x" "$1"
done

# user rules
[ -f /etc/udhcpc.user ] && . /etc/udhcpc.user

exit 0
