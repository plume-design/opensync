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
#include "memutil.h"

static char tag_marker[2] = "${";
static char gtag_marker[2] = "$[";
const struct fsm_action
{
    int fsm_action;
    char *fsm_str_action;
} action_map[] =
{
    {
        .fsm_action = FSM_ACTION_NONE,
        .fsm_str_action = "none",
    },
    {
        .fsm_action = FSM_BLOCK,
        .fsm_str_action = "blocked",
    },
    {
        .fsm_action = FSM_ALLOW,
        .fsm_str_action = "allowed",
    },
    {
        .fsm_action = FSM_OBSERVED,
        .fsm_str_action = "observed",
    },
    {
        .fsm_action = FSM_NO_MATCH,
        .fsm_str_action = "not matched",
    },
    {
        .fsm_action = FSM_REDIRECT,
        .fsm_str_action = "blocked",
    }
};

char *cache_lookup_failure = "cacheLookupFailed";
char *remote_lookup_failure = "remoteLookupFailed";

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
    if (req == NULL) return FSM_UNKNOWN_REQ_TYPE;

    return req->req_type;
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
        FREE(reply->reply_info.gk_info.gk_policy);
    }
    FREE(reply);
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


bool
fsm_policy_wildmatch(char *pattern, char *domain)
{
    const int MAX_DOTS = 10;
    char *delim = ".";
    char *pattern_copy;
    char *pattern_tmp;
    char *pattern_ptr;
    char *pattern_sub;
    char *domain_copy;
    char *domain_tmp;
    char *domain_ptr;
    char *domain_sub;
    bool rc = false;
    int ret;
    int i;


    pattern_copy = STRDUP(pattern);
    pattern_tmp = pattern_copy;

    domain_copy = STRDUP(domain);
    domain_tmp = domain_copy;

    /* Make sure we never end in some infinite loop */
    for (i = 0; i < MAX_DOTS; i++)
    {
        pattern_sub = strtok_r(pattern_tmp, delim, &pattern_ptr);
        pattern_tmp = NULL;

        domain_sub = strtok_r(domain_tmp, delim, &domain_ptr);
        domain_tmp = NULL;

        /*
         * If we have reached the end of both strings without returning false
         * then these match
         */
        if (pattern_sub == NULL && domain_sub == NULL)
        {
            rc = true;
            break;
        }

        /*
         * If one of the strings has ended, number of delim weren't even and
         * there was no match
         */
        if (pattern_sub == NULL || domain_sub == NULL)
        {
            rc = false;
            break;
        }


        ret = fnmatch(pattern_sub, domain_sub, 0);
        if (ret)
        {
            rc = false;
            break;
        }
    }

    /* Free the copies */
    FREE(domain_copy);
    FREE(pattern_copy);

    if (i == MAX_DOTS)
    {
        LOGD("%s(): Pattern is too long %s", __func__, pattern);
        rc = false;
    }

    return rc;
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

        if (op == FSM_FQDN_OP_WILD) return fsm_policy_wildmatch(entry_set, fqdn_req);

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
bool
fsm_fqdncats_in_set(struct fsm_policy_req *req,
                    struct fsm_policy *p,
                    struct fsm_policy_reply *policy_reply)
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

        policy_reply->cat_match = reply->categories[i];
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
 * @brief updates log_action with string matching the action to report
 *
 * @param req fsm policy request
 * @param policy_reply fsm policy reply
 */
static char *
set_log_action(struct fsm_policy_req *req,
               struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;
    struct fsm_url_request *url_info;
    struct fsm_url_reply *cat_reply;
    char *log_action;
    size_t nelems;
    size_t i = 0;

    nelems = ARRAY_SIZE(action_map);
    if (policy_reply->categorized != FSM_FQDN_CAT_FAILED)
    {
        for (i = 0; i < nelems; i++)
        {
            if (policy_reply->action == action_map[i].fsm_action)
            {
                log_action = action_map[i].fsm_str_action;
                return log_action;
            }
        }
    }

    pending_req = req->fqdn_req;
    url_info = pending_req->req_info;
    cat_reply = url_info->reply;

     /* cache lookup error */
    if (!cat_reply || cat_reply->lookup_status)
    {
        log_action = cache_lookup_failure;
        return log_action;
    }

    /* remote lookup error */
    log_action = remote_lookup_failure;
    return log_action;
}

/**
 * set_action: set the request's action according to the policy
 * @req: the request being processed
 * @p: the matched policy
 */
static void
set_action(struct fsm_policy_req *req,
           struct fsm_policy *p,
           struct fsm_policy_reply *policy_reply)
{
    bool rc;

    if (p->action == FSM_GATEKEEPER_REQ) return;

    if (p->action == FSM_ACTION_NONE)
    {
        policy_reply->action = FSM_OBSERVED;
        return;
    }

    rc = policy_reply->from_cache;
    rc &= (policy_reply->action == FSM_ALLOW);
    if (!rc) policy_reply->action = p->action;
}

#define UPDATEv4_TAG "tagv4_name"
#define UPDATEv6_TAG "tagv6_name"

/**
 * @brief set_excluded_devices_tag: set the excluded_devices tag.
 *
 * @req: the request being processed
 * @p: the matched policy
 * @policy_reply: policy reply
 */
void
set_excluded_devices_tag(struct fsm_policy_req *req,
                         struct fsm_policy *p,
                         struct fsm_policy_reply *policy_reply)
{
    struct str_pair *pair;
    ds_tree_t *tree;

    if (p == NULL) return;

    tree = p->other_config;
    if (tree == NULL) return;

    pair = ds_tree_find(tree, "excluded_devices");
    if (pair == NULL) return;

    policy_reply->excluded_devices = pair->value;
}


/**
 * @brief set_tag_update: set whether the request is flagged to update tag
 *
 * @req: the request being processed
 * @p: the matched policy
 * @policy_reply: policy reply
 */
void
set_tag_update(struct fsm_policy_req *req,
               struct fsm_policy *p,
               struct fsm_policy_reply *policy_reply)
{
    struct str_pair *pair;
    ds_tree_t *tree;

    if (p == NULL) return;

    tree = p->other_config;
    if (tree == NULL) return;
    pair = ds_tree_find(tree, UPDATEv4_TAG);
    if (pair == NULL) return;

    policy_reply->updatev4_tag = pair->value;

    pair = ds_tree_find(tree, UPDATEv6_TAG);
    if (pair == NULL) return;

    policy_reply->updatev6_tag = pair->value;
}

/**
 * set_reporting: set the request's reporting according to the policy
 * @req: the request being processed
 * @p: the matched policy
 * @policy_reply: policy reply
 *
 */
void
set_reporting(struct fsm_policy_req *req,
              struct fsm_policy *p,
              struct fsm_policy_reply *policy_reply)
{
    int reporting;

    reporting = p->report_type;

    // Return the highest policy reporting policy
    policy_reply->log = reporting > policy_reply->log ? reporting : policy_reply->log;
}

/**
 * set_policy_record: set the request's last matching policy record
 * @req: the request being processed
 * @p: the matched policy
 */
void
set_policy_record(struct fsm_policy_req *req,
                  struct fsm_policy *p,
                  struct fsm_policy_reply *policy_reply)
{
    policy_reply->policy = STRDUP(p->table_name);
    if (policy_reply->policy == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__,
             p->table_name);
    }
    policy_reply->policy_idx = p->idx;
    policy_reply->rule_name = STRDUP(p->rule_name);
    if (policy_reply->rule_name == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__,
             p->rule_name);
    }
}

/**
 * set_policy_redirects: set the request redirects
 * @req: the request being processed
 * @p: the matched policy
 * @policy_reply: policy reply
 */
void set_policy_redirects(struct fsm_policy_req *req,
                          struct fsm_policy *p,
                          struct fsm_policy_reply *policy_reply)
{
    ds_tree_t *tree;
    struct str_set *redirects;
    struct str_pair *ttl;
    size_t i, nelems;
    int rd_ttl = -1;

    if (p->action == FSM_GATEKEEPER_REQ) return;

    policy_reply->redirect = false;
    policy_reply->rd_ttl = -1;
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
        STRSCPY(policy_reply->redirects[i], redirects->array[i]);
    }

    policy_reply->redirect = true;
    policy_reply->rd_ttl = rd_ttl;
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
        fqdn_reply_gk->gk_policy = STRDUP(i2a_cache_gk->gk_policy);
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
bool
fsm_dns_cache_lookup(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply)
{
    struct ip2action_req  lkp_req;
    struct fsm_url_reply *reply;
    int req_type;
    size_t index;
    bool process;
    bool rc;

    /* Bail if the reuqest of no interest */
    req_type = req->req_type;
    process = (req_type == FSM_IPV4_REQ);
    process |= (req_type == FSM_IPV6_REQ);
    if (!process) return false;

    /* Bail if the request is already from the cache */
    if (policy_reply->from_cache) return true;

    /* look up the dns cache */
    memset(&lkp_req, 0, sizeof(lkp_req));
    lkp_req.device_mac = req->device_id;
    lkp_req.ip_addr = req->ip_addr;
    rc = dns_cache_ip2action_lookup(&lkp_req);

    /* bail if the dns cache lookup failed */
    if (!rc) return false;

    policy_reply->from_cache = true;
    policy_reply->cat_unknown_to_service = lkp_req.cat_unknown_to_service;
    policy_reply->action = lkp_req.action;

    reply = req->fqdn_req->req_info->reply;
    reply = CALLOC(1, sizeof(struct fsm_url_reply));
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
        FREE(lkp_req.cache_gk.gk_policy);
    }

    policy_reply->categorized = FSM_FQDN_CAT_SUCCESS;
    req->fqdn_req->req_info->reply = reply;

    return true;
}


/**
 * @brief Applies categorization policy
 *
 * @param session the requesting session
 * @param req the request
 */

bool fsm_cat_check(struct fsm_policy_req *req,
                   struct fsm_policy *policy,
                   struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy_rules *rules;
    bool rc;

    rc = fsm_dns_cache_lookup(req, policy_reply);

    /* If no categorization request, return success */
    rules = &policy->rules;
    if (!rules->cat_rule_present) return true;

    /*
     * The policy requires categorization, no web_cat provider.
     * Return failure.
     */
    if (policy_reply->categories_check == NULL) return false;

    /*
     * If the FQDN is already categorized, don't check it again.
     * This optimizes to do only one categorization lookup per fqdn request
     */
    if (policy_reply->categorized != FSM_FQDN_CAT_NOP)
    {
        rc = fsm_fqdncats_in_set(req, policy, policy_reply);

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
    rc = policy_reply->categories_check(req, policy, policy_reply);

    return rc;
}


/**
 * @brief Applies risk level policy
 *
 * @param session the requesting session
 * @param req the request
 */

bool fsm_risk_level_check(struct fsm_policy_req *req,
                          struct fsm_policy *policy,
                          struct fsm_policy_reply *policy_reply)
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
    if (policy_reply->risk_level_check == NULL) return false;

    rc = fsm_dns_cache_lookup(req, policy_reply);
    if (rc)
    {
        if (policy_reply->action != FSM_ALLOW)
        {
            reply = req->fqdn_req->req_info->reply;
            rc = risk_level_compare(reply, policy);
        }
        return rc;
    }

    /* Apply the risk rules */
    rc = policy_reply->risk_level_check(req, policy, policy_reply);

    return rc;
}


bool fsm_gatekeeper_check(struct fsm_policy_req *req,
                          struct fsm_policy *p,
                          struct fsm_policy_reply *policy_reply)
{
    bool rc;

    if (p->action != FSM_GATEKEEPER_REQ) return true;

    LOGT("%s(): invoking gatekeeper for policy check", __func__);
    /*
     * The policy requires gatekeeper checking, no gatekeeper provider.
     * Return failure.
     */
    if (policy_reply->gatekeeper_req == NULL)
    {
        LOGD("%s(): gatekeeper not configured, not performing policy check", __func__);
        return false;
    }

    /* Save the policy */
    req->policy = p;

    /* Apply the gatekeeper rules */
    rc = policy_reply->gatekeeper_req(req, policy_reply);
    if (!rc)
    {
        LOGT("%s(): gatekeeper policy check failed", __func__);
        policy_reply->action = FSM_NO_MATCH;
        policy_reply->log = FSM_REPORT_NONE;
    }

    return rc;
}

int
fsm_policy_initialize_pending_req(struct fqdn_pending_req *pending_req,
                                  struct fsm_request_args *request_args)
{
    pending_req->fsm_context = request_args->session;
    memcpy(&pending_req->dev_id, request_args->device_id, sizeof(pending_req->dev_id));
    pending_req->acc = request_args->acc;

    pending_req->req_info = CALLOC(1, sizeof(struct fsm_url_request));
    if (pending_req->req_info == NULL) return -1;

    return 0;
}

struct fsm_policy_req *
fsm_policy_initialize_request(struct fsm_request_args *request_args)
{
    struct fsm_policy_req *policy_request;
    struct fqdn_pending_req *pending_req;
    struct sockaddr_storage *ip_addr;
    os_macaddr_t *device_id;
    int ret;

    if (request_args == NULL) return NULL;


    policy_request = CALLOC(1, sizeof(*policy_request));
    if (policy_request == NULL) return NULL;

    pending_req = CALLOC(1, sizeof(*pending_req));
    if (pending_req == NULL) goto free_policy_request;

    ret = fsm_policy_initialize_pending_req(pending_req, request_args);
    if (ret == -1)
    {
        LOGD("%s(): failed to initialize pending request", __func__);
        goto free_pending_req;
    }
    policy_request->fqdn_req = pending_req;

    /* store the device id */
    device_id = CALLOC(1, sizeof(*device_id));
    if (device_id == NULL) goto free_req_info;
    MEM_CPY(device_id, request_args->device_id, sizeof(*device_id));
    policy_request->device_id = device_id;

    ip_addr = CALLOC(1, sizeof(*ip_addr));
    if (ip_addr == NULL) goto free_device_id;
    policy_request->ip_addr = ip_addr;

    policy_request->session = request_args->session;

    policy_request->acc = request_args->acc;


    return policy_request;

free_ip_addr:
    FREE(policy_request->ip_addr);
free_device_id:
    FREE(policy_request->device_id);
free_req_info:
    FREE(pending_req->req_info);
free_pending_req:
    FREE(pending_req);
free_policy_request:
    FREE(policy_request);

    return NULL;
}

void
fsm_policy_free_reply(struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): freeing policy reply == %p", __func__, policy_reply);
    FREE(policy_reply->policy);
    FREE(policy_reply->rule_name);
    FREE(policy_reply);
}

struct fsm_policy_reply*
fsm_policy_initialize_reply(struct fsm_session *session)
{
    struct fsm_policy_reply *policy_reply;

    policy_reply = CALLOC(1, sizeof(*policy_reply));
    if (policy_reply == NULL) return NULL;

    policy_reply->rd_ttl = -1;
    policy_reply->cache_ttl = 0;
    policy_reply->risk_level = -1;
    policy_reply->cat_match = -1;
    policy_reply->redirect = -1;
    policy_reply->to_report = false;
    policy_reply->fsm_checked = false;
    policy_reply->reply_type = FSM_INLINE_REPLY;
    policy_reply->gatekeeper_response = process_gk_response_cb;

    return policy_reply;
}

void
fsm_policy_free_url(struct fqdn_pending_req* pending_req)
{
    fsm_free_url_reply(pending_req->req_info->reply);
    FREE(pending_req->req_info);
}

void
fsm_policy_free_request(struct fsm_policy_req *policy_request)
{
    struct fqdn_pending_req *pending_req;
    LOGT("%s(): freeing policy request == %p", __func__, policy_request);

    pending_req = policy_request->fqdn_req;
    if (pending_req) fsm_policy_free_url(pending_req);

    FREE(policy_request->ip_addr);
    FREE(policy_request->device_id);
    FREE(policy_request->fqdn_req);
    FREE(policy_request);

}

static void
fsm_gk_reply_updates(struct fsm_policy_req *policy_request,
                     struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy *policy;

    if (policy_request == NULL) return;
    if (policy_reply == NULL) return;

    policy = policy_request->policy;
    if (policy == NULL)
    {
        LOGD("%s(): fsm policy is NULL", __func__);
        return;
    }

    LOGT("%s(): performing fsm updates on gk policy reply", __func__);

    set_reporting(policy_request, policy, policy_reply);
    set_tag_update(policy_request, policy, policy_reply);
    set_excluded_devices_tag(policy_request, policy, policy_reply);
    set_action(policy_request, policy, policy_reply);
    set_policy_record(policy_request, policy, policy_reply);
    set_policy_redirects(policy_request, policy, policy_reply);

}

void
process_gk_response_cb(struct fsm_policy_req *policy_request,
                       struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): processing response received from gatekeeper", __func__);

    if (policy_reply->categorized == FSM_FQDN_CAT_SUCCESS)
    {
        LOGT("%s(): gatekeeper response succeeded", __func__);
        fsm_gk_reply_updates(policy_request, policy_reply);
    }

    if (policy_reply->policy_response == NULL)
    {
        LOGT("%s(): policy response cb is NULL", __func__);
        return;
    }
    LOGT("%s(): calling policy response callback", __func__);
    policy_reply->policy_response(policy_request, policy_reply);

}

static bool
fsm_gk_check_required(struct fsm_policy *policy,
                      struct fsm_policy_reply *policy_reply)
{
    if (policy->action != FSM_GATEKEEPER_REQ) return false;

    if (policy_reply->gatekeeper_req == NULL)
    {
        LOGT("%s(): gatekeeper is not configured", __func__);
        return false;
    }

    return true;
}

/**
 * fsm_apply_policies: check a request against stored policies
 * @req: policy checking (mac, fqdn, categories) request
 *
 * Walks through the policies table looking for a match,
 * combines the action and report to apply
 */
int fsm_apply_policies(struct fsm_policy_req *req,
                       struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy *last_match_policy;
    struct policy_table *table;
    int action = FSM_NO_MATCH;
    struct fsm_policy *p;
    int req_type;
    bool report;
    bool gk_req;

    int i;
    bool rc, matched = false;

    table = policy_reply->policy_table;
    if (table == NULL)
    {
        policy_reply->action = FSM_NO_MATCH;
        policy_reply->log = FSM_REPORT_NONE;
        return policy_reply->action;
    }

    req_type = fsm_policy_get_req_type(req);
    LOGT("%s(): request type %d, policy_request == %p, policy_reply == %p, fqdn == %p",
         __func__,
         req_type,
         req,
         policy_reply,
         req->fqdn_req);

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
        rc = fsm_cat_check(req, p, policy_reply);
        if (!rc) continue;

        /* categories rules passed. Check risk level */
        rc = fsm_risk_level_check(req, p, policy_reply);
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

            LOGT("%s(): report flag %d, rule name %s ", __func__, req->report, req->rule_name);
        }
        if (p->action != FSM_ACTION_NONE) break;
    }

    if (matched)
    {
        p = last_match_policy;
        gk_req = fsm_gk_check_required(p, policy_reply);
        if (gk_req == true)
        {
            LOGT("%s(): performing gatekeeper check for policy request == %p", __func__, req);
            fsm_gatekeeper_check(req, p, policy_reply);
        }

        set_reporting(req, p, policy_reply);
        set_tag_update(req, p, policy_reply);
        set_excluded_devices_tag(req, p, policy_reply);
        set_action(req, p, policy_reply);
        set_policy_record(req, p, policy_reply);
        set_policy_redirects(req, p, policy_reply);
    }
    else
    {
        // No match. Report accordingly
        policy_reply->action = FSM_NO_MATCH;
        policy_reply->log = FSM_REPORT_NONE;
    }

    policy_reply->log_action = set_log_action(req, policy_reply);
    /* policy reply struct will be freed if policy_reply->policy_response is invoked
     * so copying to local variable for returning */
    action = policy_reply->action;
    if (policy_reply->policy_response != NULL)
    {
        LOGT("%s(): calling policy response", __func__);
        policy_reply->policy_response(req, policy_reply);;
    }

    return action;
}
