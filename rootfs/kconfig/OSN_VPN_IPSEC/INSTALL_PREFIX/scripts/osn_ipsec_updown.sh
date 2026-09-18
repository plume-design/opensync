#!/bin/sh
# {# jinja-parse #}
#
# This script is currently used for 2 things:
#  - To implement event-driven ipsec tunnel status monitoring.
#    It works via the charon updown plugin.
#  - For the parameters that are difficult (if not impossible) to parse
#    from "ipsec status" or "ipsec statusall", for instance virtual IPs assigned.
#    This script extracts such parameters from PLUTO_xyz vars and
#    writes them to a dedicated directory/file available to OpenSync.
#

OSN_IPSEC_STATUS_DIR="{{CONFIG_OSN_IPSEC_TMPFS_STATUS_DIR}}"

OSN_IPSEC_STATUS_FILE="${OSN_IPSEC_STATUS_DIR}/${PLUTO_CONNECTION}"

DISABLE_POLICY_LIST="{{CONFIG_OSN_IPSEC_DISABLE_POLICY_IFACE_LIST}}"

VIRT_IP_MAX=8

# get all assigned IPv4 virtual IPs into variable VIRT_IP4
get_virt_ips()
{
    i=1
    VIRT_IP4=""
    while [ $i -le $VIRT_IP_MAX ]; do
        eval IP="\$PLUTO_MY_SOURCEIP4_$i"

        if [ -z "$IP" ]; then
            break;
        fi

        [ $i -gt 1 ] && VIRT_IP4="${VIRT_IP4} "
        VIRT_IP4="${VIRT_IP4}${IP}"
        let "i=$i+1"
    done
}

# In order to use an IPsec route-based tunnel effectively, we need to make
# some /proc/sys settings
set_proc_sys_route_based()
{
    # Disable crypto transformations on the physical interface:
    echo '1' > /proc/sys/net/ipv4/conf/${PLUTO_INTERFACE}/disable_xfrm

    # Disable IPsec policy (SPD) for the physical interface
    echo '1' > /proc/sys/net/ipv4/conf/${PLUTO_INTERFACE}/disable_policy

    # Disable IPsec policy for any system interfaces statically defined
    # for the platform that need this setting:
    for IFACE in ${DISABLE_POLICY_LIST}; do
        echo '1' > /proc/sys/net/ipv4/conf/${IFACE}/disable_policy
    done
}

mkdir -p "${OSN_IPSEC_STATUS_DIR}"

case "${PLUTO_VERB}" in
    up-client)
        get_virt_ips

        if [ -n "$VIRT_IP4" ]; then
            echo "VIRT_IP4 $VIRT_IP4" > "${OSN_IPSEC_STATUS_FILE}"
        else
            echo "" > "${OSN_IPSEC_STATUS_FILE}"
        fi

        # Apply /proc/sys tweaks for route-based tunnels:
        #
        # Note: We currently don't have any specific info whether this
        # particular tunnel is policy-based or route-based. However if
        # a mark is set on the tunnel we can fairly assume this is a
        # route-based option and in that case we need to set:

        if [ -n "${PLUTO_MARK_IN}" -o -n "${PLUTO_MARK_OUT}" ]; then
            set_proc_sys_route_based
        fi

        ;;

    down-client)
        rm -f "${OSN_IPSEC_STATUS_FILE}"
        ;;
esac
