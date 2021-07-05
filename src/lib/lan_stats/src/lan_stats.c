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
#include <errno.h>

#include "os_types.h"
#include "os.h"
#include "log.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "fcm.h"
#include "fcm_filter.h"
#include "lan_stats.h"
#include "util.h"
#include "policy_tags.h"
#include "memutil.h"
#include "kconfig.h"

#define ETH_DEVICES_TAG "${@eth_devices}"

static char *dflt_fltr_name = "none";

/**
 * Singleton tracking the plugin state
 */
static lan_stats_mgr_t g_lan_stats =
{
    .initialized = false,
};

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
lan_stats_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;

    return 1;
}

lan_stats_instance_t *
lan_stats_lookup_session(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized) return NULL;

    sessions = &mgr->lan_stats_sessions;
    lan_stats_instance = ds_tree_find(sessions, collector);
    return lan_stats_instance;
}

/**
 * @brief look up or allocate a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
lan_stats_instance_t *
lan_stats_get_session(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = lan_stats_get_mgr();
    sessions = &mgr->lan_stats_sessions;

    lan_stats_instance = ds_tree_find(sessions, collector);
    if (lan_stats_instance != NULL) return lan_stats_instance;

    LOGD("%s: Adding a new session", __func__);
    lan_stats_instance = CALLOC(1, sizeof(*lan_stats_instance));
    if (lan_stats_instance == NULL) return NULL;

    lan_stats_instance->initialized = false;

    ds_tree_insert(sessions, lan_stats_instance, collector);
    return lan_stats_instance;
}

/**
 * @brief returns the pointer to the plugin's global state tracker
 */
lan_stats_mgr_t *
lan_stats_get_mgr(void)
{
    return &g_lan_stats;
}

/**
 * @brief returns the pointer to the active plugin
 */
lan_stats_instance_t *
lan_stats_get_active_instance(void)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    if (mgr == NULL) return NULL;

    return mgr->active;
}

/* Copied from openvswitchd */
/* Returns the value of 'c' as a hexadecimal digit. */
int
lan_hexit_value(int c)
{
    switch (c)
    {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return c - '0';

        case 'a': case 'A':
            return 0xa;

        case 'b': case 'B':
            return 0xb;

        case 'c': case 'C':
            return 0xc;

        case 'd': case 'D':
            return 0xd;

        case 'e': case 'E':
            return 0xe;

        case 'f': case 'F':
            return 0xf;

        default:
            return -1;
    }
}

/* An initializer or expression for an all-zero UFID. */
#define UFID_ZERO ((ovs_u128_) { .id.u32 = { 0, 0, 0, 0 } })

/* Returns the integer value of the 'n' hexadecimal digits starting at 's', or
 * UINTMAX_MAX if one of those "digits" is not really a hex digit.  Sets '*ok'
 * to true if the conversion succeeds or to false if a non-hex digit is
 * detected. */
uintmax_t
lan_hexits_value(const char *s, size_t n, bool *ok)
{
    uintmax_t value;
    size_t i;

    value = 0;
    for (i = 0; i < n; i++)
    {
        int hexit = lan_hexit_value(s[i]);
        if (hexit < 0)
        {
            *ok = false;
            return UINTMAX_MAX;
        }
        value = (value << 4) + hexit;
    }
    *ok = true;
    return value;
}

/* Sets 'ufid' to all-zero-bits. */
void
ufid_zero(ovs_u128_ *ufid)
{
    *ufid = UFID_ZERO;
}

bool
ufid_from_string_prefix(ovs_u128_ *ufid, const char *s)
{
    /* 0         1         2         3      */
    /* 012345678901234567890123456789012345 */
    /* ------------------------------------ */
    /* 00000000-1111-1111-2222-222233333333 */

    bool ok;

    ufid->id.u32[0] = lan_hexits_value(s, 8, &ok);
    if (!ok || s[8] != '-')
    {
        goto error;
    }

    ufid->id.u32[1] = lan_hexits_value(s + 9, 4, &ok) << 16;
    if (!ok || s[13] != '-')
    {
        goto error;
    }

    ufid->id.u32[1] += lan_hexits_value(s + 14, 4, &ok);
    if (!ok || s[18] != '-')
    {
        goto error;
    }

    ufid->id.u32[2] = lan_hexits_value(s + 19, 4, &ok) << 16;
    if (!ok || s[23] != '-')
    {
        goto error;
    }

    ufid->id.u32[2] += lan_hexits_value(s + 24, 4, &ok);
    if (!ok)
    {
        goto error;
    }

    ufid->id.u32[3] = lan_hexits_value(s + 28, 8, &ok);
    if (!ok)
    {
        goto error;
    }
    return true;

error:
    ufid_zero(ufid);
    return false;
}

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
void parse_lan_stats(char *buf[], lan_stats_instance_t *lan_stats_instance)
{
    dp_ctl_stats_t *stats;
    int i = 0;
    char *pos = NULL;
    int ret = 0;
    bool rc;

    if (lan_stats_instance == NULL) return;
    stats = &lan_stats_instance->stats;

    while (buf[i] != NULL)
    {
        ret = strncmp(buf[i], OVS_DUMP_UFID_PREFIX, OVS_DUMP_UFID_PREFIX_LEN);
        if (ret == 0)
        {
            // Get ufid
            pos = buf[i] + OVS_DUMP_UFID_PREFIX_LEN;
            rc = ufid_from_string_prefix(&stats->ufid, pos);
            if (!rc)
            {
                LOGE("Couldn't convert ufid string to value\n");
            }
        }
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
}

void
lan_stats_parse_flows(lan_stats_instance_t *lan_stats_instance, char *buf)
{
    char *tokens[MAX_TOKENS] = {0};
    char *tok = NULL;
    char *sep = ",";
    int i = 0;

    tok = strtok(buf, sep);
    while (tok)
    {
        tokens[i++] = tok;
        tok = strtok(NULL, sep);
        if (i >= (MAX_TOKENS - 1))
            break;
    }
    tokens[i] = NULL;
    parse_lan_stats(tokens, lan_stats_instance);
}

static int
lan_stats_alloc_aggr(lan_stats_instance_t *lan_stats_instance)
{
    struct net_md_aggregator_set aggr_set;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    struct node_info node_info;
    int report_type = 0;

    collector = lan_stats_instance->collector;
    memset(&aggr_set, 0, sizeof(aggr_set));
    node_info.node_id = collector->get_mqtt_hdr_node_id();
    node_info.location_id = collector->get_mqtt_hdr_loc_id();
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
        return -1;
    }
    lan_stats_instance->aggr = aggr;
    collector->plugin_ctx = lan_stats_instance;

   return 0;
}

static void
lan_stats_activate_window(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    bool ret = false;

    lan_stats_instance = collector->plugin_ctx;
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGD("%s: Aggergator is empty", __func__);
        return;
    }

    ret = net_md_activate_window(aggr);
    if (!ret)
    {
        LOGD("%s: Aggregator window activation failed", __func__);
        return;
    }
}

static void
lan_stats_close_window(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    struct flow_window **windows;
    struct flow_window *window;
    struct flow_report *report;
    bool ret;

    if (collector == NULL) return;

    lan_stats_instance = collector->plugin_ctx;
    if (lan_stats_instance == NULL) return;

    aggr = lan_stats_instance->aggr;
    if (aggr == NULL) return;

    ret = net_md_close_active_window(aggr);
    if (!ret)
    {
        LOGD("%s: Aggregator close window failed", __func__);
        return;
    }

    report = aggr->report;
    if (report == NULL) return;

    windows = report->flow_windows;
    window = *windows;

    LOGI("%s: %s: total flows: %zu, held flows: %zu, reported flows: %zu, eth pairs %zu",
         __func__, collector->name, aggr->total_flows, aggr->held_flows,
         window->num_stats, aggr->total_eth_pairs);
}

static void
lan_stats_send_aggr_report(lan_stats_instance_t *lan_stats_instance)
{
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    bool ret = false;

    collector = lan_stats_instance->collector;
    aggr = lan_stats_instance->aggr;
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

    LOGT("%s(): sending the report", __func__);
    ret = aggr->send_report(aggr, collector->mqtt_topic);
    if (ret == false)
    {
        LOGD("%s: Aggregator send report failed", __func__);
        return;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        net_md_log_aggr(aggr);
    }
}

static void
set_filter_info(fcm_filter_l2_info_t *l2_filter_info,
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

static void
lan_stats_aggr_add_sample(fcm_collect_plugin_t *collector, dp_ctl_stats_t *stats)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    struct flow_counters pkts_ct;
    struct net_md_flow_key key;
    char *device_tag;
    bool ret = false;

    lan_stats_instance = collector->plugin_ctx;
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGE("%s: Aggr is NULL", __func__);
        return;
    }

    device_tag = (lan_stats_instance->parent_tag != NULL) ?
                  lan_stats_instance->parent_tag : ETH_DEVICES_TAG;
    memset(&key, 0, sizeof(struct net_md_flow_key));
    memset(&pkts_ct, 0, sizeof(struct flow_counters));
    key.ufid = &stats->ufid.id;
    key.smac = &stats->smac_key;
    key.isparent_of_smac = lan_stats_is_mac_in_tag(device_tag, key.smac);
    key.dmac = &stats->dmac_key;
    key.isparent_of_dmac = lan_stats_is_mac_in_tag(device_tag, key.dmac);
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
    if (!ret) LOGD("%s: Add sample to aggregator failed", __func__);
}

static void
lan_stats_send_report_cb(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;

    if (collector == NULL) return;

    if (collector->mqtt_topic == NULL) return;

    lan_stats_instance = lan_stats_get_active_instance();
    if (lan_stats_instance != collector->plugin_ctx)
    {
      LOGT("%s(): Not an active instance, not sending report", __func__);
      return;
    }

    lan_stats_close_window(collector);
    lan_stats_send_aggr_report(lan_stats_instance);
    lan_stats_activate_window(collector);
}

void
lan_stats_flows_filter(lan_stats_instance_t *lan_stats_instance)
{
    fcm_filter_l2_info_t l2_filter_info;
    fcm_filter_stats_t   l2_filter_pkts;
    struct fcm_filter_client *client;
    fcm_collect_plugin_t *collector;
    struct fcm_session *session;
    struct fcm_filter_req *req;
    dp_ctl_stats_t *stats;
    bool allow;

    if (lan_stats_instance == NULL) return;
    stats = &lan_stats_instance->stats;

    collector = lan_stats_instance->collector;
    if (collector == NULL) return;

    session = lan_stats_instance->session;
    if (session == NULL) return;

    set_filter_info(&l2_filter_info, &l2_filter_pkts, stats);

    client = lan_stats_instance->c_client;
    if (client == NULL) return;

    req = CALLOC(1, sizeof(struct fcm_filter));
    if (req == NULL) return;

    req->pkts =  &l2_filter_pkts;
    req->l2_info = &l2_filter_info;
    req->table = client->table;

    if (collector->filters.collect != NULL)
    {
        fcm_apply_filter(session, req);
        allow = req->action;

        if (allow)
        {
            LOGD("%s: Flow collect allowed: filter_name: %s, ufid: "PRI_os_ufid_t \
                    " smac: %s, dmac: %s, vlan_id: %d, eth_type: %d, pks: %ld, " \
                    "bytes: %ld\n", __func__,
                    collector->filters.collect ?
                    collector->filters.collect : dflt_fltr_name,
                    FMT_os_ufid_t_pt(&stats->ufid.id),
                    stats->smac_addr,
                    stats->dmac_addr, stats->vlan_id, stats->eth_val,
                    stats->pkts, stats->bytes);
            lan_stats_aggr_add_sample(collector, stats);
        }
        else
            LOGD("%s: Flow collect dropped: filter_name: %s, smac: %s, "\
                    "dmac: %s, vlan_id: %d, eth_type: %d, pks: %ld, "\
                    "bytes: %ld\n", __func__,
                    collector->filters.collect ?
                    collector->filters.collect : dflt_fltr_name,
                    stats->smac_addr, stats->dmac_addr,
                    stats->vlan_id, stats->eth_val, stats->pkts, stats->bytes);
    }
    else
    {
        LOGD("%s: aggr add sample", __func__);
        lan_stats_aggr_add_sample(collector, stats);
    }
    FREE(req);
}

void
lan_stats_collect_cb(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;

    if (collector == NULL) return;

    mgr = lan_stats_get_mgr();
    lan_stats_instance = collector->plugin_ctx;

    /* collect stats only for active instance. */
    if (lan_stats_instance != mgr->active) return;

    lan_stats_instance->collect_flows(lan_stats_instance);
}


void lan_stats_plugin_exit(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    struct net_md_aggregator *aggr;
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized) return;

    if (mgr->num_sessions == 0) return;
    mgr->num_sessions--;

    lan_stats_instance = lan_stats_lookup_session(collector);
    if (lan_stats_instance == NULL)
    {
        LOGI("%s(): could not find instance", __func__);
        return;
    }

    /* free the parent tag */
    FREE(lan_stats_instance->parent_tag);

    /* free the aggregator */
    aggr = lan_stats_instance->aggr;
    if (aggr == NULL)
    {
        LOGD("%s(): Aggregator is empty", __func__);
        return;
    }
    net_md_close_active_window(aggr);
    net_md_free_aggregator(aggr);
    FREE(aggr);

    /* delete the session */
    ds_tree_remove(&mgr->lan_stats_sessions, lan_stats_instance);
    FREE(lan_stats_instance);

    /* mark the remaining session as active if any */
    lan_stats_instance = ds_tree_head(&mgr->lan_stats_sessions);
    if (lan_stats_instance != NULL) mgr->active = lan_stats_instance;

    return;
}

void
lan_stats_plugin_close_cb(fcm_collect_plugin_t *collector)
{
    LOGD("%s: lan stats stats plugin stopped", __func__);
    lan_stats_plugin_exit(collector);
}

/* Entry function for plugin */
int lan_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    lan_stats_instance_t *lan_stats_instance;
    lan_stats_mgr_t *mgr;
    char *parent_tag;
    char *active;
    char *name;
    int rc;

    mgr = lan_stats_get_mgr();
    if (!mgr->initialized)
    {
        LOGI("%s(): initializing lan stats manager", __func__);
        lan_stats_init_mgr(collector->loop);
    }

    if (mgr->num_sessions == mgr->max_sessions)
    {
        LOGI("%s: max lan stats session %d reached. Exiting", __func__,
             mgr->max_sessions);
        return -1;
    }

    lan_stats_instance = lan_stats_get_session(collector);
    if (lan_stats_instance == NULL)
    {
        LOGD("%s: could not add lan stats instance", __func__);
        return -1;
    }

    if (lan_stats_instance->initialized) return 0;
    lan_stats_instance->collector = collector;
    lan_stats_instance->name = collector->name;

    lan_stats_instance->session = collector->session;

    if (collector->collect_client != NULL)
    {
        lan_stats_instance->c_client = collector->collect_client;
    }

    if (collector->report_client != NULL)
    {
        lan_stats_instance->r_client = collector->report_client;
    }

    collector->collect_periodic = lan_stats_collect_cb;
    collector->send_report = lan_stats_send_report_cb;
    collector->close_plugin = lan_stats_plugin_close_cb;
    
    rc = lan_stats_alloc_aggr(lan_stats_instance);
    if (rc != 0)
    {
        LOGI("%s(): failed to allocate flow aggregator", __func__);
        return -1;
    }

    lan_stats_activate_window(collector);

    lan_stats_instance->collect_flows = lan_stats_collect_flows;
    lan_stats_instance->initialized = true;

    /* Check if the session has a name */
    name = collector->name;
    mgr->num_sessions++;

    /* Check if the session has the active key set */
    parent_tag = collector->get_other_config(collector,
                                             "parent_tag");

    if (parent_tag != NULL)
    {
        lan_stats_instance->parent_tag = STRDUP(parent_tag);
    }

    if (mgr->num_sessions == 1)
    {
        LOGI("%s: %s is the active session", __func__,
             name ? name : "default");
        mgr->active = lan_stats_instance;
        return 0;
    }

    /* Check if the session has the active key set */
    active = collector->get_other_config(collector,
                                         "active");

    if (active != NULL)
    {
        LOGI("%s: %s is now the active session", __func__,
             name ? name : "default");
        mgr->active = lan_stats_instance;
    }

    return 0;
}

void
lan_stats_init_mgr(struct ev_loop *loop)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->max_sessions = 2;
    ds_tree_init(&mgr->lan_stats_sessions, lan_stats_session_cmp,
                 lan_stats_instance_t, lan_stats_node);

    mgr->debug = false;
    mgr->initialized = true;

    return;
}

void
lan_stats_exit_mgr(void)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->initialized = false;
}
