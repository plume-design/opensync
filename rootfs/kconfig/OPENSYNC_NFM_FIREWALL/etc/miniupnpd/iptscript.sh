#!/bin/sh
# {# jinja-parse #}

NF_PREFIX="mupnp"
OVSH="{{INSTALL_PREFIX}}/tools/ovsh"

init()
{
    # Remove all stale rules
    $OVSH -r s Netfilter name | grep -e "^$NF_PREFIX" | while read RID
    do
        $OVSH d Netfilter --where name=="$RID"
    done
}

add_forward_rule()
{
    proto="$1"
    ext_port="$2"
    int_addr="$3"
    int_port="$4"
    rem_host="$5"

    rid="${NF_PREFIX}_fwd_${proto}_${ext_port}"

    $OVSH U Netfilter --where name=="$rid" \
        enable:=true \
        protocol:=ipv4 \
        table:=nat \
        chain:=MINIUPNPD \
        target:=DNAT \
        rule:="-p $proto --dport $ext_port -m conntrack --ctstate NEW --to $int_addr:$int_port ${rem_host:+-s ${rem_host}}"
}

del_forward_rule()
{
    proto="$1"
    ext_port="$2"
    int_addr="$3"
    int_port="$4"
    rem_host="$5"

    rid="${NF_PREFIX}_fwd_${proto}_${ext_port}"

    $OVSH d Netfilter --where name=="$rid"
}

add_filter_rule()
{
    proto="$1"
    int_addr="$2"
    int_port="$3"
    rem_host="$4"

    rid="${NF_PREFIX}_fil_${proto}_${int_addr}:${int_port}"

    $OVSH U Netfilter --where name=="$rid" \
        enable:=true \
        protocol:=ipv4 \
        table:=filter \
        chain:=MINIUPNPD \
        target:=ACCEPT \
        rule:="-p $proto -d $int_addr --dport $int_port ${rem_host:+-s ${rem_host}}"
}

del_filter_rule()
{
    proto="$1"
    int_addr="$2"
    int_port="$3"
    rem_host="$4"

    rid="${NF_PREFIX}_fil_${proto}_${int_addr}:${int_port}"
    $OVSH d Netfilter --where name=="$rid"
}

case "$1" in
    init|fini)
        init
        ;;

    add_forward_rule)
        shift
        add_forward_rule "$@"
        ;;

    del_forward_rule)
        shift
        del_forward_rule "$@"
        ;;

    add_filter_rule)
        shift
        add_filter_rule "$@"
        ;;

    del_filter_rule)
        shift
        del_filter_rule "$@"
        ;;
    *)
        echo Bad options: "$@"
        ;;
esac

