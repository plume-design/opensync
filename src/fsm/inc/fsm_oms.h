/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef FSM_OMS_H_INCLUDED
#define FSM_OMS_H_INCLUDED

#include "fsm.h"

/**
 * @brief notifies a plugin of available objects
 *
 * @param session the session to notify
 */
void
fsm_oms_notify_session(struct fsm_session *session);


/**
 * @brief return the highest version of an object
 *
 * @param session the querying fsm session
 * @param object the object name
 * @param max_version the version cap, excluded
 * @return the object with the highest version
 *
 * If @param max_version is provided, the return shall be lesser than it or NULL
 * The caller is responsible for freeing the returned object
 */
struct fsm_object *
fsm_oms_get_highest_version(struct fsm_session *session, char *name,
                            char *max_version);

/**
 * @brief return the last active version of an object
 *
 * @param session the querying fsm session
 * @param object the object name
 * @return the object with the active version
 *
 * If no last active version is saved in persistent storage return NULL
 * The caller is responsible for freeing the returned object
 */
struct fsm_object *
fsm_oms_get_last_active_version(struct fsm_session *session, char *name);

/**
 * @brief initializes the oms library
 */
void
fsm_oms_init(void);


#endif /* FSM_OMS_H_INCLUDED */
