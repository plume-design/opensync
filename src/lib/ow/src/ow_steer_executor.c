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

#include <stdint.h>
#include <log.h>
#include <ds_dlist.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_i.h"
#include "ow_steer_executor.h"

#define LOG_PREFIX(fmt, ...) "ow: steer: executor: sta: " fmt, ##__VA_ARGS__

struct ow_steer_executor {
    struct ds_dlist action_list;
};

struct ow_steer_executor*
ow_steer_executor_create(void)
{
    struct ow_steer_executor *executor = CALLOC(1, sizeof(*executor));
    ds_dlist_init(&executor->action_list, struct ow_steer_executor_action, node);
    return executor;
}

void
ow_steer_executor_free(struct ow_steer_executor *executor)
{
    ASSERT(executor != NULL, "");

    struct ow_steer_executor_action *action;
    struct ow_steer_executor_action *tmp;

    ds_dlist_foreach_safe(&executor->action_list, action, tmp)
        ds_dlist_remove(&executor->action_list, action);

    FREE(executor);
}

void
ow_steer_executor_add(struct ow_steer_executor *executor,
                      struct ow_steer_executor_action *action)
{
    ASSERT(executor != NULL, "");
    ASSERT(action != NULL, "");

    ds_dlist_insert_tail(&executor->action_list, action);
}

void
ow_steer_executor_remove(struct ow_steer_executor *executor,
                         struct ow_steer_executor_action *action)
{
    ASSERT(executor != NULL, "");
    ASSERT(action != NULL, "");

    ds_dlist_remove(&executor->action_list, action);
}

void
ow_steer_executor_call(struct ow_steer_executor *executor,
                       const struct osw_hwaddr *sta_addr,
                       const struct ow_steer_candidate_list *candidate_list,
                       struct osw_conf_mutator *conf_mutator)
{
    ASSERT(executor != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(candidate_list != NULL, "");
    ASSERT(conf_mutator != NULL, "");

    LOGT(LOG_PREFIX(OSW_HWADDR_FMT" call actions", OSW_HWADDR_ARG(sta_addr)));

    size_t i = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, i);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
        if (preference == OW_STEER_CANDIDATE_PREFERENCE_NONE) {
            LOGD(LOG_PREFIX(OSW_HWADDR_FMT" ignorring call, at least one candidate has preference: none, abort calling cations",
                 OSW_HWADDR_ARG(sta_addr)));
            return;
        }
    }

    struct ow_steer_executor_action *action;
    ds_dlist_foreach(&executor->action_list, action) {
        if (action->ops.call_fn != NULL) {
            const bool ready = action->ops.call_fn(action, candidate_list, conf_mutator);
            /* Expect a recall to happen when an executor
             * becomes ready for re-evaluation. This
             * prevents running subsequent executors until
             * every one becomes ready. For example ACL can
             * be enforced before BTM/deauth is done.
             */
            if (ready == false) return;
        }
    }

    /* TODO Settled notification? */
}

void
ow_steer_executor_conf_mutate(struct ow_steer_executor *executor,
                              const struct osw_hwaddr *sta_addr,
                              const struct ow_steer_candidate_list *candidate_list,
                              struct ds_tree *phy_tree)
{
    ASSERT(executor != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(candidate_list != NULL, "");
    ASSERT(phy_tree != NULL, "");

    LOGD(LOG_PREFIX(OSW_HWADDR_FMT" mutate conf", OSW_HWADDR_ARG(sta_addr)));

    size_t i = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, i);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
        if (preference == OW_STEER_CANDIDATE_PREFERENCE_NONE) {
            LOGD(LOG_PREFIX(OSW_HWADDR_FMT" ignorring mutate, at least one candidate has preference: none, aborting mutate conf",
                 OSW_HWADDR_ARG(sta_addr)));
            return;
        }
    }

    struct ow_steer_executor_action *action;
    ds_dlist_foreach(&executor->action_list, action)
        if (action->ops.conf_mutate_fn != NULL)
            action->ops.conf_mutate_fn(action, candidate_list, phy_tree);
}

#include "ow_steer_executor_ut.c"
