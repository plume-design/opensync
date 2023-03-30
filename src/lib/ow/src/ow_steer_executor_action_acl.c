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

#include <log.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include <osw_mux.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_priv.h"
#include "ow_steer_executor_action_acl.h"

struct ow_steer_executor_action_acl {
    struct ow_steer_executor_action *base;
    struct ow_steer_candidate_list *prev_candidate_list;
};

static void
ow_steer_executor_action_acl_call_fn(struct ow_steer_executor_action *action,
                                     const struct ow_steer_candidate_list *candidate_list,
                                     struct osw_conf_mutator *conf_mutator)
{
    struct ow_steer_executor_action_acl *acl_action = ow_steer_executor_action_get_priv(action);
    if (acl_action->prev_candidate_list == NULL)
        goto invalidate_conf;

    /*
     * FIXME
     * This code should be improved. It should only invalidate configuration when
     * list of blocked candidates changes.
     */
    /*
     * TODO
     * It also should check candidates against vif state.
     */

    const size_t prev_candidate_list_size = ow_steer_candidate_list_get_length(acl_action->prev_candidate_list);
    const size_t candidate_list_size = ow_steer_candidate_list_get_length(candidate_list);
    if (prev_candidate_list_size != candidate_list_size)
        goto invalidate_conf;

    size_t i = 0;
    for (i = 0; i < prev_candidate_list_size; i++) {
        const struct ow_steer_candidate *prev_candidate = ow_steer_candidate_list_const_get(acl_action->prev_candidate_list, i);
        const struct osw_hwaddr *prev_candidate_bssid = ow_steer_candidate_get_bssid(prev_candidate);

        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_lookup(candidate_list, prev_candidate_bssid);
        if (candidate == NULL)
            goto invalidate_conf;

        const enum ow_steer_candidate_preference prev_candidate_pref = ow_steer_candidate_get_preference(prev_candidate);
        const enum ow_steer_candidate_preference candidate_pref = ow_steer_candidate_get_preference(candidate);
        if (prev_candidate_pref != candidate_pref)
            goto invalidate_conf;
    }

    LOGI("%s done", ow_steer_executor_action_get_prefix(action));

    return;

invalidate_conf:
    osw_conf_invalidate(conf_mutator);
    ow_steer_candidate_list_free(acl_action->prev_candidate_list);
    acl_action->prev_candidate_list = ow_steer_candidate_list_copy(candidate_list);
    ow_steer_executor_action_sched_recall(action);
    LOGI("%s invalidate conf", ow_steer_executor_action_get_prefix(action));
}

static void
ow_steer_executor_action_acl_conf_mutate_fn(struct ow_steer_executor_action *action,
                                            const struct ow_steer_candidate_list *candidate_list,
                                            struct ds_tree *phy_tree)
{
    const struct osw_hwaddr *sta_addr = ow_steer_executor_action_get_sta_addr(action);
    struct osw_conf_phy* phy;

    ds_tree_foreach(phy_tree, phy) {
        struct osw_conf_vif* vif;

        ds_tree_foreach(&phy->vif_tree, vif) {
            if (vif->vif_type != OSW_VIF_AP)
                continue;

            const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_lookup(candidate_list, &vif->mac_addr);
            if (candidate == NULL)
                continue;

            const enum ow_steer_candidate_preference candidate_pref = ow_steer_candidate_get_preference(candidate);
            switch (candidate_pref) {
                case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
                    /* skip candidate */
                    continue;
                case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                    WARN_ON(true);
                    continue;
                case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                    /* process candidate */
                    break;
            };

            struct osw_conf_acl *entry = NULL;
            const bool blocked = (candidate_pref == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED) ||
                                 (candidate_pref == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);

            switch(vif->u.ap.acl_policy) {
                case OSW_ACL_NONE:
                    if (blocked == false)
                        continue;

                    vif->u.ap.acl_policy = OSW_ACL_DENY_LIST;
                    /* fall through */
                case OSW_ACL_DENY_LIST:
                    entry = ds_tree_find(&vif->u.ap.acl_tree, sta_addr);
                    if (blocked == true) {
                        if (entry != NULL)
                            continue;

                        entry = CALLOC(1, sizeof(*entry));
                        memcpy(&entry->mac_addr, sta_addr, sizeof(entry->mac_addr));
                        ds_tree_insert(&vif->u.ap.acl_tree, entry, &entry->mac_addr);

                        LOGD("%s bssid: "OSW_HWADDR_FMT" blocked: %s acl_policy: %s, add sta", ow_steer_executor_action_get_prefix(action),
                             OSW_HWADDR_ARG(&vif->mac_addr), blocked == true ? "true" : "false", osw_acl_policy_to_str(vif->u.ap.acl_policy));
                    }
                    else {
                        if (entry == NULL)
                            continue;

                        LOGI("%s bssid: "OSW_HWADDR_FMT" blocked: %s acl_policy: %s sta is already on acl, cannot remove sta", ow_steer_executor_action_get_prefix(action),
                             OSW_HWADDR_ARG(&vif->mac_addr), blocked == true ? "true" : "false", osw_acl_policy_to_str(vif->u.ap.acl_policy));
                    }
                    break;
                case OSW_ACL_ALLOW_LIST:
                    entry = ds_tree_find(&vif->u.ap.acl_tree, sta_addr);
                    if (blocked == true) {
                        if (entry == NULL)
                            continue;

                        ds_tree_remove(&vif->u.ap.acl_tree, entry);

                        LOGD("%s bssid: "OSW_HWADDR_FMT" blocked: %s acl_policy: %s, remove sta", ow_steer_executor_action_get_prefix(action),
                             OSW_HWADDR_ARG(&vif->mac_addr), blocked == true ? "true" : "false", osw_acl_policy_to_str(vif->u.ap.acl_policy));

                        FREE(entry);
                    }
                    else {
                        if (entry != NULL)
                            continue;

                        LOGI("%s bssid: "OSW_HWADDR_FMT" blocked: %s acl_policy: %s sta is not on acl, cannot add sta", ow_steer_executor_action_get_prefix(action),
                             OSW_HWADDR_ARG(&vif->mac_addr), blocked == true ? "true" : "false", osw_acl_policy_to_str(vif->u.ap.acl_policy));
                    }
                    break;
            }
        }
    }
}

struct ow_steer_executor_action_acl*
ow_steer_executor_action_acl_create(const struct osw_hwaddr *sta_addr,
                                    const struct ow_steer_executor_action_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_executor_action_ops ops = {
        .call_fn = ow_steer_executor_action_acl_call_fn,
        .conf_mutate_fn = ow_steer_executor_action_acl_conf_mutate_fn,
    };
    struct ow_steer_executor_action_acl *acl_action = CALLOC(1, sizeof(*acl_action));

    acl_action->base = ow_steer_executor_action_create("acl", sta_addr, &ops, mediator, acl_action);

    return acl_action;
}

void
ow_steer_executor_action_acl_free(struct ow_steer_executor_action_acl *acl_action)
{
    ASSERT(acl_action != NULL, "");
    ow_steer_candidate_list_free(acl_action->prev_candidate_list);
    ow_steer_executor_action_free(acl_action->base);
    FREE(acl_action);
}

struct ow_steer_executor_action*
ow_steer_executor_action_acl_get_base(struct ow_steer_executor_action_acl *acl_action)
{
    ASSERT(acl_action != NULL, "");
    return acl_action->base;
}

#include "ow_steer_executor_action_acl_ut.c"
