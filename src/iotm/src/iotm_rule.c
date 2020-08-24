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

#include "iotm_rule.h"
#include "iotm_event.h"
#include "iotm_router.h"

// private prototypes
struct iotm_rule *iotm_alloc_rule(struct schema_IOT_Rule_Config *row);
void iotm_free_rule(struct iotm_rule *rule);

// end private prototypes

void iotm_free_rule(struct iotm_rule *rule)
{
    if (rule == NULL) return;
    if (rule->name) free(rule->name);
    if (rule->event) free(rule->event);
    if (rule->filter) iotm_tree_free(rule->filter);
    if (rule->actions) iotm_tree_free(rule->actions);
    if (rule->params) iotm_tree_free(rule->params);
    if (rule) free(rule);
    return;
}

struct iotm_rule *iotm_alloc_rule(struct schema_IOT_Rule_Config *row)
{
    struct iotm_rule *rule = NULL;
    struct iotm_tree_t *filter = NULL;
    struct iotm_tree_t *actions = NULL;
    struct iotm_tree_t *params = NULL;
    struct iotm_mgr *mgr;

    mgr = iotm_get_mgr();
    if (mgr == NULL) return NULL;

    /* Get filter */
    filter = schema2iotmtree(sizeof(row->filter_keys[0]),
            sizeof(row->filter[0]),
            row->filter_len,
            row->filter_keys,
            row->filter);

    if (filter == NULL)
    {
        LOGE("%s: Error allocating filter from ovsdb rule.\n", __func__);
        goto error;
    }

    /* Get actions */
    actions = schema2iotmtree(sizeof(row->actions_keys[0]),
            sizeof(row->actions[0]),
            row->actions_len,
            row->actions_keys,
            row->actions);

    if (actions == NULL)
    {
        LOGE("%s: Error allocating actions from ovsdb rule\n", __func__);
        goto error;
    }

    /* Get params */
    if (row->params_len == 0)
    {
        params = iotm_tree_new();
    }
    else
    {
        params = schema2iotmtree(sizeof(row->params_keys[0]),
                sizeof(row->params[0]),
                row->params_len,
                row->params_keys,
                row->params);
    }

    if (params == NULL)
    {
        LOGE("%s: Error allocating params from ovsdb rule\n", __func__);
        goto error;
    }


    rule = calloc(1, sizeof(struct iotm_rule));
    if (rule == NULL) return NULL;
    rule->filter = filter;
    rule->actions = actions;
    rule->params = params;
    rule->name = strdup(row->name);
    rule->event = strdup(row->event);
    return rule;

error:
    iotm_free_rule(rule);
    return NULL;
}

void iotm_delete_rule(struct schema_IOT_Rule_Config *row)
{
    struct iotm_rule *rule;
    ds_tree_t *rules;
    ds_tree_t *events;
    struct iotm_event *event;

    events = iotm_get_events();
    // Events should be initialized
    if ( events == NULL ) return;

    event = ds_tree_find(events, row->event);
    if ( event == NULL ) return;

    rules = &event->rules;
    if ( rules == NULL ) return;

    rule = ds_tree_find(rules, row->name);
    if ( rule == NULL ) return;

    ds_tree_remove(rules, rule);
    iotm_free_rule(rule);
    event->num_rules -= 1;
    // last rule, can remove event
    if ( event->num_rules == 0 )
    {
        ds_tree_remove(events, event);
        iotm_event_free(event);
    }
    return;
}

struct routable_data_t
{
    struct iotm_rule *rule;
    struct plugin_event_t *plugin_event;
    struct iotm_tree_t *output_actions;
};


void get_routable_action_wrapper(ds_list_t *ds, struct iotm_value_t *rule_action, void *ctx)
{
    struct routable_data_t *data = (struct routable_data_t *)ctx;
    action_to_routable_command(
            data->rule,
            rule_action,
            data->plugin_event,
            data->output_actions);
}

int iotm_get_connected_routable_actions(
        struct iotm_rule *rule,
        struct iotm_tree_t *actions)
{
    if (rule == NULL) return -1;

    struct iotm_tree_t *tags = iotm_get_tags();
    if (tags == NULL) return -1;

    struct iotm_list_t *connected = iotm_tree_find(tags, CONNECT_TAG);
    if (connected == NULL) return -1;

    char *mac = iotm_tree_get_single_str(rule->filter, MAC_KEY);

    if (mac == NULL) return -1;

    if (is_in_list_str(connected, mac))
    {
        struct plugin_event_t *p_event = plugin_event_new();
        plugin_event_add_str(p_event, MAC_KEY, mac);

        struct routable_data_t data =
        {
            .rule = rule,
            .plugin_event = p_event,
            .output_actions = actions,
        };

        iotm_tree_foreach_value(
                rule->actions,
                get_routable_action_wrapper,
                &data);

        plugin_event_free(p_event);
    }

    return -1;
}

/**
 * @brief if a device is currently connected when a rule is installed, run the
 * rule
 *
 * @param rule  rule to verify connectivity and dispatch actions
 */
int iotm_route_new_rule_if_connected(struct iotm_rule *rule)
{
    struct iotm_tree_t *routable_actions = NULL;

    routable_actions = iotm_tree_new();
    if  (routable_actions == NULL) return -1;
    iotm_get_connected_routable_actions(rule, routable_actions);

    if (routable_actions->len > 0)
    {
        iotm_tree_foreach_value(routable_actions, route_actions_cb, NULL);
    }
    iotm_tree_free(routable_actions);
    return 0;
}

void iotm_add_rule(struct schema_IOT_Rule_Config *row)
{
    struct iotm_rule *rule;
    struct iotm_mgr  *mgr;
    struct iotm_event *event;
    ds_tree_t *rules;
    ds_tree_t *events;

    mgr = iotm_get_mgr();

    if ( mgr == NULL ) return;

    events = iotm_get_events();
    // Events should be initialized
    if ( events == NULL ) return;

    event = ds_tree_find(events, row->event);
    // if no event yet exists, create new entry
    if ( event == NULL )
    {
        event = iotm_event_alloc(row);
        ds_tree_insert(events, event, event->event);
    }

    rules = &event->rules;
    rule = ds_tree_find(rules, row->name);

    if ( rule != NULL ) return;

    rule = iotm_alloc_rule(row);

    if (rule == NULL) return;

    ds_tree_insert(rules, rule, rule->name);
    event->num_rules += 1;

    iotm_route_new_rule_if_connected(rule);
}

void iotm_update_rule(struct schema_IOT_Rule_Config *row) {
    iotm_delete_rule(row);
    iotm_add_rule(row);
    return;
}
