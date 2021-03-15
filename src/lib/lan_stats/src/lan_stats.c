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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ev.h>

#include "os_types.h"
#include "os.h"
#include "log.h"
#include "ds.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "fcm.h"
#include "fcm_filter.h"
#include "lan_stats.h"
#include "util.h"
#include "policy_tags.h"

#define ETH_DEVICES_TAG "${eth_devices}"

static char *dflt_fltr_name = "none";
static char *collect_cmd = OVS_DPCTL_DUMP_FLOWS;
/**
 * @brief compare flows.
 *
 * @param a flow pointer
 * @param b flow pointer
 * @return 0 if flows match
 */
static int
flow_cmp (void *a, void *b)
{
    dp_ctl_stats_t  *l2_a = (dp_ctl_stats_t *)a;
    dp_ctl_stats_t  *l2_b = (dp_ctl_stats_t *)b;
    int mac_cmp;
    int eth_type_cmp;
    int vlan_cmp;
    int vlan_eth_cmp;

    /* compare src mac-address */
    mac_cmp = memcmp(&l2_a->smac_key, &l2_b->smac_key, sizeof(os_macaddr_t));
    if (mac_cmp != 0) return mac_cmp;

    /* compare dst mac-address */
    mac_cmp = memcmp(&l2_a->dmac_key, &l2_b->dmac_key, sizeof(os_macaddr_t));
    if (mac_cmp != 0) return mac_cmp;

    /* compare eth type */
    eth_type_cmp = l2_a->eth_val - l2_b->eth_val;
    if (eth_type_cmp != 0) return eth_type_cmp;

    /* compare vlanid */
    vlan_cmp = l2_a->vlan_id - l2_b->vlan_id;
    if (vlan_cmp != 0) return vlan_cmp;

    /* compare vlan eth type */
    vlan_eth_cmp = l2_a->vlan_eth_val - l2_b->vlan_eth_val;
    if (vlan_eth_cmp != 0) return vlan_eth_cmp;

    return 0;
}


/**
 * Temporary list to merge same flows stats.
 */
ds_tree_t flow_tracker_list = DS_TREE_INIT(flow_cmp, dp_ctl_stats_t, dp_tnode);


static unsigned int get_eth_type(char *eth)
{
    unsigned int eth_val = 0;
    strtok(eth, "/");
    eth_val = strtol(eth, NULL, 16);
    return eth_val;
}

/*
 * For speedy parsing did the hand-coded parser with the assumption the
 * output format of ovs-dpctl dump-flows  is consistent
 */
static dp_ctl_stats_t *parse_lan_stats(char *buf[])
{
    int i = 0;
    char *pos = NULL;
    int ret = 0;
    dp_ctl_stats_t *stats;

    stats = calloc(1, sizeof(*stats));
    while (buf[i] != NULL)
    {
        if (strncmp(buf[i], OVS_DUMP_ETH_SRC_PREFIX, \
                    OVS_DUMP_ETH_SRC_PREFIX_LEN) == 0)
        {
            // Get src mac
            pos = buf[i] + OVS_DUMP_ETH_SRC_PREFIX_LEN;
            STRSCPY(stats->smac_addr, pos);
            ret = hwaddr_aton(stats->smac_addr, stats->smac_key.addr);
            if (ret == -1)
            {
                LOGE("address conversion failure\n");
            }
            // Obvioulsy the next index is dst mac
            // Get dst mac
            pos = buf[i+1] + OVS_DUMP_ETH_DST_PREFIX_LEN;
            STRSCPY(stats->dmac_addr, pos);
            ret = hwaddr_aton(stats->dmac_addr, stats->dmac_key.addr);
            if (ret == -1)
            {
                LOGE("address conversion failure\n");
            }
            i++;
        }
        else if (strncmp(buf[i], OVS_DUMP_ETH_TYPE_PREFIX, \
                 OVS_DUMP_ETH_TYPE_PREFIX_LEN) == 0)
        {
            // Get eth type
            pos = buf[i] + OVS_DUMP_ETH_TYPE_PREFIX_LEN;
            STRSCPY(stats->eth_type, pos);
            stats->eth_type[strlen(stats->eth_type) - 1] = '\0';
            stats->eth_val = get_eth_type(stats->eth_type);
        }
        else if (strncmp(buf[i], OVS_DUMP_VLAN_ID_PREFIX, \
                 OVS_DUMP_VLAN_ID_PREFIX_LEN) == 0)
        {
            // Get the vlan id
            pos = buf[i] + OVS_DUMP_VLAN_ID_PREFIX_LEN;
            stats->vlan_id = atoi(strsep(&pos, ")") ?: "0");
        }
        else if (strncmp(buf[i], OVS_DUMP_VLAN_ETH_TYPE_PREFIX, \
                 OVS_DUMP_VLAN_ETH_TYPE_PREFIX_LEN) == 0)
        {
            // Get vlan eth type
            pos = buf[i] + OVS_DUMP_VLAN_ETH_TYPE_PREFIX_LEN;
            STRSCPY(stats->vlan_eth_type, pos);
            stats->vlan_eth_type[strlen(stats->vlan_eth_type) - 1] = '\0';
            stats->vlan_eth_val = get_eth_type(stats->vlan_eth_type);
        }
        else if (strncmp(buf[i], OVS_DUMP_PKTS_PREFIX, \
                   OVS_DUMP_PKTS_PREFIX_LEN) == 0)
        {
            // Get pkts count
            pos = buf[i] + OVS_DUMP_PKTS_PREFIX_LEN;
            stats->pkts = atol(pos);
        }
        else if (strncmp(buf[i], OVS_DUMP_BYTES_PREFIX, \
                   OVS_DUMP_BYTES_PREFIX_LEN) == 0)
        {
            // Get bytes count
            pos = buf[i] + OVS_DUMP_BYTES_PREFIX_LEN;
            stats->bytes = atol(pos);
        }
        i++;
    }
    stats->stime = time(NULL); // sample time
    return stats;
}

static void merge_flows(dp_ctl_stats_t *new)
{
    dp_ctl_stats_t *old = NULL;

    // Check if the same flow exists.
    old = ds_tree_find(&flow_tracker_list, new);
    if (old)
    {
       old->pkts += new->pkts;
       old->bytes += new->bytes;
       free(new);
    }
    else
    {
        ds_tree_insert(&flow_tracker_list, new, new);
    }
}

static void parse_flows(char *buf)
{
    char *sep = ",";
    char *tok = NULL;
    char *tokens[MAX_TOKENS] = {0};
    int i = 0;
    dp_ctl_stats_t *new;

    tok = strtok(buf, sep);
    while (tok)
    {
        tokens[i++] = tok;
        tok = strtok(NULL, sep);
        if (i >= (MAX_TOKENS - 1))
            break;
    }
    tokens[i] = NULL;
    new = parse_lan_stats(tokens);
    merge_flows(new);
}


static void alloc_aggr(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator_set aggr_set;
    struct net_md_aggregator *aggr;
    struct node_info node_info;
    int report_type = 0;

    memset(&aggr_set, 0, sizeof(aggr_set));
    node_info.node_id = fcm_get_mqtt_hdr_node_id();
    node_info.location_id = fcm_get_mqtt_hdr_loc_id();
    aggr_set.info = &node_info;
    if (collector->fmt == FCM_RPT_FMT_CUMUL)
        report_type = NET_MD_REPORT_ABSOLUTE;
    else if (collector->fmt == FCM_RPT_FMT_DELTA)
        report_type = NET_MD_REPORT_RELATIVE;
    aggr_set.report_type = report_type;
    aggr_set.num_windows = MAX_HISTOGRAMS;
    aggr_set.acc_ttl = (2 * collector->report_interval);
    aggr_set.send_report = net_md_send_report;
    aggr = net_md_allocate_aggregator(&aggr_set);
    if (aggr == NULL)
    {
        LOGD("Aggregator allocation failed\n");
        return;
    }
   collector->plugin_ctx = aggr;
}

static void activate_window(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr = NULL;
    bool ret = false;
    aggr = collector->plugin_ctx;
    if (aggr == NULL)
    {
        LOGD("Aggergator is empty\n");
        return;
    }
    ret = net_md_activate_window(aggr);
    if (ret == false)
    {
        LOGD("Aggregator window activation failed\n");
        return;
    }
}

static void close_window(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr = NULL;
    bool ret = false;
    aggr = collector->plugin_ctx;
    if (aggr == NULL)
    {
        LOGD("Aggergator is empty\n");
        return;
    }
    ret = net_md_close_active_window(aggr);
    if (ret == false)
    {
        LOGD("Aggregator close window failed\n");
        return;
    }
}

static void send_aggr_report(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr = NULL;
    bool ret = false;
    aggr = collector->plugin_ctx;
    if (aggr == NULL)
    {
        LOGD("Aggergator is empty\n");
        return;
    }
    if (net_md_get_total_flows(aggr) <= 0)
    {
        net_md_reset_aggregator(aggr);
        return;
    }

    ret = net_md_send_report(aggr, collector->mqtt_topic);
    if (ret == false)
    {
        LOGD("Aggregator send report failed\n");
        return;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        net_md_log_aggr(aggr);
    }
}

static void set_filter_info(fcm_filter_l2_info_t *l2_filter_info,
                            fcm_filter_stats_t *l2_filter_pkts,
                            dp_ctl_stats_t *stats)
{
    STRSCPY(l2_filter_info->src_mac, stats->smac_addr);
    STRSCPY(l2_filter_info->dst_mac, stats->dmac_addr);
    l2_filter_info->vlan_id = stats->vlan_id;
    l2_filter_info->eth_type = stats->eth_val;

    l2_filter_pkts->pkt_cnt = stats->pkts;
    l2_filter_pkts->bytes = stats->bytes;
}

static bool
lan_stats_is_mac_in_tag(char *tag, os_macaddr_t *mac)
{
    char mac_s[32] = { 0 };

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    return om_tag_in(mac_s, tag);

}

static void aggr_add_sample(fcm_collect_plugin_t *collector, dp_ctl_stats_t *stats)
{
    struct net_md_aggregator *aggr = NULL;
    struct net_md_flow_key key;
    struct flow_counters pkts_ct;
    bool ret = false;

    aggr = collector->plugin_ctx;
    if (aggr == NULL)
    {
        LOGE("Aggr is NULL\n");
        return;
    }

    memset(&key, 0, sizeof(struct net_md_flow_key));
    memset(&pkts_ct, 0, sizeof(struct flow_counters));
    key.smac = &stats->smac_key;
    key.isparent_of_smac = lan_stats_is_mac_in_tag(ETH_DEVICES_TAG, key.smac);
    key.dmac = &stats->dmac_key;
    key.isparent_of_dmac = lan_stats_is_mac_in_tag(ETH_DEVICES_TAG, key.dmac);
    key.ethertype = stats->eth_val;

    if (stats->vlan_id > 0)
    {
        key.vlan_id = stats->vlan_id;
        // use vlan eth type if vlan is present
        key.ethertype = stats->vlan_eth_val;
    }
    pkts_ct.packets_count = stats->pkts;
    pkts_ct.bytes_count = stats->bytes;

    ret = net_md_add_sample(aggr, &key, &pkts_ct);
    if (!ret)
        LOGD("Add sample to aggregator failed\n");
}

static void lan_stats_send_report_cb(fcm_collect_plugin_t *collector)
{
    //struct packed_buffer *mqtt_report = NULL;

    //collector->plugin_fcm_ctx = mqtt_report;
   close_window(collector);
   send_aggr_report(collector);
   activate_window(collector);
}


static void lan_stats_collect_flows(fcm_collect_plugin_t *collector)
{
    FILE *fp = NULL;
    char line_buf[LINE_BUFF_LEN] = {0,};

    collect_cmd  = collector->fcm_plugin_ctx;
    if (collect_cmd == NULL)
        collect_cmd = OVS_DPCTL_DUMP_FLOWS;

    if ((fp = popen(collect_cmd, "r")) == NULL)
    {
        LOGE("popen error");
        return;
    }

    while (fgets(line_buf, LINE_BUFF_LEN, fp) != NULL)
    {
        LOGD("ovs-dpctl dump line %s", line_buf);
        parse_flows(line_buf);
        memset(line_buf, 0, sizeof(line_buf));
    }
    pclose(fp);
    fp = NULL;
}

static void lan_stats_flows_filter(fcm_collect_plugin_t *collector, ds_tree_t *tree)
{
    fcm_filter_l2_info_t l2_filter_info;
    fcm_filter_stats_t   l2_filter_pkts;
    bool allow = false;
    dp_ctl_stats_t *stats, *next;

    stats = ds_tree_head(tree);
    while (stats != NULL)
    {
        set_filter_info(&l2_filter_info, &l2_filter_pkts, stats);
        if (collector->filters.collect != NULL)
        {
            fcm_filter_layer2_apply(collector->filters.collect,
                                  &l2_filter_info, &l2_filter_pkts, &allow);
            if (allow)
            {
                LOGD("Flow collect allowed: filter_name: %s, smac: %s, " \
                     "dmac: %s, vlan_id: %d, eth_type: %d, pks: %ld, " \
                     "bytes: %ld\n",\
                      collector->filters.collect ?
                      collector->filters.collect : dflt_fltr_name,
                      stats->smac_addr,
                      stats->dmac_addr, stats->vlan_id, stats->eth_val,
                      stats->pkts, stats->bytes);
                aggr_add_sample(collector, stats);
            }
            else
                LOGD("Flow collect dropped: filter_name: %s, smac: %s, "\
                     "dmac: %s, vlan_id: %d, eth_type: %d, pks: %ld, "\
                     "bytes: %ld\n",\
                      collector->filters.collect ?
                      collector->filters.collect : dflt_fltr_name,
                      stats->smac_addr, stats->dmac_addr,
                      stats->vlan_id, stats->eth_val, stats->pkts, stats->bytes);
        }
        else
        {
            LOGD("Aggr add sample\n");
            aggr_add_sample(collector, stats);
        }

        next = ds_tree_next(tree, stats);
        stats = next;
    }
}


static void lan_stats_clean_flows(ds_tree_t *tree)
{
    dp_ctl_stats_t *stats, *next;
    int count = 0;

    if (tree == NULL) return;

    stats = ds_tree_head(tree);
    while (stats != NULL)
    {
        next = ds_tree_next(tree, stats);
        ds_tree_remove(tree, stats);
        free(stats);
        stats = next;
        count++;
    }
    LOGT("%s: Total flows freed: %d\n",__func__,count);
    return;
}


static void lan_stats_collect_cb(fcm_collect_plugin_t *collector)
{
    lan_stats_collect_flows(collector);
    lan_stats_flows_filter(collector, &flow_tracker_list);
    lan_stats_clean_flows(&flow_tracker_list);
}


void lan_stats_plugin_close_cb(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr = NULL;

    aggr = collector->plugin_ctx;
    if (aggr == NULL)
    {
        LOGD("Aggergator is empty\n");
        return;
    }
    close_window(collector);
    net_md_free_aggregator(aggr);
}


/* Entry function for plugin */
int lan_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    collector->collect_periodic = lan_stats_collect_cb;
    collector->send_report = lan_stats_send_report_cb;
    collector->close_plugin = lan_stats_plugin_close_cb;
    collect_cmd  = collector->fcm_plugin_ctx;
    if (collect_cmd == NULL)
        collect_cmd = OVS_DPCTL_DUMP_FLOWS;
    alloc_aggr(collector);
    activate_window(collector);
    return 0;
}
