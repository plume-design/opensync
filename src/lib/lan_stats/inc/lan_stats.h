/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef LAN_STATS_H_INCLUDED
#define LAN_STATS_H_INCLUDED

#define MAC_ADDR_STR_LEN     (18)
#define OVS_DPCTL_DUMP_FLOWS "ovs-dpctl dump-flows"
#define LINE_BUFF_LEN        (2048)
#define MAX_TOKENS           (30)

#define OVS_DUMP_ETH_SRC_PREFIX   "eth(src="
#define OVS_DUMP_ETH_DST_PREFIX   "dst="
#define OVS_DUMP_ETH_TYPE_PREFIX  "eth_type("
#define OVS_DUMP_PKTS_PREFIX      " packets:"
#define OVS_DUMP_BYTES_PREFIX     " bytes:"
#define OVS_DUMP_VLAN_ID_PREFIX   "vlan(vid="
#define OVS_DUMP_VLAN_ETH_TYPE_PREFIX "encap(eth_type("

#define OVS_DUMP_ETH_SRC_PREFIX_LEN  (8) // Length of "eth(src="
#define OVS_DUMP_ETH_DST_PREFIX_LEN  (4) // Length of "dst="
#define OVS_DUMP_ETH_TYPE_PREFIX_LEN (9) // Length of "eth_type("
#define OVS_DUMP_PKTS_PREFIX_LEN     (9) // Length of "packets:"
#define OVS_DUMP_BYTES_PREFIX_LEN    (7) // Length of "bytes:"
#define OVS_DUMP_VLAN_ID_PREFIX_LEN  (9) // Lenghth of "vlan(vid="
#define OVS_DUMP_VLAN_ETH_TYPE_PREFIX_LEN  (15) // Length of "encap(eth_type("
#define MAX_HISTOGRAMS               (1)

typedef struct dp_ctl_stats_
{
    char            smac_addr[MAC_ADDR_STR_LEN];
    char            dmac_addr[MAC_ADDR_STR_LEN];
    os_macaddr_t    smac_key;
    os_macaddr_t    dmac_key;
    char            eth_type[16];
    char            vlan_eth_type[16];
    unsigned int    eth_val;
    unsigned int    vlan_eth_val;
    unsigned int    vlan_id;
    unsigned long   pkts;
    unsigned long   bytes;
    time_t          stime;

    ds_tree_node_t  dp_tnode;
} dp_ctl_stats_t;


#endif /* LAN_STATS_H_INCLUDED */
