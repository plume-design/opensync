#!/bin/sh
#
# Check for conntrack  overflow. Sometimes Clients are miss-behaving,
# and thus causing ct overflow condition. When this condition is active
# no new connections will be allowed.
#
# To avoid loosing connectivity ct table will be flushed when reaching
# CT_ALLOWED. While condition may be recurring, at least
# clients and device should not permanently loose connectivity.
#
#

# define min free ct entries in %
CT_MIN_FFEE=5

# if y, ct will be flushed on overflow
CT_FLUSH=y

# if defined, ct tables will be dumped on overflow to this file.
CT_DUMP="/tmp/.ct_dump_"

CT_TOOL=$(which conntrack)
[ $? == 0 ] || Healthcheck_Pass

CT_MAX=$(cat /proc/sys/net/nf_conntrack_max)

if [ $CT_MAX -lt $CT_MIN_FFEE ]; then
	log_warn "nf_conntrack_max ($CT_MAX) is set to less than CT_MAX_MIN_FREE ($CT_MIN_FFEE)"
	Healthcheck_Pass
fi

CT_ALLOWED=$(($CT_MAX * (100-$CT_MIN_FFEE) /100))
CT_CURRENT=$($CT_TOOL -C)
if [ $? != 0 ]; then
	log_warn "cannot pull ct connection count ($CT_TOOL -C fail)"
	Healthcheck_Pass
fi

log_debug "ct info: ct_current: $CT_CURRENT, ct_allowed: $CT_ALLOWED, max: $CT_MAX"

if [ $CT_ALLOWED -lt $CT_CURRENT ]; then
	log_warn "conntrack overflow! Found $CT_CURRENT active connections, (ct_max: $CT_MAX, ct_allowed: $CT_ALLOWED)"
	if [ ! -z $CT_DUMP ]; then
		CT_DUMP_FILE="$CT_DUMP$(date +%s)"
		log_info "dumping ct_table to: $CT_DUMP_FILE"
		rm $CT_DUMP* > /dev/null 2>&1
		$CT_TOOL -L > $CT_DUMP_FILE
	fi
	if [ "$CT_FLUSH" == "y" ]; then
		log_info "flushing ct table"
		$CT_TOOL -F
	fi
	Healthcheck_Fail
fi

Healthcheck_Pass
