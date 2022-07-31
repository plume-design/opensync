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

#include <sys/types.h>
#include <sys/stat.h>
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "log.h"
#include "policy_tags.h"
#include "fcm_filter.h"
#include "ovsdb_utils.h"
#include "memutil.h"

static struct fcm_filter_mgr filter_mgr = { 0 };

struct fcm_filter_mgr* get_filter_mgr(void)
{
    return &filter_mgr;
}

int free_filter_app(struct fcm_filter_rule *app)
{
    if (app == NULL) return -1;

    free_str_set(app->appnames);

    free_str_set(app->app_tags);
    return 0;
}

int free_schema_struct(struct fcm_filter_rule *rule)
{
    if (rule == NULL) return -1;

    FREE(rule->name);

    rule->index = 0;

    rule->smac_rule_present = false;
    free_str_set(rule->smac);

    rule->dmac_rule_present = false;
    free_str_set(rule->dmac);

    rule->vlanid_rule_present = false;
    free_int_set(rule->vlanid);

    rule->src_ip_rule_present = false;
    free_str_set(rule->src_ip);
    rule->src_ip = NULL;

    rule->dmac_rule_present = false;
    free_str_set(rule->dst_ip);

    rule->src_port_rule_present = false;
    FREE(rule->src_port);
    rule->src_port_len = 0;

    rule->dst_port_rule_present = false;
    FREE(rule->dst_port);
    rule->dst_port_len = 0;

    rule->proto_rule_present = false;
    free_int_set(rule->proto);

    free_str_tree(rule->other_config);

    rule->smac_op = FCM_OP_NONE;
    rule->dmac_op = FCM_OP_NONE;
    rule->vlanid_op = FCM_OP_NONE;
    rule->src_ip_op = FCM_OP_NONE;
    rule->dst_ip_op = FCM_OP_NONE;
    rule->src_port_op = FCM_OP_NONE;
    rule->dst_port_op = FCM_OP_NONE;
    rule->proto_op = FCM_OP_NONE;
    rule->pktcnt = 0;
    rule->pktcnt_op = FCM_MATH_NONE;
    rule->action = 0;
    return 0;
}

static
bool fcm_find_app_names_in_tag(char *app_name, char *schema_tag)
{
    bool rc;
    int ret;

    if (schema_tag == NULL) return FCM_DEFAULT_TRUE;

    rc = om_tag_in(app_name, schema_tag);
    if (rc) return FCM_DEFAULT_TRUE;

    ret = strncmp(app_name, schema_tag, strlen(app_name));

    return (ret == 0);
}

/**
 * fcm_find_ip_addr_in_tag: looks up a mac address in a tag's value set
 * @ip_addr: string representation of a MAC
 * @schema_tag: tag name as read in ovsdb schema
 *
 * Looks up a tag by its name, then looks up the mac in the tag values set.
 * Returns true if found, false otherwise.
 */

static
bool fcm_find_ip_addr_in_tag(char *ip_addr, char *schema_tag)
{
    bool rc;
    int ret;

    if (schema_tag == NULL) return FCM_DEFAULT_TRUE;

    rc = om_tag_in(ip_addr, schema_tag);
    if (rc) return FCM_DEFAULT_TRUE;

    ret = strncmp(ip_addr, schema_tag, strlen(ip_addr));

    return (ret == 0);
}

/**
 * fcm_find_device_in_tag: looks up a mac address in a tag's value set
 * @mac_s: string representation of a MAC
 * @schema_tag: tag name as read in ovsdb schema
 *
 * Looks up a tag by its name, then looks up the mac in the tag values set.
 * Returns true if found, false otherwise.
 */

static
bool fcm_find_device_in_tag(char *mac_s, char *schema_tag)
{
    bool rc;
    int ret;

    if (schema_tag == NULL) return FCM_DEFAULT_TRUE;

    rc = om_tag_in(mac_s, schema_tag);
    if (rc) return FCM_DEFAULT_TRUE;

    ret = strncmp(mac_s, schema_tag, strlen(mac_s));

    return (ret == 0);
}

/**
 * fcm_app_name_in_set: looks up a rule that matches unique index
 * @rule: fcm_filter_rule
 * @data: fkey, that need to be verified against app
 *
 * Checking atleast one app name is present set of the app
 * Returns true if found, false otherwise.
 */
static
bool fcm_app_name_in_set(struct fcm_filter_rule *app, struct flow_key *fkey)
{
    size_t i = 0;
    size_t j = 0;
    struct flow_tags *ftag;
    bool rc;

    if (!fkey || !app) return false;

    for (j = 0; j < fkey->num_tags; j++)
    {
        ftag = fkey->tags[j];

        for (i = 0; i < app->appnames->nelems; i++)
        {
            rc = fcm_find_app_names_in_tag(ftag->app_name, app->appnames->array[i]);
            if (rc) return FCM_DEFAULT_TRUE;
        }
    }

    return false;
}

/**
 * fcm_src_ip_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the dst ip address are present set of the rule
 * Returns true if found, false otherwise.
 */

static
bool fcm_src_ip_in_set(struct fcm_filter_rule *rule, fcm_filter_l3_info_t *l3_info)
{
    char ipaddr[128] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(ipaddr, sizeof(ipaddr), "%s", l3_info->src_ip);

    for (i = 0; i < rule->src_ip->nelems; i++)
    {
        rc = fcm_find_ip_addr_in_tag(ipaddr, rule->src_ip->array[i]);
        if (rc) return FCM_DEFAULT_TRUE;
    }
    return false;
}

static
int fcm_check_option(int option, bool present)
{
    /* No operation. Consider the rule successful. */
    if (option == FCM_OP_NONE) return FCM_DEFAULT_TRUE;
    if (present && option == FCM_OP_OUT) return FCM_RULED_FALSE;
    if (!present && option == FCM_OP_IN) return FCM_RULED_FALSE;

    return FCM_RULED_TRUE;
}

static
int fcm_check_app_option(int option, bool present)
{
    if (present && option == FCM_APPNAME_OP_OUT) return FCM_RULED_FALSE;
    if (!present && option == FCM_APPNAME_OP_IN) return FCM_RULED_FALSE;

    return FCM_RULED_TRUE;
}

/**
 * fcm_src_ip_filter: looks up a rule that matches unique index
 * @mgr: fcm_filter_mgr pointer to context manager
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the src ip option like in / out in the rule.
 *
 * Returns true if found ip is in set and op set to "in", false otherwise.
 */
static
int fcm_src_ip_filter(struct fcm_filter_mgr *mgr,
                      struct fcm_filter_rule *rule,
                      fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    if (!rule->src_ip_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_src_ip_in_set(rule, l3_info);
    return fcm_check_option(rule->src_ip_op, rc);
}

/**
 * fcm_dst_ip_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the dst ip address are present set of the rule
 * Returns true if found, false otherwise.
 */

static
int fcm_dst_ip_in_set(struct fcm_filter_rule *rule, fcm_filter_l3_info_t *l3_info)
{
    char ipaddr[128] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(ipaddr, sizeof(ipaddr), "%s", l3_info->dst_ip);

    for (i = 0; i < rule->dst_ip->nelems; i++)
    {
        rc = fcm_find_ip_addr_in_tag(ipaddr,rule->dst_ip->array[i]);
        if (rc) return FCM_DEFAULT_TRUE;
    }
    return FCM_DEFAULT_FALSE;
}

/**
 * fcm_dst_ip_filter: looks up a rule that matches unique index
 * @mgr: fcm_filter_mgr pointer to context manager
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the dst ip option like in / out in the rule.
 *
 * Returns true if found ip is in set and op set to "in", false otherwise.
 */

static
int fcm_dst_ip_filter(struct fcm_filter_mgr *mgr,
                      struct fcm_filter_rule *rule,
                      fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    if (!rule->dst_ip_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_dst_ip_in_set(rule, l3_info);
    return fcm_check_option(rule->dst_ip_op, rc);
}

/**
 * fcm_sport_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the sport are present set of the rule
 * Returns true if found, false otherwise.
 */

static
int fcm_sport_in_set(struct fcm_filter_rule *rule, fcm_filter_l3_info_t *l3_info)
{
    int i = 0;

    for (i = 0; i < rule->src_port_len; i++)
    {
        if ((rule->src_port[i].port_max != 0) &&
            (rule->src_port[i].port_min <= l3_info->sport
            && l3_info->sport <= rule->src_port[i].port_max ))
        {
            return FCM_DEFAULT_TRUE;
        }
        else if (rule->src_port[i].port_min == l3_info->sport) return FCM_DEFAULT_TRUE;
    }

    return FCM_DEFAULT_FALSE;
}

static
int fcm_sport_filter(struct fcm_filter_mgr *mgr,
                     struct fcm_filter_rule *rule,
                     fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->src_port_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_sport_in_set(rule, l3_info);
    return fcm_check_option(rule->src_port_op, rc);
}

/**
 * fcm_dport_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the dst port are present set of the rule
 * Returns true if found, false otherwise.
 */

static
int fcm_dport_in_set(struct fcm_filter_rule *rule, fcm_filter_l3_info_t *l3_info)
{
    int i = 0;

    for (i = 0; i < rule->dst_port_len; i++)
    {
        if ((rule->dst_port[i].port_max != 0) &&
            (rule->dst_port[i].port_min <= l3_info->dport
            && l3_info->dport <= rule->dst_port[i].port_max ))
        {
            return FCM_DEFAULT_TRUE;
        }
        else if (rule->dst_port[i].port_min == l3_info->dport) return FCM_DEFAULT_TRUE;
    }
    return FCM_DEFAULT_FALSE;
}

static
int fcm_dport_filter(struct fcm_filter_mgr *mgr,
                     struct fcm_filter_rule *rule,
                     fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->dst_port_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_dport_in_set(rule, l3_info);
    return fcm_check_option(rule->dst_port_op, rc);
}

/**
 * fcm_dport_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l3_info: l3_info, that need to be verified against rule
 *
 * Checking the l4 protocol are present set of the rule
 * Returns true if found, false otherwise.
 */
static
int fcm_proto_in_set(struct fcm_filter_rule *rule, fcm_filter_l3_info_t *l3_info)
{
    size_t i = 0;

    for (i = 0; i < rule->proto->nelems; i++)
    {
        if (rule->proto->array[i] == l3_info->l4_proto) return FCM_DEFAULT_TRUE;
    }

    return FCM_DEFAULT_FALSE;
}

static
int fcm_l4_proto_filter(struct fcm_filter_mgr *mgr,
                        struct fcm_filter_rule *rule,
                        fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->proto_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_proto_in_set(rule, l3_info);
    return fcm_check_option(rule->proto_op, rc);
}

/**
 * fcm_smac_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l2_info: l2_info, that need to be verified against rule
 *
 * Checking the src mac address are present set of the rule
 * Returns true if found, false otherwise.
 */

static
int fcm_smac_in_set(struct fcm_filter_rule *rule, fcm_filter_l2_info_t *l2_info)
{
    char mac_s[32] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(mac_s, sizeof(mac_s), "%s",l2_info->src_mac);
    for (i = 0; i < rule->smac->nelems; i++)
    {
        rc = fcm_find_device_in_tag(mac_s, rule->smac->array[i]);
        if (rc) return FCM_DEFAULT_TRUE;
    }
    return FCM_DEFAULT_FALSE;
}

/**
 * fcm_dmac_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l2_info: l2_info, that need to be verified against rule
 *
 * Checking the dst mac address are present set of the rule
 * Returns true if found, false otherwise.
 */

static
int fcm_dmac_in_set(struct fcm_filter_rule *rule, fcm_filter_l2_info_t *l2_info)
{
    char mac_s[32] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(mac_s, sizeof(mac_s), "%s", l2_info->dst_mac);

    for (i = 0; i < rule->dmac->nelems; i++)
    {
        rc = fcm_find_device_in_tag(mac_s, rule->dmac->array[i]);
        if (rc) return FCM_DEFAULT_TRUE;
    }
    return FCM_DEFAULT_FALSE;
}

/**
 * fcm_vlanid_in_set: looks up a rule that matches unique index
 * @rule: schema_FCM_Filter rule
 * @l2_info: l2_info, that need to be verified against rule
 *
 * Checking the vlan_ids are present set of the rule
 * Returns true if found, false otherwise.
 */
static
int fcm_vlanid_in_set(struct fcm_filter_rule *rule, fcm_filter_l2_info_t *l2_info)
{
    size_t i = 0;

    for (i = 0; i < rule->vlanid->nelems; i++)
    {
        if (rule->vlanid->array[i] == (int)l2_info->vlan_id) return FCM_DEFAULT_TRUE;
    }
    return FCM_DEFAULT_FALSE;
}

/**
 * fcm_smacs_filter: looks up a rule that matches unique index
 * @mgr: fcm_filter_mgr pointer to context manager
 * @rule: schema_FCM_Filter rule
 * @l2_info: l2_info, that need to be verified against rule
 *
 * Checking the src mac option like in / out in the rule.
 *
 * Returns true if found mac is in set and op set to "in", false otherwise.
 */

static
int fcm_smacs_filter(struct fcm_filter_mgr *mgr,
                     struct fcm_filter_rule *rule,
                     fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No smac operation. Consider the rule successful. */
    if (!rule->smac_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_smac_in_set(rule, l2_info);
    return fcm_check_option(rule->smac_op, rc);
}

/**
 * fcm_dmacs_filter: looks up a rule that matches unique index
 * @mgr: fcm_filter_mgr pointer to context manager
 * @rule: schema_FCM_Filter rule
 * @l2_info: l2_info, that need to be verified against rule
 *
 * Checking the dst mac option like in / out in the rule.
 *
 * Returns true if found mac is in set and op set to "in", false otherwise.
 */
static
int fcm_dmacs_filter(struct fcm_filter_mgr *mgr,
                     struct fcm_filter_rule *rule,
                     fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->dmac_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_dmac_in_set(rule, l2_info);
    return fcm_check_option(rule->dmac_op, rc);
}

static
int fcm_vlanid_filter(struct fcm_filter_mgr *mgr,
                      struct fcm_filter_rule *rule,
                      fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No valid operation. Consider the rule successful. */
    if (!rule->vlanid_rule_present) return FCM_DEFAULT_TRUE;

    rc = fcm_vlanid_in_set(rule, l2_info);
    return fcm_check_option(rule->vlanid_op, rc);
}

static
int fcm_pkt_cnt_filter(struct fcm_filter_mgr *mgr,
                       struct fcm_filter_rule *rule,
                       fcm_filter_stats_t *pkts)
{
    if (!pkts) return FCM_DEFAULT_TRUE;
    switch (rule->pktcnt_op)
    {
        case FCM_MATH_NONE:
            return FCM_DEFAULT_TRUE;
        case FCM_MATH_LEQ:
            if (pkts->pkt_cnt <= rule->pktcnt) return FCM_RULED_TRUE;
            break;
        case FCM_MATH_LT:
            if (pkts->pkt_cnt < rule->pktcnt) return FCM_RULED_TRUE;
            break;
        case FCM_MATH_GT:
            if (pkts->pkt_cnt > rule->pktcnt) return FCM_RULED_TRUE;
            break;
        case FCM_MATH_GEQ:
            if (pkts->pkt_cnt >= rule->pktcnt) return FCM_RULED_TRUE;
            break;
        case FCM_MATH_EQ:
            if (pkts->pkt_cnt == rule->pktcnt) return FCM_RULED_TRUE;
            break;
        case FCM_MATH_NEQ:
            if (pkts->pkt_cnt != rule->pktcnt) return FCM_RULED_TRUE;
            break;
        default:
            break;
    }
    return FCM_RULED_FALSE;
}

static
int fcm_app_name_filter(struct fcm_filter_rule *app,
                        struct flow_key *fkey)
{
    bool rc = false;

    /* No app op  present. Consider the rule successful. */
    if (!app->appname_present) return FCM_DEFAULT_TRUE;

    rc = fcm_app_name_in_set(app, fkey);
    return fcm_check_app_option(app->appname_op, rc);
}

static
int fcm_action_filter(struct fcm_filter_rule *rule)
{
    bool rc = false;

    rc = (rule->action == FCM_DEFAULT_INCLUDE);
    rc |= (rule->action == FCM_INCLUDE);

    return rc;
}

void fcm_filter_app_print(struct fcm_filter_rule *app)
{
    size_t i;

    if (app->appname_present)
    {
        LOGT("%s: Printing fcm app filtering config.", __func__);
        LOGT("Number of app names : %zu", app->appnames->nelems);
        for (i = 0; i < app->appnames->nelems; i++)
        {
            LOGT("app name : %s", app->appnames->array[i]);
        }
        LOGT("app name op : %s", app->appname_op == 1 ? "in" : "out");
    }

    if (app->app_tag_present)
    {
        LOGT("Number of app tags : %zu", app->app_tags->nelems);
        for (i = 0; i < app->app_tags->nelems; i++)
        {
            LOGT("app tag : %s", app->app_tags->array[i]);
        }
        LOGT("app tag op : %s", app->app_tag_op == 1 ? "in" : "out");
    }
    LOGT("----------------");
}

void fcm_apply_filter(struct fcm_session *session, struct fcm_filter_req *req)
{
    int sport_allow, dport_allow, proto_allow;
    int vlanid_allow, pktcnt_allow;
    int src_ip_allow, dst_ip_allow;
    int smac_allow, dmac_allow;
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter *rule;
    bool action_op = true;
    bool allow = true;
    int name_allow;
    int i;

    table = req->table;
    if (table == NULL)
    {
        req->action = true;
        return;
    }

    mgr = get_filter_mgr();

    for (i = 0; i < FCM_MAX_FILTERS; i++)
    {
        allow = true;
        rule = table->lookup_array[i];
        if (rule == NULL) continue;

        /* check if it matches l3 filter */
        if (req->l3_info)
        {
            src_ip_allow = fcm_src_ip_filter(mgr, &rule->filter_rule, req->l3_info);
            allow &= (src_ip_allow == FCM_RULED_FALSE? false: true);

            dst_ip_allow = fcm_dst_ip_filter(mgr, &rule->filter_rule, req->l3_info);
            allow &= (dst_ip_allow == FCM_RULED_FALSE? false: true);

            sport_allow = fcm_sport_filter(mgr, &rule->filter_rule, req->l3_info);
            allow &= (sport_allow == FCM_RULED_FALSE? false: true);

            dport_allow = fcm_dport_filter(mgr, &rule->filter_rule, req->l3_info);
            allow &= (dport_allow == FCM_RULED_FALSE? false: true);

            proto_allow = fcm_l4_proto_filter(mgr, &rule->filter_rule, req->l3_info);
            allow &= (proto_allow == FCM_RULED_FALSE? false: true);

            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("%s: rule index %d --> src_ip_allow %d dst_ip_allow %d "
                     "sport_allow %d dport_allow %d proto_allow %d", __func__,
                     rule->filter_rule.index,
                     src_ip_allow,
                     dst_ip_allow,
                     sport_allow,
                     dport_allow,
                     proto_allow );
            }
        }

        /* check if it matches l2 filter */
        if (req->l2_info)
        {
            smac_allow = fcm_smacs_filter(mgr, &rule->filter_rule , req->l2_info);
            allow &= (smac_allow == FCM_RULED_FALSE? false: true);

            dmac_allow = fcm_dmacs_filter(mgr, &rule->filter_rule, req->l2_info);
            allow &= (dmac_allow == FCM_RULED_FALSE? false: true);

            vlanid_allow = fcm_vlanid_filter(mgr, &rule->filter_rule, req->l2_info);
            allow &= (vlanid_allow == FCM_RULED_FALSE? false: true);

            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("%s: rule index %d --> smac_allow %d dmac_allow %d"
                     "vlanid_allow %d ", __func__,
                     rule->filter_rule.index,
                     smac_allow,
                     dmac_allow,
                     vlanid_allow);
            }
        }

        /* Check if it matches app filter */
        if (req->fkey)
        {
             name_allow = fcm_app_name_filter(&rule->filter_rule, req->fkey);
             allow &= (name_allow == FCM_RULED_FALSE? false: true);

             LOGT("%s: rule index %d --> app_allow %d"
                  "rule succeeded %s", __func__,
                  rule->filter_rule.index,
                  name_allow,
                  allow?"YES":"NO");
        }

        /*
         * If there is no packets count available and the the rule enforces
         * packet count check, consider the situation as a failure
        */
        if (!req->pkts)
        {
            struct fcm_filter_rule *pkt_rule;
            pkt_rule = &rule->filter_rule;
            allow &= (pkt_rule->pktcnt_op == FCM_MATH_NONE);
        }
        else
        {
            pktcnt_allow = fcm_pkt_cnt_filter(mgr, &rule->filter_rule, req->pkts);
            allow &= (pktcnt_allow == FCM_RULED_FALSE? false: true);
            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("%s: rule index %d --> pktcnt_allow %d ", __func__,
                     rule->filter_rule.index,
                     pktcnt_allow);
            }
        }

        if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        {
            LOGT("%s: fcm_filter: rule success %s", __func__, allow ? "YES" : "NO");
        }

        action_op = fcm_action_filter(&rule->filter_rule);

        if (allow) goto tuple_out;

    }
    action_op = false;

tuple_out:
    allow &=action_op;
    req->action = allow;
}

void fcm_free_filter(struct fcm_filter *ffilter)
{
    struct filter_table *table;
    int idx;

    if (ffilter == NULL) return;

    idx = ffilter->filter_rule.index;
    table = ffilter->table;
    ds_dlist_remove(&table->filter_rules, ffilter);
    free_schema_struct(&ffilter->filter_rule);
    free_filter_app(&ffilter->filter_rule);
    FREE(ffilter);
    table->lookup_array[idx] = NULL;
}

void fcm_filter_cleanup(void)
{
    ds_tree_t *tables_tree, *filters_tree, *clients_tree;
    struct fcm_filter_client *client, *p_client;
    struct fcm_filter  *ffilter, *p_to_remove;
    struct filter_table *table, *t_to_remove;
    struct fcm_filter *rule = NULL;
    struct fcm_filter_mgr *mgr;

    mgr = get_filter_mgr();
    tables_tree = &mgr->fcm_filters;
    table = ds_tree_head(tables_tree);
    clients_tree = &mgr->clients;
    p_client = ds_tree_head(clients_tree);

    while (p_client != NULL)
    {
        client = p_client;
        p_client = ds_tree_next(clients_tree, p_client);
        ds_tree_remove(&mgr->clients, client);
        FREE(client->name);
        FREE(client);
    }

    while (table != NULL)
    {
        filters_tree = &table->filters;
        ffilter = ds_tree_head(filters_tree);
        while (ffilter != NULL)
        {
            p_to_remove = ffilter;
            ffilter = ds_tree_next(filters_tree, ffilter);
            fcm_free_filter(p_to_remove);
        }
        t_to_remove = table;
        while (!ds_dlist_is_empty(&table->filter_rules))
        {
            rule = ds_dlist_head(&table->filter_rules);
            free_schema_struct(&rule->filter_rule);
            free_filter_app(&rule->filter_rule);
            ds_dlist_remove(&table->filter_rules, rule);
            FREE(rule);
        }
        table = ds_tree_next(tables_tree, table);
        ds_tree_remove(tables_tree, t_to_remove);
        FREE(t_to_remove);
    }

    mgr->initialized = 0;
}
