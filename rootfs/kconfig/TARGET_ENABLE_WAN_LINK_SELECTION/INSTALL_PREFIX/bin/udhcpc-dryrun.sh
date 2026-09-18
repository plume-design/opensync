#!/bin/sh
[ -z "$1" ] && echo "Error: should be run by udhcpc" && exit 1

case "$1" in
    deconfig)
        rm -f /tmp/dhcp_lease_$interface
	;;
    renew)
        echo "CM: $0: renewed $ip" > /tmp/dhcp_lease_$interface
	;;
    bound)
        echo "CM: $0: obtained $ip" > /tmp/dhcp_lease_$interface
	;;
    leasefail)
        touch /tmp/dhcp_lease_$interface
	;;
esac
exit 0
