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

#ifndef IOTM_OVSDB_H_INCLUDED
#define IOTM_OVSDB_H_INCLUDED

#include "iotm_session.h"

/**
 * @file iotm_ovsdb.h
 *
 * @brief bind to all OVSDB tables
 */

/**
 * @brief send a report to the cloud
 */
void iotm_send_report(struct iotm_session *session, char *report);

/**
 * @brief initialize all components related to OVSDB
 */
int iotm_ovsdb_init(void);

/**
 * @brief send a protobuf report over mqtt
 *
 * Emits a protobuf report. Does not free the protobuf.
 * @param session the iotm session emitting the report
 * @param report the report to emit
 */
void iotm_send_pb_report(
        struct iotm_session *session,
        char *topic,
        void *pb_report, size_t pb_len);

/**
 * @brief perform an upsert on an OVSDB tag row
 * @note this updates the current tag values with the ones in the new row, this
 * utility allows for a plugin to curate a tag device list
 *
 * @param tag  matches 'name' field of row, something like whitelisted_macs
 * @param row  Openflow Tag row to update
 */
int ovsdb_upsert_tag(
        char *tag,
        struct schema_Openflow_Tag *row);

/**
 * @brief allows the plugin to install rules in order to route events
 * 
 * @param name   name for the rule, should be unique
 * @param row    rule to upsert
 */
int ovsdb_upsert_rules(
        struct schema_IOT_Rule_Config rows[],
        size_t num_rules);
/**
 * @brief allow a plugin to delete a rule matching their name.
 *
 * @note this allows the plugin to 
 */
int ovsdb_remove_rules(
        struct schema_IOT_Rule_Config rows[],
        size_t num_rules);

#endif // IOTM_OVSDB_H_INCLUDED */
