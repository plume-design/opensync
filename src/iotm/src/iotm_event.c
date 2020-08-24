/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm_event.h"
#include "iotm_event_private.h"
#include "iotm_rule.h"
#include "iotm_list.h"
#include "iotm_tag.h"

static int iotm_rules_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

void build_unique_filter_tree(ds_list_t *dl, struct iotm_value_t *value, void *ctx)
{
    struct unique_t *unique = (struct unique_t *) ctx;
    if (unique == NULL
            || unique->tree == NULL
            || value == NULL
            || value->key == NULL
            || value->value == NULL)
    {
        return;
    }

    // create a unique key->value mapping to be passed to the plugin
    int in_tree = iotm_tree_set_add_str(unique->tree, value->key, value->value);

    if (!in_tree)
    {
        // send the filter to the plugin
        unique->cb(dl, value, unique->ctx);
    }
}

void foreach_unique_filter_in_event(
        struct iotm_event *self,
        void (*cb)(ds_list_t *, struct iotm_value_t *, void *),
        void *ctx)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    struct iotm_tree_t *tags = mgr->tags;

    struct iotm_tree_t *unique = iotm_tree_new();
    struct iotm_rule *rule = ds_tree_head(&self->rules);
    while(rule != NULL)
    {
        struct iotm_list_t *filters = ds_tree_head(rule->filter->items);
        while(filters != NULL)
        {
            struct iotm_value_t *filter = NULL;
            ds_list_foreach(&filters->items, filter)
            {
                // filter value similar to ${some_tag}
                if (str_has_template(filter->value))
                {
                    iotm_list_t *tag_names = iotm_list_new(filter->key);
                    tag_extract_all_vars(
                            filter->value,
                            tag_names);
                    // get actual values from tags
                    struct iotm_value_t *name = NULL;
                    ds_list_foreach(&tag_names->items, name)
                    {
                        struct iotm_list_t *tag_matches = iotm_tree_find(tags, name->value);
                        if (tag_matches != NULL)
                        {
                            struct iotm_value_t *tag = NULL;
                            ds_list_foreach(&tag_matches->items, tag)
                            {
                                // send filter value to callback
                                iotm_tree_set_add_str(unique, filter->key, tag->value);
                            }
                        }
                        else LOGD("%s: No current tags matching [%s]", __func__, name->value);
                    }
                    iotm_list_free(tag_names);
                }
                else
                {
                    iotm_tree_set_add_str(unique, filter->key, filter->value);
                }
            }
            filters = ds_tree_next(rule->filter->items, filters);
        }
        rule = ds_tree_next(&self->rules, rule);
    }
    iotm_tree_foreach_value(unique, cb, ctx);
    iotm_tree_free(unique);
}

void add_tag_values(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    struct filter_iter_hlpr_t *hlpr = (struct filter_iter_hlpr_t *) ctx;
    expand_tags(val->value, val->key, hlpr->cb, hlpr->ctx);
}

void filter_tag_expansion(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    struct filter_iter_hlpr_t *hlpr = (struct filter_iter_hlpr_t *) ctx;

    // matches a tag
    if (str_has_template(val->value))
    {
        // Create list with key of match, i.e. mac
        iotm_list_t *tags = iotm_list_new(val->key);
        if (tags == NULL) return;

        // extract tags matching ${tag} format
        tag_extract_all_vars(
                val->value,
                tags);

        // For every tag found in the action, get all values and add to items
        // passed to callback
        iotm_list_foreach(tags, add_tag_values, hlpr);
        iotm_list_free(tags);
    }
    else
    {
        hlpr->cb(dl, val, hlpr->ctx);
    }
}

void foreach_rule(struct iotm_event *self, void (*cb)(struct iotm_rule *, void *), void *context)
{
    if ( self == NULL ) return;
    struct iotm_rule *rule = ds_tree_head(&self->rules);
    while(rule != NULL)
    {
        cb(rule, context);
        rule = ds_tree_next(&self->rules, rule);
    }
}

struct iotm_event *iotm_event_alloc(struct schema_IOT_Rule_Config *row)
{
    struct iotm_event *ev;

    ev = calloc(1, sizeof(struct iotm_event));

    ds_tree_init(&ev->rules, iotm_rules_cmp,
            struct iotm_rule, iotm_rule_node);

    ev->event = strdup(row->event);
    ev->foreach_rule = foreach_rule;
    ev->foreach_filter = foreach_unique_filter_in_event;
    return ev;
}
void iotm_event_free(struct iotm_event *event)
{
    if ( event == NULL ) return;

    ds_tree_t *rules = &event->rules;
    struct iotm_rule *last = NULL;
    struct iotm_rule *rule = ds_tree_head(rules);
    while (rule != NULL)
    {
        last = rule;
        rule = ds_tree_next(rules, rule);
        iotm_free_rule(last);
    }

    free(event->event);
    free(event);
}

struct iotm_event *iotm_event_get(char *ev_key) {
    struct iotm_mgr  *mgr;
    ds_tree_t *events;

    mgr = iotm_get_mgr();
    if ( mgr == NULL ) return NULL;
    events = iotm_get_events();
    if ( events == NULL ) return NULL;
    return ds_tree_find(events, ev_key);
}
