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
#include <const.h>
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
    struct ow_steer_candidate_list *candidate_list;
    struct osw_state_observer state_obs;
    bool syncing;
};

static bool
ow_steer_executor_action_acl_in_sync(struct ow_steer_executor_action_acl *action)
{
    const struct osw_hwaddr *sta_addr = ow_steer_executor_action_get_sta_addr(action->base);
    const struct ow_steer_candidate_list *list = action->candidate_list;
    const size_t n = list != NULL ? ow_steer_candidate_list_get_length(list) : 0;
    size_t i;
    for (i = 0; i < n; i++) {
        const struct ow_steer_candidate *c = ow_steer_candidate_list_const_get(list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(c);
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(c);
        const struct osw_state_vif_info *info = osw_state_vif_lookup_by_mac_addr(bssid);

        const bool non_local_vif = (info == NULL);
        if (non_local_vif) continue;

        const bool not_running = (info->drv_state->status != OSW_VIF_ENABLED);
        if (not_running) continue;

        const bool not_an_ap = (info->drv_state->vif_type != OSW_VIF_AP);
        if (not_an_ap) continue;

        bool valid = false;
        bool want_blocked = false;
        switch (pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                want_blocked = true;
                valid = true;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                want_blocked = false;
                valid = true;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                valid = false;
                break;
        }

        if (valid == false) continue;

        const struct osw_drv_vif_state_ap *ap = &info->drv_state->u.ap;
        bool is_blocked = false;
        switch (ap->acl_policy) {
            case OSW_ACL_NONE:
                is_blocked = false;
                break;
            case OSW_ACL_DENY_LIST:
                is_blocked = (osw_hwaddr_list_contains(ap->acl.list, ap->acl.count, sta_addr) == true);
                break;
            case OSW_ACL_ALLOW_LIST:
                is_blocked = (osw_hwaddr_list_contains(ap->acl.list, ap->acl.count, sta_addr) == false);
                break;
        }

        if (want_blocked != is_blocked) {
            return false;
        }
    }

    return true;
}

static void
ow_steer_executor_action_vif_changed_cb(struct osw_state_observer *obs,
                                        const struct osw_state_vif_info *info)
{
    struct ow_steer_executor_action_acl *acl_action = container_of(obs, struct ow_steer_executor_action_acl, state_obs);
    struct ow_steer_executor_action *action = acl_action->base;

    if (acl_action->syncing == false) return;
    if (ow_steer_executor_action_acl_in_sync(acl_action) == false) return;

    ow_steer_executor_action_sched_recall(action);
}

static void
ow_steer_executor_action_acl_set_candidates(struct ow_steer_executor_action_acl *acl_action,
                                            const struct ow_steer_candidate_list *candidate_list)
{
    ow_steer_candidate_list_free(acl_action->candidate_list);
    acl_action->candidate_list = NULL;
    if (candidate_list == NULL) return;

    acl_action->candidate_list = ow_steer_candidate_list_copy(candidate_list);
}

static bool
ow_steer_executor_action_acl_call_fn(struct ow_steer_executor_action *action,
                                     const struct ow_steer_candidate_list *candidate_list,
                                     struct osw_conf_mutator *conf_mutator)
{
    struct ow_steer_executor_action_acl *acl_action = ow_steer_executor_action_get_priv(action);

    ow_steer_executor_action_acl_set_candidates(acl_action, candidate_list);

    if (ow_steer_executor_action_acl_in_sync(acl_action)) {
        if (acl_action->syncing) {
            LOGI("%s syncing -> synced", ow_steer_executor_action_get_prefix(action));
            acl_action->syncing = false;
        }
        return true;
    }

    if (acl_action->syncing) {
        LOGD("%s syncing -> syncing", ow_steer_executor_action_get_prefix(action));
    }
    else {
        LOGI("%s synced -> syncing", ow_steer_executor_action_get_prefix(action));
    }

    acl_action->syncing = true;
    osw_conf_invalidate(conf_mutator);

    return false;
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
    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .vif_added_fn = ow_steer_executor_action_vif_changed_cb,
        .vif_changed_fn = ow_steer_executor_action_vif_changed_cb,
        .vif_removed_fn = ow_steer_executor_action_vif_changed_cb,
    };

    struct ow_steer_executor_action_acl *acl_action = CALLOC(1, sizeof(*acl_action));

    acl_action->base = ow_steer_executor_action_create("acl", sta_addr, &ops, mediator, acl_action);
    acl_action->state_obs = state_obs;
    osw_state_register_observer(&acl_action->state_obs);

    return acl_action;
}

void
ow_steer_executor_action_acl_free(struct ow_steer_executor_action_acl *acl_action)
{
    ASSERT(acl_action != NULL, "");
    osw_state_unregister_observer(&acl_action->state_obs);
    ow_steer_executor_action_acl_set_candidates(acl_action, NULL);
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
