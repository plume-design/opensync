#!/bin/sh

vlan_log()
{
    logger -st vlan "$@"
}

vlan_ifname()
{
    echo "$1.$2"
}

vlan_add()
{
    [ -d "/sys/class/net/$1.$2" ] && return 0
    vlan_log "Adding VLAN interface $1.$2 usig vconfig"
    vconfig set_name_type DEV_PLUS_VID_NO_PAD
    vconfig add "$1" "$2"
}

vlan_del()
{
    [ ! -d "/sys/class/net/$1.$2" ] && return 0
    vlan_log "Removing VLAN interface $1.$2 using vconfig"
    vconfig rem "$1.$2"
}
