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

#
# Check for conntrack  overflow. Sometimes Clients are miss-behaving,
# and thus causing ct overflow condition. When this condition is active
# no new connections will be allowed.
#
# To avoid losing connectivity ct table will be flushed when reaching
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
