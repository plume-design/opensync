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

#include "util.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "log.h"
#include "policy_tags.h"
#include "fcm_filter.h"
#include "ovsdb_utils.h"
#include "memutil.h"

ovsdb_table_t table_FCM_Filter;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_Openflow_Tag_Group;

static int
filter_id_cmp(void *id1, void *id2)
{
    int i = *(int *)id1;
    int j = *(int *)id2;

    return i - j;
}

static bool
fcm_check_conversion(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}

static struct ip_port*
port_to_int1d(int element_len, int element_witdh,
              char array[][element_witdh])
{
    struct ip_port *arr = NULL;
    char *pos = NULL;
    int i;

    if (array == NULL) return NULL;

    arr = CALLOC(element_len, sizeof(struct ip_port));
    if (arr == NULL) return NULL;

    for (i = 0; i < element_len; i++)
    {
        pos =  strstr(array[i], "-");
        arr[i].port_min = atoi(&array[i][0]);
        if (pos)
        {
            arr[i].port_max = atoi(pos+1);
        }

        LOGD("%s: port min=%d max=%d", __func__,
             arr[i].port_min, arr[i].port_max);
    }
    return arr;
}

static int
convert_math_str_enum(char *op_string)
{
    if (strncmp(op_string, "leq", strlen("leq")) == 0)      return FCM_MATH_LEQ;
    else if (strncmp(op_string, "lt", strlen("lt")) == 0)   return FCM_MATH_LT;
    else if (strncmp(op_string, "gt", strlen("gt")) == 0)   return FCM_MATH_GT;
    else if (strncmp(op_string, "geq", strlen("geq")) == 0) return FCM_MATH_GEQ;
    else if (strncmp(op_string, "eq", strlen("eq")) == 0)   return FCM_MATH_EQ;
    else if (strncmp(op_string, "neq", strlen("neq")) == 0) return FCM_MATH_NEQ;
    else return FCM_MATH_NONE;
}

static int
convert_str_enum(char *op_string)
{
    int ret;

    ret = strncmp(op_string, "in", strlen("in"));
    if (ret == 0) return FCM_OP_IN;

    ret = strncmp(op_string, "out", strlen("out"));
    if (ret == 0) return FCM_OP_OUT;

    return FCM_OP_NONE;
}

static int
convert_action_enum(char *op_string)
{
    if (strncmp(op_string, "include", strlen("include")) == 0) return FCM_INCLUDE;
    else if (strncmp(op_string, "exclude", strlen("exclude")) == 0) return FCM_EXCLUDE;
    return FCM_DEFAULT_INCLUDE;
}

static int
copy_from_schema_struct(struct fcm_filter_rule *rule,
                        struct schema_FCM_Filter *filter)
{
    bool check;

    if (rule == NULL) return -1;

    /* copy filter name */
    rule->name = STRDUP(filter->name);
    if (rule->name == NULL) return -1;

    /* copy index value; */
    rule->index =  filter->index;

    rule->smac_rule_present = filter->smac_op_exists;
    if (rule->smac_rule_present)
    {
        rule->smac = schema2str_set(sizeof(filter->smac[0]),
                                    filter->smac_len,
                                    filter->smac);
        check = fcm_check_conversion(rule->smac, filter->smac_len);
        if (!check) goto free_rule;
    }

    rule->dmac_rule_present = filter->dmac_op_exists;
    if (rule->dmac_rule_present)
    {
        rule->dmac = schema2str_set(sizeof(filter->dmac[0]),
                                    filter->dmac_len,
                                    filter->dmac);
        check = fcm_check_conversion(rule->dmac, filter->dmac_len);
        if (!check) goto free_rule;
    }

    rule->vlanid_rule_present = filter->vlanid_op_exists;
    if (rule->vlanid_rule_present)
    {
        rule->vlanid = schema2int_set(filter->vlanid_len, filter->vlanid);
        check = fcm_check_conversion(rule->vlanid, filter->vlanid_len);
        if (!check) goto free_rule;
    }

    rule->src_ip_rule_present = filter->src_ip_op_exists;
    if (rule->src_ip_rule_present)
    {
        rule->src_ip = schema2str_set(sizeof(filter->src_ip[0]),
                                      filter->src_ip_len,
                                      filter->src_ip);
        check = fcm_check_conversion(rule->src_ip, filter->src_ip_len);
        if (!check) goto free_rule;
    }

    rule->dst_ip_rule_present = filter->dst_ip_op_exists;
    if (rule->dst_ip_rule_present)
    {
        rule->dst_ip = schema2str_set(sizeof(filter->dst_ip[0]),
                                      filter->dst_ip_len,
                                      filter->dst_ip);
        check = fcm_check_conversion(rule->dst_ip, filter->dst_ip_len);
        if (!check) goto free_rule;
    }

    rule->src_port_rule_present = filter->src_port_op_exists;
    if (rule->src_port_rule_present)
    {
        rule->src_port = port_to_int1d(filter->src_port_len,
                                       sizeof(filter->src_port[0]),
                                       filter->src_port);
        rule->src_port_len = filter->src_port_len;
        check = fcm_check_conversion(rule->src_port, filter->src_port_len);
        if (!check) goto free_rule;
    }

    rule->dst_port_rule_present = filter->dst_port_op_exists;
    if (rule->dst_port_rule_present)
    {
        rule->dst_port = port_to_int1d(filter->dst_port_len,
                                       sizeof(filter->dst_port[0]),
                                       filter->dst_port);
        rule->dst_port_len = filter->dst_port_len;
        check = fcm_check_conversion(rule->dst_port, filter->dst_port_len);
        if (!check) goto free_rule;
    }

    rule->proto_rule_present = filter->proto_op_exists;
    if (rule->proto_rule_present)
    {
        rule->proto = schema2int_set(filter->proto_len, filter->proto);
        check = fcm_check_conversion(rule->proto, filter->proto_len);
        if (!check) goto free_rule;
    }

    rule->other_config = schema2tree(sizeof(filter->other_config_keys[0]),
                                     sizeof(filter->other_config[0]),
                                     filter->other_config_len,
                                     filter->other_config_keys,
                                     filter->other_config);
    check = fcm_check_conversion(rule->other_config, filter->other_config_len);
    if (!check) goto free_rule;

    rule->pktcnt_rule_present = filter->pktcnt_op_exists;
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

static
bool fcm_filter_set_app_names(struct fcm_filter_rule *app,
                              struct schema_FCM_Filter *rule)
{
    bool check;
    int cmp;

    app->appname_present = rule->appname_op_exists;
    if (!app->appname_present) return true;

    app->appname_op = -1;
    cmp = strcmp(rule->appname_op, "in");
    if (!cmp) app->appname_op = FCM_APPNAME_OP_IN;

    cmp = strcmp(rule->appname_op, "out");
    if (!cmp) app->appname_op = FCM_APPNAME_OP_OUT;

    if (app->appname_op == -1) return false;

    app->appnames = schema2str_set(sizeof(rule->appnames[0]),
                                   rule->appnames_len,
                                   rule->appnames);

    check = fcm_check_conversion(app->appnames, rule->appnames_len);
    return check;

}

static bool
fcm_filter_set_app_tags(struct fcm_filter_rule *app,
                        struct schema_FCM_Filter *rule)
{
    bool check;
    int cmp;

    app->app_tag_present = rule->apptag_op_exists;
    if (!app->app_tag_present) return true;

    app->app_tag_op = -1;
    cmp = strcmp(rule->apptag_op, "in");
    if (!cmp) app->app_tag_op = FCM_APPTAG_OP_IN;

    cmp = strcmp(rule->apptag_op, "out");
    if (!cmp) app->app_tag_op = FCM_APPTAG_OP_OUT;

    if (app->app_tag_op == -1) return false;

    app->app_tags = schema2str_set(sizeof(rule->apptags[0]),
                                   rule->apptags_len,
                                   rule->apptags);

    check = fcm_check_conversion(app->app_tags, rule->apptags_len);
    return check;
}

struct fcm_filter *
fcm_filter_insert_schema(struct filter_table *table,
                         struct schema_FCM_Filter *sfilter)
{
    struct fcm_filter *ffilter;
    size_t idx;
    bool rc;
    int ret;

    ffilter = CALLOC(1, sizeof(*ffilter));
    if (ffilter == NULL) return NULL;

    ffilter->filter_name = table->name;
    idx = (size_t)sfilter->index;
    ffilter->filter_rule.index = idx;

    ret = copy_from_schema_struct(&ffilter->filter_rule, sfilter);
    if (ret < 0) goto free_rule;

    rc = fcm_filter_set_app_names(&ffilter->filter_rule, sfilter);
    if (rc) rc = fcm_filter_set_app_tags(&ffilter->filter_rule, sfilter);
    if (!rc)
    {
        LOGE("%s: Failed to add app names/tags", __func__);
        goto free_rule;
    }

    ffilter->table = table;
    table->lookup_array[idx] = ffilter;
    ds_dlist_insert_tail(&table->filter_rules, ffilter);

    return ffilter;

free_rule:
    LOGE("%s: unable to add rule", __func__);
    FREE(ffilter);

    return NULL;
}

struct fcm_filter *
fcm_filter_lookup(struct schema_FCM_Filter *filter)
{
    struct fcm_filter_mgr *mgr;
    struct fcm_filter *sfilter;
    struct filter_table *table;
    ds_tree_t *tree;
    int index;

    mgr = get_filter_mgr();
    tree = &mgr->fcm_filters;
    table = ds_tree_find(tree, filter->name);

    if (table == NULL) return NULL;

    index = filter->index;
    sfilter = table->lookup_array[index];
    sfilter->table = table;

    return sfilter;
}

struct fcm_filter *
fcm_filter_get(struct schema_FCM_Filter *sfilter)
{
    struct fcm_filter_mgr *mgr;
    struct fcm_filter **ffilter;
    struct filter_table *table;
    ds_tree_t *tree;
    char *name;
    int index;

    mgr = get_filter_mgr();
    name = sfilter->name;
    tree = &mgr->fcm_filters;
    table = ds_tree_find(tree, name);

    if (table == NULL)
    {
        table = CALLOC(1, sizeof(*table));
        if (table == NULL) return NULL;

        STRSCPY(table->name, name);
        ds_tree_init(&table->filters, filter_id_cmp,
                     struct fcm_filter, filter_node);
        ds_tree_insert(tree, table, table->name);
        ds_dlist_init(&table->filter_rules, struct fcm_filter, filter_node);
    }

    index = sfilter->index;
    ffilter = &table->lookup_array[index];
    if (*ffilter != NULL) return *ffilter;
    *ffilter = fcm_filter_insert_schema(table, sfilter);

    /* Update policy clients */
    fcm_filter_update_clients(table);

    return *ffilter;
}

/**
 * fcm_add_filter: add a FCM Filter
 * @filter: the filter to add
 */
void fcm_add_filter(struct schema_FCM_Filter *filter)
{
    struct fcm_filter *ffilter;
    int idx;

    if (filter == NULL) return;

    idx = filter->index;

    if ((idx < 0) || (idx >= FCM_MAX_FILTERS))
    {
        LOGE("%s: Invalid filter index %d", __func__, filter->index);
        return;
    }

    ffilter = fcm_filter_get(filter);
    if (ffilter == NULL)
    {
        LOGE("%s: Exceeded the max[%d] number of filter names.",
             __func__, FCM_MAX_FILTER_BY_NAME);
        return;
    }
}

/**
 * fcm_delete_filter: remove rule from list that matching index
 * @filter: the filter to delete
 */
void
fcm_delete_filter(struct schema_FCM_Filter *filter)
{
    struct fcm_filter *rule;

    if (filter == NULL) return;

    rule = fcm_filter_lookup(filter);
    if (rule == NULL) return;

    fcm_free_filter(rule);
}

/**
 * fcm_update_filter: modify existing FCM filter rule
 * @filter: pass new filter details
 */
void
fcm_update_filter(struct schema_FCM_Filter *old_rec,
                  struct schema_FCM_Filter *sfilter)
{
    int index;

    index = sfilter->index;
    LOGD("%s: Updating filter index %d", __func__, index);
    if ((index < 0) || (index >= FCM_MAX_FILTER_BY_NAME))
    {
        LOGE("%s: Invalid filter index %d", __func__, sfilter->index);
        return;
    }

    /* Delete existing filter */
    fcm_delete_filter(sfilter);

    /* Add provided filter */
    fcm_add_filter(sfilter);
}

void
callback_FCM_Filter(ovsdb_update_monitor_t *mon,
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

void
callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
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

void
callback_Openflow_Tag_Group(ovsdb_update_monitor_t *mon,
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

int
table_name_compare(void *a, void *b)
{
    char *name_a = (char *)a;
    char *name_b = (char *)b;

    return strncmp(name_a, name_b, FILTER_NAME_SIZE);
}

void
fcm_filter_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(FCM_Filter);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    OVSDB_TABLE_MONITOR(FCM_Filter, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, false);
}

void
fcm_filter_ovsdb_exit(void)
{
    /* Deregister monitor events */
    ovsdb_unregister_update_cb(table_FCM_Filter.monitor.mon_id);
    ovsdb_unregister_update_cb(table_Openflow_Tag.monitor.mon_id);
    ovsdb_unregister_update_cb(table_Openflow_Tag_Group.monitor.mon_id);
}

int
fcm_filter_init(void)
{
    struct fcm_filter_mgr *mgr;

    mgr = get_filter_mgr();
    if (mgr->initialized) return 1;

    ds_tree_init(&mgr->fcm_filters, table_name_compare,
                 struct filter_table, table_node);

    fcm_filter_client_init();

    if (mgr->ovsdb_init == NULL) mgr->ovsdb_init = fcm_filter_ovsdb_init;
    if (mgr->ovsdb_exit == NULL) mgr->ovsdb_exit = fcm_filter_ovsdb_exit;

    mgr->ovsdb_init();
    mgr->initialized = 1;
    return 0;
}
