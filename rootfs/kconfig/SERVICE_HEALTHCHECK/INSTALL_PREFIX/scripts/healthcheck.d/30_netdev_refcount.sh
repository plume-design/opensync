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
# platform. Reworking the logic in the script to
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
