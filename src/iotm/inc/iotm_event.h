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

#ifndef IOTM_EVENT_H_INCLUDED
#define IOTM_EVENT_H_INCLUDED

/**
 * @file iotm_event.h
 *
 * @brief encapsulates groupings of rules as events
 *
 * An event is a container for all rules that match, it allows for interaction
 * with all rules connected to a given trigger, such as 'connected'
 */

#include "iotm.h"
#include "iotm_rule.h"
#include "schema.h"

/**
 * @brief event rb tree node, stored in mgr
 */
struct iotm_event
{
    char *event; /**< name of the event */
    ds_tree_t rules; /**< rules associated with event */
    size_t    num_rules; /**< number of rules installed for this event */
    void (*foreach_rule)(struct iotm_event *self, void (*cb)(struct iotm_rule *, void *), void *); /**< iterate over each rule, cb may use context */
    void (*foreach_filter)(struct iotm_event *self, void (*cb)(ds_list_t *, struct iotm_value_t *, void *), void *); /**< allows a plugin to view all filters for an event */
    ds_tree_node_t iotm_event_node; /**< node to track events */
};

/**
 * @brief allow iteration over each rule in a given event
 */
void foreach_rule(
        struct iotm_event *self,
        void (*cb)(struct iotm_rule *, void *),
        void *context);

/**
 * @brief allocate a new event
 *
 * @param row OVSDB row to initialize values
 *
 * @return event pointer
 */
struct iotm_event *iotm_event_alloc(struct schema_IOT_Rule_Config *row);

/**
 * @brief free an iotm event
 *
 * @param event pointer to event
 */
void iotm_event_free(struct iotm_event *event);

/**
 * @brief lookup an event in tree of events tracked my manager
 *
 * @param key for event, i.e. 'ble_advertised'
 *
 * @return event pointer to event
 */
struct iotm_event *iotm_event_get(char *ev_key);

/**
 * @brief structure to assist filter_tag_expansion
 */
struct filter_iter_hlpr_t {
    void(*cb)(ds_list_t *, struct iotm_value_t *, void *); /**< method to be called for every match */
    void *ctx; /**< context to pass through expansion */
} filter_iter_hlpr_t;

/**
 * @brief expand a filter node into it's matching tag values
 *
 * @note val : [ mac : ${whitelisted} ] -> [ mac : ['firstmac', 'secondmac'] ]
 * @note passed as cb to a iotm_list_foreach or iotm_tree_foreach_value
 *
 * @param  dl  list currently being iterated over
 * @param  val containing a filter to expand
 * @param  ctx ref to struct filter_iter_hlpr_t
 */
void filter_tag_expansion(ds_list_t *dl, struct iotm_value_t *val, void *ctx);

/**
 * @brief iterate over ever unique key in an event's filter
 *
 * @note if a mac appears multiple time in rules, it will only be passed once
 *
 * @param event event to iterate over all filter values of
 * @param cb    callback to be called for each match
 * @param ctx   context passthrough for callback
 *
 */
void foreach_unique_filter_in_event(
				struct iotm_event *self,
				void (*cb)(ds_list_t *, struct iotm_value_t *, void *),
				void *ctx);

/**
 * @brief callback to update the key for all nodes in a list
 */
void change_passed_key(ds_list_t *dl, struct iotm_value_t *value, void *ctx);

#endif // IOTM_EVENT_H_INCLUDED */
