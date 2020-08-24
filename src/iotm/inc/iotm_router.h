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

#ifndef IOTM_ROUTER_H_INCLUDED
#define IOTM_ROUTER_H_INCLUDED

/**
 * @file iotm_router.h
 *
 * @brief recieve event from plugin, lookup rule matches, send commands
 */

#include "iotm.h"

/**
 * @brief iterate over each action that has been queued and send it to the
 * plugin handler that matches
 */
void route_actions_cb(ds_list_t *ds, struct iotm_value_t *val, void *ctx);

/**
 * @brief iterate over each action in a rule and create a command if it isn't
 * in the list
 *
 * @param rule         parent rule of action passed, params get added to action
 * @param rule_action  action key->value being iterated over, comes from rule
 * @param plugin_event event that  matched rule, params need to be added to
 * command
 * @param[out] output_actions tree containing all actions that need to be
 * routed to plugins and commands built to pass to them
 */
int action_to_routable_command(
        struct iotm_rule *rule,
        struct iotm_value_t *rule_action,
        struct plugin_event_t *plugin_event,
        struct iotm_tree_t *output_actions);

/**
 * @brief catch an event emitted from a plugin
 *
 * @note will see if there are any rules matching the event, any matches will
 * be routed according to rule actions
 *
 * @param session  session associated with plugin that emitted event
 * @param event    event struct containing information about the event
 */
void emit(struct iotm_session *session, struct plugin_event_t *event);

/**
 * @brief send a command to the corresponding plugin
 *
 * @param plugin  name of plugin, session name from OVSDB handler
 * @param cmd     command struct with action and parameters
 */
void route(char *plugin, struct plugin_command_t *cmd);


#endif // IOTM_ROUTER_H_INCLUDED */
