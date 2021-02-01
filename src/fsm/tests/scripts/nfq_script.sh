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
#-------------------------------------------------------------------------------------
# Usage : ./nfq_script.sh  pod_wan_ip pod_lan_ip
#-------------------------------------------------------------------------------------

if [ $# -lt 2 ]
then
    echo "Usage: ./nfq_script.sh pod_wan_ip pod_lan_ip"
    exit 1
fi

wan_ip=$1
lan_ip=$2

echo "Setting fsm tap type to NFQUEUE"
ovsh U Flow_Service_Manager_Config -w handler==core_dpi_dispatch other_config:='["map",[["tap_type","fsm_tap_nfqueues"],["included_devices","$[all_clients]"]]]';

# mark zone 0 conntrack entries in table 9
echo "Adding Openflow rules to mark and check for zone 0 conntrack entries"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_bypass_untrkd rule:="ip,ct_state=-trk" priority:=150 bridge:=br-home action:="ct(zone=0,table=9)"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_mrk_dns_to_pod rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0,ip,nw_dst=$lan_ip" priority:=140 bridge:=br-home action:="resubmit(,8),ct(zone=0,commit,exec(load:0x1->NXM_NX_CT_MARK[])"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_mrk_dns_from_pod rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0,ip,nw_src=$lan_ip" priority:=140 bridge:=br-home action:="resubmit(,8),ct(zone=0,commit,exec(load:0x1->NXM_NX_CT_MARK[])"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_inspect_mrk0 rule:="ip,ct_state=+trk,ct_mark=0,ct_zone=0" priority:=140 bridge:=br-home action:="resubmit(,8),ct(zone=0,nat(src=$wan_ip),commit,exec(load:0x1->NXM_NX_CT_MARK[]))"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_inspect_mrk1 rule:="ip,ct_state=+trk,ct_mark=1,ct_zone=0" priority:=135 bridge:=br-home action:="resubmit(,8)"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_dpi_passthrough rule:="ip,ct_state=+trk,ct_mark=2,ct_zone=0" priority:=135 bridge:=br-home action:="resubmit(,8)"
ovsh i Openflow_Config table:=9 token:=dev_ct_ipv4_not_allowed rule:="ct_state=+trk,ct_mark=3,ct_zone=0" priority:=135 bridge:=br-home action:=drop


# iptables rules to mark and zone the packets
echo "Iptables rules to match the mark and jump to NFQUEUE"
iptables -t mangle -A FORWARD -m connmark --mark 0x1  -j NFQUEUE --queue-num 0 --queue-bypass
iptables -t mangle -A INPUT -m connmark --mark 0x1  -j NFQUEUE --queue-num 0 --queue-bypass
iptables -t mangle -A OUTPUT -m connmark --mark 0x1  -j NFQUEUE --queue-num 0 --queue-bypass

# Restarting fsm
echo "Restart fsm"
killall fsm;

echo "All rules are applied and begin testing"
