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
# Collect common OpenSync info
#
. "$LOGPULL_LIB"

collect_osync()
{
    collect_cmd  $CONFIG_INSTALL_PREFIX/bin/dm --show-info
    collect_file $CONFIG_INSTALL_PREFIX/etc/kconfig
    if [ -e $CONFIG_INSTALL_PREFIX/.version ]; then
        collect_file $CONFIG_INSTALL_PREFIX/.version
    fi
    if [ -e $CONFIG_INSTALL_PREFIX/.versions ]; then
        collect_file $CONFIG_INSTALL_PREFIX/.versions
    fi
}

collect_bm()
{
    # This will put dbg events in syslog
    killall -s SIGUSR1 bm
    sleep 1
    cat /var/log/messages | grep "BM\[" > "$LOGPULL_TMP_DIR"/_bm_dbg_events
}

collect_osync
collect_bm
