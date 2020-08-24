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

#include "iotm_router.h"
#include "iotm_router_private.h"
#include "iotm_event.h"
#include "iotm_session.h"

bool compare(char *first, char *second)
{
    if (strcmp(first, second) == 0) return true;
    if (strcmp(first, "*") == 0) return true;
    return false;
}

void cmp_cb(ds_list_t *ds, struct iotm_value_t *val, void *ctx)
{
    struct cmp_t *cmp = (struct cmp_t *) ctx;
    if (compare(cmp->to, val->value))
    {
        cmp->result = true;
    }
}

/**
 * @brief contstruct a plugin command
 */
struct action_container_t {
    struct iotm_tree_t *actions; /**< list to store actions that are loaded */
    struct iotm_tree_t *params; /**< params pulled from event */
    struct iotm_rule *rule; /**< rule that matched filter */
};

void free_other_cmd(void *ctx)
{
    struct plugin_command_t *cmd = (struct plugin_command_t *) ctx;
    plugin_command_free(cmd);
}

int action_to_routable_command(
        struct iotm_rule *rule,
        struct iotm_value_t *rule_action,
        struct plugin_event_t *plugin_event,
        struct iotm_tree_t *output_actions)
{
    struct iotm_value_t *iotm_val = (struct iotm_value_t *)calloc(1, sizeof(struct iotm_value_t));
    iotm_val->key = strdup(rule_action->key);
    iotm_val->value = strdup(rule_action->value);
    iotm_val->free_other = free_other_cmd;

    // generate the command to send to the TL
    struct plugin_command_t *cmd = plugin_command_new();

    // add any parameters from the plugin event
    if (plugin_event->params->len > 0)
    {
        iotm_tree_concat_str(cmd->params, plugin_event->params);
    }

    // add any parameters from the rule
    if (rule->params != NULL
            && rule->params->len > 0)
    {
        iotm_tree_concat_str(cmd->params, rule->params);
    }

    cmd->action = strdup(rule_action->value);
    iotm_val->other = cmd;
    iotm_tree_set_add(output_actions, iotm_val->key, iotm_val);

    return 0;
}

void add_action_cb(ds_list_t *ds, struct iotm_value_t *add, void *ctx)
{

    struct emit_data_t *data = (struct emit_data_t *)ctx;

    if (data == NULL
            || data->rule == NULL
            || data->plug == NULL
            || data->plug->params == NULL
            || data->actions == NULL)
    {
        LOGE("%s: Context not valid. Returning.\n", __func__);
        return;
    }

    action_to_routable_command(
            data->rule,
            add,
            data->plug,
            data->actions);
}

void debug_printer(ds_list_t *ds, struct iotm_value_t *add, void *ctx)
{
    LOGD("%s: [%s]->[%s]\n", __func__, add->key, add->value);
}

void find_parameter_matches(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx)
{

    struct emit_data_t *data = (struct emit_data_t *)ctx;
    struct plugin_event_t *plug = data->plug;

    // see if current filter is in parameter list
    struct iotm_list_t *params = plugin_event_find(plug, val->key);

    // filter not found as parameter in plugin event
    if ( params == NULL )
    {
        LOGD("Parameter [%s] not found in event!\n", val->key);
        data->match = false;
        return;
    }

    // filter not found as parameter in plugin event
    if ( params == NULL ) return;

    struct cmp_t compare = 
    {
        .to = val->value,
        .result = data->match, // persist any matches
    };

    if (params->len <= 0)
    {
        LOGD("%s: No parameters to validate filter against. No match.\n",
                __func__);
        data->match = false;
        return;
    }

    // check if filter matches a parameter in the event
    params->foreach(params, cmp_cb, &compare);
    data->match = compare.result;
    return;
}

/**
 * @brief iterate over every filter and check for match in params
 */
void verify_filter(ds_tree_t *dl, struct iotm_list_t *list, void *ctx)
{
    struct emit_data_t *data = (struct emit_data_t *)ctx;

    if (!data->match)
    {
        LOGD("%s: Match was not toggled back to true while iterating over list. Not continuing.\n",
                __func__);
        return;
    }

    struct filter_iter_hlpr_t hlpr =
    {
        .cb = find_parameter_matches,
        .ctx = ctx,
    };

    data->match = false; // will get set to true if any match in the list is found
    iotm_list_foreach(list, filter_tag_expansion, &hlpr);
}

void get_matched_rule_actions(struct iotm_rule *rule, void *ctx)
{
    struct emit_data_t *data = (struct emit_data_t *)ctx;
    struct iotm_tree_t *filter = rule->filter;
    data->rule = rule;

    if ( filter == NULL ) return;

    // If filter conditions are matched, add the action
    if (rule->filter->len > 0)
    {
        // see if each tree has a matching item in the list
        data->match = true; // start as true
        iotm_tree_foreach(rule->filter, verify_filter, ctx);

        if (data->match)
        {
            LOGD("%s: Match for rule found, filter: \n", __func__);
            iotm_tree_foreach_value(rule->filter, debug_printer, NULL);
            LOGD("----------------------------------\n");

            LOGD("%s: Params are : \n", __func__);
            iotm_tree_foreach_value(data->plug->params, debug_printer, NULL);
            LOGD("----------------------------------\n");

            iotm_tree_foreach_value(data->rule->actions, add_action_cb, data);
        }
    }
}

void route_actions_cb(ds_list_t *ds, struct iotm_value_t *val, void *ctx)
{
    struct plugin_command_t *cmd = (struct plugin_command_t *) val->other;

    route(val->key, cmd);
}

/**
 * @brief handle event built by a plugin
 *
 * @note this will find if there are any matching rules and then route the rule
 * to the appropriate plugin
 */
void emit(struct iotm_session *session, struct plugin_event_t *event)
{
    if ( session == NULL ) return;
    if ( event == NULL ) return;
    if ( session->ops.get_event == NULL ) return;
    LOGD("%s: recieved a [%s] event from a plugin.\n", __func__, event->name);

    // get all rules matching the event
    struct iotm_event *ev_rules = session->ops.get_event(session, event->name);

    if ( ev_rules == NULL )
    {
        LOGD("%s: no rules matching interested event. exiting emit method.",
                __func__);
        return;
    }

    struct emit_data_t e_data;
    memset(&e_data, 0, sizeof(e_data));

    e_data.plug = event;
    e_data.actions = iotm_tree_new();
    e_data.num_actions = 0;

    ev_rules->foreach_rule(ev_rules, get_matched_rule_actions, &e_data);

    if (e_data.actions->len <= 0)
    {
        LOGD("%s: No actions to route. exiting.\n",
                __func__);
        return;
    }

    LOGI("%s: Routing [%lu] actions.\n",
            __func__, e_data.actions->len);

    iotm_tree_foreach_value(e_data.actions, route_actions_cb, NULL);

    iotm_tree_free(e_data.actions);

    LOGD("%s: completed all routing, exiting emit method.\n", __func__);
    return;
}

/**
 * @brief handle event built by a plugin
 */
void route(char *plugin, struct plugin_command_t *cmd)
{

    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = get_session(sessions, plugin);

    if (session == NULL)
    {
        LOGE("%s: Could not find session matching plugin [%s]", __func__, plugin);
        return;
    }

    session->ops.handle(session, cmd);
    LOGD("%s: Sent command [%s] to plugin [%s]",
            __func__, cmd->action, plugin);
    return;
}
