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

# {# jinja-parse #}

NF_PREFIX="mupnp"
OVSH="{{INSTALL_PREFIX}}/tools/ovsh"

init()
{
    # Remove all stale rules
    $OVSH -r s Netfilter name | grep -e "^$NF_PREFIX" | while read RID
    do
        $OVSH d Netfilter --where name=="$RID"
    done
}

add_forward_rule()
{
    proto="$1"
    ext_port="$2"
    int_addr="$3"
    int_port="$4"
    rem_host="$5"

    rid="${NF_PREFIX}_fwd_${proto}_${ext_port}"

    $OVSH U Netfilter --where name=="$rid" \
        enable:=true \
        protocol:=ipv4 \
        table:=nat \
        chain:=MINIUPNPD \
        target:=DNAT \
        rule:="-p $proto --dport $ext_port --to $int_addr:$int_port ${rem_host:+-s ${rem_host}}"
}

del_forward_rule()
{
    proto="$1"
    ext_port="$2"
    int_addr="$3"
    int_port="$4"
    rem_host="$5"

    rid="${NF_PREFIX}_fwd_${proto}_${ext_port}"

    $OVSH d Netfilter --where name=="$rid"
}

add_filter_rule()
{
    proto="$1"
    int_addr="$2"
    int_port="$3"
    rem_host="$4"

    rid="${NF_PREFIX}_fil_${proto}_${int_addr}:${int_port}"

    $OVSH U Netfilter --where name=="$rid" \
        enable:=true \
        protocol:=ipv4 \
        table:=filter \
        chain:=MINIUPNPD \
        target:=ACCEPT \
        rule:="-p $proto -d $int_addr --dport $int_port ${rem_host:+-s ${rem_host}}"
}

del_filter_rule()
{
    proto="$1"
    int_addr="$2"
    int_port="$3"
    rem_host="$4"

    rid="${NF_PREFIX}_fil_${proto}_${int_addr}:${int_port}"
    $OVSH d Netfilter --where name=="$rid"
}

case "$1" in
    init|fini)
        init
        ;;

    add_forward_rule)
        shift
        add_forward_rule "$@"
        ;;

    del_forward_rule)
        shift
        del_forward_rule "$@"
        ;;

    add_filter_rule)
        shift
        add_filter_rule "$@"
        ;;

    del_filter_rule)
        shift
        del_filter_rule "$@"
        ;;
    *)
        echo Bad options: "$@"
        ;;
esac

