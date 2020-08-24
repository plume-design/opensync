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

#ifndef IOTM_SESSION_PRIVATE
#define IOTM_SESSION_PRIVATE
/**
 * @file iotm_session_private.h
 *
 * @brief Wraps OVSDB session helpers
 *
 * @note a session is an instance of a Plugin. Each row of IOT_Manager_Config
 * corresponds to a session node.
 */

void iotm_free_session(struct iotm_session *session);
struct iotm_session *get_session(ds_tree_t *sessions, char *name);
struct iotm_session *iotm_alloc_session(struct schema_IOT_Manager_Config *conf);
struct iotm_event *session_event_get(struct iotm_session *session, char *ev);
void iotm_modify_session(struct schema_IOT_Manager_Config *conf);
bool iotm_session_update(struct iotm_session *session,
                   struct schema_IOT_Manager_Config *conf);
bool validate_schema_tree(void *converted, int len);
/**
 * @brief free the ovsdb configuration of the session
 *
 * Frees the iotm_session ovsdb conf settings
 * @param session the iotm session to update
 */
void iotm_free_session_conf(struct iotm_session_conf *conf);

/**
 * @brief update the ovsdb configuration of the session
 *
 * Copy ovsdb fields in their iotm_session recipient
 * @param session the iotm session to update
 * @param conf a pointer to the ovsdb provided configuration
 */
bool
iotm_session_update(
        struct iotm_session *session,
        struct schema_IOT_Manager_Config *conf);

#endif // IOTM_SESSION_PRIVATE

