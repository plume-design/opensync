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


# Prerequisites: Pod in router mode and disable inline from noc.
# Execute this script with the following options:
# /tmp/multi_nfqueues.sh --cmd=enable --lan_ip=x.x.x.x --wan_ip=x.x.x.x
# /tmp/multi_nfqueues.sh --cmd=disable --lan_ip=x.x.x.x --wan_ip=x.x.x.x

prog=$0

configure_openflow()
{
    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_bypass_untrkd \
    rule:="ip,ct_state=-trk" priority:=150 bridge:=br-home  \
    action:="ct(zone=0,table=9)";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_mrk_dns_to_pod \
    rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0,ip,nw_dst=$lan_ip" \
    priority:=140 bridge:=br-home \
    action:="resubmit(,8),ct(zone=0,commit,exec(load:0x1->NXM_NX_CT_MARK[])";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_mrk_dns_from_pod \
    rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0,ip,nw_src=$lan_ip" \
    priority:=140 bridge:=br-home \
    action:="resubmit(,8),ct(zone=0,commit,exec(load:0x1->NXM_NX_CT_MARK[])";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_inspect_mrk0 \
    rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0" priority:=140 bridge:=br-home \
    action:="resubmit(,8),ct(zone=0,nat(src=$wan_ip),commit,exec(load:0x1->NXM_NX_CT_MARK[]))";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_inspect_mrk1 \
    rule:="ip,ct_state=+trk,ct_mark=1,ct_zone=0" priority:=135 bridge:=br-home \
    action:="resubmit(,8)";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_passthrough \
    rule:="ip,ct_state=+trk,ct_mark=2,ct_zone=0" priority:=135 bridge:=br-home \
    action:="resubmit(,8)";

    ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_not_allowed \
    rule:="ct_state=+trk,ct_mark=3,ct_zone=0" priority:=135 bridge:=br-home \
    action:=drop;
}

flush_openflow()
{
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_bypass_untrkd;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_mrk_dns_to_pod;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_mrk_dns_from_pod;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_dpi_inspect_mrk0;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_dpi_inspect_mrk1;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_dpi_passthrough;
    ovsh d Openflow_Config table==9 -w token==dev_ct_ipv4_not_allowed;
}

configure_iptables()
{
    # create custom chains in mangle table.
    iptables -t mangle -N chain-nfqueue;
    iptables -t mangle -N chain-forward-dpi;
    iptables -t mangle -N chain-input-dpi;
    iptables -t mangle -N chain-output-dpi;
    iptables -t mangle -I FORWARD -j chain-forward-dpi;
    iptables -t mangle -I INPUT -j chain-input-dpi;
    iptables -t mangle -I OUTPUT -j chain-output-dpi;
    iptables -t mangle -I chain-forward-dpi -j chain-nfqueue;
    iptables -t mangle -I chain-input-dpi -j chain-nfqueue;
    iptables -t mangle -I chain-output-dpi -j chain-nfqueue;

    # Redirect DNS traffic to NFQUEUE 1
    iptables -t mangle -A chain-nfqueue -p udp --dport 53 -m connmark --mark 0x1 -j NFQUEUE --queue-num 1 --queue-bypass;
    iptables -t mangle -A chain-nfqueue -p udp --sport 53 -m connmark --mark 0x1 -j NFQUEUE --queue-num 1 --queue-bypass;
    # Rest of the traffic to NFQUEUE 0
    iptables -t mangle -A chain-nfqueue -m connmark --mark 0x1 -j NFQUEUE --queue-num 0 --queue-bypass;
}

flush_iptables()
{
    iptables -t mangle -D FORWARD -j chain-forward-dpi;
    iptables -t mangle -D INPUT -j chain-input-dpi;
    iptables -t mangle -D OUTPUT -j chain-output-dpi;

    iptables -t mangle -D chain-nfqueue -p udp --dport 53 -m connmark --mark 0x1 -j NFQUEUE --queue-num 1 --queue-bypass;
    iptables -t mangle -D chain-nfqueue -p udp --sport 53 -m connmark --mark 0x1 -j NFQUEUE --queue-num 1 --queue-bypass;
    iptables -t mangle -D chain-nfqueue -m connmark --mark 0x1 -j NFQUEUE --queue-num 0 --queue-bypass;

    iptables -t mangle -D chain-forward-dpi -j chain-nfqueue;
    iptables -t mangle -D chain-input-dpi -j chain-nfqueue;
    iptables -t mangle -D chain-output-dpi -j chain-nfqueue;

    iptables -t mangle -X chain-forward-dpi;
    iptables -t mangle -X chain-input-dpi;
    iptables -t mangle -X chain-output-dpi;
    iptables -t mangle -X chain-nfqueue;
}

cmd_enable()
{
    # insert tap_type and multiple queues queue 0 and 1 in core_dpi_dispatch
    # NFQUEUE 1 is dedicated for DNS
    # NFQUEUE 0 is dedicated for rest of the traffic
    ovsh U Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
    other_config:='["map",[["tap_type","fsm_tap_nfqueues"],["queue_num","0-1"],["included_devices","$[all_clients]"]]]';

    sleep 1
    # configure openflow rules
    echo "configuring openflow rules"
    configure_openflow;

    sleep 1
    # configure iptables rules
    echo "configuring iptables"
    configure_iptables;

    sleep 1
    # restart fsm
    echo "restarting fsm"
    killall fsm;
}

cmd_disable()
{
    ovsh U Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
    other_config:='["map",[["excluded_devices","${all_gateways}"]]]'

    sleep 1
    echo "flush iptables rules"
    flush_iptables;

    sleep 1
    echo "flush openflow rules"
    flush_openflow;

    sleep 1
    # restart fsm
    echo "restarting fsm"
    killall fsm;
}

# usage
usage()
{
  cat <<EOF
    Usage: ${prog} <[options]>
        Options:
            -h this message
            --cmd=<enable | disable>            REQUIRED
            --lan_ip=<lan ip address>           REQUIRED
            --wan_ip=<wan ip address>           REQUIRED
EOF
  exit 1
}



# h for help, long options otherwise
optspec="h-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -)
            LONG_OPTARG="${OPTARG#*=}"
            case "${OPTARG}" in
               lan_ip=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   LAN=$val
                   ;;
               cmd=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   CMD=$val
                   ;;
               wan_ip=?* )
                   val=${LONG_OPTARG}
                   opt=${OPTARG%=$val}
                   WAN=$val
                   ;;
               *)
                   if [ "$OPTERR" = 1 ] && [ "${optspec:0:1}" != ":" ]; then
                       echo "Unknown option --${OPTARG}" >&2
                   fi
                   ;;
            esac;;
        h)
            usage
            exit 2
            ;;
        *)
            if [ "$OPTERR" != 1 ] || [ "${optspec:0:1}" = ":" ]; then
                echo "Non-option argument: '-${OPTARG}'"
            fi
            ;;
    esac
done


cmd=${CMD}
lan_ip=${LAN}
wan_ip=${WAN}

if [ -z ${cmd} ] && [ -z ${lan_ip} ] && [ -z ${wan_ip} ]; then
    usage
fi

case ${cmd} in
    "enable")
        cmd_enable ;;
    "disable")
        cmd_disable ;;
    *)
        usage
        exit 0 ;;
esac

sleep 1

#ovsh -q U Flow_Service_Manager_Config -w handler==core_dpi_dispatch \
#     other_config:ins:'["map",[["included_devices","$[all_clients]"]]]' > /dev/null
