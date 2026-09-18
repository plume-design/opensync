#!/bin/sh
#
# Collect info about qdiscs and classes
#
. "$LOGPULL_LIB"

collect_qdisc_dump()
{
    for raw_name in $(ip -o link show | awk -F': ' '{print $2}'); do
        # Some interfaces have '@' in the name, remove it and everything after it
        local ifname="${raw_name%@*}"

        echo "################## Interface $ifname ##################"

        echo "qdiscs:"
        tc -s -d qdisc show dev "$ifname"

        echo "filters:"
        tc -s -d filter show dev "$ifname"
        tc -s -d filter show dev "$ifname" root
        tc -s -d filter show dev "$ifname" egress
        tc -s -d filter show dev "$ifname" ingress
        # Ingress filter before clsact existed
        tc -s -d filter show dev "$ifname" parent ffff:

        echo "classes:"
        tc -s -d class show dev "$ifname"
        tc -s -d class show dev "$ifname" root

        echo ""
        echo ""
    done
}

collect_qdisc_dump > "$LOGPULL_TMP_DIR/qdisc_dump"
