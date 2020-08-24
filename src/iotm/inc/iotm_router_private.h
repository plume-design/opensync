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

#ifndef IOTM_ROUTER_PRIVATE_H_INCLUDED
#define IOTM_ROUTER_PRIVATE_H_INCLUDED

#include "iotm.h"

/**
 * @brief context to allow foreach iterator to check for parameter matches
 */
struct cmp_t
{
    char *to; /**< value to compare with */
    bool result; /**< whether or not value matched */
};

/**
 * @brief callback to compare a value with an element from the compare struct
 *
 * @note returns true if there is one match in the list
 */
void cmp_cb(ds_list_t *ds, struct iotm_value_t *val, void *ctx);

/**
 * @brief container for actions that are to be routed
 */
struct emit_data_t
{
    struct iotm_tree_t *actions; /**< output of callbacks, this will be a set of all desired actions to route */
    size_t num_actions; /**< number of actions in set */
    struct plugin_event_t *plug; /**< event to be evaulated */
    struct iotm_rule *rule; /**< current rule to compare event with */
    bool match; /**< whether a match was found in rule and param list */
    size_t num_matches; /**< number of filter matches required to add action */
};

/**
 * @brief add an action and all related parameters to the action set
 *
 * @param ds  unused
 * @param add  key/value pair to add, key is plugin to handle command, value is
 * command 
 * @param ctx  emit_data_t, contains rule, event, and match boolean
 * and parameters from rule
 */
void add_action_cb(ds_list_t *ds, struct iotm_value_t *add, void *ctx);

/**
 * @brief given a filter element in a rule, verifies if there is a match in the
 * plugin event and if there is, add the action to the ouptut set
 *
 * @param dl   unused
 * @param val  filter key/value, i.e. key : mac, value: AA:BB:CC:DD:EE:FF
 * @param ctx  emit_data_t struct, uses plug parameter to verify event contents
 */
void add_action_cb(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx);

/**
 * @brief check if param (val) is in a plugin event
 *
 * @param dl    param list being iterated over
 * @param val   value to check
 * @param ctx   cast to struct emit_data_t * to get plugin event
 */
void find_parameter_matches(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx);

/**
 * @brief iterate over each filter in a rule, if filter matches event add
 * actions to set
 *
 * @param rule passed to foreach callback, every rule in given event
 * @param ctx contains the emit_data which has the event, rule, and an ouptut
 * set of actions
 */
void get_matched_rule_actions(struct iotm_rule *rule, void *ctx);

#endif // IOTM_ROUTER_PRIVATE_H_INCLUDED */
