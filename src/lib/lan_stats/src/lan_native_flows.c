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

#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#include "lan_stats.h"
#include "nf_utils.h"
#include "memutil.h"
#include "neigh_table.h"
#include "log.h"
#include "os_random.h"


static void
generate_os_ufid(ctflow_info_t *ct_entry, dp_ctl_stats_t *stats)
{
    uint16_t sport;
    uint16_t dport;
    uint8_t proto;
    ct_flow_t *flow;
    os_ufid_t *id;

    id = &stats->ufid.id;
    flow = &ct_entry->flow;

    sport = ntohs(flow->layer3_info.src_port);
    dport = ntohs(flow->layer3_info.dst_port);
    proto = flow->layer3_info.proto_type;

    id->u32[0] = stats->smac_key.addr[0] ^ stats->smac_key.addr[1] ^
                 stats->smac_key.addr[2] ^ stats->smac_key.addr[3] ^
                 stats->smac_key.addr[4] ^ stats->smac_key.addr[5] ^
                 sport;
    id->u32[2] = stats->dmac_key.addr[0] ^ stats->dmac_key.addr[1] ^
                 stats->dmac_key.addr[2] ^ stats->dmac_key.addr[3] ^
                 stats->dmac_key.addr[4] ^ stats->dmac_key.addr[5] ^
                 dport ^ proto;

    LOGD("%s: Generated ufid: "PRI_os_ufid_t,
          __func__, FMT_os_ufid_t_pt(&stats->ufid.id));
    return;
}


/**
 * Determines whether to include a contrack entry in the LAN statistics.
 *
 * @param lan_stats_instance The LAN statistics instance.
 * @param ct_entry The contrack entry.
 * @param stats flow stats
 * @return Returns true if the ct entry should be included, false otherwise.
 */
static bool
lan_stats_include_ct_entry(lan_stats_instance_t *lan_stats_instance, ctflow_info_t *ct_entry, dp_ctl_stats_t *stats)
{
    struct sockaddr_storage *ssrc;
    struct sockaddr_storage *sdst;
    char *device_tag;
    bool smac_lookup;
    bool dmac_lookup;
    ct_flow_t *flow;
    bool is_eth_dev;

    flow = &ct_entry->flow;
    ssrc = &flow->layer3_info.src_ip;
    sdst = &flow->layer3_info.dst_ip;

    /* populates smac_key and dmac_key */
    smac_lookup = neigh_table_lookup(ssrc, &stats->smac_key);
    dmac_lookup = neigh_table_lookup(sdst, &stats->dmac_key);

    /* include ct entry if both smac and dmac are present (LAN to LAN traffic) */
    if (smac_lookup && dmac_lookup) return true;

    /* check if it is ethernet traffic */
    device_tag = (lan_stats_instance->parent_tag != NULL) ?
                  lan_stats_instance->parent_tag : ETH_DEVICES_TAG;

    /* Check if the device is an ethernet device */
    is_eth_dev = (lan_stats_is_mac_in_tag(device_tag, &stats->smac_key) ||
                  lan_stats_is_mac_in_tag(device_tag, &stats->dmac_key));

    /* if ethernet device include the entry */
    if (is_eth_dev) return true;

    /* ignore this ct entry */
    return false;
}

bool
lan_stats_parse_ct(lan_stats_instance_t *lan_stats_instance, ctflow_info_t *ct_entry, dp_ctl_stats_t *stats)
{
    pkts_ct_info_t  *pkt_info;
    ct_flow_t *flow;
    bool rc = false;
    bool include_entry;
    int  af;

    flow = &ct_entry->flow;
    pkt_info = &flow->pkt_info;

    af = flow->layer3_info.src_ip.ss_family;

    include_entry = lan_stats_include_ct_entry(lan_stats_instance, ct_entry, stats);
    if (include_entry == false) return rc;

    stats->eth_val = (af == AF_INET ? 0x0800 : 0x86DD);
    stats->pkts = pkt_info->pkt_cnt;
    stats->bytes = pkt_info->bytes;
    generate_os_ufid(ct_entry, stats);
    rc = true;
    return rc;
}


static bool
lan_stats_get_flows(lan_stats_instance_t *lan_stats_instance)
{
    ds_dlist_t *ct_list;
    bool rc = false;

    if (lan_stats_instance == NULL) return rc;
    ct_list = &lan_stats_instance->ct_list;

    rc = nf_ct_get_flow_entries(AF_INET, ct_list, lan_stats_instance->ct_zone);
    if (rc == false)
    {
        LOGE("%s: Failed to collect IPv4 conntrack flows.", __func__);
        return rc;

    }

    rc = nf_ct_get_flow_entries(AF_INET6, ct_list, lan_stats_instance->ct_zone);
    if (rc == false)
    {
        LOGE("%s: Failed to collect IPv6 conntrack flows.", __func__);
        return rc;

    }
    return rc;
}


static bool
lan_stats_filter_flow(lan_stats_instance_t *lan_stats_instance, ctflow_info_t *ct_entry)
{
    bool skip_flow;
    int af;

    af = ct_entry->flow.layer3_info.dst_ip.ss_family;
    skip_flow = nf_ct_filter_ip(af, &ct_entry->flow.layer3_info.dst_ip);
    if (skip_flow) return true;

    skip_flow = nf_ct_filter_ip(af, &ct_entry->flow.layer3_info.src_ip);
    if (skip_flow) return true;

    lan_stats_instance->node_count++;
    return false;
}


bool
lan_stats_process_ct_flows(lan_stats_instance_t *lan_stats_instance)
{
    ctflow_info_t *ct_entry;
    dp_ctl_stats_t stats;
    ds_dlist_t *ct_list;
    bool rc = false;

    if (lan_stats_instance == NULL) return rc;

    ct_list = &lan_stats_instance->ct_list;


    ds_dlist_foreach(ct_list, ct_entry)
    {
        MEMZERO(stats);
        if (lan_stats_filter_flow(lan_stats_instance, ct_entry)) continue;

        if (!lan_stats_parse_ct(lan_stats_instance, ct_entry, &stats)) continue;

        lan_stats_add_uplink_info(lan_stats_instance, &stats);

        lan_stats_flows_filter(lan_stats_instance, &stats);
    }

    return rc;
}


void
lan_stats_collect_flows(lan_stats_instance_t *lan_stats_instance)
{
    ds_dlist_t *ct_list;
    bool rc = false;

    if (lan_stats_instance == NULL) return;

    ct_list = &lan_stats_instance->ct_list;

    rc = lan_stats_get_flows(lan_stats_instance);
    if (rc == false) return;

    lan_stats_process_ct_flows(lan_stats_instance);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) nf_ct_print_entries(ct_list);

    nf_free_ct_flow_list(ct_list);
    return;
}
