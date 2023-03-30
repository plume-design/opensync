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

#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <util.h>
#include <module.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_btm.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <const.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_btm_response.h"

#define OW_STEER_POLICY_RESPONSE_RELEVANCE_SEC 10

static const char *g_policy_name = "btm_response";

struct ow_steer_policy_btm_retry_neigh {
    struct osw_hwaddr bssid;
    int preference;
    struct ds_dlist_node node;
};

struct ow_steer_policy_btm_response {
    struct ow_steer_policy *base;
    struct osw_btm_response_observer btm_resp_obs;
    struct ds_dlist neigh_list;
    uint64_t neigh_timestamp;
};

struct ow_steer_policy*
ow_steer_policy_btm_response_get_base(struct ow_steer_policy_btm_response *btm_response_policy)
{
    ASSERT(btm_response_policy != NULL, "");
    return btm_response_policy->base;
}

void
ow_steer_policy_btm_response_free(struct ow_steer_policy_btm_response *btm_response_policy)
{
    if (btm_response_policy == NULL)
        return;

    struct ow_steer_policy_btm_retry_neigh *neigh;
    while ((neigh = ds_dlist_remove_head(&btm_response_policy->neigh_list)) != NULL) {
        FREE(neigh);
    }

    ow_steer_policy_free(btm_response_policy->base);
    FREE(btm_response_policy);
}

static bool
ow_steer_policy_btm_response_is_bssid_present(const struct ow_steer_policy_btm_response *btm_response_policy,
                                              const struct osw_hwaddr *bssid)
{
    ASSERT(btm_response_policy != NULL, "");
    ASSERT(bssid != NULL, "");

    struct ow_steer_policy_btm_response *brp = (struct ow_steer_policy_btm_response *)btm_response_policy;
    struct ow_steer_policy_btm_retry_neigh *neigh;
    ds_dlist_foreach(&brp->neigh_list, neigh) {
        const bool bssid_equal = (osw_hwaddr_cmp(bssid, &neigh->bssid) == 0);
        if (bssid_equal == true) return true;
    }
    return false;
}

static void
ow_steer_policy_btm_response_recalc_cb(struct ow_steer_policy *policy,
                                       struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_policy_btm_response *btm_response_policy = ow_steer_policy_get_priv(policy);

    /* proceed only if btm response is fresh */
    const uint64_t response_t_delta = osw_time_mono_clk() - btm_response_policy->neigh_timestamp;
    const int response_age = OSW_TIME_SEC(response_t_delta);
    if (response_age > OW_STEER_POLICY_RESPONSE_RELEVANCE_SEC) {
        LOGT("%s btm response too old - skipping",
                ow_steer_policy_get_prefix(btm_response_policy->base));
        return;
    }

    /* mask all neighbors that are _not_ present in btm response */
    size_t i = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const bool is_cand_in_resp_neigh_list = ow_steer_policy_btm_response_is_bssid_present(btm_response_policy,
                                                                                              bssid);
        if (is_cand_in_resp_neigh_list == false) {
            ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
        }

        LOGD("%s bssid: "OSW_HWADDR_FMT
             " preference: %s",
             ow_steer_policy_get_prefix(policy),
             OSW_HWADDR_ARG(bssid),
             ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate)));
    }
}

static void
ow_steer_policy_btm_response_sigusr1_dump_cb(struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    LOGI("ow: steer:         response neighbors:");
    struct ow_steer_policy_btm_response *btm_response_policy = ow_steer_policy_get_priv(policy);
    struct ow_steer_policy_btm_retry_neigh *neigh;
    ds_dlist_foreach(&btm_response_policy->neigh_list, neigh) {
        LOGI("ow: steer:           bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&neigh->bssid));
        LOGI("ow: steer:             preference: %d", neigh->preference);
    }
}

static void
ow_steer_policy_btm_response_cb(struct osw_btm_response_observer *observer,
                                const int response_code,
                                const struct osw_btm_retry_neigh_list *retry_neigh_list)
{
    struct ow_steer_policy_btm_response *btm_response_policy = container_of(observer,
                                                                            struct ow_steer_policy_btm_response,
                                                                            btm_resp_obs);

    LOGD("%s backoff period stopped", ow_steer_policy_get_prefix(btm_response_policy->base));

    unsigned int i;
    for  (i = 0; i < retry_neigh_list->neigh_len; i++) {
        const struct osw_btm_retry_neigh *retry_neigh = &retry_neigh_list->neigh[i];
        struct ow_steer_policy_btm_retry_neigh *new_retry_neigh = CALLOC(1, sizeof(*new_retry_neigh));
        new_retry_neigh->preference = retry_neigh->preference;
        memcpy(&new_retry_neigh->bssid,
               &retry_neigh->neigh.bssid,
               sizeof(new_retry_neigh->bssid));

        struct ow_steer_policy_btm_response *brp = (struct ow_steer_policy_btm_response *)btm_response_policy;
        ds_dlist_insert_tail(&brp->neigh_list, new_retry_neigh);
    }

    btm_response_policy->neigh_timestamp = osw_time_mono_clk();
}

struct ow_steer_policy_btm_response*
ow_steer_policy_btm_response_create(unsigned int priority,
                                    const char *name,
                                    const struct osw_hwaddr *sta_addr,
                                    const struct ow_steer_policy_mediator *mediator)
{
    ASSERT(name != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops policy_ops = {
        .sigusr1_dump_fn = ow_steer_policy_btm_response_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_btm_response_recalc_cb,
    };

    struct ow_steer_policy_btm_response *btm_response_policy = CALLOC(1, sizeof(*btm_response_policy));
    btm_response_policy->base = ow_steer_policy_create(strfmta("%s_%s", g_policy_name, name),
                                                       priority,
                                                       sta_addr,
                                                       &policy_ops,
                                                       mediator,
                                                       btm_response_policy);

    struct osw_btm_response_observer *btm_resp_obs = &btm_response_policy->btm_resp_obs;
    memcpy(&btm_resp_obs->sta_addr, sta_addr, sizeof(btm_resp_obs->sta_addr));
    btm_resp_obs->btm_response_fn = ow_steer_policy_btm_response_cb;
    osw_btm_register_btm_response_observer(btm_resp_obs);

    ds_dlist_init(&btm_response_policy->neigh_list, struct ow_steer_policy_btm_retry_neigh, node);

    return btm_response_policy;
}

#include "ow_steer_policy_btm_response_ut.c"
