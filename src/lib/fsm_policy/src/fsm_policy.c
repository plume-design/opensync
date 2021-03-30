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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <limits.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <netdb.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "fsm.h"
#include "policy_tags.h"
#include "fsm_policy.h"
#include "dns_cache.h"


static char tag_marker[2] = "${";
static char gtag_marker[2] = "$[";


static struct fsm_policy_session policy_mgr =
{
    .initialized = false,
};


struct fsm_policy_session * fsm_policy_get_mgr(void)
{
    return &policy_mgr;
}


int
fsm_policy_get_req_type(struct fsm_policy_req *req)
{
    struct fqdn_pending_req *fqdn_req;

    if (req == NULL) return FSM_UNKNOWN_REQ_TYPE;

    fqdn_req = req->fqdn_req;
    if (fqdn_req == NULL) return FSM_UNKNOWN_REQ_TYPE;

    return fqdn_req->req_type;
}


/**
 * @brief walk through fsm policy macs values
 *
 * Used for debug purposes
 * @param p policy
 */
void fsm_walk_policy_macs(struct fsm_policy *p)
{
    struct str_set *macs_set;
    char mac_type[32] = { 0 };
    size_t i, nelems, mac_len;

    if (p == NULL) return;

    macs_set = p->rules.macs;
    nelems = macs_set->nelems;
    mac_len = sizeof(mac_type);

    for (i = 0; i < nelems; i++)
    {
        char *s;
        om_tag_t *tag;
        bool is_tag, is_gtag, tag_has_marker;

        s = macs_set->array[i];
        is_tag = !strncmp(s, tag_marker, sizeof(tag_marker));
        is_gtag = !strncmp(s, gtag_marker, sizeof(gtag_marker));

        if (is_tag) snprintf(mac_type, mac_len, "type: tag");
        else if (is_gtag) snprintf(mac_type, mac_len, "type: group tag");
        else snprintf(mac_type, mac_len, "type: mac address");

        LOGT("mac %zu: %s, %s", i, s, mac_type);
        if (is_tag || is_gtag)
        {
            om_tag_list_entry_t *e;
            char tag_name[256];
            char *tag_s = s + 2; /* pass tag marker */

            /* pass tag values marker */
            tag_has_marker = (*tag_s == TEMPLATE_DEVICE_CHAR);
            tag_has_marker |= (*tag_s == TEMPLATE_CLOUD_CHAR);
            tag_has_marker |= (*tag_s == TEMPLATE_LOCAL_CHAR);
            if (tag_has_marker) tag_s += 1;

            /* Copy tag name, remove end marker */
            STRSCPY_LEN(tag_name, tag_s, -1);
            tag = om_tag_find_by_name(tag_name, is_gtag);
            if (tag == NULL) continue;

            LOGT("tag %s values", tag_name);
            e = ds_tree_head(&tag->values);
            while (e != NULL)
            {
                LOGT("%s", e->value);
                e = ds_tree_next(&tag->values, e);
            }
        }
    }
}


void fsm_free_url_reply(struct fsm_url_reply *reply)
{
    if (reply == NULL) return;

    if (reply->service_id == URL_GK_SVC)
    {
        free(reply->reply_info.gk_info.gk_policy);
    }
    free(reply);
}


bool find_mac_in_set(os_macaddr_t *mac, struct str_set *macs_set)
{
    char mac_s[32] = { 0 };
    size_t nelems;
    size_t i;
    bool rc;
    int ret;

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    nelems = macs_set->nelems;

    for (i = 0; i < nelems; i++)
    {
        char *set_entry;

        set_entry = macs_set->array[i];

        rc = om_tag_in(mac_s, set_entry);
        if (rc) return true;

        ret = strncmp(mac_s, set_entry, strlen(mac_s));
        if (ret != 0) continue;

        /* Found device */
        return true;
    }

    return false;
}

/**
 * @brief looks up a mac address in a policy's macs set.
 *
 * Looks up a mac in the policy macs value set. An entry in the value set can be
 * the string representation of a MAC address (assumed to be using lower cases),
 * a tag or a tag group.
 * @param req the fqdn check request
 * @param p the policy
 * @return true if found, false otherwise.
 */
bool fsm_device_in_set(struct fsm_policy_req *req, struct fsm_policy *p)
{
    struct str_set *macs_set;
    os_macaddr_t *mac;

    macs_set = p->rules.macs;
    mac = req->device_id;

    if (macs_set == NULL) return false;

    return find_mac_in_set(mac, macs_set);
}


/**
 * @brief check if a mac matches the policy's mac rule
 *
 * @param req the request being processed
 * @param policy the policy
 * @return true the the mac checks the policy's mac rule, false otherwise
 */
static bool fsm_mac_check(struct fsm_policy_req *req,
                          struct fsm_policy *policy)
{
    struct fsm_policy_rules *rules;
    bool rc;

    rules = &policy->rules;

    /* No mac rule. Consider the rule successful */
    if (!rules->mac_rule_present) return true;

    rc = fsm_device_in_set(req, policy);

    /*
     * If the device in set and the policy applies to devices out of the set,
     * the device does not match this policy
     */
    if (rc && (rules->mac_op == MAC_OP_OUT)) return false;

    /*
     * If the device is out of set and the policy applies to devices in set,
     * the device does not match this policy
     */
    if (!rc && (rules->mac_op == MAC_OP_IN)) return false;

    return true;
}


bool wildmatch(char *pattern, char *domain)
{
    char *delim = ".";
    char *saveptr1;
    char *saveptr2;
    char *str1;
    char *str2;
    char *sub1;
    char *sub2;
    int ret;
    int j;

   for (j = 1, str1 = strdup(pattern), str2 = strdup(domain); ;
        j++, str1 = NULL, str2 = NULL)
   {
        sub1 = strtok_r(str1, delim, &saveptr1);
        sub2 = strtok_r(str2, delim, &saveptr2);
        /*
         * If we have reached the end of both strings without returning false
         * then these match
         */
        if (sub1 == NULL && sub2 == NULL)
        {
            free(str1);
            free(str2);

            return true;
        }

        /*
         * If one of the strings has ended, they weren't even and
         * there was no match
         */
        if (sub1 == NULL || sub2 == NULL)
        {
            free(str1);
            free(str2);

            return false;
        }


        ret = fnmatch(sub1, sub2, 0);
        if (ret)
        {
            free(str1);
            free(str2);

            return false;
        }

        free(str1);
        free(str2);
   }

   return false;
}


/**
 * fsm_fqdn_in_set: looks up a fqdn in a policy's fqdns values set.
 * @req: the policy request
 * @p: policy
 * @op: lookup
 *
 * Checks if the request's fqdn is either an exact match, start from right
 * or start form left superset of an entry in the policy's fqdn set entry.
 */
static bool fsm_fqdn_in_set(struct fsm_policy_req *req, struct fsm_policy *p,
                            int op)
{
    struct str_set *fqdns_set;
    size_t nelems, i;
    int rc;

    fqdns_set = p->rules.fqdns;
    if (fqdns_set == NULL) return false;
    nelems = fqdns_set->nelems;
    for (i = 0; i < nelems; i++)
    {
        char *fqdn_req = req->url;
        char *entry_set = fqdns_set->array[i];
        int entry_set_len = strlen(entry_set);
        int fqdn_req_len = strlen(fqdn_req);

        if (entry_set_len > fqdn_req_len) continue;

        if (op == FSM_FQDN_OP_WILD) return wildmatch(entry_set, fqdn_req);

        if (op == FSM_FQDN_OP_SFR) fqdn_req += (fqdn_req_len - entry_set_len);

        rc = strncmp(fqdn_req, entry_set, entry_set_len);
        if (rc == 0) return true;
    }
    return false;
}

/**
 * fsm_fqdn_check: check if a fqdn matches the policy's fqdn rule
 * @req: the request being processed
 * @policy: the policy being checked against
 *
 */
static bool fsm_fqdn_check(struct fsm_policy_req *req,
                          struct fsm_policy *policy)
{
    struct fsm_policy_rules *rules;
    bool rc = false;
    bool in_policy, sfr, sfl, wild;
    int op;

    op = FSM_FQDN_OP_XM;

    rules = &policy->rules;
    if (!rules->fqdn_rule_present) return true;

    /* set policy types */
    in_policy = (rules->fqdn_op == FQDN_OP_IN);
    in_policy |= (rules->fqdn_op == FQDN_OP_SFR_IN);
    in_policy |= (rules->fqdn_op == FQDN_OP_SFL_IN);
    in_policy |= (rules->fqdn_op == FQDN_OP_WILD_IN);


    sfr = (rules->fqdn_op == FQDN_OP_SFR_IN);
    sfr |= (rules->fqdn_op == FQDN_OP_SFR_OUT);
    if (sfr) op = FSM_FQDN_OP_SFR;

    sfl = (rules->fqdn_op == FQDN_OP_SFL_IN);
    sfl |= (rules->fqdn_op == FQDN_OP_SFL_OUT);
    if (sfl) op = FSM_FQDN_OP_SFL;


    wild = (rules->fqdn_op == FQDN_OP_WILD_IN);
    wild |= (rules->fqdn_op == FQDN_OP_WILD_OUT);
    if (wild) op = FSM_FQDN_OP_WILD;


    rc = fsm_fqdn_in_set(req, policy, op);

    /* If fqdn in set and policy applies to fqdns out of set, no match */
    if ((rc) && (!in_policy)) return false;

    /* If fqdn out of set and policy applies to fqdns in set, no match */
    if ((!rc) && (in_policy)) return false;

    return true;
}


/**
 * cat_search: search a category within a policy setfilter
 * @val: category value to look for within the policy'sset of categories
 * @p: policy
 *
 * Returns true if the category is found.
 */
static inline bool cat_search(int val, struct fsm_policy *p)
{
    struct int_set *categories_set;
    void *base, *key, *res;
    size_t nmemb;
    size_t size;

    categories_set = p->rules.categories;
    if (categories_set == NULL) return false;

    base = (void *)(categories_set->array);
    size = sizeof(categories_set->array[0]);
    nmemb = categories_set->nelems;
    key = &val;
    res = bsearch(key, base, nmemb, size, fsm_cat_cmp);

    return (res != NULL);
}


/**
 * fsm_find_fqdncats_in_set: looks up a fqdn categories in a policy's
 *                           fqdn categories set.
 * @req: the policy request
 * @p: policy
 *
 * Looks up fqdn categories in the policy fqdn categories value set.
 * Returns true if found, false otherwise.
 */
bool fsm_fqdncats_in_set(struct fsm_policy_req *req, struct fsm_policy *p)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *reply;
    size_t i;
    bool rc;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    reply = req_info->reply;

    for (i = 0; i < reply->nelems; i++)
    {
        rc = cat_search(reply->categories[i], p);
        if (!rc) continue;

        fqdn_req->cat_match = reply->categories[i];
        return true;
    }
    return false;
}


/**
 * @brief looks up a fqdn in a policy's fqdns values set.
 * @param req the policy request
 * @param p the policy
 * @param op the lookup operation
 *
 * Checks if the request's ip is either an exact match, start from right
 * or start form left superset of an entry in the policy's fqdn set entry.
 */
static bool fsm_ip_in_set(struct fsm_policy_req *req, struct fsm_policy *p,
                          int op)
{
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    struct str_set *ips_set;
    size_t ip_str_len;
    size_t nelems;
    int af_family;
    size_t len;
    size_t i;
    int rc;

    acc = req->acc;
    key = acc->key;
    af_family = 0;
    if (key->ip_version == 4) af_family = AF_INET;
    if (key->ip_version == 6) af_family = AF_INET6;
    if (af_family == 0) return false;

    ips_set = p->rules.ipaddrs;
    if (ips_set == NULL) return false;

    ip_str_len = strlen(req->url);
    nelems = ips_set->nelems;
    for (i = 0; i < nelems; i++)
    {
        char *entry_set;

        entry_set = ips_set->array[i];
        len = strlen(entry_set);
        if (len != ip_str_len) continue;
        rc = strncmp(req->url, entry_set, len);
        if (!rc) return true;

    }
    return false;
}


/**
 * @brief check if a fqdn matches the policy's ipaddr rule
 * @req: the request being processed
 * @policy: the policy being checked against
 *
 */
static bool fsm_ip_check(struct fsm_policy_req *req,
                         struct fsm_policy *policy)
{
    struct fsm_policy_rules *rules;
    bool in_policy;
    bool rc;
    int op;

    rules = &policy->rules;
    if (!rules->ip_rule_present) return true;

    in_policy = (rules->ip_op == IP_OP_IN);
    op = rules->ip_op;
    rc = fsm_ip_in_set(req, policy, op);

    /* If fqdn in set and policy applies to fqdns out of set, no match */
    if ((rc) && (!in_policy)) return false;

    /* If fqdn out of set and policy applies to fqdns in set, no match */
    if ((!rc) && (in_policy)) return false;

    return true;
}


/**
 * set_action: set the request's action according to the policy
 * @req: the request being processed
 * @p: the matched policy
 */
static void set_action(struct fsm_policy_req *req, struct fsm_policy *p)
{
    if (p->action == FSM_GATEKEEPER_REQ) return;

    if (p->action == FSM_ACTION_NONE)
    {
        req->reply.action = FSM_OBSERVED;
        return;
    }

    req->reply.action = p->action;
}

#define UPDATEv4_TAG "tagv4_name"
#define UPDATEv6_TAG "tagv6_name"

/**
 * @brief set_excluded_devices_tag: set the excluded_devices tag.
 *
 * @req: the request being processed
 * @p: the matched policy
 */
void set_excluded_devices_tag(struct fsm_policy_req *req, struct fsm_policy *p)
{
    struct str_pair *pair;
    ds_tree_t *tree;

    if (p == NULL) return;

    tree = p->other_config;
    if (tree == NULL) return;

    pair = ds_tree_find(tree, "excluded_devices");
    if (pair == NULL) return;

    req->reply.excluded_devices = pair->value;
}


/**
 * @brief set_tag_update: set whether the request is flagged to update tag
 *
 * @req: the request being processed
 * @p: the matched policy
 */
void set_tag_update(struct fsm_policy_req *req, struct fsm_policy *p)
{
    struct str_pair *pair;
    ds_tree_t *tree;

    if (p == NULL) return;

    tree = p->other_config;
    if (tree == NULL) return;
    pair = ds_tree_find(tree, UPDATEv4_TAG);
    if (pair == NULL) return;

    req->reply.updatev4_tag = pair->value;

    pair = ds_tree_find(tree, UPDATEv6_TAG);
    if (pair == NULL) return;

    req->reply.updatev6_tag = pair->value;
}

/**
 * set_reporting: set the request's reporting according to the policy
 * @req: the request being processed
 * @p: the matched policy
 *
 */
void set_reporting(struct fsm_policy_req *req, struct fsm_policy *p)
{
    int reporting;

    reporting = p->report_type;

    // Return the highest policy reporting policy
    req->reply.log = reporting > req->reply.log ? reporting : req->reply.log;
}

/**
 * set_policy_record: set the request's last matching policy record
 * @req: the request being processed
 * @p: the matched policy
 */
void set_policy_record(struct fsm_policy_req *req, struct fsm_policy *p)
{
    req->reply.policy = strdup(p->table_name);
    if (req->reply.policy == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__,
             p->table_name);
    }
    req->reply.policy_idx = p->idx;
    req->reply.rule_name = strdup(p->rule_name);
    if (req->reply.rule_name == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__,
             p->rule_name);
    }
}

/**
 * set_policy_redirects: set the request redirects
 * @req: the request being processed
 * @p: the matched policy
 */
void set_policy_redirects(struct fsm_policy_req *req,
                          struct fsm_policy *p)
{
    ds_tree_t *tree;
    struct str_set *redirects;
    struct str_pair *ttl;
    size_t i, nelems;
    int rd_ttl = -1;

    if (p->action == FSM_GATEKEEPER_REQ) return;

    req->reply.redirect = false;
    req->reply.rd_ttl = -1;
    rd_ttl = -1;

    /* Check if the policy's other_config map was set */
    tree = p->other_config;
    if (tree)
    {
        /* Check if a ttl value was passed to the policy */
        ttl = ds_tree_find(tree, "rd_ttl");
        if (ttl == NULL) return;
        LOGT("%s: key: %s, value: %s", __func__, ttl->key, ttl->value);


        /* Convert ttl string to integer */
        errno = 0;
        rd_ttl = strtol(ttl->value, NULL, 10);
        if (errno != 0) return;
    }
    redirects = p->redirects;
    if (redirects == NULL) return;

    /* Set reply's redirects for both IPv4 and IPv6 */
    nelems = redirects->nelems;
    for (i = 0; i < nelems; i++)
    {
        LOGT("%s: Policy %s: redirect to %s (ttl %d seconds)",
             __func__, p->table_name, redirects->array[i], rd_ttl);
        STRSCPY(req->fqdn_req->redirects[i], redirects->array[i]);
    }

    req->reply.redirect = true;
    req->reply.rd_ttl = rd_ttl;
}

/**
 * @brief Set wb details..
 *
 * receive wb dst and src.
 *
 * @return void.
 *
 */
void populate_wb_cache_entry(struct fsm_wp_info *fqdn_reply_wb,
                             struct ip2action_wb_info *i2a_cache_wb)
{
    fqdn_reply_wb->risk_level = i2a_cache_wb->risk_level;
}

/**
 * @brief Set bc details.
 *
 * receive bc dst, src and nelems.
 *
 * @return void.
 *
 */
void populate_bc_cache_entry(struct fsm_bc_info *fqdn_reply_bc,
                             struct ip2action_bc_info *i2a_cache_bc,
                             uint8_t nelems)
{
    size_t index;

    fqdn_reply_bc->reputation = i2a_cache_bc->reputation;
    for (index = 0; index < nelems; index++)
    {
        fqdn_reply_bc->confidence_levels[index] =
            i2a_cache_bc->confidence_levels[index];
    }
}

/**
 * @brief Set gk details.
 *
 * receive gk dst and src.
 *
 * @return void.
 *
 */
void populate_gk_cache_entry(struct fsm_gk_info *fqdn_reply_gk,
                             struct ip2action_gk_info *i2a_cache_gk)
{
    fqdn_reply_gk->confidence_level = i2a_cache_gk->confidence_level;
    fqdn_reply_gk->category_id = i2a_cache_gk->category_id;
    if (i2a_cache_gk->gk_policy)
    {
        fqdn_reply_gk->gk_policy = strdup(i2a_cache_gk->gk_policy);
    }
}

bool risk_level_compare(struct fsm_url_reply *reply,
                        struct fsm_policy *policy)
{
    const char *risk_op = "<unknown>";
    int risk_level = -1;
    bool result;

    if (reply->service_id == IP2ACTION_WP_SVC)
    {
        risk_level = reply->wb.risk_level;
    }
    else if (reply->service_id == IP2ACTION_BC_SVC)
    {
        risk_level = reply->bc.reputation;
    }
    else if (reply->service_id == IP2ACTION_GK_SVC)
    {
        risk_level = reply->gk.confidence_level;
    }

    switch (policy->rules.risk_op)
    {
        case RISK_OP_EQ:
            risk_op = "=";
            result = (risk_level == policy->rules.risk_level);
            break;
        case RISK_OP_GT:
            risk_op = ">";
            result = (risk_level > policy->rules.risk_level);
            break;
        case RISK_OP_GTE:
            risk_op = ">=";
            result = (risk_level >= policy->rules.risk_level);
            break;
        case RISK_OP_LT:
            risk_op = "<";
            result = (risk_level < policy->rules.risk_level);
            break;
        case RISK_OP_LTE:
            risk_op = "<=";
            result = (risk_level <= policy->rules.risk_level);
            break;
        case RISK_OP_NEQ:
            risk_op = "!=";
            result = (risk_level != policy->rules.risk_level);
            break;
        default:
            LOGI("%s: Invalid risk operation %d", __func__,
                 policy->rules.risk_op);
            result = false;
            break;
    }

    LOGD("%s: risk %d %s policy risk %d", __func__,
         risk_level, risk_op, policy->rules.risk_level);

    return result;
}


/**
 * @brief Look up for dns cache entry
 *
 * @param req the request
 */
bool fsm_dns_cache_lookup(struct fsm_policy_req *req)
{
    struct ip2action_req  lkp_req;
    struct fsm_url_reply *reply;
    int req_type;
    size_t index;
    bool process;
    bool rc;

    /* Bail if the reuqest of no interest */
    req_type = req->fqdn_req->req_type;
    process = (req_type == FSM_IPV4_REQ);
    process |= (req_type == FSM_IPV6_REQ);
    if (!process) return false;

    /* Bail if the request is already from the cache */
    if (req->fqdn_req->from_cache) return true;

    /* look up the dns cache */
    memset(&lkp_req, 0, sizeof(lkp_req));
    lkp_req.device_mac = req->device_id;
    lkp_req.ip_addr = req->ip_addr;
    rc = dns_cache_ip2action_lookup(&lkp_req);

    /* bail if the dns cache lookup failed */
    if (!rc) return false;

    req->fqdn_req->from_cache = true;

    reply = req->fqdn_req->req_info->reply;
    reply = calloc(1, sizeof(struct fsm_url_reply));
    if (reply == NULL) return false;

    reply->service_id = lkp_req.service_id;
    reply->nelems = lkp_req.nelems;

    for (index = 0; index < lkp_req.nelems; ++index)
    {
        reply->categories[index] = lkp_req.categories[index];
    }

    if (lkp_req.service_id == IP2ACTION_BC_SVC)
    {
        populate_bc_cache_entry(&reply->bc, &lkp_req.cache_bc,
                                lkp_req.nelems);
    }
    else if (lkp_req.service_id == IP2ACTION_WP_SVC)
    {
        populate_wb_cache_entry(&reply->wb, &lkp_req.cache_wb);
    }
    else if (lkp_req.service_id == IP2ACTION_GK_SVC)
    {
        populate_gk_cache_entry(&reply->gk, &lkp_req.cache_gk);
        free(lkp_req.cache_gk.gk_policy);
    }

    req->fqdn_req->categorized = FSM_FQDN_CAT_SUCCESS;
    req->fqdn_req->req_info->reply = reply;

    return true;
}


/**
 * @brief Applies categorization policy
 *
 * @param session the requesting session
 * @param req the request
 */

bool fsm_cat_check(struct fsm_session *session,
                   struct fsm_policy_req *req,
                   struct fsm_policy *policy)
{
    struct fsm_policy_rules *rules;
    bool rc;

    rc = fsm_dns_cache_lookup(req);

    /* If no categorization request, return success */
    rules = &policy->rules;
    if (!rules->cat_rule_present) return true;

    /*
     * The policy requires categorization, no web_cat provider.
     * Return failure.
     */
    if (req->fqdn_req->categories_check == NULL) return false;

    /*
     * If the FQDN is already categorized, don't check it again.
     * This optimizes to do only one categorization lookup per fqdn request
     */
    if (req->fqdn_req->categorized != FSM_FQDN_CAT_NOP)
    {
        rc = fsm_fqdncats_in_set(req, policy);

        /*
         * If category in set and policy applies to categories out of the set,
         * no match
         */
        if (rc && (rules->cat_op == CAT_OP_OUT)) return false;

        /*
         * If category not in set and policy applies to categories in the set,
         * no match
         */
        if (!rc && (rules->cat_op == CAT_OP_IN)) return false;

        return true;
    }

    /* Apply the web_cat rules */
    rc = req->fqdn_req->categories_check(session, req, policy);

    return rc;
}


/**
 * @brief Applies risk level policy
 *
 * @param session the requesting session
 * @param req the request
 */

bool fsm_risk_level_check(struct fsm_session *session,
                          struct fsm_policy_req *req,
                          struct fsm_policy *policy)
{
    struct fsm_policy_rules *rules;
    struct fsm_url_reply *reply;
    bool rc;


    /* If no risk level request, return success */
    rules = &policy->rules;
    if (!rules->risk_rule_present) return true;

    /*
     * The policy requires risk checking, no web_risk provider.
     * Return failure.
     */
    if (req->fqdn_req->risk_level_check == NULL) return false;

    rc = fsm_dns_cache_lookup(req);
    if (rc)
    {
        reply = req->fqdn_req->req_info->reply;
        rc = risk_level_compare(reply, policy);
        return rc;
    }

    /* Apply the risk rules */
    rc = req->fqdn_req->risk_level_check(session, req, policy);

    return rc;
}


bool fsm_gatekeeper_check(struct fsm_session *session,
                          struct fsm_policy_req *req,
                          struct fsm_policy *p)
{
    bool rc;

    if (p->action != FSM_GATEKEEPER_REQ) return true;


    /*
     * The policy requires gatekeeper checking, no gatekeeper provider.
     * Return failure.
     */
    if (req->fqdn_req->gatekeeper_req == NULL) return false;

    /* Save the policy */
    req->policy = p;

    /* Apply the gatekeeper rules */
    rc = req->fqdn_req->gatekeeper_req(session, req);
    return rc;
}


/**
 * fsm_apply_policies: check a request against stored policies
 * @req: policy checking (mac, fqdn, categories) request
 *
 * Walks through the policies table looking for a match,
 * combines the action and report to apply
 */
void fsm_apply_policies(struct fsm_session *session,
                        struct fsm_policy_req *req)
{
    struct fsm_policy *last_match_policy;
    struct policy_table *table;
    struct fsm_policy *p;
    int req_type;
    bool report;

    int i;
    bool rc, matched = false;

    table = req->fqdn_req->policy_table;
    if (table == NULL)
    {
        req->reply.action = FSM_NO_MATCH;
        req->reply.log = FSM_REPORT_NONE;
        return;
    }

    req_type = fsm_policy_get_req_type(req);
    LOGT("%s(): request type %d", __func__, req_type);

    last_match_policy = NULL;
    req->report = false;

    for (i = 0; i < FSM_MAX_POLICIES; i++)
    {
        p = table->lookup_array[i];
        if (p == NULL) continue;

        /* Check if the device matches the policy's macs rule */
        rc = fsm_mac_check(req, p);
        if (!rc) continue;

        /* MAC rule passed. Check FQDN */
        rc = fsm_fqdn_check(req, p);
        if (!rc) continue;

        /* IP rule passed. Check IP */
        rc = fsm_ip_check(req, p);
        if (!rc) continue;

        /* fqdn rule passed. Check categories */
        rc = fsm_cat_check(session, req, p);
        if (!rc) continue;

        /* categories rules passed. Check risk level */
        rc = fsm_risk_level_check(session, req, p);
        if (!rc) continue;

        LOGT("%s: %s:%s succeeded", __func__, p->table_name, p->rule_name);

        /*
         * No action implicitely means going to the next entry.
         * Though record we had a match
         */
        matched = true;
        last_match_policy = p;

        /* Explicit check for reporting is required for
         * gatekeeper policy, since gatekeeper reports
         * events only for BLOCK and REDIRECT actions.
         */
        if (p->action != FSM_GATEKEEPER_REQ)
        {
            /* check if reporting is required */
            report = (p->report_type == FSM_REPORT_ALL);
            req->report |= report;
            req->rule_name = p->rule_name;
            req->policy_index = p->idx;
            req->action = (p->action == FSM_ACTION_NONE ? FSM_OBSERVED : p->action);

            LOGN("%s(): report flag %d, rule name %s ", __func__, req->report, req->rule_name);
        }
        if (p->action != FSM_ACTION_NONE) break;
    }

    if (matched)
    {
        p = last_match_policy;
        rc = fsm_gatekeeper_check(session, req, p);
        if (!rc)
        {
            req->reply.action = FSM_NO_MATCH;
            req->reply.log = FSM_REPORT_NONE;
            return;
        }
        set_reporting(req, p);
        set_tag_update(req, p);
        set_excluded_devices_tag(req, p);
        set_action(req, p);
        set_policy_record(req, p);
        set_policy_redirects(req, p);
    }
    else
    {
        // No match. Report accordingly
        req->reply.action = FSM_NO_MATCH;
        req->reply.log = FSM_REPORT_NONE;
    }
    return;
}
