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



if [ -z "$FUT_TOPDIR" ]; then
    export FUT_TOPDIR="/tmp/fut-base"
fi

if [ -z "$OPENSYNC_ROOTDIR" ]; then
    export OPENSYNC_ROOTDIR="/usr/opensync"
fi

if [ -z "$LOGREAD" ]; then
    export LOGREAD="cat /var/log/messages"
fi

if [ -z "$DEFAULT_WAIT_TIME" ]; then
    export DEFAULT_WAIT_TIME=30
fi

if [ -z "$OVSH" ]; then
    export OVSH="${OPENSYNC_ROOTDIR}/tools/ovsh --quiet --timeout=180000"
fi

if [ -z "$OVSH_FAST" ]; then
    export OVSH_FAST="${OPENSYNC_ROOTDIR}/tools/ovsh --quiet --timeout=10000"
fi

if [ -z "$OVSH_SLOW" ]; then
    export OVSH_SLOW="${OPENSYNC_ROOTDIR}/tools/ovsh --quiet --timeout=50000"
fi

if [ -z "$LIB_OVERRIDE_FILE" ]; then
    export LIB_OVERRIDE_FILE="/tmp/fut-base/shell/config/empty.sh"
fi

if [ -z "$PATH" ]; then
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:${OPENSYNC_ROOTDIR}/tools:${OPENSYNC_ROOTDIR}/bin"
fi
