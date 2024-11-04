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
#include <string.h>
#include <ev.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "os_types.h"
#include "os.h"
#include "log.h"
#include "ds.h"
#include "fcm.h"
#include "ct_stats.h"
#include "ct_stats_remark.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "fcm_filter.h"
#include "fcm_report_filter.h"
#include "neigh_table.h"
#include "util.h"
#include "fsm_dpi_utils.h"
#include "nf_utils.h"
#include "kconfig.h"
#include "memutil.h"
#include "sockaddr_storage.h"
#include "data_report_tags.h"
#include "fsm_ipc.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * Singleton tracking the plugin state
 */
static flow_stats_mgr_t g_ct_stats =
{
    .initialized = false,
};


/**
 * @brief imc callback processing the protobuf received from fsm
 *
 * @param data a pointer to the protobuf
 * @param len the protobuf length
 */
static void
proto_recv_cb(void *data, size_t len)
{
    struct net_md_aggregator *aggr;
    struct packed_buffer recv_pb;
    flow_stats_t *ct_stats;

    ct_stats = ct_stats_get_active_instance();
    if (ct_stats == NULL)
    {
        LOGD("%s: No active instance", __func__);
        return;
    }

    aggr = ct_stats->aggr;
    recv_pb.buf = data;
    recv_pb.len = len;
    net_md_update_aggr(aggr, &recv_pb);
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
ct_stats_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief look up a session
 *
 * Looks up a session.
 * @param session the session to lookup
 * @return the session if found, NULL otherwise
 */
flow_stats_t *
ct_stats_lookup_session(fcm_collect_plugin_t *collector)
{
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = ct_stats_get_mgr();
    sessions = &mgr->ct_stats_sessions;

    ct_stats = ds_tree_find(sessions, collector);
    return ct_stats;
}


/**
 * @brief look up or allocate a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
flow_stats_t *
ct_stats_get_session(fcm_collect_plugin_t *collector)
{
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    ds_tree_t *sessions;

    mgr = ct_stats_get_mgr();
    sessions = &mgr->ct_stats_sessions;

    ct_stats = ds_tree_find(sessions, collector);
    if (ct_stats != NULL) return ct_stats;

    LOGD("%s: Adding a new session", __func__);
    ct_stats = CALLOC(1, sizeof(*ct_stats));

    ct_stats->initialized = false;

    ds_tree_insert(sessions, ct_stats, collector);
    return ct_stats;
}


/**
 * @brief returns the pointer to the plugin's global state tracker
 */
flow_stats_mgr_t *
ct_stats_get_mgr(void)
{
    return &g_ct_stats;
}


/**
 * @brief returns the pointer to the active plugin
 */
flow_stats_t *
ct_stats_get_active_instance(void)
{
    flow_stats_mgr_t *mgr;

    mgr = ct_stats_get_mgr();
    if (mgr == NULL) return NULL;

    return mgr->active;
}

/**
 * @brief checks if an ipv4 or ipv6 address represents
 *        a broadcast or multicast ip.
 *        ipv4 multicast starts 0xE.
 *        ipv4 broadcast starts 0xF.
 *        ipv6 multicast starts 0XFF.
 * @param af the the inet family
 * @param ip a pointer to the ip address buffer
 * @return true if broadcast/multicast false otherwise.
 */
static bool
ct_stats_filter_ip(int af, void *ip)
{
    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)ip;
        if (((in4->sin_addr.s_addr & htonl(0xE0000000)) == htonl(0xE0000000) ||
            (in4->sin_addr.s_addr & htonl(0xF0000000)) == htonl(0xF0000000)))
        {
            LOGD("%s: Dropping ipv4 broadcast/multicast[%x]\n",
                  __func__, in4->sin_addr.s_addr);
            return true;
        }
        else if ((in4->sin_addr.s_addr & htonl(0x7F000000)) == htonl(0x7F000000))
        {
            LOGD("%s: Dropping ipv4 localhost[%x]\n",
                  __func__, in4->sin_addr.s_addr);
            return true;
        }
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ip;
        if ((in6->sin6_addr.s6_addr[0] & 0xF0) == 0xF0)
        {
            LOGD("%s: Dropping ipv6 multicast starting with [%x%x]\n",
                  __func__, in6->sin6_addr.s6_addr[0], in6->sin6_addr.s6_addr[1]);
            return true;
        }
        else if (memcmp(&in6->sin6_addr, &in6addr_loopback,
                        sizeof(struct in6_addr)) == 0)
        {
            LOGD("%s: Dropping ipv6 localhost [::%x]\n",
                  __func__, in6->sin6_addr.s6_addr[15]);
            return true;
        }
    }
    return false;
}


int
ct_get_stats(void)
{
    flow_stats_t *ct_stats;
    int rc;

    ct_stats = ct_stats_get_active_instance();
    if (ct_stats == NULL) return -1;

    /* get IPv4 conntrack entries from kernel */
    rc = nf_ct_get_flow_entries(AF_INET, &ct_stats->ctflow_list, ct_stats->ct_zone);
    if (rc == -1)
    {
        LOGE("%s: IPV4 conntrack flow collection error", __func__);
        return -1;
    }

    /* get IPv6 conntrack entries from kernel */
    rc = nf_ct_get_flow_entries(AF_INET6, &ct_stats->ctflow_list, ct_stats->ct_zone);
    if (rc == -1)
    {
        LOGE("%s: IPv6 conntrack flow collection error", __func__);
        return -1;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) nf_ct_print_entries(&ct_stats->ctflow_list);

    return 0;
}

/**
 * @brief frees the temporary list of parsed flows
 *
 * @param ct_stats the container of the list
 */
static void
free_ct_flow_list(flow_stats_t *ct_stats)
{
    ct_flow_t *flow;
    int del_count;

    del_count = 0;
    while (!ds_dlist_is_empty(&ct_stats->ctflow_list))
    {
        flow = ds_dlist_remove_tail(&ct_stats->ctflow_list);
        FREE(flow);
        ct_stats->node_count--;
        del_count++;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        LOGT("%s: del_count %d node_count %d", __func__,
             del_count, ct_stats->node_count);
    }
    ct_stats->node_count = 0;
}


/**
 * @brief apply the named filter to the given flow
 *
 * @param filter_name the filter name
 * @param mac_filter the mac filter
 * @param flow the flow to filter
 */
static bool
apply_filter(flow_stats_t *ct_stats, fcm_filter_l2_info_t *mac_filter,
             ct_flow_t *flow)
{
    struct fcm_filter_client *client;
    struct fcm_session *session;
    fcm_filter_l3_info_t filter;
    struct fcm_filter_req *req;
    fcm_filter_stats_t pkt;
    bool action;
    int rc;

    if (ct_stats == NULL) return true;

    session = ct_stats->session;
    if (session == NULL) return true;

    rc = getnameinfo((struct sockaddr *)&flow->layer3_info.src_ip,
                     sizeof(struct sockaddr_storage),
                     filter.src_ip, sizeof(filter.src_ip),
                     0, 0, NI_NUMERICHOST);
    if (rc < 0) return false;

    rc = getnameinfo((struct sockaddr *)&flow->layer3_info.dst_ip,
                     sizeof(struct sockaddr_storage),
                     filter.dst_ip, sizeof(filter.dst_ip),
                     0, 0, NI_NUMERICHOST);
    if (rc < 0) return false;

    filter.sport = ntohs(flow->layer3_info.src_port);
    filter.dport = ntohs(flow->layer3_info.dst_port);
    filter.l4_proto = flow->layer3_info.proto_type;
    filter.ip_type = flow->layer3_info.family_type;

    pkt.pkt_cnt = flow->pkt_info.pkt_cnt;
    pkt.bytes = flow->pkt_info.bytes;

    client = ct_stats->c_client;
    if (client == NULL) return true;

    req = CALLOC(1, sizeof(struct fcm_filter));
    if (req == NULL) return true;

    req->pkts = &pkt;
    req->l3_info = &filter;
    req->l2_info = mac_filter;
    req->table = client->table;

    fcm_apply_filter(session, req);
    action = req->action;
    FREE(req);

    return action;
}

/**
 * @brief adds collected conntrack info to the plugin aggregator
 *
 * @param ct_stats the aggregator container
 */
void
ct_flow_add_sample(flow_stats_t *ct_stats)
{
    struct net_md_aggregator *aggr;
    struct flow_counters pkts_ct;
    struct net_md_flow_key key;
    ctflow_info_t *flow_info;
    int sample_count;
    ct_flow_t *flow;
    bool ret;

    aggr = ct_stats->aggr;
    sample_count = 0;

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        bool                     smac_lookup;
        bool                     dmac_lookup;
        bool                     skip_flow;
        fcm_filter_l2_info_t     mac_filter;
        struct sockaddr_storage *ssrc;
        struct sockaddr_storage *sdst;
        os_macaddr_t             smac;
        os_macaddr_t             dmac;
        int                      af;

        memset(&smac, 0, sizeof(os_macaddr_t));
        memset(&dmac, 0, sizeof(os_macaddr_t));

        flow = &flow_info->flow;
        af = flow->layer3_info.src_ip.ss_family;

        ssrc = &flow->layer3_info.src_ip;
        sdst = &flow->layer3_info.dst_ip;

        // Lookup source ip.
        smac_lookup = neigh_table_lookup(ssrc, &smac);
        dmac_lookup = neigh_table_lookup(sdst, &dmac);

        ct_stats->node_count++;
        /* add only if smac or dmac is present */
        if (!(smac_lookup ^ dmac_lookup)) continue;

        snprintf(mac_filter.src_mac, sizeof(mac_filter.src_mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 smac.addr[0], smac.addr[1],
                 smac.addr[2], smac.addr[3],
                 smac.addr[4], smac.addr[5]);
        snprintf(mac_filter.dst_mac, sizeof(mac_filter.src_mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 dmac.addr[0], dmac.addr[1],
                 dmac.addr[2], dmac.addr[3],
                 dmac.addr[4], dmac.addr[5]);


        skip_flow = nf_ct_filter_ip(af, &flow->layer3_info.dst_ip);
        if (skip_flow) continue;

        skip_flow = nf_ct_filter_ip(af, &flow->layer3_info.src_ip);
        if (skip_flow) continue;

        if (apply_filter(ct_stats, &mac_filter, flow))
        {
            memset(&key, 0, sizeof(struct net_md_flow_key));
            memset(&pkts_ct, 0, sizeof(struct flow_counters));
            if (smac_lookup) key.smac = &smac;
            if (dmac_lookup) key.dmac = &dmac;

            key.ip_version = (af == AF_INET ? 4 : 6);
            if (af == AF_INET)
            {
                struct sockaddr_in *ssrc;
                struct sockaddr_in *sdst;

                ssrc = (struct sockaddr_in *)&flow->layer3_info.src_ip;
                sdst = (struct sockaddr_in *)&flow->layer3_info.dst_ip;
                key.src_ip = (uint8_t *)&ssrc->sin_addr.s_addr;
                key.dst_ip = (uint8_t *)&sdst->sin_addr.s_addr;
            }
            else if (af == AF_INET6)
            {
                struct sockaddr_in6 *ssrc;
                struct sockaddr_in6 *sdst;

                ssrc = (struct sockaddr_in6 *)&flow->layer3_info.src_ip;
                sdst = (struct sockaddr_in6 *)&flow->layer3_info.dst_ip;
                key.src_ip = ssrc->sin6_addr.s6_addr;
                key.dst_ip = sdst->sin6_addr.s6_addr;
            }
            key.ipprotocol = flow->layer3_info.proto_type;
            key.sport = flow->layer3_info.src_port;
            key.dport = flow->layer3_info.dst_port;
            key.flowmarker = flow->ct_mark;
            pkts_ct.packets_count = flow->pkt_info.pkt_cnt;
            pkts_ct.bytes_count = flow->pkt_info.bytes;
            if (flow->start) key.fstart = true;
            if (flow->end) key.fend = true;
            sample_count++;

            ret = net_md_add_sample(aggr, &key, &pkts_ct);
            if (!ret)
            {
                LOGW("%s: some error with net_md_add_sample", __func__);
                break;
            }
        }
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        LOGT("%s: sample add %d count %d", __func__,
             sample_count, ct_stats->node_count);
    }
    free_ct_flow_list(ct_stats);
}

void
ct_stats_update_flow(struct net_md_stats_accumulator *acc, int action)
{
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    int new_action;
    int old_action;
    int af;

    fkey = acc->fkey;
    key = acc->key;
    if (key == NULL) return;

    af = 0;
    if (key->ip_version == 4) af = AF_INET;
    if (key->ip_version == 6) af = AF_INET6;
    if (af == 0) return;

    new_action = action == FSM_BLOCK ? FSM_DPI_DROP : FSM_DPI_PASSTHRU;
    old_action =  fkey->flowmarker == CT_MARK_DROP ? FSM_DPI_DROP: FSM_DPI_PASSTHRU;

    if (old_action == new_action)
    {
        LOGT("%s: action not modified, not updating the flow", __func__);
        return;
    }

    LOGI("%s: Updating flow src: %s, dst: %s, proto: %d, sport: %d, dport: %d from: %s, to: %s",
         __func__,
         fkey->src_ip, fkey->dst_ip, fkey->protocol, fkey->sport, fkey->dport,
         old_action == FSM_BLOCK ? "drop" : "allow",
         action == FSM_BLOCK ? "drop" : "allow");

    fsm_set_ip_dpi_state(NULL, key->src_ip, key->dst_ip,
                         key->sport, key->dport,
                         key->ipprotocol, af, (action == FSM_BLOCK ? FSM_DPI_DROP : FSM_DPI_PASSTHRU), acc->flow_marker);
}


/**
 * @brief collector filter callback processing flows pushed from fsm
 *
 * This routine applies the collector filter on flows provided by FSM.
 *
 * @param aggr the aggregator processed in the network_metadata library
 * @key the flow key pushed by fsm
 */
bool
ct_stats_collect_filter_cb(struct net_md_aggregator *aggr,
                           struct net_md_flow_key *key, char *app_name)
{
    fcm_collect_plugin_t *collector;
    struct sockaddr_storage dst_ip;
    flow_stats_t *ct_stats;
    bool rc;
    int af;

    if (app_name != NULL)
    {
        LOGD("%s: processing fsm tag %s", __func__, app_name);
    }

    net_md_log_key(key, __func__);

    ct_stats = ct_stats_get_active_instance();
    if (ct_stats == NULL)
    {
        LOGD("%s: no active instance", __func__);
        return false;
    }

    collector = ct_stats->collector;
    fcm_filter_context_init(collector);
    if (key->ip_version == 4 || key->ip_version == 6)
    {
        af = (key->ip_version == 4 ? AF_INET : AF_INET6);
        sockaddr_storage_populate(af, key->dst_ip, &dst_ip);
        rc = ct_stats_filter_ip(af, &dst_ip);
        if (rc)
        {
            LOGD("%s: Dropping  v4/v6 broadcast/multicast flows.", __func__);
            return false;
        }
    }

    rc = fcm_collect_filter_nmd_callback(key);
    LOGD("%s: flow %s", __func__, rc ? "included" : "filtered out");

    return rc;
}


static void
ct_stats_set_data_report_tag(struct net_md_stats_accumulator *acc)
{
    struct data_report_tags **report_tags_array;
    struct data_report_tags *report_tags;
    struct str_set *smac_report_set;
    struct str_set *dmac_report_set;
    struct net_md_flow_key *key;
    struct str_set *report_tag;
    struct flow_key *fkey;
    char smac[32];
    char dmac[32];
    size_t idx;
    size_t i;

    key = acc->key;
    if (key == NULL) return;

    fkey = acc->fkey;
    if (fkey == NULL) return;

    fkey->num_data_report = 0;
    smac_report_set = NULL;
    dmac_report_set = NULL;

    if (key->smac != NULL)
    {
        MEMZERO(smac);
        snprintf(smac, sizeof(smac), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(key->smac));
        smac_report_set = data_report_tags_get_tags(key->smac);
        if (smac_report_set != NULL)
        {
            fkey->num_data_report++;
        }
        else
        {
            LOGD("%s(): report details are empty for smac %s", __func__,
                 smac);
        }
    }

    if (key->dmac != NULL)
    {
        MEMZERO(dmac);
        snprintf(dmac, sizeof(dmac), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(key->dmac));
        dmac_report_set = data_report_tags_get_tags(key->dmac);
        if (dmac_report_set != NULL)
        {
            fkey->num_data_report++;
        }
        else
        {
            LOGD("%s(): report details are empty for dmac %s", __func__,
                 dmac);
        }
    }

    if (fkey->num_data_report == 0) return;

    report_tags_array = CALLOC(fkey->num_data_report, sizeof(*report_tags_array));
    idx = 0;

    if (smac_report_set != NULL)
    {
        report_tags = CALLOC(1, sizeof(*report_tags));
        report_tags_array[idx] = report_tags;

        report_tag = CALLOC(smac_report_set->nelems, sizeof(*report_tag));
        report_tags->data_report = report_tag;

        report_tag->nelems = smac_report_set->nelems;
        report_tag->array = CALLOC(report_tag->nelems, sizeof(*report_tag->array));
        for (i = 0; i < report_tag->nelems; i++)
        {
            report_tag->array[i] = STRDUP(smac_report_set->array[i]);
        }
        report_tags->id = STRDUP(smac);
        idx++;
    }

    if (dmac_report_set != NULL)
    {
        report_tags = CALLOC(1, sizeof(*report_tags));
        report_tags_array[idx] = report_tags;

        report_tag = CALLOC(dmac_report_set->nelems, sizeof(*report_tag));
        report_tags->data_report = report_tag;

        report_tag->nelems = dmac_report_set->nelems;
        report_tag->array = CALLOC(report_tag->nelems, sizeof(*report_tag->array));
        for (i = 0; i < report_tag->nelems; i++)
        {
            report_tag->array[i] = STRDUP(dmac_report_set->array[i]);
        }
        report_tags->id = STRDUP(dmac);
    }

    fkey->data_report = report_tags_array;

    return;
}


/**
 * @brief callback from the accumulator reporting
 *
 * Called on the reporting of an accumulator
 * @param aggr the ct_stats aggregator
 * @param the accumulator being reported
 */
static void
ct_stats_on_acc_report(struct net_md_aggregator *aggr,
                       struct net_md_stats_accumulator *acc)
{
    struct net_md_stats_accumulator *rev_acc;
    struct flow_key *rev_fkey;
    struct flow_key *fkey;

    if (aggr == NULL) return;
    if (acc == NULL) return;

    fkey = acc->fkey;

    /* Add data report tags */
    ct_stats_set_data_report_tag(acc);

    rev_acc = net_md_lookup_reverse_acc(aggr, acc);
    if (rev_acc == NULL) return;

    /* update the networkid */
    if (fkey && (fkey->networkid == NULL))
    {
        rev_fkey = rev_acc->fkey;
        if (rev_fkey && rev_fkey->networkid)
        {
            fkey->networkid = STRDUP(rev_fkey->networkid);
        }
    }

    /* update the direction */
    if (acc->direction != NET_MD_ACC_UNSET_DIR) return;

    if (rev_acc->direction != NET_MD_ACC_UNSET_DIR)
    {
        acc->direction = rev_acc->direction;
        acc->originator = (rev_acc->originator == NET_MD_ACC_ORIGINATOR_SRC ?
                           NET_MD_ACC_ORIGINATOR_DST : NET_MD_ACC_ORIGINATOR_SRC);
    }

    return;
}


/**
 * @brief allocates a flow aggregator
 *
 * @param the collector info passed by fcm
 * @return 0 if successful, -1 otherwise
 */
static int
ct_stats_alloc_aggr(flow_stats_t *ct_stats)
{
    struct net_md_aggregator_set aggr_set;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    struct node_info node_info;
    int report_type;

    collector = ct_stats->collector;
    memset(&aggr_set, 0, sizeof(aggr_set));
    node_info.node_id = collector->get_mqtt_hdr_node_id();
    node_info.location_id = collector->get_mqtt_hdr_loc_id();
    aggr_set.info = &node_info;
    if (collector->fmt == FCM_RPT_FMT_CUMUL)
    {
        report_type = NET_MD_REPORT_ABSOLUTE;
    }
    else if (collector->fmt == FCM_RPT_FMT_DELTA)
    {
        report_type = NET_MD_REPORT_RELATIVE;
    }
    else
    {
        LOGE("%s: unknown report type request ed: %d", __func__,
             collector->fmt);
        return -1;
    }

    aggr_set.report_type = report_type;
    aggr_set.num_windows = 1;
    aggr_set.acc_ttl = (2 * collector->report_interval);
    aggr_set.report_filter = fcm_report_filter_nmd_callback;
    aggr_set.collect_filter = ct_stats_collect_filter_cb;
    aggr_set.neigh_lookup = neigh_table_lookup_af;
    aggr_set.on_acc_report = ct_stats_on_acc_report;
    aggr_set.process = ct_stats_get_dev2apps;
    aggr_set.on_acc_destroy = ct_stats_on_destroy_acc;
    aggr = net_md_allocate_aggregator(&aggr_set);
    if (aggr == NULL)
    {
        LOGD("%s: Aggregator allocation failed", __func__);
        return -1;
    }

    ct_stats->aggr = aggr;
    collector->plugin_ctx = ct_stats;

    return 0;
}

/**
 * @brief activates the flow aggregator window
 *
 * @param the collector info passed by fcm
 * @return 0 if successful, -1 otherwise
 */
int
ct_stats_activate_window(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    flow_stats_t *ct_stats;
    bool ret;

    ct_stats = collector->plugin_ctx;
    aggr = ct_stats->aggr;

    if (aggr == NULL)
    {
        LOGD("%s: Aggregator is empty", __func__);
        return -1;
    }

    ret = net_md_activate_window(aggr);
    if (ret == false)
    {
        LOGD("%s: Aggregator window activation failed", __func__);
        return -1;
    }

    return 0;
}


/**
 * @brief closes the flow aggregator window
 *
 * @param the collector info passed by fcm
 * @return 0 if successful, -1 otherwise
 */
void
ct_stats_close_window(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    flow_stats_t *ct_stats;
    bool ret;

    ct_stats = collector->plugin_ctx;
    aggr = ct_stats->aggr;

    if (aggr == NULL) return;

    ret = net_md_close_active_window(aggr);

    if (!ret)
    {
        LOGD("%s: Aggregator close window failed", __func__);
        return;
    }
}


/**
 * @brief send flow aggregator report
 *
 * @param the collector info passed by fcm
 * @return 0 if successful, -1 otherwise
 */
void
ct_stats_send_aggr_report(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    flow_stats_t *ct_stats;
    size_t n_flows;
    bool ret;

    ct_stats = collector->plugin_ctx;
    aggr = ct_stats->aggr;

    if (aggr == NULL) return;
    LOGI("%s: total flows: %zu held flows: %zu",
             __func__, aggr->total_flows, aggr->held_flows);

    n_flows = net_md_get_total_flows(aggr);
    if (n_flows <= 0)
    {
        net_md_reset_aggregator(aggr);
        return;
    }

    ret = aggr->send_report(aggr, collector->mqtt_topic);
    if (ret == false)
    {
        LOGD("%s: Aggregator send report failed", __func__);
        return;
    }
}


/**
 * @brief triggers conntrack records collection
 *
 * @param the collector info passed by fcm
 */
void
ct_stats_collect_cb(fcm_collect_plugin_t *collector)
{
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    int rc;

    if (collector == NULL) return;

    mgr = ct_stats_get_mgr();
    ct_stats = collector->plugin_ctx;
    if (ct_stats != mgr->active) return;

    rc = ct_get_stats();
    if (rc == -1) return;

    ct_stats = collector->plugin_ctx;
    ct_stats->collect_filter = collector->filters.collect;

    ct_flow_add_sample(ct_stats);
}


/**
 * @brief triggers conntrack records report
 *
 * @param the collector info passed by fcm
 */
void
ct_stats_report_cb(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    unsigned long max_flows;
    flow_stats_t *ct_stats;
    char *str_max_flows;
    uint16_t tmp_zone;
    char *ct_zone;

    if (collector == NULL) return;

    if (collector->mqtt_topic == NULL) return;

    ct_stats = ct_stats_get_active_instance();
    if (ct_stats != collector->plugin_ctx) return;

    fcm_filter_context_init(collector);
    ct_stats_close_window(collector);
    ct_stats_send_aggr_report(collector);
    ct_stats_activate_window(collector);

    /* Accept zone change after reporting */
    ct_zone = collector->get_other_config(collector, "ct_zone");
    tmp_zone = 0;
    if (ct_zone) tmp_zone = atoi(ct_zone);

    if (ct_stats->ct_zone != tmp_zone)
    {
        ct_stats->ct_zone = tmp_zone;
        LOGD("%s: updated zone: %d", __func__, ct_stats->ct_zone);
    }

    str_max_flows = collector->get_other_config(collector,
                                                "max_flows_per_window");
    max_flows = 0;
    if (str_max_flows != NULL)
    {
        max_flows = strtoul(str_max_flows, NULL, 10);
        if (max_flows == ULONG_MAX)
        {
            LOGD("%s: conversion of %s failed: %s", __func__,
                 str_max_flows, strerror(errno));
            max_flows = 0;
        }
    }
    aggr = collector->plugin_ctx;
    if (aggr == NULL) return;

    aggr->max_reports = (size_t)max_flows;
}


/**
 * @brief releases ct_stats plugin resources
 *
 * @param the collector info passed by fcm
 */
void
ct_stats_plugin_close_cb(fcm_collect_plugin_t *collector)
{
    LOGD("%s: CT stats plugin stopped", __func__);
    ct_stats_plugin_exit(collector);

    return;
}

/**
 * @brief initializes a ct_stats collector session
 *
 * @param collector ct_stats object provided by fcm
 */
int
ct_stats_plugin_init(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    unsigned long max_flows;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    char *str_max_flows;
    char *ct_zone;
    char *active;
    char *name;
    int rc;

    mgr = ct_stats_get_mgr();
    if (!mgr->initialized) ct_stats_init_mgr(collector->loop);

    if (mgr->num_sessions == mgr->max_sessions)
    {
        LOGI("%s: max session %d reached. Exiting", __func__,
             mgr->max_sessions);
        return -1;
    }

    ct_stats = ct_stats_get_session(collector);

    if (ct_stats == NULL)
    {
        LOGD("%s: could not add instance", __func__);
        return -1;
    }

    if (ct_stats->initialized) return 0;
    ct_stats->collector = collector;
    ct_stats->node_count = 0;
    ct_stats->name = collector->name;
    collector->collect_periodic = ct_stats_collect_cb;
    collector->send_report = ct_stats_report_cb;
    collector->close_plugin = ct_stats_plugin_close_cb;
    collector->process_flush_cache = ct_stats_process_flush_cache;

    ct_stats->session = collector->session;

    if (collector->collect_client != NULL)
    {
        ct_stats->c_client = collector->collect_client;
    }

    if (collector->report_client != NULL)
    {
        ct_stats->r_client = collector->report_client;
    }

    fcm_filter_context_init(collector);

    ds_dlist_init(&ct_stats->ctflow_list, ctflow_info_t, ct_node);
    ds_tree_init(&ct_stats->device2apps, ds_str_cmp, struct ct_device2apps, d2a_node);

    ct_zone = collector->get_other_config(collector, "ct_zone");
    if (ct_zone) ct_stats->ct_zone = atoi(ct_zone);
    else ct_stats->ct_zone = 0;
    LOGD("%s: configured zone: %d", __func__, ct_stats->ct_zone);

    rc = ct_stats_alloc_aggr(ct_stats);
    if (rc != 0) return -1;

    str_max_flows = collector->get_other_config(collector,
                                                "max_flows_per_window");
    max_flows = 0;
    if (str_max_flows != NULL)
    {
        max_flows = strtoul(str_max_flows, NULL, 10);
        if (max_flows == ULONG_MAX)
        {
            LOGD("%s: conversion of %s failed: %s", __func__,
                 str_max_flows, strerror(errno));
            max_flows = 0;
        }
    }

    /* Check if the session ihas the active key set */
    active = collector->get_other_config(collector,
                                         "active");
    aggr = ct_stats->aggr;
    if (aggr == NULL) goto err;

    aggr->max_reports = (size_t)max_flows;

    rc = ct_stats_activate_window(collector);
    if (rc != 0) goto err;

    ct_stats->initialized = true;

    /* Check if the session has a name */
    name = collector->name;
    mgr->num_sessions++;
    if (mgr->num_sessions == 1)
    {
        LOGI("%s: %s is now the active session", __func__,
             name ? name : "default");
        mgr->active = ct_stats;
        return 0;
    }

    /* Check if the session has the active key set */
    active = collector->get_other_config(collector,
                                         "active");
    if (active != NULL)
    {
        LOGI("%s: %s is now the active session", __func__,
             name ? name : "default");
        mgr->active = ct_stats;
    }

    return 0;

err:
    net_md_free_aggregator(ct_stats->aggr);
    FREE(ct_stats->aggr);
    collector->plugin_ctx = NULL;
    ct_stats->aggr = NULL;

    return -1;
}

/**
 * @brief delete ct_stats collector session
 *
 * @param collector ct_stats object provided by fcm
 */
void
ct_stats_plugin_exit(fcm_collect_plugin_t *collector)
{
    struct net_md_aggregator *aggr;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;

    mgr = ct_stats_get_mgr();
    if (!mgr->initialized) return;

    if (mgr->num_sessions == 0) return;
    mgr->num_sessions--;

    ct_stats = ct_stats_lookup_session(collector);

    if (ct_stats == NULL)
    {
        LOGI("%s: could not find instance", __func__);
        return;
    }

    /* free the aggregator */
    aggr = ct_stats->aggr;
    net_md_close_active_window(aggr);
    net_md_free_aggregator(aggr);
    FREE(aggr);

    /* delete the session */
    ds_tree_remove(&mgr->ct_stats_sessions, ct_stats);
    FREE(ct_stats);

    /* mark the remaining session as active if any */
    ct_stats = ds_tree_head(&mgr->ct_stats_sessions);
    if (ct_stats != NULL) mgr->active = ct_stats;

    if (mgr->num_sessions == 0) ct_stats_exit_mgr();

    return;
}

void
ct_stats_init_mgr(struct ev_loop *loop)
{
    flow_stats_mgr_t *mgr;
    int rc;

    mgr = ct_stats_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->max_sessions = 2;
    ds_tree_init(&mgr->ct_stats_sessions, ct_stats_session_cmp,
                 flow_stats_t, ct_stats_node);

    rc = fsm_ipc_server_init(loop, proto_recv_cb);
    if (rc != 0) return;

    mgr->debug = false;
    mgr->initialized = true;

    return;
}

void
ct_stats_exit_mgr(void)
{
    flow_stats_mgr_t *mgr;

    fsm_ipc_server_close();

    mgr = ct_stats_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->initialized = false;
}
