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
# Collect info about qdiscs and classes
#
. "$LOGPULL_LIB"

collect_qdisc_dump()
{
    for raw_name in $(ip -o link show | awk -F': ' '{print $2}'); do
        # Some interfaces have '@' in the name, remove it and everything after it
        local ifname="${raw_name%@*}"

        echo "################## Interface $ifname ##################"

        echo "qdiscs:"
        tc -s -d qdisc show dev "$ifname"

        echo "filters:"
        tc -s -d filter show dev "$ifname"
        tc -s -d filter show dev "$ifname" root
        tc -s -d filter show dev "$ifname" egress
        tc -s -d filter show dev "$ifname" ingress
        # Ingress filter before clsact existed
        tc -s -d filter show dev "$ifname" parent ffff:

        echo "classes:"
        tc -s -d class show dev "$ifname"
        tc -s -d class show dev "$ifname" root

        echo ""
        echo ""
    done
}

collect_qdisc_dump > "$LOGPULL_TMP_DIR/qdisc_dump"
