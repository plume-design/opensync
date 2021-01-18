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

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "log.h"
#include "network_metadata_report.h"

#define MAX_STRLEN 256

/**
 * @brief compares 2 flow keys'ethernet content
 *
 * Compares source mac, dest mac and vlan id
 * Used to lookup a node in the manager's eth_pairs tree
 *
 * @param a void pointer cast to a net_md_flow_key struct
 * @param a void pointer cast to a net_md_flow_key struct
 */
int net_md_eth_cmp(void *a, void *b)
{
    struct net_md_flow_key *key_a = a;
    struct net_md_flow_key *key_b = b;
    int cmp;

    if ((key_a->smac == NULL) ^ (key_b->smac == NULL)) return -1;
    if ((key_a->dmac == NULL) ^ (key_b->dmac == NULL)) return 1;

    /* Compare source mac addresses */
    if (key_a->smac != NULL)
    {
        cmp = memcmp(key_a->smac->addr, key_b->smac->addr,
                     sizeof(key_a->smac->addr));
        if (cmp != 0) return cmp;
    }

    /* Compare destination mac addresses */
    if (key_a->dmac != NULL)
    {
        cmp = memcmp(key_a->dmac->addr, key_b->dmac->addr,
                     sizeof(key_a->dmac->addr));
        if (cmp != 0) return cmp;
    }

    /* Compare vlan id */
   cmp = (int)(key_a->vlan_id) - (int)(key_b->vlan_id);

   return cmp;
}


/**
 * @brief compares 2 flow keys'ethertype
 *
 * Compares ethertype
 * Used to lookup a node in the manager's eth_pairs ethertype tree
 *
 * @param a void pointer cast to a net_md_flow_key struct
 * @param a void pointer cast to a net_md_flow_key struct
 */
int net_md_ethertype_cmp(void *a, void *b)
{
    struct net_md_flow_key *key_a = a;
    struct net_md_flow_key *key_b = b;
    int cmp;

    /* Compare vlan id */
   cmp = (int)(key_a->ethertype) - (int)(key_b->ethertype);

   return cmp;
}


/**
 * @brief compares 2 five tuples content
 *
 * Compare the 5 tuple content of the given keys
 * Used to lookup a node in an eth_pair's 5 tuple tree.
 * The lookup could be organized differently, using a hash approach.
 * The approach can be optimized later on without changing the API
 *
 * @param a void pointer cast to a net_md_flow_key struct
 * @param a void pointer cast to a net_md_flow_key struct
 */
int net_md_5tuple_cmp(void *a, void *b)
{
    struct net_md_flow_key *key_a  = a;
    struct net_md_flow_key *key_b  = b;
    size_t ipl;
    int cmp;

    /* Compare ip versions */
    cmp = (int)(key_a->ip_version) - (int)(key_b->ip_version);
    if (cmp != 0) return cmp;

    /* Get ip version compare length */
    ipl = (key_a->ip_version == 4 ? 4 : 16);

    /* Compare source IP addresses */
    cmp = memcmp(key_a->src_ip, key_b->src_ip, ipl);
    if (cmp != 0) return cmp;

    /* Compare destination IP addresses */
    cmp = memcmp(key_a->dst_ip, key_b->dst_ip, ipl);
    if (cmp != 0) return cmp;

    /* Compare ip protocols */
    cmp = (int)(key_a->ipprotocol) - (int)(key_b->ipprotocol);
    if (cmp != 0) return cmp;

    /* Compare source ports */
    cmp = (int)(key_a->sport) - (int)(key_b->sport);
    if (cmp != 0) return cmp;

    /* Compare destination ports */
    cmp = (int)(key_a->dport) - (int)(key_b->dport);
    return cmp;
}


/**
 * @brief helper function: string to os_macaddr_t
 *
 * @param strmac: ethernet mac in string representation
 * @return a os_macaddr_t pointer
 */
os_macaddr_t *str2os_mac(char *strmac)
{
    os_macaddr_t *mac;
    size_t len, i, j;

    if (strmac == NULL) return NULL;

    /* Validate the input string */
    len = strlen(strmac);
    if (len != 17) return NULL;

    mac = calloc(1, sizeof(*mac));
    if (mac == NULL) return NULL;

    i = 0;
    j = 0;
    do {
        char a = strmac[i++];
        char b = strmac[i++];
        uint8_t v;

        if (!isxdigit(a)) goto err_free_mac;
        if (!isxdigit(b)) goto err_free_mac;

        v = (isdigit(a) ? (a - '0') : (toupper(a) - 'A' + 10));
        v *= 16;
        v += (isdigit(b) ? (b - '0') : (toupper(b) - 'A' + 10));
        mac->addr[j] = v;

        if (i == len) break;
        if (strmac[i++] != ':') goto err_free_mac;
        j++;
    } while (i < len);

    return mac;

err_free_mac:
    free(mac);

    return NULL;
}


char * net_md_set_str(char *in_str)
{
    char *out;
    size_t len;

    if (in_str == NULL) return NULL;

    len = strnlen(in_str, MAX_STRLEN);
    if (len == 0) return NULL;

    out = strndup(in_str, MAX_STRLEN);

    return out;
}

os_macaddr_t * net_md_set_os_macaddr(os_macaddr_t *in_mac)
{
    os_macaddr_t *mac;

    if (in_mac == NULL) return NULL;

    mac = calloc(1, sizeof(*mac));
    if (mac == NULL) return NULL;

    memcpy(mac, in_mac, sizeof(*mac));
    return mac;
}


bool net_md_set_ip(uint8_t ipv, uint8_t *ip, uint8_t **ip_tgt)
{
    size_t ipl;

    if ((ipv != 4) && (ipv != 6))
    {
        *ip_tgt = NULL;
        return true;
    }

    ipl = (ipv == 4 ? 4 : 16);

    *ip_tgt = calloc(1, ipl);
    if (*ip_tgt == NULL) return false;

    memcpy(*ip_tgt, ip, ipl);
    return true;
}


struct node_info * net_md_set_node_info(struct node_info *info)
{
    struct node_info *node;

    if (info == NULL) return NULL;

    node = calloc(1, sizeof(*node));
    if (node == NULL) return NULL;

    node->node_id = net_md_set_str(info->node_id);
    if (node->node_id == NULL) goto err_free_node;;

    node->location_id = net_md_set_str(info->location_id);
    if (node->location_id == NULL) goto err_free_node_id;

    return node;

err_free_node_id:
    free(node->node_id);

err_free_node:
    free(node);

    return NULL;
}


void free_node_info(struct node_info *node)
{
    if (node == NULL) return;

    free(node->node_id);
    free(node->location_id);

    free(node);
}


void
free_flow_key_tag(struct flow_tags *tag)
{
    size_t i;

    free(tag->vendor);
    free(tag->app_name);

    for (i = 0; i < tag->nelems; i++) free(tag->tags[i]);
    free(tag->tags);

    free(tag);
}

void
free_flow_key_tags(struct flow_key *key)
{
    size_t i;

    for (i = 0; i < key->num_tags; i++)
    {
        free_flow_key_tag(key->tags[i]);
        key->tags[i] = NULL;
    }

    free(key->tags);
}

void
free_flow_key_vendor_data(struct flow_vendor_data *vd)
{
    struct vendor_data_kv_pair *kv;
    size_t i;

    for (i = 0; i < vd->nelems; i++)
    {
        kv = vd->kv_pairs[i];
        free(kv->key);
        free(kv->str_value);
        free(kv);
        vd->kv_pairs[i] = NULL;
    }

    free(vd->vendor);
    free(vd->kv_pairs);
    free(vd);
}

void
free_flow_key_vdr_data(struct flow_key *key)
{
    size_t i;

    for (i = 0; i < key->num_vendor_data; i++)
    {
        free_flow_key_vendor_data(key->vdr_data[i]);
        key->vdr_data[i] = NULL;
    }

    free(key->vdr_data);
}

void
free_flow_key(struct flow_key *key)
{
    if (key == NULL) return;

    free(key->smac);
    free(key->dmac);
    free(key->src_ip);
    free(key->dst_ip);

    free_flow_key_tags(key);
    free_flow_key_vdr_data(key);
    free(key);
}


void free_flow_counters(struct flow_counters *counters)
{
    if (counters == NULL) return;

    free(counters);
}


void free_window_stats(struct flow_stats *stats)
{
    if (stats == NULL) return;

    if (stats->owns_key) free_flow_key(stats->key);
    free_flow_counters(stats->counters);

    free(stats);
}


void free_report_window(struct flow_window *window)
{
    size_t i;

    if (window == NULL) return;

    for (i = 0; i < window->num_stats; i++)
    {
        free_window_stats(window->flow_stats[i]);
        window->flow_stats[i] = NULL;
    }

    free(window->flow_stats);

    free(window);
}


void free_flow_report(struct flow_report *report)
{
    size_t i;

    if (report == NULL) return;

    for (i = 0; i < report->num_windows; i++)
    {
        free_report_window(report->flow_windows[i]);
        report->flow_windows[i] = NULL;
    }

    free(report->flow_windows);
    free_node_info(report->node_info);
    free(report);
}


void free_net_md_flow_key(struct net_md_flow_key *lkey)
{
    if (lkey == NULL) return;

    free(lkey->smac);
    free(lkey->dmac);
    free(lkey->src_ip);
    free(lkey->dst_ip);

    free(lkey);
}


struct net_md_flow_key * set_net_md_flow_key(struct net_md_flow_key *lkey)
{
    struct net_md_flow_key *key;
    bool ret, err;

    key = calloc(1, sizeof(*key));
    if (key == NULL) return NULL;

    key->smac = net_md_set_os_macaddr(lkey->smac);
    err = ((key->smac == NULL) && (lkey->smac != NULL));
    if (err) goto err_free_key;

    key->isparent_of_smac = lkey->isparent_of_smac;

    key->dmac = net_md_set_os_macaddr(lkey->dmac);
    err = ((key->dmac == NULL) && (lkey->dmac != NULL));
    if (err) goto err_free_smac;

    key->isparent_of_dmac = lkey->isparent_of_dmac;

    ret = net_md_set_ip(lkey->ip_version, lkey->src_ip, &key->src_ip);
    if (!ret) goto err_free_dmac;

    ret = net_md_set_ip(lkey->ip_version, lkey->dst_ip, &key->dst_ip);
    if (!ret) goto err_free_src_ip;

    key->ip_version = lkey->ip_version;
    key->vlan_id = lkey->vlan_id;
    key->ethertype = lkey->ethertype;
    key->ipprotocol = lkey->ipprotocol;
    key->sport = lkey->sport;
    key->dport = lkey->dport;
    key->fstart = lkey->fstart;
    key->fend = lkey->fend;

    return key;

err_free_src_ip:
    free(key->src_ip);

err_free_dmac:
    free(key->dmac);

err_free_smac:
    free(key->smac);

err_free_key:
    free(key);

    return NULL;
}


struct flow_key * net_md_set_flow_key(struct net_md_flow_key *key)
{
    char buf[INET6_ADDRSTRLEN];
    struct flow_key *fkey;
    const char *res;
    int family;
    size_t ip_size;

    fkey = calloc(1, sizeof(*fkey));
    if (fkey == NULL) return NULL;

    if (key->smac != NULL)
    {
        snprintf(buf, sizeof(buf), PRI_os_macaddr_lower_t,
                 FMT_os_macaddr_pt(key->smac));
        fkey->smac = strndup(buf, sizeof(buf));
        if (fkey->smac == NULL) goto err_free_fkey;
        fkey->isparent_of_smac = key->isparent_of_smac;
    }

    if (key->dmac != NULL)
    {
        snprintf(buf, sizeof(buf), PRI_os_macaddr_lower_t,
                 FMT_os_macaddr_pt(key->dmac));
        fkey->dmac = strndup(buf, sizeof(buf));
        if (fkey->dmac == NULL) goto err_free_smac;
        fkey->isparent_of_dmac = key->isparent_of_dmac;
    }

    fkey->vlan_id = key->vlan_id;
    fkey->ethertype = key->ethertype;

    if (key->ip_version == 0) return fkey;

    family = ((key->ip_version == 4) ? AF_INET : AF_INET6);
    ip_size = ((family == AF_INET) ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);

    fkey->ip_version = key->ip_version;

    fkey->src_ip = calloc(1, ip_size);
    if (fkey->src_ip == NULL) goto err_free_dmac;

    res = inet_ntop(family, key->src_ip, fkey->src_ip, ip_size);
    if (res == NULL) goto err_free_src_ip;

    fkey->dst_ip = calloc(1, ip_size);
    if (fkey->dst_ip == NULL) goto err_free_src_ip;

    res = inet_ntop(family, key->dst_ip, fkey->dst_ip, ip_size);
    if (res == NULL) goto err_free_dst_ip;

    fkey->protocol = key->ipprotocol;
    fkey->sport = ntohs(key->sport);
    fkey->dport = ntohs(key->dport);

    /* New flow is observed */
    fkey->state.first_obs = time(NULL);

    return fkey;

err_free_dst_ip:
    free(fkey->dst_ip);

err_free_src_ip:
    free(fkey->src_ip);

err_free_dmac:
    free(fkey->dmac);

err_free_smac:
    free(fkey->smac);

err_free_fkey:
    free(fkey);

    return NULL;
}

void net_md_acc_destroy_cb(struct net_md_stats_accumulator *acc)
{
    struct net_md_aggregator *aggr;

    aggr = acc->aggr;
    if (aggr == NULL) return;
    if (aggr->on_acc_destroy == NULL) return;

    aggr->on_acc_destroy(aggr, acc);
}


void net_md_free_acc(struct net_md_stats_accumulator *acc)
{
    if (acc == NULL) return;

    net_md_acc_destroy_cb(acc);

    free_net_md_flow_key(acc->key);
    free_flow_key(acc->fkey);
    if (acc->free_plugins != NULL) acc->free_plugins(acc);

    free(acc);
}


struct net_md_stats_accumulator *
net_md_set_acc(struct net_md_aggregator *aggr,
               struct net_md_flow_key *key)
{
    struct net_md_stats_accumulator *acc;

    if (key == NULL) return NULL;

    acc = calloc(1, sizeof(*acc));
    if (acc == NULL) return NULL;

    acc->key = set_net_md_flow_key(key);
    if (acc->key == NULL) goto err_free_acc;

    acc->fkey = net_md_set_flow_key(key);
    if (acc->fkey == NULL) goto err_free_md_flow_key;

    acc->fkey->state.report_attrs = true;

    if (aggr->on_acc_create != NULL) aggr->on_acc_create(aggr, acc);
    acc->aggr = aggr;

    return acc;

err_free_md_flow_key:
    free_net_md_flow_key(acc->key);

err_free_acc:
    free(acc);

    return NULL;
}


void net_md_free_flow(struct net_md_flow *flow)
{
    if (flow == NULL) return;

    net_md_free_acc(flow->tuple_stats);
    free(flow);
}


void net_md_free_flow_tree(ds_tree_t *tree)
{
    struct net_md_flow *flow, *next;

    if (tree == NULL) return;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        next = ds_tree_next(tree, flow);
        ds_tree_remove(tree, flow);
        net_md_free_flow(flow);
        flow = next;
    }

}


void net_md_free_eth_pair(struct net_md_eth_pair *pair)
{
    if (pair == NULL) return;

    net_md_free_acc(pair->mac_stats);
    net_md_free_flow_tree(&pair->ethertype_flows);
    net_md_free_flow_tree(&pair->five_tuple_flows);
    free(pair);
}


struct net_md_eth_pair *
net_md_set_eth_pair(struct net_md_aggregator *aggr,
                    struct net_md_flow_key *key)
{
    struct net_md_eth_pair *eth_pair;

    if (key == NULL) return NULL;

    eth_pair = calloc(1, sizeof(*eth_pair));
    if (eth_pair == NULL) return NULL;

    eth_pair->mac_stats = net_md_set_acc(aggr, key);
    if (eth_pair->mac_stats == NULL) goto err_free_eth_pair;

    ds_tree_init(&eth_pair->ethertype_flows, net_md_ethertype_cmp,
                 struct net_md_flow, flow_node);

    ds_tree_init(&eth_pair->five_tuple_flows, net_md_5tuple_cmp,
                 struct net_md_flow, flow_node);

    return eth_pair;

err_free_eth_pair:
    free(eth_pair);

    return NULL;
}


bool is_eth_only(struct net_md_flow_key *key)
{
    return (key->ip_version == 0);
}


struct net_md_stats_accumulator *
net_md_tree_lookup_acc(struct net_md_aggregator *aggr,
                       ds_tree_t *tree,
                       struct net_md_flow_key *key)
{
    struct net_md_flow *flow;
    struct net_md_stats_accumulator *acc;

    flow = ds_tree_find(tree, key);
    if (flow != NULL) return flow->tuple_stats;

    /* Allocate flow */
    flow = calloc(1, sizeof(*flow));
    if (flow == NULL) return NULL;

    /* Allocate the flow accumulator */
    acc = net_md_set_acc(aggr, key);
    if (acc == NULL) goto err_free_flow;

    flow->tuple_stats = acc;
    ds_tree_insert(tree, flow, acc->key);
    aggr->total_flows++;

    return acc;

err_free_flow:
    free(flow);

    return NULL;
}


struct net_md_stats_accumulator *
net_md_lookup_acc_from_pair(struct net_md_aggregator *aggr,
                            struct net_md_eth_pair *pair,
                            struct net_md_flow_key *key)
{
    ds_tree_t *tree;

    /* Check if the key refers to a L2 flow */
    tree = is_eth_only(key) ? &pair->ethertype_flows : &pair->five_tuple_flows;

    return net_md_tree_lookup_acc(aggr, tree, key);
}


bool has_eth_info(struct net_md_flow_key *key)
{
    bool ret;

    ret = (key->smac != NULL);
    ret |= (key->dmac != NULL);

    return ret;
}


struct net_md_eth_pair * net_md_lookup_eth_pair(struct net_md_aggregator *aggr,
                                                struct net_md_flow_key *key)
{
    struct net_md_eth_pair *eth_pair;
    bool has_eth;

    if (aggr == NULL) return NULL;
    has_eth = has_eth_info(key);
    if (!has_eth) return NULL;

    eth_pair = ds_tree_find(&aggr->eth_pairs, key);
    if (eth_pair != NULL) return eth_pair;

    /* Allocate and insert a new ethernet pair */
    eth_pair = net_md_set_eth_pair(aggr, key);
    if (eth_pair == NULL) return NULL;

    ds_tree_insert(&aggr->eth_pairs, eth_pair, eth_pair->mac_stats->key);

    return eth_pair;
}


struct net_md_stats_accumulator *
net_md_lookup_eth_acc(struct net_md_aggregator *aggr,
                      struct net_md_flow_key *key)
{
    struct net_md_eth_pair *eth_pair;
    struct net_md_stats_accumulator *acc;

    if (aggr == NULL) return NULL;

    eth_pair = net_md_lookup_eth_pair(aggr, key);
    if (eth_pair == NULL) return NULL;

    acc = net_md_lookup_acc_from_pair(aggr, eth_pair, key);
    if (acc != NULL) acc->aggr = aggr;

    return acc;
}


struct net_md_stats_accumulator *
net_md_lookup_acc(struct net_md_aggregator *aggr,
                  struct net_md_flow_key *key)
{
    struct net_md_stats_accumulator *acc;

    if (aggr == NULL) return NULL;

    if (has_eth_info(key)) return net_md_lookup_eth_acc(aggr, key);

    acc = net_md_tree_lookup_acc(aggr, &aggr->five_tuple_flows, key);
    if (acc != NULL) acc->aggr = aggr;

    return acc;
}


void net_md_set_counters(struct net_md_aggregator *aggr,
                         struct net_md_stats_accumulator *acc,
                         struct flow_counters *counters)
{
    if (acc->state != ACC_STATE_WINDOW_ACTIVE) aggr->active_accs++;

    acc->counters = *counters;
    acc->state = ACC_STATE_WINDOW_ACTIVE;
    acc->last_updated = time(NULL);
    acc->fkey->state.last_obs = acc->last_updated;
}


/* Get aggregator's active windows */
struct flow_window * net_md_active_window(struct net_md_aggregator *aggr)
{
    struct flow_report *report;
    struct flow_window *window;
    size_t idx;

    report = aggr->report;
    idx = aggr->windows_cur_idx;
    if (idx == aggr->max_windows) return NULL;

    window = report->flow_windows[idx];
    if (window != NULL) return window;

    window = calloc(1, sizeof(*window));
    if (window == NULL) return NULL;

    report->flow_windows[idx] = window;
    aggr->report->num_windows++;
    return window;
}


void net_md_close_counters(struct net_md_aggregator *aggr,
                           struct net_md_stats_accumulator *acc)
{
    acc->report_counters = acc->counters;

    /* Relative report */
    if (aggr->report_type == NET_MD_REPORT_RELATIVE)
    {
        if (acc->report_counters.bytes_count >= acc->first_counters.bytes_count)
        {
            acc->report_counters.bytes_count -= acc->first_counters.bytes_count;
        }

        if (acc->report_counters.packets_count >= acc->first_counters.packets_count)
        {
            acc->report_counters.packets_count -= acc->first_counters.packets_count;
        }
    }

    acc->first_counters = acc->counters;
}


bool net_md_add_sample_to_window(struct net_md_aggregator *aggr,
                                 struct net_md_stats_accumulator *acc)
{
    struct flow_window *window;
    struct flow_stats *stats;
    struct flow_key *fkey;
    size_t stats_idx;
    bool filter_add;

    window = net_md_active_window(aggr);
    if (window == NULL) return false;

    if (aggr->report_filter != NULL)
    {
        filter_add = aggr->report_filter(acc);
        if (filter_add == false)
        {
            fkey = acc->fkey;
            if (fkey == NULL) return false;

            /* request adding vendor attributes in the next report */
            fkey->state.report_attrs = true;

            return false;
        }
    }

    stats_idx = aggr->stats_cur_idx;
    if (stats_idx == window->provisioned_stats)
    {
        fkey = acc->fkey;
        if (fkey == NULL) return false;

        /* request adding vendor attributes in the next report */
        fkey->state.report_attrs = true;

        window->dropped_stats++;

        return false;
    }

    stats = window->flow_stats[stats_idx];
    stats->counters = calloc(1, sizeof(*(stats->counters)));
    if (stats->counters == NULL) return false;

    stats->owns_key = false;
    stats->key = acc->fkey;
    *stats->counters = acc->report_counters;

    aggr->stats_cur_idx++;
    aggr->total_report_flows++;
    return true;
}


void net_md_report_5tuples_accs(struct net_md_aggregator *aggr,
                                ds_tree_t *tree)
{
    struct net_md_flow *flow;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        struct net_md_stats_accumulator *acc;
        struct net_md_flow *next;
        struct net_md_flow *remove;
        time_t now;
        double cmp;
        bool active_flow;
        bool retire_flow;
        bool refd_flow;
        bool keep_flow;

        acc = flow->tuple_stats;
        active_flow = (acc->state == ACC_STATE_WINDOW_ACTIVE);
        active_flow |= acc->report;
        if (active_flow)
        {
            net_md_close_counters(aggr, acc);
            net_md_add_sample_to_window(aggr, acc);
            acc->state = ACC_STATE_WINDOW_RESET;
        }

        /* Clear the reporting request */
        acc->report = false;

        next = ds_tree_next(tree, flow);
        /* Check if the accumulator is old enough to be removed */
        now = time(NULL);
        cmp = difftime(now, acc->last_updated);
        retire_flow = (cmp >= aggr->acc_ttl);
        refd_flow = (acc->refcnt != 0);

        /* Account for inactive yet referenced flows */
        if (retire_flow && refd_flow) aggr->held_flows++;

        retire_flow &= (!refd_flow);

        /* keep the flow if it's active and not yet retired */
        keep_flow = (active_flow || !retire_flow);
        if (!keep_flow)
        {
            remove = flow;
            ds_tree_remove(tree, remove);
            net_md_free_flow(remove);
            aggr->total_flows--;
        }

        flow = next;
    }
}


void net_md_update_eth_acc(struct net_md_stats_accumulator *eth_acc,
                           struct net_md_stats_accumulator *acc)
{
    struct flow_counters *from, *to;

    from = &acc->counters;
    to = &eth_acc->counters;
    to->bytes_count += from->bytes_count;
    to->packets_count += from->packets_count;

    /* Don't count twice the previously reported counters */
    from = &acc->first_counters;
    to->bytes_count -= from->bytes_count;
    to->packets_count -= from->packets_count;
}


void net_md_report_eth_acc(struct net_md_aggregator *aggr,
                           struct net_md_eth_pair *eth_pair)
{
    struct net_md_stats_accumulator *eth_acc;
    ds_tree_t *tree;
    struct net_md_flow *flow;

    eth_acc = eth_pair->mac_stats;
    tree = &eth_pair->ethertype_flows;
    flow = ds_tree_head(tree);

    while (flow != NULL)
    {
        struct net_md_stats_accumulator *acc;
        struct net_md_flow *next;
        struct net_md_flow *remove;
        time_t now;
        double cmp;
        bool active_flow;
        bool retire_flow;
        bool refd_flow;
        bool keep_flow;

        acc = flow->tuple_stats;
        active_flow = (acc->state == ACC_STATE_WINDOW_ACTIVE);
        active_flow |= acc->report;
        if (active_flow)
        {
            eth_acc->state = ACC_STATE_WINDOW_ACTIVE;
            net_md_update_eth_acc(eth_acc, acc);
            net_md_close_counters(aggr, acc);
            if (aggr->report_all_samples) net_md_add_sample_to_window(aggr, acc);
            acc->state = ACC_STATE_WINDOW_RESET;
        }

        next = ds_tree_next(tree, flow);

        /* Check if the accumulator is old enough to be removed */
        now = time(NULL);
        cmp = difftime(now, acc->last_updated);
        retire_flow = (cmp >= aggr->acc_ttl);
        refd_flow = (acc->refcnt != 0);

        /* Account for inactive yet referenced flows */
        if (retire_flow && refd_flow) aggr->held_flows++;

        refd_flow = (acc->refcnt != 0);

        /* Account for inactive yet referenced flows */
        if (retire_flow && refd_flow) aggr->held_flows++;

        retire_flow &= (!refd_flow);

        /* keep the flow if it's active and not yet retired */
        keep_flow = (active_flow || !retire_flow);
        if (!keep_flow)
        {
            remove = flow;
            ds_tree_remove(tree, remove);
            net_md_free_flow(remove);
            aggr->total_flows--;
        }

        flow = next;
    }

    if (eth_acc->state == ACC_STATE_WINDOW_ACTIVE)
    {
        net_md_close_counters(aggr, eth_acc);
        net_md_add_sample_to_window(aggr, eth_acc);
        eth_acc->state = ACC_STATE_WINDOW_RESET;
    }
}


void net_md_report_accs(struct net_md_aggregator *aggr)
{
    struct net_md_eth_pair *eth_pair;

    eth_pair = ds_tree_head(&aggr->eth_pairs);
    while (eth_pair != NULL)
    {
        net_md_report_eth_acc(aggr, eth_pair);
        net_md_report_5tuples_accs(aggr, &eth_pair->five_tuple_flows);
        eth_pair = ds_tree_next(&aggr->eth_pairs, eth_pair);
    }

    net_md_report_5tuples_accs(aggr, &aggr->five_tuple_flows);
}


static void net_md_free_stats(struct flow_stats *stats)
{
    /* Don't free the key, it is a reference */
    free(stats->counters);
}


static void net_md_free_free_window(struct flow_window *window)
{
    struct flow_stats **stats_array;
    struct flow_stats *stats;
    size_t i, n;

    if (window == NULL) return;

    n = window->provisioned_stats;
    if (n == 0)
    {
        free(window);
        return;
    }

    stats_array = window->flow_stats;
    stats = *stats_array;

    for (i = 0; i < n; i++) net_md_free_stats(stats_array[i]);
    free(stats);
    free(stats_array);
    window->provisioned_stats = 0;
    window->num_stats = 0;
    free(window);
}


void net_md_free_flow_report(struct flow_report *report)
{
    struct flow_window **windows_array;
    size_t i, n;

    free_node_info(report->node_info);

    n = report->num_windows;

    windows_array = report->flow_windows;
    for (i = 0; i < n; i++) net_md_free_free_window(windows_array[i]);
    free(windows_array);
    free(report);
}


void net_md_reset_aggregator(struct net_md_aggregator *aggr)
{
    struct flow_report *report;
    struct flow_window **windows_array;
    struct flow_window *window;
    size_t i, n;

    if (aggr == NULL) return;

    report = aggr->report;
    n = report->num_windows;
    if (n == 0) return;

    windows_array = report->flow_windows;
    for (i = 0; i < n; i++)
    {
        window = windows_array[i];
        net_md_free_free_window(window);
        windows_array[i] = NULL;
    }

    report->num_windows = 0;
    aggr->windows_cur_idx = 0;
    aggr->stats_cur_idx = 0;
    aggr->active_accs = 0;
    aggr->total_report_flows = 0;
}


size_t net_md_get_total_flows(struct net_md_aggregator *aggr)
{
    if (aggr == NULL) return 0;

    return aggr->total_report_flows;
}


/**
 * @brief popludates a sockaddr_storage structure from ip parameters
 *
 * @param af the the inet family
 * @param ip a pointer to the ip address buffer
 * @param dst the sockaddr_storage structure to fill
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static void
net_md_populate_sockaddr(int af, void *ip, struct sockaddr_storage *dst)
{
    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip, sizeof(in4->sin_addr));
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip, sizeof(in6->sin6_addr));
    }
    return;
}


bool
net_md_ip2mac(struct net_md_aggregator *aggr, int af,
              void *ip, os_macaddr_t *mac)
{
    struct sockaddr_storage ss;
    bool ret;

    if (mac == NULL) return false;
    if (ip == NULL) return false;

    memset(&ss, 0, sizeof(ss));
    net_md_populate_sockaddr(af, ip, &ss);
    ret = aggr->neigh_lookup(&ss, mac);

    return ret;
}


void
pbkeymacs2net_md_macs(struct net_md_aggregator *aggr, struct net_md_flow_key *key)
{
    bool ret;
    int af;

    if (aggr->neigh_lookup == NULL) return;

    af = (key->ip_version == 4 ? AF_INET : AF_INET6);
    ret = net_md_ip2mac(aggr, af, key->src_ip, key->smac);
    if (!ret)
    {
        free(key->smac);
        key->smac = NULL;
    }

    ret = net_md_ip2mac(aggr, af, key->dst_ip, key->dmac);
    if (!ret)
    {
        free(key->dmac);
        key->dmac = NULL;
    }

}


/**
 * @brief: translates protobuf key structure in a net_md_flow_key
 *
 * @param aggr the aggregator the ky will check against
 * @param in_key the reader friendly key2net
 * @return a pointer to a net_md_flow_key
 */
struct net_md_flow_key *
pbkey2net_md_key(struct net_md_aggregator *aggr, Traffic__FlowKey *pb_key)
{
    struct net_md_flow_key *key;
    int domain;
    bool err;
    int ret;

    key = calloc(1, sizeof(*key));
    if (key == NULL) return NULL;

    /*
     * Set key's macs as provided by the protobuf.
     * They might be adjusted once the IP info is processed.
     */
    key->smac = str2os_mac(pb_key->srcmac);
    err = ((pb_key->srcmac != NULL) && (key->smac == NULL));
    if (err) goto err_free_key;

    key->dmac = str2os_mac(pb_key->dstmac);
    err = ((pb_key->dstmac != NULL) && (key->dmac == NULL));
    if (err) goto err_free_smac;

    key->vlan_id = pb_key->vlanid;
    key->ethertype = (uint16_t)(pb_key->ethertype);

    key->src_ip = calloc(1, sizeof(struct in6_addr));
    if (key->src_ip == NULL) goto err_free_dmac;

    if (pb_key->srcip == NULL) return key;
    if (pb_key->dstip == NULL) return key;

    ret = inet_pton(AF_INET, pb_key->srcip, key->src_ip);
    if (ret == 1)
    {
        key->ip_version = 4;
    }
    else
    {
        ret = inet_pton(AF_INET6, pb_key->srcip, key->src_ip);
        if (ret == 0)
        {
            free(key->src_ip);
            return key;
        }
        key->ip_version = 6;
    }

    domain = ((key->ip_version == 4) ? AF_INET : AF_INET6);

    key->dst_ip = calloc(1, sizeof(struct in6_addr));
    if (key->dst_ip == NULL) goto err_free_src_ip;

    ret = inet_pton(domain, pb_key->dstip, key->dst_ip);
    if (ret != 1) goto err_free_dst_ip;

    /* Update the macs based on the IPs */
    pbkeymacs2net_md_macs(aggr, key);

    key->ipprotocol = (uint8_t)(pb_key->ipprotocol);
    key->sport = htons((uint16_t)(pb_key->tptsrcport));
    key->dport = htons((uint16_t)(pb_key->tptdstport));

    return key;

err_free_dst_ip:
    free(key->dst_ip);

err_free_src_ip:
    free(key->src_ip);

err_free_dmac:
    free(key->dmac);

err_free_smac:
    free(key->smac);

err_free_key:
    free(key);

    return NULL;
}


static int
net_md_set_tags(struct flow_tags *tag,
               Traffic__FlowTags *pb_tag)
{
    char *pb_tagval;
    char **pb_tags;
    char *tagval;
    size_t ntags;
    char **tags;
    size_t i;

    tag->vendor = strdup(pb_tag->vendor);
    if (tag->vendor == NULL) return -1;

    if (pb_tag->appname != NULL)
    {
        tag->app_name = strdup(pb_tag->appname);
        if (tag->app_name == NULL) return -1;
    }


    ntags = pb_tag->n_apptags;
    tags = calloc(ntags, sizeof(*tags));
    if (tags == NULL) return -1;

    tag->tags = tags;
    pb_tags = pb_tag->apptags;

    for (i = 0; i < ntags; i++)
    {
        pb_tagval = *pb_tags;
        tagval = strdup(pb_tagval);
        if (tagval == NULL) return -1;

        *tags = tagval;
        tag->nelems++;
        pb_tags++;
        tags++;
    }

    return 0;
}


static void
net_md_update_flow_tags(struct flow_key *fkey, Traffic__FlowKey *flowkey_pb)
{
    struct flow_tags **old_tags;
    struct flow_tags **new_tags;
    Traffic__FlowTags **pb_tags;
    Traffic__FlowTags *pb_tag;
    struct flow_tags **tags;
    struct flow_tags *tag;
    size_t tag_index;
    size_t n_to_add;
    size_t num_tags;
    size_t *to_add;
    size_t *adding;
    bool found;
    size_t i;
    size_t j;
    int rc;

    if (!flowkey_pb->n_flowtags) return;

    to_add = calloc(flowkey_pb->n_flowtags, sizeof(*to_add));
    if (to_add == NULL) return;

    /* skip tags from a vendor already recorded */
    old_tags = fkey->tags;
    pb_tags = flowkey_pb->flowtags;
    n_to_add = 0;
    adding = to_add;
    for (i = 0; i < flowkey_pb->n_flowtags; i++)
    {
        found = false;
        for (j = 0; j < fkey->num_tags && !found; j++)
        {
            rc = strcmp(old_tags[j]->vendor, pb_tags[i]->vendor);
            found = (rc == 0);
        }
        if (!found)
        {
            *adding++ = i;
            n_to_add++;
        }
    }

    num_tags = fkey->num_tags + n_to_add;
    new_tags = calloc(num_tags, sizeof(*fkey->tags));
    if (new_tags == NULL) return;

    old_tags = fkey->tags;
    tags = new_tags;
    for (i = 0; i < fkey->num_tags; i++)
    {
        *tags = *old_tags;
        old_tags++;
        tags++;
    }

    pb_tags = flowkey_pb->flowtags;
    for (i = fkey->num_tags; i < num_tags; i++)
    {
        tag = calloc(1, sizeof(*tag));
        if (tag == NULL) goto err_free_new_tags;
        *tags = tag;

        /* Access the tag to add */
        tag_index = to_add[i - fkey->num_tags];
        pb_tag = pb_tags[tag_index];

        rc = net_md_set_tags(tag, pb_tag);
        if (rc != 0) goto err_free_new_tags;

        tags++;
    }

    free(to_add);
    free(fkey->tags);
    fkey->num_tags = num_tags;
    fkey->tags = new_tags;

    return;

err_free_new_tags:
    tags = new_tags;
    tag = *tags;
    while (tag != NULL)
    {
        free_flow_key_tag(tag);
        tags++;
        tag = *tags;
    }
    free(new_tags);

    return;
}


static int
net_md_set_kvp(struct vendor_data_kv_pair *kvp,
               Traffic__VendorDataKVPair *pb_kvp)
{
    kvp->key = strdup(pb_kvp->key);
    if (kvp->key == NULL) return -1;

    if (pb_kvp->val_str != NULL)
    {
        kvp->str_value = strdup(pb_kvp->val_str);
        if (kvp->str_value == NULL) goto err;

        kvp->value_type = NET_VENDOR_STR;
    }
    else if (pb_kvp->has_val_u32)
    {
        kvp->value_type = NET_VENDOR_U32;
        kvp->u32_value = pb_kvp->val_u32;
    }
    else if (pb_kvp->has_val_u64)
    {
        kvp->value_type = NET_VENDOR_U64;
        kvp->u64_value = pb_kvp->val_u64;
    }
    else goto err;

    return 0;

err:
    free(kvp->key);
    return -1;
}


static int
net_md_set_vendor_data(struct flow_vendor_data *vd, Traffic__VendorData *pb_vd)
{
    Traffic__VendorDataKVPair **pb_kvps;
    Traffic__VendorDataKVPair *pb_kvp;
    struct vendor_data_kv_pair **kvps;
    struct vendor_data_kv_pair *kvp;
    size_t nkvps;
    size_t i;
    int rc;

    vd->vendor = strdup(pb_vd->vendor);
    if (vd->vendor == NULL) return -1;

    nkvps = pb_vd->n_vendorkvpair;
    kvps = calloc(nkvps, sizeof(*kvps));
    if (kvps == NULL) return -1;

    vd->kv_pairs = kvps;
    pb_kvps = pb_vd->vendorkvpair;
    for (i = 0; i < nkvps; i++)
    {
        kvp = calloc(1, sizeof(struct vendor_data_kv_pair));
        if (kvp == NULL) return -1;

        *kvps = kvp;
        pb_kvp = *pb_kvps;
        rc = net_md_set_kvp(kvp, pb_kvp);
        if (rc != 0) return -1;

        vd->nelems++;
        pb_kvps++;
        kvps++;
    }

    return 0;
}


static void
net_md_update_vendor_data(struct flow_key *fkey, Traffic__FlowKey *flowkey_pb)
{
    struct flow_vendor_data **old_vds;
    struct flow_vendor_data **new_vds;
    struct flow_vendor_data **vds;
    Traffic__VendorData **pb_vds;
    struct flow_vendor_data *vd;
    Traffic__VendorData *pb_vd;
    size_t vd_index;
    size_t n_to_add;
    size_t *to_add;
    size_t *adding;
    size_t num_vds;
    bool found;
    size_t i;
    size_t j;
    int rc;

    if (!flowkey_pb->n_vendordata) return;

    to_add = calloc(flowkey_pb->n_vendordata, sizeof(*to_add));
    if (to_add == NULL) return;

    /* skip tags from a vendor already recorded */
    old_vds = fkey->vdr_data;
    pb_vds = flowkey_pb->vendordata;
    n_to_add = 0;
    adding = to_add;
    for (i = 0; i < flowkey_pb->n_vendordata; i++)
    {
        found = false;
        for (j = 0; j < fkey->num_vendor_data && !found; j++)
        {
            rc = strcmp(old_vds[j]->vendor, pb_vds[i]->vendor);
            found = (rc == 0);
        }
        if (!found)
        {
            *adding++ = i;
            n_to_add++;
        }
    }

    num_vds = fkey->num_vendor_data + n_to_add;
    new_vds = calloc(num_vds, sizeof(*new_vds));
    if (new_vds == NULL) return;

    vds = new_vds;
    for (i = 0; i < fkey->num_vendor_data; i++)
    {
        *vds = *old_vds;
        old_vds++;
        vds++;
    }

    for (i = fkey->num_vendor_data; i < num_vds; i++)
    {
        vd = calloc(1, sizeof(*vd));
        if (vd == NULL) goto err_free_new_vds;

        *vds = vd;

        /* Access the vendor data to add */
        vd_index = to_add[i - fkey->num_vendor_data];
        pb_vd = pb_vds[vd_index];
        rc = net_md_set_vendor_data(vd, pb_vd);
        if (rc != 0) goto err_free_new_vds;

        vds++;
    }

    free(to_add);
    free(fkey->vdr_data);
    fkey->num_vendor_data = num_vds;
    fkey->vdr_data = new_vds;

    return;

err_free_new_vds:
    vds = new_vds;
    vd = *vds;
    while (vd != NULL)
    {
        free_flow_key_vendor_data(vd);
        vds++;
        vd = *vds;
    }
    free(new_vds);

    return;
}


/**
 * @brief Updates the content of flow windows from flow report protobuf
 *
 * @param aggr the aggregator to update
 * @param report_pb flow report protobuf
 */
static void
net_md_update_flow_key(struct net_md_aggregator *aggr,
                       Traffic__FlowKey *flowkey_pb)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    bool rc;

    if ((!flowkey_pb->n_flowtags) && (!flowkey_pb->n_vendordata)) return;

    key = pbkey2net_md_key(aggr, flowkey_pb);
    if (key == NULL) return;

    /* Apply the collector filter if present */
    if (aggr->collect_filter != NULL)
    {
        rc = aggr->collect_filter(aggr, key);
        if (!rc) goto free_flow_key;
    }

    acc = net_md_lookup_acc(aggr, key);

    if (acc == NULL) goto free_flow_key;
    fkey = acc->fkey;

    /* Update flow tags */
    net_md_update_flow_tags(fkey, flowkey_pb);

    /* Update vendor data */
    net_md_update_vendor_data(fkey, flowkey_pb);

    /* Mark the accumulator for report */
    acc->report = true;
    if (acc->state != ACC_STATE_WINDOW_ACTIVE) aggr->active_accs++;

free_flow_key:
    /* Free the lookup key */
    free_net_md_flow_key(key);
}


/**
 * @brief Updates the content of flow windows from flow report protobuf
 *
 * @param aggr the aggregator to update
 * @param report_pb flow report protobuf
 */
static void
net_md_update_flow_stats(struct net_md_aggregator *aggr,
                         Traffic__FlowStats *flow_pb)
{
    Traffic__FlowKey *flowkey_pb;

    flowkey_pb = flow_pb->flowkey;

    net_md_update_flow_key(aggr, flowkey_pb);
}


/**
 * @brief Updates the content of flow windows from flow report protobuf
 *
 * @param aggr the aggregator to update
 * @param report_pb flow report protobuf
 */
static void
net_md_update_observation_window(struct net_md_aggregator *aggr,
                                 Traffic__ObservationWindow *window_pb)
{
    Traffic__FlowStats **flowstats_pb;
    size_t i;

    flowstats_pb = window_pb->flowstats;

    for (i = 0; i < window_pb->n_flowstats; i++)
    {
        /* Update the observation window protobuf content */
        net_md_update_flow_stats(aggr, *flowstats_pb);
        flowstats_pb++;
    }
}


/**
 * @brief Updates the content of flow windows from flow report protobuf
 *
 * @param aggr the aggregator to update
 * @param report_pb flow report protobuf
 */
static void
net_md_update_flow_windows(struct net_md_aggregator *aggr,
                           Traffic__FlowReport *report_pb)
{
    Traffic__ObservationWindow **windows_pb;
    size_t i;

    windows_pb = report_pb->observationwindow;

    for (i = 0; i < report_pb->n_observationwindow; i++)
    {
        /* Update the observation window protobuf content */
        net_md_update_observation_window(aggr, *windows_pb);
        windows_pb++;
    }
}


/**
 * @brief Updates an aggregator with the contents of a flow report protobuf
 *
 * Updates the aggregator with the contents of the flow report protobuf.
 * @param aggr the aggregator to update
 * @param pb the packed buffer containing the flow report protobuf
 */
void
net_md_update_aggr(struct net_md_aggregator *aggr, struct packed_buffer *pb)
{
    Traffic__FlowReport *pb_report;

    pb_report = traffic__flow_report__unpack(NULL, pb->len, pb->buf);
    if (pb_report == NULL) return;

    net_md_update_flow_windows(aggr, pb_report);

    /* Free the deserialized content */
    traffic__flow_report__free_unpacked(pb_report, NULL);

    return;
}


/**
 * @brief logs the content of an accumulator
 *
 * @param acc the accumulator to log
 */
void
net_md_log_acc(struct net_md_stats_accumulator *acc)
{
    char src_ip[INET6_ADDRSTRLEN] = {0};
    char dst_ip[INET6_ADDRSTRLEN] = {0};
    struct net_md_flow_key *key;
    struct flow_tags *ftag;
    os_macaddr_t null_mac;
    struct flow_key *fkey;
    os_macaddr_t *smac;
    os_macaddr_t *dmac;
    size_t i, j;
    int af;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) return;

    if (acc->key == NULL) return;
    if (acc->fkey == NULL) return;

    fkey = acc->fkey;
    key = acc->key;

    memset(&null_mac, 0, sizeof(null_mac));

    if (key->ip_version == 4 || key->ip_version == 6)
    {
        af = key->ip_version == 4 ? AF_INET : AF_INET6;
        inet_ntop(af, key->src_ip, src_ip, INET6_ADDRSTRLEN);
        inet_ntop(af, key->dst_ip, dst_ip, INET6_ADDRSTRLEN);
    }

    smac = (key->smac != NULL ? key->smac : &null_mac);
    dmac = (key->dmac != NULL ? key->dmac : &null_mac);

    LOGD("%s: Printing key => net_md_flow_key :: fkey => flow_key",
         __func__);
    LOGD("------------");
    LOGD(" smac:" PRI_os_macaddr_lower_t \
         " dmac:" PRI_os_macaddr_lower_t \
         " isparent_of_smac: %s"         \
         " isparent_of_dmac: %s"         \
         " vlanid: %d"                   \
         " ethertype: %d"                \
         " ip_version: %d"               \
         " src_ip: %s"                   \
         " dst_ip: %s"                   \
         " ipprotocol: %d"               \
         " sport: %d"                    \
         " dport: %d",
         FMT_os_macaddr_pt(smac),
         FMT_os_macaddr_pt(dmac),
         (key->isparent_of_smac ? "true" : "false"),
         (key->isparent_of_dmac ? "true" : "false"),
         key->vlan_id,
         key->ethertype,
         key->ip_version,
         src_ip,
         dst_ip,
         key->ipprotocol,
         ntohs(key->sport),
         ntohs(key->dport));
    if (key->fstart) LOGD(" Flow Starts");
    if (key->fend) LOGD(" Flow Ends");
    LOGD("------------");

    LOGD(" smac: %s"      \
         " dmac: %s"      \
         " isparent_of_smac: %s"    \
         " isparent_of_dmac: %s"    \
         " vlanid: %d"    \
         " ethertype: %d" \
         " ip_version: %d"\
         " src_ip: %s"    \
         " dst_ip: %s"    \
         " protocol: %d"  \
         " sport: %d"     \
         " dport: %d",
         fkey->smac,
         fkey->dmac,
         (fkey->isparent_of_smac ? "true" : "false"),
         (fkey->isparent_of_dmac ? "true" : "false"),
         fkey->vlan_id,
         fkey->ethertype,
         fkey->ip_version,
         fkey->src_ip ? fkey->src_ip : "None",
         fkey->dst_ip ? fkey->dst_ip : "None",
         fkey->protocol,
         fkey->sport,
         fkey->dport);
    LOGD(" Flow State:");
    LOGD(" First observed : %s", ctime(&fkey->state.first_obs));
    LOGD(" Last  observed : %s", ctime(&fkey->state.last_obs));
    if (fkey->state.fstart) LOGD(" Flow Starts");
    if (fkey->state.fend) LOGD(" Flow Ends");
    for (i = 0; i < fkey->num_tags; i++)
    {
        ftag = fkey->tags[i];
        LOGD(" vendor: %s" \
             " app_name: %s",
             ftag->vendor,
             ftag->app_name);
        for (j = 0; j < ftag->nelems; j++)
        {
            LOGD(" tag[%zu]: %s",
                 j, ftag->tags[j]);
        }
    }
    LOGD("%s: ------------", __func__);
    LOGD("%s: counter packets_count = %" PRIu64 ", bytes_count = %" PRIu64,
         __func__,
         acc->counters.packets_count,
         acc->counters.bytes_count);
}


/**
 * @brief log the content of accumulators in the given tree
 *
 * @param aggr the aggregator containing the accumulators
 * @tree the tree of accumulators
 */
void
net_md_log_accs(struct net_md_aggregator *aggr,
                ds_tree_t *tree)
{
    struct net_md_flow *flow;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        struct net_md_stats_accumulator *acc;
        struct net_md_flow *next;

        acc = flow->tuple_stats;
        net_md_log_acc(acc);
        next = ds_tree_next(tree, flow);
        flow = next;
    }
}


void
net_md_log_eth_acc(struct net_md_aggregator *aggr,
                   struct net_md_eth_pair *eth_pair)
{
    struct net_md_stats_accumulator *eth_acc;
    ds_tree_t *tree;

    eth_acc = eth_pair->mac_stats;
    net_md_log_acc(eth_acc);
    tree = &eth_pair->ethertype_flows;
    net_md_log_accs(aggr, tree);
}


/**
 * @brief logs the content of an aggregator
 *
 * Walks the aggregator and logs its accumulators
 * @param aggr the accumulator to log
 */
void net_md_log_aggr(struct net_md_aggregator *aggr)
{
    struct net_md_eth_pair *eth_pair;

    eth_pair = ds_tree_head(&aggr->eth_pairs);
    while (eth_pair != NULL)
    {
        net_md_log_eth_acc(aggr, eth_pair);
        net_md_log_accs(aggr, &eth_pair->five_tuple_flows);
        eth_pair = ds_tree_next(&aggr->eth_pairs, eth_pair);
    }

    net_md_log_accs(aggr, &aggr->five_tuple_flows);
}

/**
 * @brief log the content of accumulators in the given tree
 *
 * @param aggr the aggregator containing the accumulators
 * @tree the tree of accumulators
 */
void
net_md_process_accs(struct net_md_aggregator *aggr,
                    ds_tree_t *tree)
{
    struct net_md_flow *flow;

    flow = ds_tree_head(tree);
    while (flow != NULL)
    {
        struct net_md_stats_accumulator *acc;
        struct net_md_flow *next;

        acc = flow->tuple_stats;
        aggr->process(acc);
        next = ds_tree_next(tree, flow);
        flow = next;
    }
}

void
net_md_process_eth_acc(struct net_md_aggregator *aggr,
                       struct net_md_eth_pair *eth_pair)
{
    struct net_md_stats_accumulator *eth_acc;
    ds_tree_t *tree;

    eth_acc = eth_pair->mac_stats;
    aggr->process(eth_acc);
    tree = &eth_pair->ethertype_flows;
    net_md_process_accs(aggr, tree);
}


/**
 * @brief process an accumulator
 *
 * Process an accumulator
 * @param acc the accumulator to process
 */
void net_md_process_aggr(struct net_md_aggregator *aggr)
{
    struct net_md_eth_pair *eth_pair;

    if (aggr->process == NULL) return;

    eth_pair = ds_tree_head(&aggr->eth_pairs);
    while (eth_pair != NULL)
    {
        net_md_process_eth_acc(aggr, eth_pair);
        net_md_process_accs(aggr, &eth_pair->five_tuple_flows);
        eth_pair = ds_tree_next(&aggr->eth_pairs, eth_pair);
    }

    net_md_process_accs(aggr, &aggr->five_tuple_flows);
}
