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

#include "memutil.h"
#include "ds_tree.h"
#include "reflink.h"
#include "ovsdb_table.h"
#include "policy_tags.h"

#include "qosm_filter_internal.h"
#include "qosm_ic_template.h"
#include "qosm_ip_iface.h"

#define MAX_TAGS_PER_RULE   5

typedef enum {
    TAG_FILTER_NORMAL       = 0,
    TAG_FILTER_MATCH,
    TAG_FILTER_MISMATCH
} tag_filter_t;

typedef struct {
    char            *name;
    bool            group;
    char            *value;
} om_tdata_tv_t;

typedef struct {
    om_tdata_tv_t   tv[MAX_TAGS_PER_RULE];
    tag_filter_t    filter;
    ds_tree_t       *tag_override_values;
    char            *tag_override_name;
    bool            ignore_err;
    int             tv_cnt;
} om_tdata_t;

// Initialize a tag list
void
qosm_tag_list_init(ds_tree_t *list)
{
    ds_tree_init(list, (ds_key_cmp_t *)strcmp, om_tag_list_entry_t, dst_node);
    return;
}

// Free a template filter structure
static void
qosm_filter_free(qosm_ic_tmpl_filter_t *filter)
{
    if (filter == NULL) return;

    FREE(filter->token);
    FREE(filter->match);
    FREE(filter->action);

    om_tag_list_free(&filter->tags);
    FREE(filter);

    return;
}

// Detect vars within the rule
static bool
qosm_filter_detect_vars(const char *token,
                       const char *rule,
                       char *what,
                       char var_chr,
                       char begin,
                       char end,
                       ds_tree_t *list,
                       uint8_t base_flags)
{
    uint8_t flag;
    char *mrule = NULL;
    char *p;
    char *s;
    bool ret = true;

    // Make a copy of the rule we can modify
    mrule = strdup(rule);

    // Detect tags
    p = mrule;
    s = p;
    while ((s = strchr(s, var_chr)))
    {
        s++;
        if (*s != begin)
        {
            continue;
        }
        s++;

        flag = base_flags;
        if (*s == TEMPLATE_DEVICE_CHAR)
        {
            s++;
            flag |= OM_TLE_FLAG_DEVICE;
        }
        else if (*s == TEMPLATE_CLOUD_CHAR)
        {
            s++;
            flag |= OM_TLE_FLAG_CLOUD;
        }
        else if (*s == TEMPLATE_LOCAL_CHAR)
        {
            s++;
            flag |= OM_TLE_FLAG_LOCAL;
        }
        if (!(p = strchr(s, end)))
        {
            LOGW(
                "[%s] Template rule has malformed %s (no ending '%c')", token, what, end);
            continue;
        }
        *p++ = '\0';
        LOGT("[%s] Template rule detected %s %s'%s'",
             token,
             what,
             om_tag_get_tle_flag(flag),
             s);

        if (!om_tag_list_entry_find_by_val_flags(list, s, base_flags))
        {
            if (!om_tag_list_entry_add(list, s, flag))
            {
                ret = false;
                goto exit;
            }
        }
        s = p;
    }
exit:
    FREE(mrule);
    if (ret == false) { om_tag_list_free(list); }
    return ret;
}

// Detect tags within the rule
static bool
qosm_filter_detect_tags(const char *token, const char *rule, ds_tree_t *list)
{
    return qosm_filter_detect_vars(token,
                                  rule,
                                  "tag",
                                  TEMPLATE_VAR_CHAR,
                                  TEMPLATE_TAG_BEGIN,
                                  TEMPLATE_TAG_END,
                                  list,
                                  0);
}

// Detect groups within the rule
static bool
qosm_filter_detect_groups(const char *token, const char *rule, ds_tree_t *list)
{
    return qosm_filter_detect_vars(token,
                                  rule,
                                  "tag group",
                                  TEMPLATE_VAR_CHAR,
                                  TEMPLATE_GROUP_BEGIN,
                                  TEMPLATE_GROUP_END,
                                  list,
                                  OM_TLE_FLAG_GROUP);
}

qosm_ic_tmpl_filter_t *
qosm_ic_filter_find_by_token(char *token)
{
    struct qosm_filter *qosm_filter;

    qosm_filter = qosm_filter_get();
    return (qosm_ic_tmpl_filter_t *)ds_tree_find(&qosm_filter->qosm_ic_template_tree, token);
}

static qosm_ic_tmpl_filter_t *
qosm_filter_alloc_from_schema(struct schema_Interface_Classifier *config)
{
    qosm_ic_tmpl_filter_t *filter;
    bool success;

    TRACE();

    filter = CALLOC(1, sizeof(*filter));
    qosm_tag_list_init(&filter->tags);

    success = qosm_filter_detect_tags(config->token, config->match, &filter->tags);
    if (!success) goto alloc_error;

    success = qosm_filter_detect_groups(config->token, config->match, &filter->tags);
    if (!success) goto alloc_error;

    if (!(filter->token = strdup(config->token)))
    {
        goto alloc_error;
    }

    if (!(filter->match = strdup(config->match)))
    {
        goto alloc_error;
    }

    if (!(filter->action = strdup(config->action)))
    {
        goto alloc_error;
    }

    filter->priority = config->priority;

    return filter;

alloc_error:
    LOGE("%s(): Failed to allocate memory for template rule '%s'", __func__, config->token);

    if (filter) qosm_filter_free(filter);

    return NULL;
}

bool
qosm_ic_is_template_rule(struct schema_Interface_Classifier *config)
{
    bool istemplate = false;
    char *rule;

    if (config == NULL) return false;
    if (config->match_exists == false) return false;

    rule = config->match;

    while ((rule = strchr(rule, TEMPLATE_VAR_CHAR)))
    {
        rule++;
        if (*rule == TEMPLATE_TAG_BEGIN)
        {
            if (strchr(rule, TEMPLATE_TAG_END))
            {
                istemplate = true;
                break;
            }
        }
        else if (*rule == TEMPLATE_GROUP_BEGIN)
        {
            if (strchr(rule, TEMPLATE_GROUP_END))
            {
                istemplate = true;
                break;
            }
        }
    }

    LOGT("%s(): rule is%s a template rule: %s", __func__, istemplate ? "" : " not", rule);
    return istemplate;
}

static char *
qosm_template_tdata_get_value(om_tdata_t *tdata, char *tag_name, bool group)
{
    int i;

    for(i = 0;i < tdata->tv_cnt;i++) {
        if (!strcmp(tdata->tv[i].name, tag_name) && tdata->tv[i].group == group) {
            return tdata->tv[i].value;
        }
    }

    return NULL;
}

static size_t
qosm_template_rule_len(qosm_ic_tmpl_filter_t *filter, om_tdata_t *tdata)
{
    int         len;
    int         i;

    len = strlen(filter->match);
    for(i = 0;i < tdata->tv_cnt;i++) {
        len -= (strlen(tdata->tv[i].name) + 3 /* ${} */);
        len += strlen(tdata->tv[i].value);
    }

    // Add room for NULL termination
    return len + 1;
}

static char *
qosm_template_rule_expand(qosm_ic_tmpl_filter_t *filter, om_tdata_t *tdata)
{
    char        *mrule = NULL;
    char        *erule = NULL;
    char        *nval;
    char        *p;
    char        *s;
    char        *e;
    char        end;
    bool        group;
    int         nlen;

    // Duplicate rule we can modify
    if (!(mrule = strdup(filter->match))) {
        LOGE("[%s] Error expanding tags, memory alloc failed", filter->token);
        goto err;
    }

    // Determine new length, and allocate memory for expanded rule
    nlen = qosm_template_rule_len(filter, tdata);
    erule = CALLOC(1, nlen);

    // Copy rule, replacing tags
    p = mrule;
    s = p;
    while((s = strchr(s, TEMPLATE_VAR_CHAR))) {
        if (*(s+1) == TEMPLATE_TAG_BEGIN) {
           end = TEMPLATE_TAG_END;
           group = false;
        }
        else if (*(s+1) == TEMPLATE_GROUP_BEGIN) {
           end = TEMPLATE_GROUP_END;
           group = true;
        }
        else {
            s++;
            continue;
        }
        *s = '\0';
        strscat(erule, p, nlen);

        s += 2;
        if (*s == TEMPLATE_DEVICE_CHAR || *s == TEMPLATE_CLOUD_CHAR || *s == TEMPLATE_LOCAL_CHAR) {
            s++;
        }
        if (!(e = strchr(s, end))) {
            LOGE("[%s] Error expanding tags!", filter->token);
            goto err;
        }
        *e++ = '\0';
        p = e;

        if (!(nval = qosm_template_tdata_get_value(tdata, s, group))) {
            LOGE("[%s] Error expanding %stag '%s', not found", filter->token, group ? "group " : "", s);
            goto err;
        }
        strscat(erule, nval, nlen);

        s = p;
    }
    if (*p != '\0') {
        strscat(erule, p, nlen);
    }

    FREE(mrule);
    return erule;

err:
    if (mrule) {
        FREE(mrule);
    }
    if (erule) {
        FREE(erule);
    }

    return NULL;
}

// Fill in schema structure from template filter with expanded rule
bool
qosm_ic_tmpl_filter_to_schema(qosm_ic_tmpl_filter_t *filter, char *erule,
                    struct schema_Interface_Classifier *ic)
{
    // Zero it out first
    memset(ic, 0, sizeof(*ic));

    // Token
    STRSCPY(ic->token, filter->token);

    // Action
    STRSCPY(ic->action, filter->action);
    ic->action_exists = true;

    // Rule (from Expanded rule passed in)
    STRSCPY(ic->match, erule);
    ic->match_exists = true;

    return true;
}

static bool
qosm_template_apply(osn_tc_t *ipi_tc, om_action_t type, qosm_ic_tmpl_filter_t *filter, om_tdata_t *tdata)
{
    struct schema_Interface_Classifier ic;
    char                            *erule;
    bool                            ret = false;

    TRACE();

    if (!(erule = qosm_template_rule_expand(filter, tdata))) {
        // It reports error
        return false;
    }

    LOGD("[%s] %s expanded rule '%s'",
                filter->token,
                (type == OM_ACTION_ADD) ? "Adding" : "Removing",
                erule);

    switch(type) {

    case OM_ACTION_ADD:
        if (qosm_ic_tmpl_filter_to_schema(filter, erule, &ic)) {
            osn_tc_filter_begin(ipi_tc, filter->priority, filter->ingress, erule, filter->action);

            osn_tc_filter_end(ipi_tc);
            ret = true;
        }
        break;

    default:
        break;

    }
    FREE(erule);
    return ret;
}

static bool
qosm_template_apply_tag(osn_tc_t *ipi_tc, om_action_t type, qosm_ic_tmpl_filter_t *tcfilter,
                    om_tag_list_entry_t *ttle, ds_tree_iter_t *iter, om_tdata_t *tdata, size_t tdn)
{
    om_tag_list_entry_t *tle;
    om_tag_list_entry_t *ntle;
    om_tag_list_entry_t *ftle;
    ds_tree_iter_t      *niter;
    tag_filter_t        filter = TAG_FILTER_NORMAL;
    ds_tree_t           *tlist;
    om_tag_t            *tag;
    uint8_t             filter_flags;
    bool                ret = true;

    if (tdn == 0) {
        LOGI("[%s] %s Interface classifier rules from template %s",
             tcfilter->token, (type == OM_ACTION_ADD) ? "Adding" : "Removing",
             ttle->value);
    }

    if (tdata->tag_override_name && !strcmp(ttle->value, tdata->tag_override_name)) {
        tlist  = tdata->tag_override_values;
        filter = tdata->filter;
    }
    else {
        if (!(tag = om_tag_find_by_name(ttle->value, (ttle->flags & OM_TLE_FLAG_GROUP) ? true : false))) {
            LOGW("[%s] Template filter not applied, %stag '%s' not found",
                                                  tcfilter->token,
                                                  (ttle->flags & OM_TLE_FLAG_GROUP) ? "group " : "",
                                                  ttle->value);
            return false;
        }
        tlist = &tag->values;


    }

    if (!(ftle = om_tag_list_entry_find_by_val_flags(&tcfilter->tags, ttle->value, ttle->flags))) {
        LOGW("[%s] Template filter does not contain %stag '%s'",
                                                  tcfilter->token,
                                                  (ttle->flags & OM_TLE_FLAG_GROUP) ? "group " : "",
                                                  ttle->value);
        return false;
    }
    filter_flags = OM_TLE_VAR_FLAGS(ftle->flags);

    ntle = ds_tree_inext(iter);

    tdata->tv[tdn].name  = ttle->value;
    tdata->tv[tdn].group = (ttle->flags & OM_TLE_FLAG_GROUP) ? true : false;

    ds_tree_foreach(tlist, tle) {
        switch(filter) {

        default:
        case TAG_FILTER_NORMAL:
            if (filter_flags != 0 && (tle->flags & filter_flags) == 0) {
                continue;
            }
            break;

        case TAG_FILTER_MATCH:
            if (filter_flags == 0 || (tle->flags & filter_flags) == 0) {
                continue;
            }
            break;

        case TAG_FILTER_MISMATCH:
            if (filter_flags == 0 || (tle->flags & filter_flags) != 0) {
                continue;
            }
            break;
        }

        tdata->tv[tdn].value = tle->value;
        if (ntle) {
            if ((tdn+1) >= (sizeof(tdata->tv)/sizeof(om_tdata_tv_t))) {
                LOGE("[%s] Template filter rule not applied, too many tags", tcfilter->token);
                return false;
            }
            if (!(niter = MALLOC(sizeof(*niter)))) {
                LOGE("[%s] Template filter rule not applied, memory alloc failed", tcfilter->token);
                return false;
            }
            memcpy(niter, iter, sizeof(*niter));

            ret = qosm_template_apply_tag(ipi_tc, type, tcfilter, ntle, niter, tdata, tdn+1);
            FREE(niter);
            if (!ret) {
                break;
            }
        }
        else {
            tdata->tv_cnt = tdn+1;
            ret = qosm_template_apply(ipi_tc, type, tcfilter, tdata);
            LOGT("%s(): ret value is %d", __func__, ret);
            if (!ret) {
                if (!tdata->ignore_err) {
                    break;
                }
                ret = true;
            }
        }
    }

    return ret;
}

// Update rules based on add/remove of template
bool
qosm_ic_template_filter_update(osn_tc_t *ipi_tc, om_action_t type, qosm_ic_tmpl_filter_t *filter)
{
    om_tag_list_entry_t *tle;
    ds_tree_iter_t      iter;
    om_tdata_t          tdata;

    if (ds_tree_head(&filter->tags)) {
        memset(&tdata, 0, sizeof(tdata));
        tdata.filter     = TAG_FILTER_NORMAL;
        tdata.ignore_err = false;
        tle = ds_tree_ifirst(&iter, &filter->tags);
        if (!qosm_template_apply_tag(ipi_tc, type, filter, tle, &iter, &tdata, 0)) {
            return false;
        }
    }

    return true;
}

bool
qosm_ic_template_del_from_schema(char *token)
{
    qosm_ic_tmpl_filter_t *filter;
    struct qosm_filter *qosm_filter;
    char tbuf[256];

    filter = qosm_ic_filter_find_by_token(token);
    if (!filter) return false;

    qosm_filter = qosm_filter_get();
    ds_tree_remove(&qosm_filter->qosm_ic_template_tree, filter);

    om_tag_list_to_buf(&filter->tags, 0, tbuf, sizeof(tbuf) - 1);
    LOGN("[%s] Template filter removed (\"%s\"), tags:%s", filter->token, filter->match, tbuf);

    qosm_filter_free(filter);

    return true;
}

bool
qosm_ic_template_add_from_schema(struct schema_Interface_Classifier *config)
{
    qosm_ic_tmpl_filter_t *filter;
    struct qosm_filter *qosm_filter;
    char tbuf[256];

    LOGT("%s(): inserting template rule: %s", __func__, config->token);
    qosm_filter = qosm_filter_get();

    filter = qosm_ic_filter_find_by_token(config->token);
    /* return if the tag is already present */
    if (filter != NULL) return false;

    filter = qosm_filter_alloc_from_schema(config);
    if (filter == NULL) return false;

    ds_tree_insert(&qosm_filter->qosm_ic_template_tree, filter, filter->token);

    om_tag_list_to_buf(&filter->tags, 0, tbuf, sizeof(tbuf) - 1);
    LOGN("[%s] Template rule inserted ( \"%s\"), tags:%s",
         filter->token, filter->match, tbuf);

    return true;
}

void
qosm_ic_template_init(void)
{
    struct qosm_filter *qosm_filter;

    qosm_filter = qosm_filter_get();

    ds_tree_init(&qosm_filter->qosm_ic_template_tree, ds_str_cmp, qosm_ic_tmpl_filter_t, dst_node);
}

void
qosm_ic_template_set_parent(struct qosm_ip_iface *ipi, char *token)
{
    qosm_ic_tmpl_filter_t *filter;

    TRACE();

    /* check if ic with token is present in the token tree */
    filter = qosm_ic_filter_find_by_token(token);
    if(filter == NULL) return;

    LOGT("%s(): assigning parent pointer to %s", __func__, token);
    /* element present assign the parent pointer */
    filter->parent = ipi;
}

bool
qosm_ic_template_tag_update(om_tag_t *tag,
                        ds_tree_t *removed,
                        ds_tree_t *added,
                        ds_tree_t *updated)
{
    om_tag_list_entry_t *tle;
    struct qosm_filter *qosm_filter;
    qosm_ic_tmpl_filter_t *filter;
    ds_tree_t *tcfilters;

    TRACE();
    qosm_filter = qosm_filter_get();

    // Fetch flow tree
    tcfilters = &qosm_filter->qosm_ic_template_tree;

    // Walk template flows and find ones which reference this tag
    ds_tree_foreach(tcfilters, filter)
    {
        tle = om_tag_list_entry_find_by_value(&filter->tags, tag->name);
        if (tle)
        {
            LOGT("%s(): tag %s in use is updated", __func__, tag->name);
            qosm_ip_iface_start(filter->parent);
            continue;
        }
    }
    return true;
}
