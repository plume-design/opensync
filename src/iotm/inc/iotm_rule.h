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

#ifndef IOTM_RULE
#define IOTM_RULE

/**
 * @file iotm_rule.h
 *
 * @brief in memory representation of a OVSDB 'rule' row
 *
 * Includes mgr struct and data shared with plugins
 */

#include "iotm_tree.h"
#include "schema.h"


/**
 * @brief rule node, stored in a red black tree in an event node
 */
struct iotm_rule {
    char *name; /**< name of event from OVSDB, unique key */
    char *event; /**< event this rule is bound to, maps to event node parent */
    struct iotm_tree_t *filter;
    struct iotm_tree_t *params; /**< TODO: create params in ovsdb */
    struct iotm_tree_t *actions;
    ds_tree_node_t iotm_rule_node; /**< rule node handle */
};

/**
 * @brief if a new rule matches a currently connected device, generate an event
 * for the rule and route it
 *
 * @param rule          rule to verify if it applies
 * @param[out] actions  actions to be taken if rule passes check
 */
int iotm_get_connected_routable_actions(
        struct iotm_rule *rule,
        struct iotm_tree_t *actions);

/**
 * @brief free and remove row from ds
 *
 * @param row  OVSDB row that was removed
 */
void iotm_delete_rule(struct schema_IOT_Rule_Config *row);

/**
 * @brief add and allocate a new rule
 *
 * @param row  OVSDB row that was removed
 */
void iotm_add_rule(struct schema_IOT_Rule_Config *row);

/**
 * @brief update the values of an existing row
 *
 * @note sub optimal, just calls delete and then add
 * @param row  OVSDB row that was removed
 */
void iotm_update_rule(struct schema_IOT_Rule_Config *row);

/**
 * @brief free all elements of a row struct
 */
void iotm_free_rule(struct iotm_rule *rule);

// Part of private API, don't use
#ifdef UNIT_TESTS
struct iotm_rule *iotm_alloc_rule(struct schema_IOT_Rule_Config *row);
#endif

#endif // IOTM_RULE 
