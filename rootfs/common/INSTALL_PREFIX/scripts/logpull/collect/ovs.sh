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
# Collect common Open vSwitch info
#
. "$LOGPULL_LIB"

collect_ovs()
{
    OVSDB_PID=`pidof ovsdb-server`
    OVSDB_DB=`cat /proc/$OVSDB_PID/cmdline | tr "\0" "\n" | grep conf.db`

    collect_cmd ovsdb-client dump
    collect_cmd ovsdb-client -f json dump
    collect_cmd ovsdb-tool -mm show-log $OVSDB_DB
    collect_cmd ovs-vsctl --version
    collect_cmd ovs-vsctl show
    collect_cmd ovs-vsctl list bridge
    collect_cmd ovs-vsctl list interface
    collect_cmd ovs-appctl dpif/show
    collect_cmd ovs-appctl ovs/route/show

    for br in $(ovs-vsctl list-br); do
        collect_cmd ovs-ofctl dump-flows $br
        collect_cmd ovs-appctl fdb/show $br
        collect_cmd ovs-appctl mdb/show $br
        collect_cmd ovs-appctl dpif/dump-flows $br
    done
}

collect_ovs
