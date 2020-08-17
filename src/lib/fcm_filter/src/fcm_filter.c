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
#include <sys/types.h>
#include <netdb.h>
#include <sys/stat.h>

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
#include "policy_tags.h"
#include "fcm_filter.h"
#include "ds_tree.h"
#include "ovsdb_utils.h"



ovsdb_table_t table_FCM_Filter;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_Openflow_Tag_Group;

static bool
fcm_filter_set_app_names(struct fcm_filter_app *app,
                         struct schema_FCM_Filter *rule);
static bool
fcm_filter_set_app_tags(struct fcm_filter_app *app,
                        struct schema_FCM_Filter *rule);
static
fcm_filter_t* fcm_filter_find_rule(ds_dlist_t *filter_list,
                                   int32_t index);
static ds_dlist_t
*fcm_filter_get_list_by_name(char *filter_name);

static
int free_schema_struct(schema_FCM_Filter_rule_t *rule);

static void
free_filter_app(struct fcm_filter_app *app);

static struct fcm_filter_mgr filter_mgr = { 0 };

struct fcm_filter_mgr* get_filter_mgr(void)
{
    return &filter_mgr;
}

static
struct ip_port* port_to_int1d(int element_len, int element_witdh,
                              char array[][element_witdh])
{
    struct ip_port *arr = NULL;
    int i;
    char *pos = NULL;

    if (!array) return NULL;

    arr = (struct ip_port*) calloc(element_len, sizeof(struct ip_port));
    if (!arr) return NULL;

    for (i = 0; i < element_len; i++)
    {
        pos =  strstr(array[i], "-");
        arr[i].port_min = atoi(&array[i][0]);
        if (pos)
        {
            arr[i].port_max = atoi(pos+1);
        }
        LOGT("fcm_filter: port min=%d max=%d \n", arr[i].port_min, arr[i].port_max);
    }
    return arr;
}

static
enum fcm_operation convert_str_enum(char *op_string)
{
     if (strncmp(op_string, "in", strlen("in")) == 0)
        return FCM_OP_IN;
    else if (strncmp(op_string, "out", strlen("out")) == 0)
        return FCM_OP_OUT;
    else
        return FCM_OP_NONE;
}

static
enum fcm_action convert_action_enum(char *op_string)
{
    if (strncmp(op_string, "include", strlen("include")) == 0)
        return FCM_INCLUDE;
    else if (strncmp(op_string, "exclude", strlen("exclude")) == 0)
        return FCM_EXCLUDE;
    else
        return FCM_DEFAULT_INCLUDE;
}

static
enum fcm_math convert_math_str_enum(char *op_string)
{
    if (strncmp(op_string, "leq", strlen("leq")) == 0)
        return FCM_MATH_LEQ;
    else if (strncmp(op_string, "lt", strlen("lt")) == 0)
        return FCM_MATH_LT;
    else if (strncmp(op_string, "gt", strlen("gt")) == 0)
        return FCM_MATH_GT;
    else if (strncmp(op_string, "geq", strlen("geq")) == 0)
        return FCM_MATH_GEQ;
    else if (strncmp(op_string, "eq", strlen("eq")) == 0)
        return FCM_MATH_EQ;
    else if (strncmp(op_string, "neq", strlen("neq")) == 0)
        return FCM_MATH_NEQ;
    else
        return FCM_MATH_NONE;
}

static
char* convert_enum_str(enum fcm_operation op)
{
    switch(op)
    {
        case FCM_OP_NONE:
            return "none";
        case FCM_OP_IN:
            return "in";
        case FCM_OP_OUT:
            return "out";
        default:
            return "unknown";
    }
}

static
int copy_from_schema_struct(schema_FCM_Filter_rule_t *rule,
                            struct schema_FCM_Filter *filter)
{
    if (!rule) return -1;

    /* copy filter name */
    rule->name = strdup(filter->name);
    if (!rule->name) return -1;

    /* copy index value; */
    rule->index =  filter->index;

    rule->smac = schema2str_set(sizeof(filter->smac[0]),
                                filter->smac_len,
                                filter->smac);
    if (!rule->smac && filter->smac_len != 0) goto free_rule;

    rule->dmac = schema2str_set(sizeof(filter->dmac[0]),
                                filter->dmac_len,
                                filter->dmac);
    if (!rule->dmac && filter->dmac_len != 0) goto free_rule;

    rule->vlanid = schema2int_set(filter->vlanid_len, filter->vlanid);
    if (!rule->vlanid && filter->vlanid_len != 0) goto free_rule;

    rule->src_ip = schema2str_set(sizeof(filter->src_ip[0]),
                                  filter->src_ip_len,
                                  filter->src_ip);
    if (!rule->src_ip && filter->src_ip_len != 0) goto free_rule;

    rule->dst_ip = schema2str_set(sizeof(filter->dst_ip[0]),
                                  filter->dst_ip_len,
                                  filter->dst_ip);
    if (!rule->dst_ip && filter->dst_ip_len != 0) goto free_rule;

    rule->src_port = port_to_int1d(filter->src_port_len,
                                   sizeof(filter->src_port[0]),
                                   filter->src_port);
    rule->src_port_len = filter->src_port_len;
    if (!rule->src_port && rule->src_port_len != 0) goto free_rule;

    rule->dst_port = port_to_int1d(filter->dst_port_len,
                                   sizeof(filter->dst_port[0]),
                                   filter->dst_port);
    rule->dst_port_len = filter->dst_port_len;
    if (!rule->dst_port && rule->dst_port_len != 0) goto free_rule;

    rule->proto = schema2int_set(filter->proto_len, filter->proto);
    if (!rule->proto && filter->proto_len != 0) goto free_rule;

    rule->other_config = schema2tree(sizeof(filter->other_config_keys[0]),
                                     sizeof(filter->other_config[0]),
                                     filter->other_config_len,
                                     filter->other_config_keys,
                                     filter->other_config);
    if (!rule->other_config && filter->other_config_len != 0) goto free_rule;

    rule->pktcnt = filter->pktcnt;

    rule->smac_op = convert_str_enum(filter->smac_op);
    rule->dmac_op = convert_str_enum(filter->dmac_op);
    rule->vlanid_op = convert_str_enum(filter->vlanid_op);
    rule->src_ip_op = convert_str_enum(filter->src_ip_op);
    rule->dst_ip_op = convert_str_enum(filter->dst_ip_op);
    rule->src_port_op = convert_str_enum(filter->src_port_op);
    rule->dst_port_op = convert_str_enum(filter->dst_port_op);
    rule->proto_op = convert_str_enum(filter->proto_op);
    rule->pktcnt_op = convert_math_str_enum(filter->pktcnt_op);
    rule->action = convert_action_enum(filter->action);

    return 0;

free_rule:
    free_schema_struct(rule);
    return -1;
}


void free_filter_app(struct fcm_filter_app *app)
{
    if (!app) return;

    free_str_set(app->names);
    app->names = NULL;

    free_str_set(app->tags);
    app->tags = NULL;
    return;
}

static
int free_schema_struct(schema_FCM_Filter_rule_t *rule)
{
    if (!rule) return -1;

    free(rule->name);
    rule->name = NULL;

    rule->index = 0;

    free_str_set(rule->smac);
    rule->smac = NULL;

    free_str_set(rule->dmac);
    rule->dmac = NULL;

    free_int_set(rule->vlanid);
    rule->vlanid = NULL;

    free_str_set(rule->src_ip);
    rule->src_ip = NULL;

    free_str_set(rule->dst_ip);
    rule->dst_ip = NULL;

    free(rule->src_port);
    rule->src_port_len = 0;
    rule->src_port = NULL;

    free(rule->dst_port);
    rule->dst_port_len = 0;
    rule->dst_port = NULL;

    free_int_set(rule->proto);
    rule->proto = NULL;

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

static void
fcm_remove_rule_from_filter(ds_dlist_t *filter_head, struct fcm_filter *rule)
{
    if (!filter_head || !rule) return;

    LOGT("fcm_filter: Removing filter with index %d", rule->filter_rule.index);
    ds_dlist_remove(filter_head, rule);
    free_schema_struct(&rule->filter_rule);
    free_filter_app(&rule->app);
    free(rule);
    return;
}


/**
 * fcm_filter_find_rule: looks up a rule that matches unique index
 * @filter_list: head ds_dlist_t
 * @index: index tag
 *
 * Looks up a rule by its index.
 * Returns filter node if found, NULL otherwise.
 */
static
fcm_filter_t *fcm_filter_find_rule(ds_dlist_t *filter_list, int32_t index)
{
    struct fcm_filter *rule = NULL;
    ds_dlist_foreach(filter_list, rule)
    {
        if (rule->filter_rule.index == index) return rule;
    }
    return NULL;
}

static
void fcm_filter_insert_rule(ds_dlist_t *filter_list, fcm_filter_t *rule_new)
{
    struct fcm_filter *rule = NULL;
    /* check empty */
    if (filter_list == NULL || ds_dlist_is_empty(filter_list))
    {
        /* insert at tail */
        ds_dlist_insert_tail(filter_list, rule_new);
        return;
    }

    ds_dlist_foreach(filter_list, rule)
    {
        if (rule_new->filter_rule.index < rule->filter_rule.index)
        {
            ds_dlist_insert_before(filter_list, rule, rule_new);
            return;
        }
        else if (rule_new->filter_rule.index == rule->filter_rule.index)
        {
            LOGE("%s: Index number exist, rule not inserted,"
                 " filter name %s index no %d",
                 __func__, rule_new->filter_rule.name,
                 rule_new->filter_rule.index);
            return;
        }
    }
    ds_dlist_insert_tail(filter_list, rule_new);
}

static
bool fcm_find_app_names_in_tag(char *app_name, char *schema_tag)
{
    bool rc;
    int ret;

    if (schema_tag == NULL) return true;

    rc = om_tag_in(app_name, schema_tag);
    if (rc) return true;

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

    if (schema_tag == NULL) return true;

    rc = om_tag_in(ip_addr, schema_tag);
    if (rc) return true;

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

    if (schema_tag == NULL) return true;

    rc = om_tag_in(mac_s, schema_tag);
    if (rc) return true;

    ret = strncmp(mac_s, schema_tag, strlen(mac_s));

    return (ret == 0);
}

/**
 * fcm_app_name_in_set: looks up a rule that matches unique index
 * @rule: fcm_filter_app
 * @data: fkey, that need to be verified against app
 *
 * Checking atleast one app name is present set of the app
 * Returns true if found, false otherwise.
 */
static
bool fcm_app_name_in_set(struct fcm_filter_app *app, struct flow_key *fkey)
{
    size_t i = 0;
    size_t j = 0;
    struct flow_tags *ftag;
    bool rc;

    if (!fkey || !app) return false;

    for (j = 0; j < fkey->num_tags; j++)
    {
        ftag = fkey->tags[j];

        for (i = 0; i < app->names->nelems; i++)
        {
            rc = fcm_find_app_names_in_tag(ftag->app_name, app->names->array[i]);
            if (rc) return true;
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
bool fcm_src_ip_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l3_info_t *l3_info)
{
    char ipaddr[128] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(ipaddr, sizeof(ipaddr), "%s", l3_info->src_ip);

    for (i = 0; i < rule->src_ip->nelems; i++)
    {
        rc = fcm_find_ip_addr_in_tag(ipaddr, rule->src_ip->array[i]);
        if (rc) return true;
    }
    return false;
}

static
enum fcm_rule_op fcm_check_option(enum fcm_operation option, bool present)
{

    /* No operation. Consider the rule successful. */
    if (option == FCM_OP_NONE) return FCM_DEFAULT_TRUE;
    if (present && option == FCM_OP_OUT) return FCM_RULED_FALSE;
    if (!present && option == FCM_OP_IN) return FCM_RULED_FALSE;

    return FCM_RULED_TRUE;
}

static
enum fcm_rule_op fcm_check_app_option(enum fcm_appname_op option, bool present)
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
enum fcm_rule_op fcm_src_ip_filter(struct fcm_filter_mgr *mgr,
                                schema_FCM_Filter_rule_t *rule,
                                fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    if (!rule->src_ip) return FCM_DEFAULT_TRUE;
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
bool fcm_dst_ip_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l3_info_t *l3_info)
{
    char ipaddr[128] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(ipaddr, sizeof(ipaddr), "%s", l3_info->dst_ip);

    for (i = 0; i < rule->dst_ip->nelems; i++)
    {
        rc = fcm_find_ip_addr_in_tag(ipaddr,rule->dst_ip->array[i]);
        if (rc) return true;
    }
    return false;
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
enum fcm_rule_op fcm_dst_ip_filter(struct fcm_filter_mgr *mgr,
                                schema_FCM_Filter_rule_t *rule,
                                fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    if (!rule->dst_ip) return FCM_DEFAULT_TRUE;
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
bool fcm_sport_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l3_info_t *l3_info)
{
    int i = 0;

    for (i = 0; i < rule->src_port_len; i++)
    {
        if ((rule->src_port[i].port_max != 0) &&
            (rule->src_port[i].port_min <= l3_info->sport
             && l3_info->sport <= rule->src_port[i].port_max ))
        {
            return true;
        }
        else if (rule->src_port[i].port_min == l3_info->sport) return true;
    }

    return false;
}

static
enum fcm_rule_op fcm_sport_filter(struct fcm_filter_mgr *mgr,
                               schema_FCM_Filter_rule_t *rule,
                               fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->src_port) return FCM_DEFAULT_TRUE;
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
bool fcm_dport_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l3_info_t *l3_info)
{
    int i = 0;

    for (i = 0; i < rule->dst_port_len; i++)
    {
        if ((rule->dst_port[i].port_max != 0) &&
            (rule->dst_port[i].port_min <= l3_info->dport
             && l3_info->dport <= rule->dst_port[i].port_max ))
        {
            return true;
        }
        else if (rule->dst_port[i].port_min == l3_info->dport) return true;
    }
    return false;
}

static
enum fcm_rule_op fcm_dport_filter(struct fcm_filter_mgr *mgr,
                               schema_FCM_Filter_rule_t *rule,
                               fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->dst_port) return FCM_DEFAULT_TRUE;
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
bool fcm_proto_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l3_info_t *l3_info)
{
    size_t i = 0;

    for (i = 0; i < rule->proto->nelems; i++)
    {
        if (rule->proto->array[i] == l3_info->l4_proto) return true;
    }

    return false;
}

static
enum fcm_rule_op fcm_l4_proto_filter(struct fcm_filter_mgr *mgr,
                                  schema_FCM_Filter_rule_t *rule,
                                  fcm_filter_l3_info_t *l3_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->proto) return FCM_DEFAULT_TRUE;
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
int fcm_smac_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l2_info_t *l2_info)
{
    char mac_s[32] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(mac_s, sizeof(mac_s), "%s",l2_info->src_mac);

    for (i = 0; i < rule->smac->nelems; i++)
    {
        rc = fcm_find_device_in_tag(mac_s, rule->smac->array[i]);
        if (rc) return true;
    }
    return false;
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
bool fcm_dmac_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l2_info_t *l2_info)
{
    char mac_s[32] = { 0 };
    size_t i = 0;
    bool rc;

    snprintf(mac_s, sizeof(mac_s), "%s", l2_info->dst_mac);


    for (i = 0; i < rule->dmac->nelems; i++)
    {
        rc = fcm_find_device_in_tag(mac_s, rule->dmac->array[i]);
        if (rc) return true;
    }
    return false;
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
bool fcm_vlanid_in_set(schema_FCM_Filter_rule_t *rule, fcm_filter_l2_info_t *l2_info)
{
    size_t i = 0;

    for (i = 0; i < rule->vlanid->nelems; i++)
    {
        if (rule->vlanid->array[i] == (int)l2_info->vlan_id) return true;
    }
    return false;
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
enum fcm_rule_op fcm_smacs_filter(struct fcm_filter_mgr *mgr,
                               schema_FCM_Filter_rule_t *rule,
                               fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No smac operation. Consider the rule successful. */
    if (!rule->smac) return FCM_DEFAULT_TRUE;
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
enum fcm_rule_op fcm_dmacs_filter(struct fcm_filter_mgr *mgr,
                               schema_FCM_Filter_rule_t *rule,
                               fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No dmacs operation. Consider the rule successful. */
    if (!rule->dmac) return FCM_DEFAULT_TRUE;
    rc = fcm_dmac_in_set(rule, l2_info);
    return fcm_check_option(rule->dmac_op, rc);
}

static
enum fcm_rule_op fcm_vlanid_filter(struct fcm_filter_mgr *mgr,
                                schema_FCM_Filter_rule_t *rule,
                                fcm_filter_l2_info_t *l2_info)
{
    bool rc = false;

    /* No valid operation. Consider the rule successful. */
    if (!rule->vlanid) return FCM_DEFAULT_TRUE;
    rc = fcm_vlanid_in_set(rule, l2_info);
    return fcm_check_option(rule->vlanid_op, rc);
}

static
enum fcm_rule_op fcm_pkt_cnt_filter(struct fcm_filter_mgr *mgr,
                             schema_FCM_Filter_rule_t *rule,
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
enum fcm_rule_op fcm_app_name_filter(struct fcm_filter_app *app,
                                     struct flow_key *fkey)
{
    bool rc = false;

    /* No app op  present. Consider the rule successful. */
    if (!app->name_present) return FCM_DEFAULT_TRUE;
    rc = fcm_app_name_in_set(app, fkey);
    return fcm_check_app_option(app->name_op, rc);
}

static
bool fcm_action_filter(schema_FCM_Filter_rule_t *rule)
{
    bool rc = false;

    rc = (rule->action == FCM_DEFAULT_INCLUDE);
    rc |= (rule->action == FCM_INCLUDE);

    return rc;
}

void fcm_filter_app_print(struct fcm_filter_app *app)
{
    size_t i;

    if (app->name_present)
    {
        LOGT("fcm_filter: Printing fcm app filtering config.");
        LOGT("Number of app names : %zu", app->names->nelems);
        for (i = 0; i < app->names->nelems; i++)
        {
            LOGT("app name : %s", app->names->array[i]);
        }
        LOGT("app name op : %s", app->name_op == 1 ? "in" : "out");
    }

    if (app->tag_present)
    {
        LOGT("Number of app tags : %zu", app->tags->nelems);
        for (i = 0; i < app->tags->nelems; i++)
        {
            LOGT("app tag : %s", app->tags->array[i]);
        }
        LOGT("app tag op : %s", app->tag_op == 1 ? "in" : "out");
    }
    LOGT("----------------");
}

void fcm_filter_print (void)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    struct fcm_filter *rule = NULL;
    size_t i = 0;
    int j = 0;

    unsigned int head_index = 0;

    LOGT("fcm_filter: Printing fcm filtering config.");
    for (head_index = 0; head_index < FCM_MAX_FILTER_BY_NAME; head_index++)
    {
        ds_dlist_foreach(&mgr->filter_type_list[head_index], rule)
        {
            LOGT("** name : %s index %d", rule->filter_rule.name,
                 rule->filter_rule.index);
            if (rule->filter_rule.src_ip)
            {
                LOGT("src_ip present : %zu", rule->filter_rule.src_ip->nelems);
                for (i = 0; i < rule->filter_rule.src_ip->nelems; i++)
                {
                    LOGT("  src_ip : %s", rule->filter_rule.src_ip->array[i]);
                }
            }
            if (rule->filter_rule.dst_ip)
            {
                LOGT("dst_ip present : %zu", rule->filter_rule.dst_ip->nelems);
                for (i = 0; i < rule->filter_rule.dst_ip->nelems; i++)
                {
                    LOGT("  dst_ip : %s", rule->filter_rule.dst_ip->array[i]);
                }
            }
            LOGT("src_port present : %d", rule->filter_rule.src_port_len);
            for (j = 0; j < rule->filter_rule.src_port_len; j++)
            {
                LOGT("  src_port : min %d to max %d",
                    rule->filter_rule.src_port[j].port_min,
                    rule->filter_rule.src_port[j].port_max);
            }
            LOGT("dst_port present : %d ", rule->filter_rule.dst_port_len);
            for (j = 0; j < rule->filter_rule.dst_port_len; j++)
            {
                LOGT("  dst_port : min %d to max %d",
                    rule->filter_rule.dst_port[j].port_min,
                    rule->filter_rule.dst_port[j].port_max);
            }
            if (rule->filter_rule.proto)
            {
                LOGT(
                    "l4 protocol present : %zu",
                    rule->filter_rule.proto->nelems);
                for (i = 0; i < rule->filter_rule.proto->nelems; i++)
                {
                    LOGT("  protocol : %d", rule->filter_rule.proto->array[i]);
                }
            }
            if (rule->filter_rule.smac)
            {
                LOGT("smac present : %zu", rule->filter_rule.smac->nelems);
                for (i = 0; i < rule->filter_rule.smac->nelems; i++)
                {
                    LOGT("  src_mac : %s", rule->filter_rule.smac->array[i]);
                }
            }
            if (rule->filter_rule.dmac)
            {
                LOGT("dmac present : %zu", rule->filter_rule.dmac->nelems);
                for (i = 0; i < rule->filter_rule.dmac->nelems; i++)
                {
                    LOGT("  dst_mac : %s", rule->filter_rule.dmac->array[i]);
                }
            }
            if (rule->filter_rule.vlanid)
            {
                LOGT("vlan id present : %zu", rule->filter_rule.vlanid->nelems);
                for (i = 0; i < rule->filter_rule.vlanid->nelems; i++)
                {
                    LOGT("  vlan_ids : %d", rule->filter_rule.vlanid->array[i]);
                }
            }

            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
                fcm_filter_app_print(&rule->app);

            LOGT("src_ip_op :  \t%s",
                convert_enum_str(rule->filter_rule.src_ip_op));
            LOGT("dst_ip_op :  \t%s",
                convert_enum_str(rule->filter_rule.dst_ip_op));
            LOGT("src_port_op :\t%s",
                convert_enum_str(rule->filter_rule.src_port_op));
            LOGT("dst_port_op :\t%s",
                convert_enum_str(rule->filter_rule.dst_port_op));
            LOGT("proto_op :   \t%s",
                convert_enum_str(rule->filter_rule.proto_op));
            LOGT("smac_op :    \t%s",
                convert_enum_str(rule->filter_rule.smac_op));
            LOGT("dmac_op :    \t%s",
                convert_enum_str(rule->filter_rule.dmac_op));
            LOGT("vlanid_op :  \t%s",
                convert_enum_str(rule->filter_rule.vlanid_op));
            LOGT("pktcnt : %ld pkt_op : %d", rule->filter_rule.pktcnt,
                rule->filter_rule.pktcnt_op);
            LOGT("action : %d ", rule->filter_rule.action);
        }
    }
    LOGT("---------------------------");
}

static bool fcm_filter_set_app_names(struct fcm_filter_app *app,
                                     struct schema_FCM_Filter *rule)
{
    int cmp;

    app->name_present = rule->appname_op_exists;
    if (!app->name_present) return true;

    app->name_op = -1;
    cmp = strcmp(rule->appname_op, "in");
    if (!cmp) app->name_op = FCM_APPNAME_OP_IN;

    cmp = strcmp(rule->appname_op, "out");
    if (!cmp) app->name_op = FCM_APPNAME_OP_OUT;

    if (app->name_op == -1) return false;

    app->names = schema2str_set(sizeof(rule->appnames[0]),
                                  rule->appnames_len,
                                  rule->appnames);

    if (rule->appnames_len && !app->names) return false;
    return true;
}

static bool fcm_filter_set_app_tags(struct fcm_filter_app *app,
                                   struct schema_FCM_Filter *rule)
{
    int cmp;

    app->tag_present = rule->apptag_op_exists;
    if (!app->tag_present) return true;

    app->tag_op = -1;
    cmp = strcmp(rule->apptag_op, "in");
    if (!cmp) app->tag_op = FCM_APPTAG_OP_IN;

    cmp = strcmp(rule->apptag_op, "out");
    if (!cmp) app->tag_op = FCM_APPTAG_OP_OUT;

    if (app->tag_op == -1) return false;

    app->tags = schema2str_set(sizeof(rule->apptags[0]),
                                  rule->apptags_len,
                                  rule->apptags);

    if (rule->apptags_len && !app->tags) return false;

    return true;
}

static
ds_dlist_t *fcm_filter_get_list_by_name(char *filter_name)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    struct fcm_filter *rule = NULL;
    int i = 0;

    for (i = 0; i < FCM_MAX_FILTER_BY_NAME; i++)
    {
        rule = ds_dlist_head(&mgr->filter_type_list[i]);
        if (!rule) return &mgr->filter_type_list[i];
        else if (!strncmp(rule->filter_rule.name,
                          filter_name,
                          strlen(rule->filter_rule.name)))
        {
            return &mgr->filter_type_list[i];
        }
    }
    return NULL;
}

/**
 * fcm_add_filter: add a FCM Filter
 * @policy: the policy to add
 */

static
void fcm_add_filter(struct schema_FCM_Filter *filter)
{
    struct fcm_filter *rule = NULL;

    ds_dlist_t *filter_head = NULL;

    if (!filter) return;

    filter_head = fcm_filter_get_list_by_name(filter->name);
    if (!filter_head)
    {
        LOGE("fcm_filter: Exceeded the max[%d] number of filter names.",FCM_MAX_FILTER_BY_NAME);
        return;
    }

    rule = calloc(1, sizeof(struct fcm_filter));
    if (!rule) return;

    if (copy_from_schema_struct(&rule->filter_rule, filter) < 0)
        goto free_rule;

    if (!fcm_filter_set_app_names(&rule->app, filter) ||
        !fcm_filter_set_app_tags(&rule->app, filter))
    {
        LOGE("fcm_filter: Failed to add app names/tags.");
        goto free_rule;
    }

    rule->valid = true;
    fcm_filter_insert_rule(filter_head, rule);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        fcm_filter_print();

    return;
free_rule:
    LOGE("fcm_filter: unable to add rule");
    free(rule);
}

/**
 * fcm_delete_filter: remove rule from list that matching index
 * @policy: the policy to add
 */

static
void fcm_delete_filter(struct schema_FCM_Filter *filter)
{
    struct fcm_filter *rule = NULL;
    ds_dlist_t *filter_head = NULL;

    if (!filter) return;
    filter_head = fcm_filter_get_list_by_name(filter->name);
    if (!filter_head)
    {
        LOGE("fcm_filter: Couldn't find the filter[%s] to delete.", filter->name);
        return;
    }

    rule = fcm_filter_find_rule(filter_head, filter->index);


    fcm_remove_rule_from_filter(filter_head, rule);

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        fcm_filter_print();
}

/**
 * fcm_update_filter: modify existing FCM filter rule
 * @filter: pass new filter details
 */
static
void fcm_update_filter(struct schema_FCM_Filter *old_rec,
                       struct schema_FCM_Filter *new_rec)
{
    struct fcm_filter *rule = NULL;
    ds_dlist_t *filter_head = NULL;

    if (!new_rec || !old_rec) return;

    filter_head = fcm_filter_get_list_by_name(new_rec->name);
    if (!filter_head)
    {
        LOGE("fcm_filter: Couldn't find the filter[%s] to update", new_rec->name);
        return;
    }

    /* find the rule based on index */
    if (old_rec->index_exists)
    {
        rule = fcm_filter_find_rule(filter_head, old_rec->index);

        if (rule) {
            fcm_remove_rule_from_filter(filter_head, rule);
        }
    }

    rule = fcm_filter_find_rule(filter_head, new_rec->index);

    if (!rule) {
        return fcm_add_filter(new_rec);
    }


    free_schema_struct(&rule->filter_rule);
    free_filter_app(&rule->app);

    if (copy_from_schema_struct(&rule->filter_rule, new_rec) < 0)
        goto free_rule;

    if (!fcm_filter_set_app_names(&rule->app, new_rec) ||
        !fcm_filter_set_app_tags(&rule->app, new_rec))
    {
        LOGE("fcm_filter: Failed to update app names/tags.");
        goto free_rule;
    }

    rule->valid = true;

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        fcm_filter_print();
    return;

free_rule:
    LOGE("fcm_filter: unable to update rule");
    free(rule);
}

void callback_FCM_Filter(ovsdb_update_monitor_t *mon,
                         struct schema_FCM_Filter *old_rec,
                         struct schema_FCM_Filter *new_rec)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        fcm_add_filter(new_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        fcm_delete_filter(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        fcm_update_filter(old_rec, new_rec);
    }
}

void callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
                           struct schema_Openflow_Tag *old_rec,
                           struct schema_Openflow_Tag *tag)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        om_tag_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        om_tag_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        om_tag_update_from_schema(tag);
    }
}

void callback_Openflow_Tag_Group(ovsdb_update_monitor_t *mon,
                                 struct schema_Openflow_Tag_Group *old_rec,
                                 struct schema_Openflow_Tag_Group *tag)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        om_tag_group_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        om_tag_group_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        om_tag_group_update_from_schema(tag);
    }
}

void fcm_filter_layer2_apply(char *filter_name, struct fcm_filter_l2_info *l2_info,
                           struct fcm_filter_stats *pkts, bool *action)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    struct fcm_filter *rule = NULL;
    bool allow = true;
    bool action_op = true;

    enum fcm_rule_op smac_allow, dmac_allow;
    enum fcm_rule_op vlanid_allow, pktcnt_allow;

    ds_dlist_t *filter_head = NULL;

    if (!l2_info)
    {
        *action = true;
        return;
    }
    filter_head = fcm_filter_get_list_by_name(filter_name);
    if (!filter_head || ds_dlist_is_empty(filter_head))
    {
        *action = true;
        return;
    }

    ds_dlist_foreach(filter_head, rule)
    {
        allow = true;
        smac_allow = fcm_smacs_filter(mgr, &rule->filter_rule, l2_info);
        allow &= (smac_allow == FCM_RULED_FALSE? false: true);

        dmac_allow = fcm_dmacs_filter(mgr, &rule->filter_rule, l2_info);
        allow &= (dmac_allow == FCM_RULED_FALSE? false: true);

        vlanid_allow = fcm_vlanid_filter(mgr, &rule->filter_rule, l2_info);
        allow &= (vlanid_allow == FCM_RULED_FALSE? false: true);

        pktcnt_allow = fcm_pkt_cnt_filter(mgr, &rule->filter_rule, pkts);
        allow &= (pktcnt_allow == FCM_RULED_FALSE? false: true);

        if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
        {
            LOGT("fcm_filter: rule index %d --> smac_allow %d dmac_allow %d"
                 "vlanid_allow %d pktcnt_allow %d"
                 " rule sucess %s ",
                 rule->filter_rule.index,
                 smac_allow,
                 dmac_allow,
                 vlanid_allow,
                 pktcnt_allow,
                 allow?"YES":"NO" );
        }
        action_op = fcm_action_filter(&rule->filter_rule);
        if (allow) goto out;
    }
    action_op = false;

out:
    allow &=action_op;
    *action = allow;
}

void fcm_filter_7tuple_apply(char *filter_name, struct fcm_filter_l2_info *l2_info,
                             struct fcm_filter_l3_info *l3_info,
                             struct fcm_filter_stats *pkts,
                             struct flow_key *fkey,
                             bool *action)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    struct fcm_filter *rule = NULL;
    bool allow = true;
    bool action_op = true;
    ds_dlist_t *filter_head = NULL;
    enum fcm_rule_op src_ip_allow, dst_ip_allow;
    enum fcm_rule_op sport_allow, dport_allow, proto_allow;

    enum fcm_rule_op smac_allow, dmac_allow;
    enum fcm_rule_op vlanid_allow, pktcnt_allow;

    enum fcm_rule_op name_allow;
    if (!l2_info && !l3_info)
    {
        *action = true;
        return;
    }

    filter_head = fcm_filter_get_list_by_name(filter_name);
    if (!filter_head || ds_dlist_is_empty(filter_head))
    {
        *action = true;
        return;
    }
    ds_dlist_foreach(filter_head, rule)
    {
        allow = true;
        if (l3_info)
        {
            src_ip_allow = fcm_src_ip_filter(mgr, &rule->filter_rule, l3_info);
            allow &= (src_ip_allow == FCM_RULED_FALSE? false: true);

            dst_ip_allow = fcm_dst_ip_filter(mgr, &rule->filter_rule, l3_info);
            allow &= (dst_ip_allow == FCM_RULED_FALSE? false: true);

            sport_allow = fcm_sport_filter(mgr, &rule->filter_rule, l3_info);
            allow &= (sport_allow == FCM_RULED_FALSE? false: true);

            dport_allow = fcm_dport_filter(mgr, &rule->filter_rule, l3_info);
            allow &= (dport_allow == FCM_RULED_FALSE? false: true);

            proto_allow = fcm_l4_proto_filter(mgr, &rule->filter_rule, l3_info);
            allow &= (proto_allow == FCM_RULED_FALSE? false: true);
            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("fcm_filter: rule index %d --> src_ip_allow %d dst_ip_allow %d "
                     "sport_allow %d dport_allow %d proto_allow %d",
                     rule->filter_rule.index,
                     src_ip_allow,
                     dst_ip_allow,
                     sport_allow,
                     dport_allow,
                     proto_allow );
            }
        }
        if (l2_info)
        {
            smac_allow = fcm_smacs_filter(mgr, &rule->filter_rule, l2_info);
            allow &= (smac_allow == FCM_RULED_FALSE? false: true);

            dmac_allow = fcm_dmacs_filter(mgr, &rule->filter_rule, l2_info);
            allow &= (dmac_allow == FCM_RULED_FALSE? false: true);

            vlanid_allow = fcm_vlanid_filter(mgr, &rule->filter_rule, l2_info);
            allow &= (vlanid_allow == FCM_RULED_FALSE? false: true);

            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("fcm_filter: rule index %d --> smac_allow %d dmac_allow %d"
                     "vlanid_allow %d ",
                     rule->filter_rule.index,
                     smac_allow,
                     dmac_allow,
                     vlanid_allow);
            }
        }
        if (fkey)
        {
            name_allow = fcm_app_name_filter(&rule->app, fkey);
            allow &= (name_allow == FCM_RULED_FALSE? false: true);

            LOGT("fcm_filter: rule index %d --> app_allow %d "
                 "rule succeeded %s ",
                 rule->filter_rule.index,
                 name_allow,
                 allow?"YES":"NO" );
        }

        /*
         * If there is no packets count available and the the rule enforces
         * packet count check, consider the situation as a failure
         */
        if (!pkts)
        {
            schema_FCM_Filter_rule_t *pkt_rule;

            pkt_rule = &rule->filter_rule;
            allow &= (pkt_rule->pktcnt_op == FCM_MATH_NONE);
        }
        else
        {
            pktcnt_allow = fcm_pkt_cnt_filter(mgr, &rule->filter_rule, pkts);
            allow &= (pktcnt_allow == FCM_RULED_FALSE? false: true);

            if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            {
                LOGT("fcm_filter: rule index %d --> pktcnt_allow %d ",
                     rule->filter_rule.index,
                     pktcnt_allow);
            }
        }
        if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
            LOGT("fcm_filter: rule success %s", allow?"YES":"NO" );

        action_op = fcm_action_filter(&rule->filter_rule);
        if (allow) goto tuple_out;
    }
    action_op = false;

tuple_out:
    allow &=action_op;
    *action = allow;
}

void fcm_filter_app_apply(char *filter_name,
                          struct flow_key *fkey,
                          bool *action)
{
    struct fcm_filter *rule = NULL;
    bool allow = true;
    bool action_op = true;

    enum fcm_rule_op name_allow;

    ds_dlist_t *filter_head = NULL;

    if (!fkey)
    {
        *action = true;
        return;
    }

    if (!fkey->num_tags)
    {
        *action = true;
        return;
    }

    filter_head = fcm_filter_get_list_by_name(filter_name);
    if (!filter_head || ds_dlist_is_empty(filter_head))
    {
        *action = true;
        return;
    }

    ds_dlist_foreach(filter_head, rule)
    {
        allow = true;
        name_allow = fcm_app_name_filter(&rule->app, fkey);
        allow &= (name_allow == FCM_RULED_FALSE? false: true);

        LOGT("fcm_filter: rule index %d --> app_allow %d "
             "rule succeeded %s ",
              rule->filter_rule.index,
              name_allow,
              allow?"YES":"NO" );
        action_op = (rule->filter_rule.action == FCM_INCLUDE);
        if (allow) goto out;
    }
    action_op = false;

out:
    allow &=action_op;
    *action = allow;
}

void  fcm_filter_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(FCM_Filter);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    OVSDB_TABLE_MONITOR(FCM_Filter, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, false);
}

int fcm_filter_init(void)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    int i;

    if (mgr->initialized) return 1;

    for (i = 0; i < FCM_MAX_FILTER_BY_NAME; i++)
    {
        ds_dlist_init(&mgr->filter_type_list[i], fcm_filter_t, dl_node);
    }

    if (mgr->ovsdb_init == NULL) mgr->ovsdb_init = fcm_filter_ovsdb_init;

    mgr->ovsdb_init();
    mgr->initialized = 1;
    return 0;
}

void fcm_filter_cleanup(void)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    struct fcm_filter *rule = NULL;
    int i = 0;


    for (i = 0; i < FCM_MAX_FILTER_BY_NAME; i++)
    {
        while (!ds_dlist_is_empty(&mgr->filter_type_list[i]))
        {
            rule = ds_dlist_head(&mgr->filter_type_list[i]);
            free_schema_struct(&rule->filter_rule);
            free_filter_app(&rule->app);
            ds_dlist_remove(&mgr->filter_type_list[i], rule);
            free(rule);
        }
    }
    mgr->initialized = 0;
}
