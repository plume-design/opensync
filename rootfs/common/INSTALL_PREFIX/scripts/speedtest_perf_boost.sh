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

OVSH="{{INSTALL_PREFIX}}/tools/ovsh"

SPEEDTEST_APPS="iperf|ookla"
SPEEDTEST_CONNECTIONS="/sys/kernel/debug/lhost_accel/connections"
if [ ! -e "$SPEEDTEST_CONNECTIONS"  ]; then
    echo "$SPEEDTEST_CONNECTIONS does not exist."
    exit 1
fi

UPLINK_IF="$($OVSH s Connection_Manager_Uplink -w is_used==true if_name --raw)"
XMIT_IF=$(ip route | grep default | awk '{for (i=1; i<NF; i++) if ($i == "dev") print $(i+1)}')
ENABLE_GRE="false"

function is_mac() {
    case "$1" in
        ([0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]) return 0 ;;
        (*) return 1 ;;
    esac
}

function setup() {
    if [ -z "$UPLINK_IF" ]; then
        # If UPLINK_IF wasn't parsed correctly, we can't do anything for upload traffic
        echo "Uplink interface not found"
        return 0
    fi

    echo "Uplink interface is '$UPLINK_IF'"

    # Only allow uplink acceleration for specific interfaces
    UPLINK_IF_TYPE="$($OVSH s Connection_Manager_Uplink -w is_used==true -w if_name==$UPLINK_IF --raw if_type)"
    case "$UPLINK_IF_TYPE" in
        eth|gre|vif)
            echo "Uplink interface type $UPLINK_IF_TYPE"
            ;;
        pppoe)
            # PPPoE uplink does not have a MAC header, hence no MAC addresses needed
            echo "PPPoE uplink interface $UPLINK_IF_TYPE"
            echo "$UPLINK_IF" > /sys/kernel/debug/lhost_accel/uplink_iface
            echo "" > /sys/kernel/debug/lhost_accel/gre_iface
            return 0
            ;;
        *)
            echo "Not accelerating traffic for uplink interface type $UPLINK_IF_TYPE"
            return 0
            ;;
    esac

    if [ -z "$XMIT_IF" ]; then
        # If XMIT_IF wasn't parsed correctly, we can't accelerate L2 traffic
        echo "Unable to obtain uplink interface, skipping upload acceleration"
        return 0
    fi

    # Only enable GRE encapsulation on platforms where HW acceleration cannot
    # accelerate packets which are already in Linux network stack (QCA, ..)
    if ip -d link show $UPLINK_IF | grep -q gretap; then
        if lsmod | egrep -q 'qca_nss_ppe|qca_nss_drv'; then
            ENABLE_GRE="true"
        fi
    fi

    # Source MAC
    local SOURCE_MAC="$(ip a show $XMIT_IF | awk '/ether/ { print $2 }')"
    echo "src $SOURCE_MAC" > /sys/kernel/debug/lhost_accel/mac_address

    # Destination MAC
    local DEFAULT_GATEWAY="$(ip route | grep default | awk '{ print $3 }')"
    local NEXTHOP_MAC="$(ip neigh show nud reachable | grep -F "$DEFAULT_GATEWAY" | awk '{ print $5 }')"
    if ! is_mac "$NEXTHOP_MAC"; then
        echo "Unable to obtain nexthop MAC address, skipping upload acceleration"
        return 0
    fi
    echo "dst $NEXTHOP_MAC" > /sys/kernel/debug/lhost_accel/mac_address

    if [ "$ENABLE_GRE" = "true" ]; then
        echo "GRE encapsulation is enabled"

        # Uplink GRE
        UPLINK_STA=$(ip -d link show $UPLINK_IF | grep qdisc | tr '@' ' ' | tr ':' ' ' | awk '{ print $3 }')
        echo "Uplink interface is $UPLINK_STA, GRE interface is $UPLINK_IF"
        echo "$UPLINK_STA" > /sys/kernel/debug/lhost_accel/uplink_iface
        echo "$UPLINK_IF" > /sys/kernel/debug/lhost_accel/gre_iface

        # GRE source MAC
        local GRE_SOURCE_MAC="$(ip a show $UPLINK_STA | awk '/ether/ { print $2 }')"
        echo "gre_src $GRE_SOURCE_MAC" > /sys/kernel/debug/lhost_accel/mac_address

        # GRE destination MAC
        local GRE_DEST_IP="$(ip -d link show $UPLINK_IF | grep gretap | awk '{ print $3 }')"
        local GRE_DEST_MAC="$(ip neigh show nud reachable | grep -F "$GRE_DEST_IP " | awk '{ print $5 }')"
        echo "GRE destination IP: $GRE_DEST_IP, destination MAC: $GRE_DEST_MAC"
        echo "gre_dst $GRE_DEST_MAC" > /sys/kernel/debug/lhost_accel/mac_address
    else
        echo "$UPLINK_IF" > /sys/kernel/debug/lhost_accel/uplink_iface
        echo "" > /sys/kernel/debug/lhost_accel/gre_iface
    fi
}

function cleanup {
    echo "Clearing any remaining speedtest connections"
    echo "" > "$SPEEDTEST_CONNECTIONS"

    # Tell kernel module to drop any interface references
    echo "" > /sys/kernel/debug/lhost_accel/uplink_iface
    echo "" > /sys/kernel/debug/lhost_accel/gre_iface

    exit
}
trap cleanup SIGTERM
trap cleanup SIGINT

awk_script='{
    # Column $4 is local ip:port, column $5 is remote ip:port
    sub(/:[^:]+$/, ":12345", $4)  # Replace everything after the last colon in $4 with ":12345"
    print $5 " -> " $4            # Print the modified first column and the second column as is
}'

old_connections=""
function netstat_loop {
    # Clear any leftover connections from before (just in case)
    echo "" > "$SPEEDTEST_CONNECTIONS"

    ITER=0
    # Limit this script to max 70 seconds (speedtest will never run that long)
    while [ "$ITER" -lt 70 ]; do
        ITER=$((ITER + 1))

        sleep 1

        # Get speedtest/iperf open TCP/UDP connections using netstat
        local new_connections="$(netstat -u -t -enWp | grep -iE "$SPEEDTEST_APPS" | awk "$awk_script" | uniq)"

        # Check if connections changed
        if [[ "$new_connections" == "$old_connections" ]]; then
            continue;
        fi
        old_connections="$new_connections"

        # Push changed connections to kernel module
        echo "Connections changed!"
        echo "$new_connections"
        echo "$new_connections" > "$SPEEDTEST_CONNECTIONS"
    done
}

setup
netstat_loop
