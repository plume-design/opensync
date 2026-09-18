#!/bin/sh
LASTLINEFILE=/tmp/netdev_refcount.lastline
LASTLINE=$(cat "$LASTLINEFILE" 2>/dev/null)
REGEX="unregister_netdevice: waiting for .* to become free. Usage count"

# Kernel prints these messages every 10s.
#
# If the system prints them consistently then it's either
# stuck for good or is getting stuck and unstuck very
# often. Either case it's bad.
#
# Healthcheck runs roughly every 60s. If it sees the
# message every time in log delta since last run it'll
# report that.
#
# This assumes kernel logs have timestamps in them. Without
# timestamps this will detect false positives
# really easily. If that happens it is preferred
# to enable kernel log timestamps on the given
# paltform. Reworking the logic in the script to
# work with no timestamps is not trivial.
dmesg | awk -v "LASTLINE=$LASTLINE" '
	/'"$REGEX"'/ { FOUND=$0 }
	END {
		if (FOUND != "" && FOUND != LASTLINE) {
			print FOUND > "'"$LASTLINEFILE"'"
			exit 1
		}
	}
'
