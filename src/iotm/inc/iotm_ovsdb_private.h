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

#ifndef IOTM_OVSDB_PRIVATE_H_INCLUDED
#define IOTM_OVSDB_PRIVATE_H_INCLUDED

#include "iotm_ovsdb.h"
#include "iotm_session.h"

/**
 * @brief modify a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void iotm_modify_session(struct schema_IOT_Manager_Config *conf);

/**
 * @brief tell all plugins that a rule has been updated
 *
 * @param mon  ovsdb transaction type (insert, update, delete)
 * @param rule rule that changed
 */
void iotm_notify_rule_update(ovsdb_update_monitor_t *mon, struct iotm_rule *rule);

/**
 * @name OVSDBCallbacks
 * @brief OVSDB Table callback methods
 */
///@{
void callback_IOT_Rule_Config(ovsdb_update_monitor_t *mon,
        struct schema_IOT_Rule_Config *old_rec,
        struct schema_IOT_Rule_Config *conf);

void callback_IOT_Manager_Config(ovsdb_update_monitor_t *mon,
        struct schema_IOT_Manager_Config *old_rec,
        struct schema_IOT_Manager_Config *conf);

void callback_Openflow_Tag(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag *old_rec,
        struct schema_Openflow_Tag *conf);

void callback_Openflow_Tag_Group(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag_Group *old_rec,
        struct schema_Openflow_Tag_Group *conf);
///@}

/**
 * @brief delete recorded mqtt headers
 */
void iotm_rm_awlan_headers(void);

// Part of private API, don't use
#ifdef UNIT_TESTS
void iotm_get_awlan_headers(struct schema_AWLAN_Node *awlan);
#endif

#endif // IOTM_OVSDB_PRIVATE_H_INCLUDED */

