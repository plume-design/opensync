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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "memory.h"
#include "fsm_policy.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_cache_cmp.h"
#include "gatekeeper_hero_stats.h"
#include "memutil.h"
#include "sockaddr_storage.h"

static struct str_set *
allocated_empty_str_set()
{
    struct str_set *new_set;

    new_set = CALLOC(1, sizeof(*new_set));
    if (new_set == NULL) return NULL;
    new_set->nelems = 0;

    return new_set;
}

int
gkc_flush_hostname(struct per_device_cache *cache, struct fsm_policy_rules *rules)
{
    hostname_comparator hostname_cmp_fct;
    struct attr_cache *checked_entry;
    cat_comparator category_cmp_fct;
    risk_comparator risk_cmp_fct;
    ds_tree_t *hostname_cache;
    struct attr_cache *entry;
    int is_matching_fqdn;
    int total_count = 0;
    bool need_delete;
    char *hostname;
    bool out_set;
    size_t i;
    bool rc;

    /* We need an actual empty set as opposed to NULL */
    if (rules->fqdns == NULL) rules->fqdns = allocated_empty_str_set();

    if (rules->risk_rule_present)
        risk_cmp_fct = get_risk_cmp(rules->risk_op);
    else
        risk_cmp_fct = get_risk_cmp(RISK_OP_TRUE);

    if (rules->cat_rule_present)
        category_cmp_fct = get_cat_cmp(rules->cat_op);
    else
        category_cmp_fct = get_cat_cmp(CAT_OP_TRUE);

    out_set = ( rules->fqdn_op == FQDN_OP_OUT ||
                rules->fqdn_op == FQDN_OP_SFR_OUT ||
                rules->fqdn_op == FQDN_OP_SFL_OUT ||
                rules->fqdn_op == FQDN_OP_WILD_OUT );

    hostname_cache = &cache->hostname_tree;

    hostname_cmp_fct = get_hostname_cmp(rules->fqdn_op);

    entry = ds_tree_head(hostname_cache);
    while (entry != NULL)
    {
        checked_entry = entry;
        entry = ds_tree_next(hostname_cache, entry);

        /* fast checks first */
        rc = risk_cmp_fct(checked_entry->confidence_level, rules->risk_level);
        if (!rc) continue;
        rc = category_cmp_fct(checked_entry->confidence_level, rules->categories);
        if (!rc) continue;

        need_delete = out_set;

        /* now scan the list of names exhaustively */
        hostname = checked_entry->attr.host_name->name;
        for (i = 0; i < rules->fqdns->nelems; i++)
        {
            is_matching_fqdn = hostname_cmp_fct(hostname, rules->fqdns->array[i]);
            if (is_matching_fqdn)
            {
                if (out_set) need_delete = false;
                else need_delete = true;
                break;
            }
        }

        if (need_delete)
        {
            gkc_free_attr_entry(checked_entry, GK_CACHE_REQ_TYPE_HOST);
            ds_tree_remove(hostname_cache, checked_entry);
            FREE(checked_entry);

            total_count++;
        }
    }

    return total_count;
}

/**
 * @brief
 *
 * @remark caller needs to ensure parameters are valid.
 */
int
gkc_flush_app(struct per_device_cache *cache, struct fsm_policy_rules *rules)
{
    struct attr_cache *checked_entry;
    risk_comparator risk_cmp_fct;
    app_comparator app_cmp_fct;
    struct attr_cache *entry;
    ds_tree_t *app_cache;
    bool is_matching_app;
    int total_count = 0;
    bool need_delete;
    char *app_name;
    bool out_set;
    size_t i;
    bool rc;

    /* We need an actual empty set as opposed to NULL */
    if (rules->apps == NULL) rules->apps = allocated_empty_str_set();

    if (rules->risk_rule_present)
        risk_cmp_fct = get_risk_cmp(rules->risk_op);
    else
        risk_cmp_fct = get_risk_cmp(RISK_OP_TRUE);

    app_cmp_fct = get_app_cmp(rules->app_op);

    out_set = (rules->app_op == APP_OP_OUT);

    app_cache = &cache->app_tree;

    entry = ds_tree_head(app_cache);
    while (entry != NULL)
    {
        checked_entry = entry;
        entry = ds_tree_next(app_cache, entry);

        /* fast check first */
        rc = risk_cmp_fct(checked_entry->confidence_level, rules->risk_level);
        if (!rc) continue;

        need_delete = out_set;

        /* now scan the list of names exhaustively */
        app_name = checked_entry->attr.app_name->name;
        for (i = 0; i < rules->apps->nelems; i++)
        {
            is_matching_app = app_cmp_fct(app_name, rules->apps->array[i]);
            if (is_matching_app)
            {
                if (out_set) need_delete = false;
                else need_delete = true;
                break;
            }
        }

        if (need_delete)
        {
            /* delete this entry */
            gkc_free_attr_entry(checked_entry, GK_CACHE_REQ_TYPE_APP);
            ds_tree_remove(app_cache, checked_entry);
            FREE(checked_entry);

            total_count++;
        }
    }

    return total_count;
}

/*
 * IP caches flushing functions
 */
int
gkc_flush_ipv4(struct per_device_cache *cache, struct fsm_policy_rules *rules)
{
    struct sockaddr_storage *checked_entry_ipv4;
    struct attr_cache *checked_entry;
    struct sockaddr_storage one_ipv4;
    cat_comparator category_cmp_fct;
    risk_comparator risk_cmp_fct;
    struct attr_cache *entry;
    ip_comparator ip_cmp_fct;
    struct sockaddr_in *in4;
    ds_tree_t *ipv4_cache;
    int is_matching_ipv4;
    int total_count = 0;
    bool need_delete;
    bool out_set;
    size_t i;
    int ret;
    bool rc;

    /* We need an actual empty set as opposed to NULL */
    /* If the function is called from gkc_flush_ip(), this will never be NULL */
    if (rules->ipaddrs == NULL) rules->ipaddrs = allocated_empty_str_set();

    one_ipv4.ss_family = AF_INET;

    if (rules->risk_rule_present)
        risk_cmp_fct = get_risk_cmp(rules->risk_op);
    else
        risk_cmp_fct = get_risk_cmp(RISK_OP_TRUE);

    if (rules->cat_rule_present)
        category_cmp_fct = get_cat_cmp(rules->cat_op);
    else
        category_cmp_fct = get_cat_cmp(CAT_OP_TRUE);

    out_set = (rules->ip_op == IP_OP_OUT);

    ipv4_cache = &cache->ipv4_tree;

    ip_cmp_fct = get_ip_cmp(rules->ip_op);

    entry = ds_tree_head(ipv4_cache);
    while (entry != NULL)
    {
        checked_entry = entry;
        entry = ds_tree_next(ipv4_cache, entry);

        /* fast checks first */
        rc = risk_cmp_fct(checked_entry->confidence_level, rules->risk_level);
        if (!rc) continue;
        rc = category_cmp_fct(checked_entry->confidence_level, rules->categories);
        if (!rc) continue;

        need_delete = out_set;

        /* now scan the list of IPv4 addresses exhaustively */
        checked_entry_ipv4 = &checked_entry->attr.ipv4->ip_addr;
        for (i = 0; i < rules->ipaddrs->nelems; i++)
        {
            /* Convert rules->ipaddrs->array[i] to IPv4 */
            in4 = (struct sockaddr_in *)&one_ipv4;
            ret = inet_pton(AF_INET, rules->ipaddrs->array[i], &in4->sin_addr);
            if (ret != 1)
                continue;

            is_matching_ipv4 = ip_cmp_fct(checked_entry_ipv4, &one_ipv4);
            if (is_matching_ipv4)
            {
                if (out_set) need_delete = false;
                else need_delete = true;
                break;
            }
        }

        if (need_delete)
        {
            gkc_free_attr_entry(checked_entry, GK_CACHE_REQ_TYPE_IPV4);
            ds_tree_remove(ipv4_cache, checked_entry);
            FREE(checked_entry);

            total_count++;
        }
    }

    return total_count;
}

int
gkc_flush_ipv6(struct per_device_cache *cache, struct fsm_policy_rules *rules)
{
    struct sockaddr_storage *checked_entry_ipv6;
    struct attr_cache *checked_entry;
    struct sockaddr_storage one_ipv6;
    cat_comparator category_cmp_fct;
    risk_comparator risk_cmp_fct;
    struct attr_cache *entry;
    ip_comparator ip_cmp_fct;
    struct sockaddr_in6 *in6;
    ds_tree_t *ipv6_cache;
    int is_matching_ipv6;
    int total_count = 0;
    bool need_delete;
    bool out_set;
    size_t i;
    int ret;
    bool rc;

    /* We need an actual empty set as opposed to NULL */
    /* If the function is called from gkc_flush_ip(), this will never be NULL */
    if (rules->ipaddrs == NULL) rules->ipaddrs = allocated_empty_str_set();

    one_ipv6.ss_family = AF_INET6;

    if (rules->risk_rule_present)
        risk_cmp_fct = get_risk_cmp(rules->risk_op);
    else
        risk_cmp_fct = get_risk_cmp(RISK_OP_TRUE);

    if (rules->cat_rule_present)
        category_cmp_fct = get_cat_cmp(rules->cat_op);
    else
        category_cmp_fct = get_cat_cmp(CAT_OP_TRUE);

    out_set = (rules->ip_op == IP_OP_OUT);

    ipv6_cache = &cache->ipv6_tree;

    ip_cmp_fct = get_ip_cmp(rules->ip_op);

    entry = ds_tree_head(ipv6_cache);
    while (entry != NULL)
    {
        checked_entry = entry;
        entry = ds_tree_next(ipv6_cache, entry);

        /* fast checks first */
        rc = risk_cmp_fct(checked_entry->confidence_level, rules->risk_level);
        if (!rc) continue;
        rc = category_cmp_fct(checked_entry->confidence_level, rules->categories);
        if (!rc) continue;

        need_delete = out_set;

        /* now scan the list of IPv6 exhaustively */
        checked_entry_ipv6 = &checked_entry->attr.ipv6->ip_addr;
        for (i = 0; i < rules->ipaddrs->nelems; i++)
        {
            /* Convert rules->ipaddrs->array[i] to IPv6 */
            in6 = (struct sockaddr_in6 *)&one_ipv6;
            ret = inet_pton(AF_INET6, rules->ipaddrs->array[i], &in6->sin6_addr);
            if (ret != 1)
                continue;

            is_matching_ipv6 = ip_cmp_fct(checked_entry_ipv6, &one_ipv6);

            if (is_matching_ipv6)
            {
                if (out_set) need_delete = false;
                else need_delete = true;
                break;
            }
        }

        if (need_delete)
        {
            gkc_free_attr_entry(checked_entry, GK_CACHE_REQ_TYPE_IPV4);
            ds_tree_remove(ipv6_cache, checked_entry);
            FREE(checked_entry);

            total_count++;
        }
    }

    return total_count;
}

int
gkc_flush_ip(struct per_device_cache *cache, struct fsm_policy_rules *rules)
{
    struct str_set *orig_ipset;
    struct str_set ipv4_set;
    struct str_set ipv6_set;
    int total_count = 0;
    char ip_addr[16];
    size_t i;
    int ret;
    bool rc;

    /* We need an actual empty set as opposed to NULL */
    if (rules->ipaddrs == NULL) rules->ipaddrs = allocated_empty_str_set();

    /* We need to build 2 subsets: IPv4 and IPv6... */
    /* Save original set so we can restore it later */
    orig_ipset = rules->ipaddrs;

    MEMZERO(ipv4_set);
    MEMZERO(ipv6_set);

    if (orig_ipset->nelems > 0)
    {
        memset(&ipv4_set, 0, sizeof(ipv4_set));
        ipv4_set.array = CALLOC(orig_ipset->nelems, sizeof(*ipv4_set.array));

        memset(&ipv6_set, 0, sizeof(ipv6_set));
        ipv6_set.array = CALLOC(orig_ipset->nelems, sizeof(*ipv6_set.array));

        for (i = 0; i < rules->ipaddrs->nelems; i++)
        {
            rc = inet_pton(AF_INET, rules->ipaddrs->array[i], ip_addr);
            if (rc == 1)
            {
                ipv4_set.array[ipv4_set.nelems] = STRDUP(rules->ipaddrs->array[i]);
                ipv4_set.nelems++;
                continue;
            }
            rc = inet_pton(AF_INET6, rules->ipaddrs->array[i], ip_addr);
            if (rc == 1)
            {
                ipv6_set.array[ipv6_set.nelems] = STRDUP(rules->ipaddrs->array[i]);
                ipv6_set.nelems++;
                continue;
            }
        }
    }

    /* ... then change rules->ipaddrs to point to each subset and flush
     * each set individually
     */
    rules->ipaddrs = &ipv4_set;
    ret = gkc_flush_ipv4(cache, rules);
    if (ret < 0)
    {
        total_count = ret;
        goto cleanup_local;
    }
    total_count += ret;

    rules->ipaddrs = &ipv6_set;
    ret = gkc_flush_ipv6(cache, rules);
    if (ret < 0)
    {
        total_count = ret;
        goto cleanup_local;
    }
    total_count += ret;

cleanup_local:
    /* restore original set */
    rules->ipaddrs = orig_ipset;

    /* Free all local stuff */
    for (i = 0; i < ipv4_set.nelems; i++)
        FREE(ipv4_set.array[i]);
    FREE(ipv4_set.array);
    for (i = 0; i < ipv6_set.nelems; i++)
        FREE(ipv6_set.array[i]);
    FREE(ipv6_set.array);

    return total_count;
}

int
gkc_flush_per_device(struct per_device_cache *entry, struct fsm_policy_rules *rules)
{
    int total_count = 0;
    int ret;

    if (rules->fqdn_rule_present)
    {
        ret = gkc_flush_hostname(entry, rules);
        if (ret < 0) return ret;
        total_count += ret;
    }

    if (rules->app_rule_present)
    {
        ret = gkc_flush_app(entry, rules);
        if (ret < 0) return ret;
        total_count += ret;
    }

    if (rules->ip_rule_present)
    {
        ret = gkc_flush_ip(entry, rules);
        if (ret < 0) return ret;
        total_count += ret;
    }

    return total_count;
}

int
gkc_flush_rules(struct fsm_policy_rules *rules)
{
    struct per_device_cache *remove_device;
    struct per_device_cache *pdevice;
    ds_tree_t *per_device_cache;
    mac_comparator mac_cmp_fct;
    struct gk_cache_mgr *mgr;
    bool has_extra_rules;
    bool is_matching_mac;
    int total_count = 0;
    bool need_delete;
    os_macaddr_t mac;
    bool out_set;
    size_t i;
    int ret;
    bool rc;

    if (rules == NULL) return -1;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return -1;

    if (rules->mac_rule_present) mac_cmp_fct = get_mac_cmp(rules->mac_op);
    else mac_cmp_fct = mac_cmp_true;

    out_set = (rules->mac_op == MAC_OP_OUT);

    has_extra_rules = (rules->fqdn_rule_present || rules->ip_rule_present || rules->app_rule_present);

    /* We go thru every single device we have cached so far,
     * and drill further if we are required to.
     */
    per_device_cache = &mgr->per_device_tree;

    pdevice = ds_tree_head(per_device_cache);
    while (pdevice != NULL)
    {
        /* if no mac_rule_present, then apply for all MACs */
        if (!rules->mac_rule_present)
        {
            ret = gkc_flush_per_device(pdevice, rules);
            if (ret < 0) return ret;
            total_count += ret;
        }
        else
        {
            /* if macs list is empty */
            if (rules->macs == NULL) break;

            /* Proceed with every mac from the policy rule */
            need_delete = out_set;
            for (i = 0; i < rules->macs->nelems; i++)
            {
                /* Align format for 2 MAC addresses */
                rc = str2os_mac_ref(rules->macs->array[i], &mac);
                if (!rc)
                {
                    LOGD("%s(): Invalid MAC address. Skipping.", __func__);
                    continue;
                }

                is_matching_mac = mac_cmp_fct(pdevice->device_mac, &mac);
                if (is_matching_mac)
                {
                    if (out_set) need_delete = false;
                    else need_delete = true;
                    break;
                }
            }

            if (need_delete)
            {
                /* if we have no extra rule (fqdn, app or IP), then delete
                 * the entire per device information in the cache.
                 */
                if (!has_extra_rules)
                {
                    remove_device = pdevice;
                    pdevice = ds_tree_next(per_device_cache, pdevice);
                    ret = gk_clean_per_device_entry(remove_device);
                    ds_tree_remove(per_device_cache, remove_device);
                    FREE(remove_device);
                    total_count += ret;
                    continue;
                }

                ret = gkc_flush_per_device(pdevice, rules);
                if (ret < 0) return ret;
                total_count += ret;
            }
        }

        /* Next device in the tree */
        pdevice = ds_tree_next(per_device_cache, pdevice);
    }

    return total_count;
}

int
gkc_flush_all(struct fsm_policy_rules *rules)
{
    int num_entries;

    /* if there is no mac rule, we flush everything */
    if (!rules->mac_rule_present)
    {
        num_entries = (int)gk_get_cache_count();
        gk_cache_cleanup();
    }
    else
    {
        num_entries = gkc_flush_rules(rules);
    }

    return num_entries;
}

int
gkc_flush_client(struct fsm_session *session, struct fsm_policy *policy)
{
    int num_hero_stats_records;
    int num_flushed_records;
    char *name;
    bool chk;

    if (!policy) return -1;

    chk = (session != NULL);
    if (chk) chk &= (session->name != NULL);
    if (chk) chk &= (strlen(session->name) != 0);
    if (chk)
    {
        name = session->name;
    }
    else
    {
        name = "'No name'";
    }

    chk  = (policy->action == FSM_FLUSH_CACHE);
    chk |= (policy->action == FSM_FLUSH_ALL_CACHE);
    if (chk)
    {
        num_hero_stats_records = gkhc_send_report(session, 5);  /* Do not report if a report was sent within 5 seconds */
        if (num_hero_stats_records > 0)
        {
            LOGT("%s(): Reported into %d hero_stats records",
                 __func__, num_hero_stats_records);
        }
        else if (num_hero_stats_records < 0)
        {
            LOGD("%s(): Failed to report hero_stats", __func__);
        }
    }

    if (policy->action == FSM_FLUSH_CACHE)
    {
        num_flushed_records = gkc_flush_rules(&policy->rules);
        if (num_flushed_records >= 0)
        {
            LOGD("%s(): FLUSH %d records from GK cache in %s",
                 __func__, num_flushed_records, name);
        }
        else
        {
            LOGD("%s(): Policy did not flush any entry from GK cache in %s",
                 __func__, name);
        }
        return num_flushed_records;
    }

    if (policy->action == FSM_FLUSH_ALL_CACHE)
    {
        num_flushed_records = gkc_flush_all(&policy->rules);
        if (num_flushed_records >= 0)
        {
            LOGD("%s(): FLUSH_ALL %d records from GK cache in %s",
                 __func__, num_flushed_records, name);
        }
        else
        {
            LOGD("%s(): Policy did not flush any entry from GK cache in %s",
                 __func__, name);
        }
        return num_flushed_records;
    }

    LOGD("%s(): Unsupported action %d", __func__, policy->action);
    return -1;
}
