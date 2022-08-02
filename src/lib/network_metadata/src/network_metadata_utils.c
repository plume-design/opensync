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
#include "memutil.h"
#include "network_metadata_report.h"
#include "sockaddr_storage.h"

/**
 * @brief compares 2 flow keys'ethernet content
 *
 * Compares source mac, dest mac and vlan id
 * Used to lookup a node in the manager's eth_pairs tree
 *
 * @param a void pointer cast to a net_md_flow_key struct
 * @param a void pointer cast to a net_md_flow_key struct
 */
int net_md_eth_cmp(const void *a, const void *b)
{
    const struct net_md_flow_key *key_a = a;
    const struct net_md_flow_key *key_b = b;
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
   if (cmp != 0) return cmp;

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
int net_md_eth_flow_cmp(const void *a, const void *b)
{
    const struct net_md_flow_key *key_a = a;
    const struct net_md_flow_key *key_b = b;
    int cmp;

   /* Compare vlan id */
   cmp = (int)(key_a->ethertype) - (int)(key_b->ethertype);
   if (cmp != 0) return cmp;

   /* Compare ufid */
   if ((key_a->ufid != NULL) && (key_b->ufid != NULL))
        cmp = memcmp(key_a->ufid, key_b->ufid, sizeof(os_ufid_t));

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
int net_md_5tuple_cmp(const void *a, const void *b)
{
    const struct net_md_flow_key *key_a = a;
    const struct net_md_flow_key *key_b = b;
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
 *
 * @remark called is responsible for freeing the allocated MAC
 */
os_macaddr_t *
str2os_mac(char *strmac)
{
    os_macaddr_t *mac;
    bool ret;

    mac = CALLOC(1, sizeof(*mac));
    if (mac == NULL) return NULL;

    ret = str2os_mac_ref(strmac, mac);
    if (!ret) goto err_free_mac;

    return mac;

err_free_mac:
    FREE(mac);
    return NULL;
}

/**
 * @brief helper function: string to os_macaddr_t
 *
 * @param strmac: ethernet mac in string representation
 * @param mac: pre allocated variable to receive the MAC address
 * @return true is success
 *
 * @remark caller is resposnible for allocating MAC
 */
bool
str2os_mac_ref(char* strmac, os_macaddr_t *mac)
{
    size_t len, i, j;

    if (mac == NULL) return false;
    if (strmac == NULL) goto err_reset_mac;

    /* Validate the input string */
    len = strlen(strmac);
    if (len != 17) goto err_reset_mac;

    i = 0;
    j = 0;
    do
    {
        char a = strmac[i++];
        char b = strmac[i++];
        uint8_t v;

        if (!isxdigit(a)) goto err_reset_mac;
        if (!isxdigit(b)) goto err_reset_mac;

        v = (isdigit(a) ? (a - '0') : (toupper(a) - 'A' + 10));
        v <<= 4;
        v += (isdigit(b) ? (b - '0') : (toupper(b) - 'A' + 10));
        mac->addr[j] = v;

        if (i == len) break;
        if (strmac[i++] != ':') goto err_reset_mac;
        j++;
    } while (i < len);

    return true;

err_reset_mac:
    memset(mac->addr, 0, sizeof(mac->addr));
    return false;
}



char *net_md_set_str(char *in_str)
{
    char *out;
    size_t len;

    if (in_str == NULL) return NULL;

    len = strnlen(in_str, MD_MAX_STRLEN);
    if (len == 0) return NULL;

    out = STRNDUP(in_str, len);

    return out;
}

os_ufid_t *net_md_set_ufid(os_ufid_t *in_ufid)
{
    os_ufid_t *ufid;

    if (in_ufid == NULL) return NULL;

    ufid = CALLOC(1, sizeof(*in_ufid));
    if (ufid == NULL) return NULL;

    memcpy(ufid, in_ufid, sizeof(*ufid));
    return ufid;
}

os_macaddr_t *net_md_set_os_macaddr(os_macaddr_t *in_mac)
{
    os_macaddr_t *mac;

    if (in_mac == NULL) return NULL;

    mac = CALLOC(1, sizeof(*mac));
    if (mac == NULL) return NULL;

    memcpy(mac, in_mac, sizeof(*mac));
    return mac;
}


bool net_md_set_ip(uint8_t ipv, uint8_t *ip, uint8_t **ip_tgt)
{
    size_t ipl;

    if (ip_tgt == NULL) return false;

    if ((ip == NULL) || ((ipv != 4) && (ipv != 6)))
    {
        *ip_tgt = NULL;
        return true;
    }

    ipl = (ipv == 4 ? 4 : 16);

    *ip_tgt = CALLOC(ipl, sizeof(**ip_tgt));
    if (*ip_tgt == NULL) return false;

    memcpy(*ip_tgt, ip, ipl);
    return true;
}


struct node_info * net_md_set_node_info(struct node_info *info)
{
    struct node_info *node;

    if (info == NULL) return NULL;

    node = CALLOC(1, sizeof(*node));
    if (node == NULL) return NULL;

    node->node_id = net_md_set_str(info->node_id);
    if (node->node_id == NULL) goto err_free_node;;

    node->location_id = net_md_set_str(info->location_id);
    if (node->location_id == NULL) goto err_free_node_id;

    return node;

err_free_node_id:
    FREE(node->node_id);

err_free_node:
    FREE(node);

    return NULL;
}


void free_node_info(struct node_info *node)
{
    if (node == NULL) return;
    CHECK_DOUBLE_FREE(node);

    FREE(node->node_id);
    FREE(node->location_id);
}


void
free_flow_key_tag(struct flow_tags *tag)
{
    size_t i;

    CHECK_DOUBLE_FREE(tag);

    FREE(tag->vendor);
    FREE(tag->app_name);

    for (i = 0; i < tag->nelems; i++) FREE(tag->tags[i]);
    FREE(tag->tags);
}

void
free_flow_key_tags(struct flow_key *key)
{
    size_t i;

    CHECK_DOUBLE_FREE(key);

    for (i = 0; i < key->num_tags; i++)
    {
        free_flow_key_tag(key->tags[i]);
        FREE(key->tags[i]);
    }
    FREE(key->tags);
}

void
free_flow_key_vendor_data(struct flow_vendor_data *vd)
{
    struct vendor_data_kv_pair *kv;
    size_t i;

    if (vd == NULL) return;
    CHECK_DOUBLE_FREE(vd);

    for (i = 0; i < vd->nelems; i++)
    {
        kv = vd->kv_pairs[i];
        FREE(kv->key);
        FREE(kv->str_value);
        FREE(kv);
    }

    FREE(vd->vendor);
    FREE(vd->kv_pairs);
}

void
free_flow_key_vdr_data(struct flow_key *key)
{
    size_t i;

    if (key == NULL) return;
    CHECK_DOUBLE_FREE(key);

    for (i = 0; i < key->num_vendor_data; i++)
    {
        free_flow_key_vendor_data(key->vdr_data[i]);
        FREE(key->vdr_data[i]);
    }

    FREE(key->vdr_data);
}

void
free_flow_key(struct flow_key *key)
{
    if (key == NULL) return;
    CHECK_DOUBLE_FREE(key);

    FREE(key->smac);
    FREE(key->dmac);
    FREE(key->src_ip);
    FREE(key->dst_ip);

    free_flow_key_tags(key);
    free_flow_key_vdr_data(key);
}


void free_flow_counters(struct flow_counters *counters)
{
    if (counters == NULL) return;
    CHECK_DOUBLE_FREE(counters);
}


void free_window_stats(struct flow_stats *stats)
{
    if (stats == NULL) return;
    CHECK_DOUBLE_FREE(stats);

    if (stats->owns_key)
    {
        free_flow_key(stats->key);
        FREE(stats->key);
    }
    free_flow_counters(stats->counters);
    FREE(stats->counters);

}

/**
 * @brief free an uplink structure
 *
 * Free dynamically allocated fields
 *
 * @param uplink structure to free
 * @return none
 */
static void net_md_free_uplink(struct flow_uplink *uplink)
{
    if (uplink == NULL) return;
    CHECK_DOUBLE_FREE(uplink);

    if (uplink->uplink_if_type != NULL)
    {
        CHECK_DOUBLE_FREE(uplink->uplink_if_type);
        FREE(uplink->uplink_if_type);
    }
}

void free_report_window(struct flow_window *window)
{
    size_t i;

    if (window == NULL) return;
    CHECK_DOUBLE_FREE(window);

    for (i = 0; i < window->num_stats; i++)
    {
        free_window_stats(window->flow_stats[i]);
        FREE(window->flow_stats[i]);
    }

    net_md_free_uplink(window->uplink);
    FREE(window->uplink);
    FREE(window->flow_stats);
}


void free_flow_report(struct flow_report *report)
{
    size_t i;

    if (report == NULL) return;
    CHECK_DOUBLE_FREE(report);

    for (i = 0; i < report->num_windows; i++)
    {
        free_report_window(report->flow_windows[i]);
        FREE(report->flow_windows[i]);
    }
    FREE(report->flow_windows);

    free_node_info(report->node_info);
    FREE(report->node_info);
}


void free_net_md_flow_key(struct net_md_flow_key *lkey)
{
    if (lkey == NULL) return;
    CHECK_DOUBLE_FREE(lkey);

    FREE(lkey->ufid);
    FREE(lkey->smac);
    FREE(lkey->dmac);
    FREE(lkey->src_ip);
    FREE(lkey->dst_ip);
}


struct net_md_flow_key * set_net_md_flow_key(struct net_md_flow_key *lkey)
{
    struct net_md_flow_key *key;
    bool ret, err;

    key = CALLOC(1, sizeof(*key));
    if (key == NULL) return NULL;

    key->ufid = net_md_set_ufid(lkey->ufid);

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
    key->tcp_flags = lkey->tcp_flags;
    key->icmp_type = lkey->icmp_type;
    return key;

err_free_src_ip:
    FREE(key->src_ip);

err_free_dmac:
    FREE(key->dmac);

err_free_smac:
    FREE(key->smac);

err_free_key:
    FREE(key->ufid);
    FREE(key);

    return NULL;
}


struct flow_key * net_md_set_flow_key(struct net_md_flow_key *key)
{
    char buf[INET6_ADDRSTRLEN];
    struct flow_key *fkey;
    const char *res;
    int family;
    size_t ip_size;

    fkey = CALLOC(1, sizeof(*fkey));
    if (fkey == NULL) return NULL;

    if (key->smac != NULL)
    {
        snprintf(buf, sizeof(buf), PRI_os_macaddr_lower_t,
                 FMT_os_macaddr_pt(key->smac));
        fkey->smac = STRNDUP(buf, sizeof(buf));
        if (fkey->smac == NULL) goto err_free_fkey;
        fkey->isparent_of_smac = key->isparent_of_smac;
    }

    if (key->dmac != NULL)
    {
        snprintf(buf, sizeof(buf), PRI_os_macaddr_lower_t,
                 FMT_os_macaddr_pt(key->dmac));
        fkey->dmac = STRNDUP(buf, sizeof(buf));
        if (fkey->dmac == NULL) goto err_free_smac;
        fkey->isparent_of_dmac = key->isparent_of_dmac;
    }

    fkey->vlan_id = key->vlan_id;
    fkey->ethertype = key->ethertype;

    if (key->ip_version == 0) return fkey;

    family = ((key->ip_version == 4) ? AF_INET : AF_INET6);
    ip_size = ((family == AF_INET) ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);

    fkey->ip_version = key->ip_version;

    fkey->src_ip = CALLOC(ip_size, sizeof(*fkey->src_ip));
    if (fkey->src_ip == NULL) goto err_free_dmac;

    res = inet_ntop(family, key->src_ip, fkey->src_ip, ip_size);
    if (res == NULL) goto err_free_src_ip;

    fkey->dst_ip = CALLOC(ip_size, sizeof(*fkey->dst_ip));
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
    FREE(fkey->dst_ip);

err_free_src_ip:
    FREE(fkey->src_ip);

err_free_dmac:
    FREE(fkey->dmac);

err_free_smac:
    FREE(fkey->smac);

err_free_fkey:
    FREE(fkey);

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
    CHECK_DOUBLE_FREE(acc);

    net_md_acc_destroy_cb(acc);

    free_net_md_flow_key(acc->key);
    FREE(acc->key);
    free_flow_key(acc->fkey);
    FREE(acc->fkey);
    if (acc->free_plugins != NULL) acc->free_plugins(acc);
    // dpi_plugins tree for the acc is set to NULL.
    acc->dpi_plugins = NULL;
}


struct net_md_stats_accumulator *
net_md_set_acc(struct net_md_aggregator *aggr,
               struct net_md_flow_key *key)
{
    struct net_md_stats_accumulator *acc;

    if (key == NULL || aggr == NULL) return NULL;

    acc = CALLOC(1, sizeof(*acc));
    if (acc == NULL) return NULL;

    acc->key = set_net_md_flow_key(key);
    if (acc->key == NULL) goto err_free_acc;

    acc->fkey = net_md_set_flow_key(key);
    if (acc->fkey == NULL) goto err_free_md_flow_key;
    acc->fkey->acc = acc;
    acc->fkey->state.report_attrs = true;

    if (aggr->on_acc_create != NULL) aggr->on_acc_create(aggr, acc);
    acc->aggr = aggr;

    return acc;

err_free_md_flow_key:
    free_net_md_flow_key(acc->key);
    FREE(acc->key);

err_free_acc:
    FREE(acc);

    return NULL;
}


void net_md_free_flow(struct net_md_flow *flow)
{
    if (flow == NULL) return;
    CHECK_DOUBLE_FREE(flow);

    net_md_free_acc(flow->tuple_stats);
    FREE(flow->tuple_stats);
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
        FREE(flow);
        flow = next;
    }

}


void net_md_free_eth_pair(struct net_md_eth_pair *pair)
{
    if (pair == NULL) return;
    CHECK_DOUBLE_FREE(pair);

    net_md_free_acc(pair->mac_stats);
    FREE(pair->mac_stats);
    net_md_free_flow_tree(&pair->ethertype_flows);
    net_md_free_flow_tree(&pair->five_tuple_flows);
}


struct net_md_eth_pair *
net_md_set_eth_pair(struct net_md_aggregator *aggr,
                    struct net_md_flow_key *key)
{
    struct net_md_eth_pair *eth_pair;
    os_ufid_t *ufid;

    if (key == NULL) return NULL;
    if (key->flags == NET_MD_ACC_LOOKUP_ONLY) return NULL;

    eth_pair = CALLOC(1, sizeof(*eth_pair));
    if (eth_pair == NULL) return NULL;

    /* Do not stash the ufid for the eth_pair accumulator */
    ufid = key->ufid;
    key->ufid = NULL;
    eth_pair->mac_stats = net_md_set_acc(aggr, key);
    key->ufid = ufid;

    if (eth_pair->mac_stats == NULL) goto err_free_eth_pair;

    ds_tree_init(&eth_pair->ethertype_flows, net_md_eth_flow_cmp,
                 struct net_md_flow, flow_node);

    ds_tree_init(&eth_pair->five_tuple_flows, net_md_5tuple_cmp,
                 struct net_md_flow, flow_node);

    aggr->total_eth_pairs++;
    return eth_pair;

err_free_eth_pair:
    FREE(eth_pair);

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

    /* Return if the acc creation is not requested */
    if (key->flags == NET_MD_ACC_LOOKUP_ONLY) return NULL;

    /* Allocate flow */
    flow = CALLOC(1, sizeof(*flow));
    if (flow == NULL) return NULL;

    /* Allocate the flow accumulator */
    acc = net_md_set_acc(aggr, key);
    if (acc == NULL) goto err_free_flow;

    flow->tuple_stats = acc;
    ds_tree_insert(tree, flow, acc->key);
    aggr->total_flows++;

    return acc;

err_free_flow:
    FREE(flow);

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


struct net_md_stats_accumulator *
net_md_lookup_reverse_acc(struct net_md_aggregator *aggr,
                          struct net_md_stats_accumulator *acc)
{
    struct net_md_stats_accumulator *reverse_acc;
    struct net_md_flow_key *okey;
    struct net_md_flow_key rkey;

    memset(&rkey, 0, sizeof(rkey));
    okey = acc->key;
    if (okey == NULL) return NULL;

    rkey.smac = okey->dmac;
    rkey.dmac = okey->smac;
    rkey.vlan_id = okey->vlan_id;
    rkey.ethertype = okey->ethertype;
    rkey.ip_version = okey->ip_version;
    rkey.src_ip = okey->dst_ip;
    rkey.dst_ip = okey->src_ip;
    rkey.ipprotocol = okey->ipprotocol;
    rkey.sport = okey->dport;
    rkey.dport = okey->sport;
    rkey.flags = NET_MD_ACC_LOOKUP_ONLY;
    reverse_acc = net_md_lookup_acc(aggr, &rkey);

    return reverse_acc;
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
    struct flow_uplink *uplink;
    size_t idx;

    report = aggr->report;
    idx = aggr->windows_cur_idx;
    if (idx == aggr->max_windows) return NULL;

    window = report->flow_windows[idx];
    if (window != NULL) return window;

    window = CALLOC(1, sizeof(*window));
    if (window == NULL) return NULL;

    window->uplink = CALLOC(1, sizeof(*uplink));
    if (window->uplink == NULL) goto err_free_window;

    report->flow_windows[idx] = window;
    aggr->report->num_windows++;
    return window;

err_free_window:
    FREE(window);

    return NULL;
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
    struct flow_counters *to;
    struct flow_counters *from;


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
    stats->counters = CALLOC(1, sizeof(*stats->counters));
    if (stats->counters == NULL) return false;

    stats->owns_key = false;
    stats->key = acc->fkey;
    stats->key->direction = acc->direction;
    stats->key->originator = acc->originator;

    to = stats->counters;
    from = &acc->report_counters;

    to->packets_count = from->packets_count;
    to->bytes_count = from->bytes_count;
    to->payload_bytes_count = from->payload_bytes_count;

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
            if (aggr->on_acc_report != NULL)
            {
                aggr->on_acc_report(aggr, acc);
            }
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
            FREE(remove);
            aggr->total_flows--;
        }

        flow = next;
    }
}

void net_md_update_eth_acc(struct net_md_stats_accumulator *eth_acc,
                           struct net_md_stats_accumulator *acc)
{
    struct flow_counters *from;
    struct flow_counters *to;

    from = &acc->report_counters;
    to = &eth_acc->report_counters;
    to->bytes_count += from->bytes_count;
    to->packets_count += from->packets_count;
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

    memset(&eth_acc->report_counters,
           0, sizeof(eth_acc->report_counters));

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
            net_md_close_counters(aggr, acc);
            net_md_update_eth_acc(eth_acc, acc);
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

        retire_flow &= (!refd_flow);

        /* keep the flow if it's active and not yet retired */
        keep_flow = (active_flow || !retire_flow);
        if (!keep_flow)
        {
            remove = flow;
            ds_tree_remove(tree, remove);
            net_md_free_flow(remove);
            FREE(remove);
            aggr->total_flows--;
        }

        flow = next;
    }

    if (eth_acc->state == ACC_STATE_WINDOW_ACTIVE)
    {
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
    FREE(stats->counters);
}


static void net_md_free_free_window(struct flow_window *window)
{
    struct flow_stats **stats_array;
    struct flow_stats *stats;
    size_t i, n;

    if (window == NULL) return;
    CHECK_DOUBLE_FREE(window);

    net_md_free_uplink(window->uplink);
    FREE(window->uplink);

    n = window->provisioned_stats;
    if (n == 0) return;

    stats_array = window->flow_stats;

    stats_array = window->flow_stats;  /* stats_array has pointer into the array below */
    stats = stats_array[0];            /* array of stats is built contiguous */
    for (i = 0; i < n; i++) net_md_free_stats(stats_array[i]);
    FREE(stats);
    FREE(stats_array);

    window->provisioned_stats = 0;
    window->num_stats = 0;
}


void net_md_free_flow_report(struct flow_report *report)
{
    struct flow_window **windows_array;
    struct flow_window *windows;
    size_t i, n;

    if (report == NULL) return;
    CHECK_DOUBLE_FREE(report);

    free_node_info(report->node_info);
    FREE(report->node_info);

    n = report->num_windows;

    /* None of the windows_array[x] are allocated individually.
     * They are mapped inside the larger flow_windows which
     * is allocated as a single larger array.
     * @see net_md_allocate_aggregator for allocation details.
     */
    windows_array = report->flow_windows;
    if (n > 0)
    {
        windows = *windows_array;
        /* Do NOT free() individual entries */
        for (i = 0; i < n; i++) net_md_free_free_window(windows_array[i]);
        FREE(windows);
    }
    FREE(report->flow_windows);
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
        FREE(window);
        windows_array[i] = NULL;   /* this is a reset, we re-use the array */
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


bool
net_md_ip2mac(struct net_md_aggregator *aggr, int af,
              void *ip, os_macaddr_t *mac)
{
    bool ret;

    if (mac == NULL) return false;
    if (ip == NULL) return false;

    ret = aggr->neigh_lookup(af, ip, mac);

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
        FREE(key->smac);
        key->smac = NULL;
    }

    ret = net_md_ip2mac(aggr, af, key->dst_ip, key->dmac);
    if (!ret)
    {
        FREE(key->dmac);
        key->dmac = NULL;
    }

}


static void
pbkeydir2net_md_key_dir(Traffic__FlowKey *pb_key, struct net_md_flow_key *key)
{
    Traffic__Originator origin;
    Traffic__Direction dir;

    dir = (pb_key->has_direction ?
           pb_key->direction :
           TRAFFIC__DIRECTION__FLOW_DIRECTION_UNSPECIFIED);

    switch(dir)
    {
        case TRAFFIC__DIRECTION__FLOW_DIRECTION_UNSPECIFIED:
            key->direction = NET_MD_ACC_UNSET_DIR;
            break;

        case TRAFFIC__DIRECTION__FLOW_DIRECTION_OUTBOUND:
            key->direction = NET_MD_ACC_OUTBOUND_DIR;
            break;

        case TRAFFIC__DIRECTION__FLOW_DIRECTION_INBOUND:
            key->direction = NET_MD_ACC_INBOUND_DIR;
            break;

        case TRAFFIC__DIRECTION__FLOW_DIRECTION_LAN2LAN:
            key->direction = NET_MD_ACC_LAN2LAN_DIR;
            break;

        default:
            key->direction = NET_MD_ACC_UNSET_DIR;
            break;
    }

    origin = (pb_key->has_originator ?
              pb_key->originator :
              TRAFFIC__ORIGINATOR__FLOW_ORIGINATOR_UNSPECIFIED);

    switch(origin)
    {
        case TRAFFIC__ORIGINATOR__FLOW_ORIGINATOR_UNSPECIFIED:
            key->originator = NET_MD_ACC_UNKNOWN_ORIGINATOR;
            break;

        case TRAFFIC__ORIGINATOR__FLOW_ORIGINATOR_SRC:
            key->originator = NET_MD_ACC_ORIGINATOR_SRC;
            break;

        case TRAFFIC__ORIGINATOR__FLOW_ORIGINATOR_DST:
            key->originator = NET_MD_ACC_ORIGINATOR_DST;
            break;

        default:
            key->originator = NET_MD_ACC_UNKNOWN_ORIGINATOR;
            break;
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

    key = CALLOC(1, sizeof(*key));
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

    key->src_ip = CALLOC(sizeof(struct in6_addr), sizeof(*key->src_ip));
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
            FREE(key->src_ip);
            return key;
        }
        key->ip_version = 6;
    }

    domain = ((key->ip_version == 4) ? AF_INET : AF_INET6);

    key->dst_ip = CALLOC(sizeof(struct in6_addr), sizeof(*key->dst_ip));
    if (key->dst_ip == NULL) goto err_free_src_ip;

    ret = inet_pton(domain, pb_key->dstip, key->dst_ip);
    if (ret != 1) goto err_free_dst_ip;

    /* Update the macs based on the IPs */
    pbkeymacs2net_md_macs(aggr, key);

    key->ipprotocol = (uint8_t)(pb_key->ipprotocol);
    key->sport = htons((uint16_t)(pb_key->tptsrcport));
    key->dport = htons((uint16_t)(pb_key->tptdstport));

    pbkeydir2net_md_key_dir(pb_key, key);
    return key;

err_free_dst_ip:
    FREE(key->dst_ip);

err_free_src_ip:
    FREE(key->src_ip);

err_free_dmac:
    FREE(key->dmac);

err_free_smac:
    FREE(key->smac);

err_free_key:
    FREE(key);

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
    tags = CALLOC(ntags, sizeof(*tags));
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

    to_add = CALLOC(flowkey_pb->n_flowtags, sizeof(*to_add));
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
    new_tags = CALLOC(num_tags, sizeof(*fkey->tags));
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
        tag = CALLOC(1, sizeof(*tag));
        if (tag == NULL) goto err_free_new_tags;
        *tags = tag;

        /* Access the tag to add */
        tag_index = to_add[i - fkey->num_tags];
        pb_tag = pb_tags[tag_index];

        rc = net_md_set_tags(tag, pb_tag);
        if (rc != 0) goto err_free_new_tags;

        tags++;
    }

    FREE(to_add);
    FREE(fkey->tags);
    fkey->num_tags = num_tags;
    fkey->tags = new_tags;

    return;

err_free_new_tags:
    tags = new_tags;
    tag = *tags;
    while (tag != NULL)
    {
        free_flow_key_tag(tag);
        FREE(tag);
        tags++;
        tag = *tags;
    }
    FREE(new_tags);

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
    FREE(kvp->key);
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
    kvps = CALLOC(nkvps, sizeof(*kvps));
    if (kvps == NULL) return -1;

    vd->kv_pairs = kvps;
    pb_kvps = pb_vd->vendorkvpair;
    for (i = 0; i < nkvps; i++)
    {
        kvp = CALLOC(1, sizeof(*kvp));
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

    to_add = CALLOC(flowkey_pb->n_vendordata, sizeof(*to_add));
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
    new_vds = CALLOC(num_vds, sizeof(*new_vds));
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
        vd = CALLOC(1, sizeof(*vd));
        if (vd == NULL) goto err_free_new_vds;

        *vds = vd;

        /* Access the vendor data to add */
        vd_index = to_add[i - fkey->num_vendor_data];
        pb_vd = pb_vds[vd_index];
        rc = net_md_set_vendor_data(vd, pb_vd);
        if (rc != 0) goto err_free_new_vds;

        vds++;
    }

    FREE(to_add);
    FREE(fkey->vdr_data);
    fkey->num_vendor_data = num_vds;
    fkey->vdr_data = new_vds;

    return;

err_free_new_vds:
    vds = new_vds;
    vd = *vds;
    while (vd != NULL)
    {
        free_flow_key_vendor_data(vd);
        FREE(vd);
        vds++;
        vd = *vds;
    }
    FREE(new_vds);

    return;
}


/**
 * @brief check flowkey info
 *
 * @param report_pb flow report protobuf
 * @return true protobuf has flowkey info
 *         false otherwise.
 */
bool
net_md_check_update(Traffic__FlowKey *flowkey_pb)
{
    if (flowkey_pb->n_flowtags) return true;
    if (flowkey_pb->n_vendordata) return true;
    if (flowkey_pb->has_direction) return true;
    if (flowkey_pb->has_originator) return true;

    return false;
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
    char *app_name;
    bool rc;

    rc = net_md_check_update(flowkey_pb);
    if (!rc) return;

    key = pbkey2net_md_key(aggr, flowkey_pb);
    if (key == NULL) return;

    app_name = NULL;
    if (flowkey_pb->n_flowtags != 0)
    {
        app_name = flowkey_pb->flowtags[0]->appname;
        LOGD("%s: processing app name %s", __func__, app_name);
    }

    /* Apply the collector filter if present */
    if (aggr->collect_filter != NULL)
    {
        rc = aggr->collect_filter(aggr, key, app_name);
        if (!rc) goto free_flow_key;
    }

    acc = net_md_lookup_acc(aggr, key);

    if (acc == NULL) goto free_flow_key;
    fkey = acc->fkey;
    fkey->log = 1;

    /* Update flow tags */
    net_md_update_flow_tags(fkey, flowkey_pb);

    /* Update vendor data */
    net_md_update_vendor_data(fkey, flowkey_pb);

    /* Mark the accumulator for report */
    acc->report = true;
    if (acc->state != ACC_STATE_WINDOW_ACTIVE) aggr->active_accs++;

    /* Update flow originator */
    acc->originator = key->originator;
    acc->key->originator = acc->originator;
    fkey->originator = acc->originator;

    /* Update flow direction */
    acc->direction = key->direction;
    acc->key->direction = acc->direction;
    fkey->direction = acc->direction;

    LOGD("%s: acc updated", __func__);
    net_md_log_acc(acc, __func__);

free_flow_key:
    /* Free the lookup key */
    free_net_md_flow_key(key);
    FREE(key);
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


char *
net_md_dir_to_str(int dir)
{
    char *dir_str = "unspecified";

    switch(dir)
    {
        case NET_MD_ACC_UNSET_DIR:
            dir_str = "unspecified";
            break;

        case NET_MD_ACC_OUTBOUND_DIR:
            dir_str = "outbound";
            break;

        case NET_MD_ACC_INBOUND_DIR:
            dir_str = "inbound";
            break;

        case NET_MD_ACC_LAN2LAN_DIR:
            dir_str = "lan2lan";
            break;

        default:
            dir_str = "unspecified";
            break;
    }

    return dir_str;
}


char *
net_md_origin_to_str(int origin)
{
    char *origin_str = "unspecified";

    switch(origin)
    {
        case NET_MD_ACC_UNKNOWN_ORIGINATOR:
            origin_str = "unspecified";
            break;

        case NET_MD_ACC_ORIGINATOR_SRC:
            origin_str = "source";
            break;

        case NET_MD_ACC_ORIGINATOR_DST:
            origin_str = "destination";
            break;

        default:
            origin_str = "unspecified";
            break;
    }

    return origin_str;
}

/**
 * @brief logs the content of a network_metadata key
 *
 * @param key the network_metadata key to log
 * @param caller the calling function
 */
void
net_md_log_key(struct net_md_flow_key *key, const char *caller)
{
    char src_ip[INET6_ADDRSTRLEN];
    char dst_ip[INET6_ADDRSTRLEN];
    os_macaddr_t null_mac;
    os_ufid_t null_ufid;
    os_macaddr_t *smac;
    os_macaddr_t *dmac;
    os_ufid_t *ufid;
    int af;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) return;

    LOGD("%s: caller: %s, key %p", __func__, caller, key);

    memset(src_ip, 0, sizeof(src_ip));
    memset(dst_ip, 0, sizeof(dst_ip));

    memset(&null_mac, 0, sizeof(null_mac));
    memset(&null_ufid, 0, sizeof(null_ufid));
    if (key->ip_version == 4 || key->ip_version == 6)
    {
        af = key->ip_version == 4 ? AF_INET : AF_INET6;
        inet_ntop(af, key->src_ip, src_ip, INET6_ADDRSTRLEN);
        inet_ntop(af, key->dst_ip, dst_ip, INET6_ADDRSTRLEN);
    }

    smac = (key->smac != NULL ? key->smac : &null_mac);
    dmac = (key->dmac != NULL ? key->dmac : &null_mac);
    ufid = (key->ufid != NULL ? key->ufid : &null_ufid);

    LOGD(" smac: " PRI_os_macaddr_lower_t \
         " dmac: " PRI_os_macaddr_lower_t \
         " ufid:" PRI_os_ufid_t           \
         " isparent_of_smac: %s"          \
         " isparent_of_dmac: %s"          \
         " vlanid: %d"                    \
         " ethertype: %d"                 \
         " ip_version: %d"                \
         " src_ip: %s"                    \
         " dst_ip: %s"                    \
         " ipprotocol: %d"                \
         " sport: %d"                     \
         " dport: %d"                     \
         " direction: %s"                 \
         " origin: %s",                   \
         FMT_os_macaddr_pt(smac),
         FMT_os_macaddr_pt(dmac),
         FMT_os_ufid_t_pt(ufid),
         (key->isparent_of_smac ? "true" : "false"),
         (key->isparent_of_dmac ? "true" : "false"),
         key->vlan_id,
         key->ethertype,
         key->ip_version,
         src_ip,
         dst_ip,
         key->ipprotocol,
         ntohs(key->sport),
         ntohs(key->dport),
         net_md_dir_to_str(key->direction),
         net_md_origin_to_str(key->originator)
        );
    if (key->fstart) LOGD(" Flow Starts");
    if (key->fend) LOGD(" Flow Ends");
}


/**
 * @brief logs the content of a flow key
 *
 * @param fkey the flow key to log
 * @param caller the calling function
 */
void
net_md_log_fkey(struct flow_key *fkey, const char *caller)
{
    struct flow_tags *ftag;
    size_t i, j;

    LOGD(" smac: %s"                \
         " dmac: %s"                \
         " isparent_of_smac: %s"    \
         " isparent_of_dmac: %s"    \
         " vlanid: %d"              \
         " ethertype: %d"           \
         " ip_version: %d"          \
         " src_ip: %s"              \
         " dst_ip: %s"              \
         " protocol: %d"            \
         " sport: %d"               \
         " dport: %d"               \
         " direction: %s"           \
         " origin: %s",             \
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
         fkey->dport,
         net_md_dir_to_str(fkey->direction),
         net_md_origin_to_str(fkey->originator)
        );
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
}


/**
 * @brief logs the content of an accumulator
 *
 * @param acc the accumulator to log
 * @param caller the calling function
 */
void
net_md_log_acc(struct net_md_stats_accumulator *acc, const char *caller)
{

    struct net_md_flow_key *key;
    struct flow_key *fkey;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG)) return;

    if (acc->key == NULL) return;
    if (acc->fkey == NULL) return;

    fkey = acc->fkey;
    key = acc->key;

    LOGD("%s: caller: %s, acc %p", __func__, caller, acc);
    LOGD("%s: Printing key => net_md_flow_key :: fkey => flow_key",
         __func__);
    LOGD("------------");

    net_md_log_key(key, caller);

    LOGD("------------");

    net_md_log_fkey(fkey, caller);

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
        net_md_log_acc(acc, __func__);
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
    net_md_log_acc(eth_acc, __func__);
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


/**
 * @brief provides local and remote info
 *
 * @param acc the accumulator
 * @param info the returning info
 *
 * @return true if filled, false otherwise
 */
bool
net_md_get_flow_info(struct net_md_stats_accumulator *acc,
                     struct net_md_flow_info *info)
{
    struct net_md_flow_key *key;

    if (acc == NULL) return false;
    if (info == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    info->ip_version = key->ip_version;
    info->direction = key->direction;

    switch (acc->direction)
    {
        case NET_MD_ACC_OUTBOUND_DIR:
        {
            if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
            {
                info->local_mac = key->smac;
                info->local_ip = key->src_ip;
                info->local_port = key->sport;
                info->remote_mac = key->dmac;
                info->remote_ip = key->dst_ip;
                info->remote_port = key->dport;
            }
            else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
            {
                info->local_mac = key->dmac;
                info->local_ip = key->dst_ip;
                info->local_port = key->dport;
                info->remote_mac = key->smac;
                info->remote_ip = key->src_ip;
                info->remote_port = key->sport;
            }
            break;
        }
        case NET_MD_ACC_INBOUND_DIR:
        {
            if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
            {
                info->local_mac = key->dmac;
                info->local_ip = key->dst_ip;
                info->local_port = key->dport;
                info->remote_mac = key->smac;
                info->remote_ip = key->src_ip;
                info->remote_port = key->sport;
            }
            else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
            {
                info->local_mac = key->smac;
                info->local_ip = key->src_ip;
                info->local_port = key->sport;
                info->remote_mac = key->dmac;
                info->remote_ip = key->dst_ip;
                info->remote_port = key->dport;
            }
            break;
        }
        case NET_MD_ACC_LAN2LAN_DIR:
        {
            if (acc->originator == NET_MD_ACC_ORIGINATOR_SRC)
            {
                info->local_mac = key->smac;
                info->local_ip = key->src_ip;
                info->local_port = key->sport;
                info->remote_mac = key->dmac;
                info->remote_ip = key->dst_ip;
                info->remote_port = key->dport;
            }
            else if (acc->originator == NET_MD_ACC_ORIGINATOR_DST)
            {
                info->local_mac = key->dmac;
                info->local_ip = key->dst_ip;
                info->local_port = key->dport;
                info->remote_mac = key->smac;
                info->remote_ip = key->src_ip;
                info->remote_port = key->sport;
            }
            break;
        }
        default:
        {
            return false;
        }
    }

    return true;
}


/**
 * @brief provides local and remote info
 *
 * @param key the network metadata key
 * @param info the returning info
 *
 * @return true if filled, false otherwise
 */
bool
net_md_get_key_info(struct net_md_flow_key *key,
                    struct net_md_flow_info *info)
{
    bool process;

    if (key == NULL) return false;
    if (info == NULL) return false;

    info->ip_version = key->ip_version;
    process = (key->direction == NET_MD_ACC_OUTBOUND_DIR);
    process |= (key->direction == NET_MD_ACC_INBOUND_DIR);
    if (!process)
    {
        info->local_mac = key->smac;
        info->local_ip = key->src_ip;
        info->local_port = key->sport;
        info->remote_mac = key->dmac;
        info->remote_ip = key->dst_ip;
        info->remote_port = key->dport;

        return true;
    }

    if (key->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        if (key->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            info->local_mac = key->smac;
            info->local_ip = key->src_ip;
            info->local_port = key->sport;
            info->remote_mac = key->dmac;
            info->remote_ip = key->dst_ip;
            info->remote_port = key->dport;
        }
        else if (key->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            info->local_mac = key->dmac;
            info->local_ip = key->dst_ip;
            info->local_port = key->dport;
            info->remote_mac = key->smac;
            info->remote_ip = key->src_ip;
            info->remote_port = key->sport;
        }
    }
    else if (key->direction == NET_MD_ACC_INBOUND_DIR)
    {
        if (key->originator == NET_MD_ACC_ORIGINATOR_SRC)
        {
            info->local_mac = key->dmac;
            info->local_ip = key->dst_ip;
            info->local_port = key->dport;
            info->remote_mac = key->smac;
            info->remote_ip = key->src_ip;
            info->remote_port = key->sport;
        }
        else if (key->originator == NET_MD_ACC_ORIGINATOR_DST)
        {
            info->local_mac = key->smac;
            info->local_ip = key->src_ip;
            info->local_port = key->sport;
            info->remote_mac = key->dmac;
            info->remote_ip = key->dst_ip;
            info->remote_port = key->dport;
        }
    }

    return true;
}
