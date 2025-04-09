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
#include <module.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_diag.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_band_filter.h"

struct ow_steer_policy_band_filter_band {
    enum osw_band band;
    struct ds_tree_node node;
};

struct ow_steer_policy_band_filter {
    struct ow_steer_policy *base;
    struct ow_steer_policy_band_filter_preference included_preference;
    struct ow_steer_policy_band_filter_preference excluded_preference;
    struct ds_tree band_tree;
};

static bool
ow_steer_policy_band_filter_is_cleared(struct ow_steer_policy_band_filter *filter_policy)
{
    return (filter_policy->included_preference.override == false) &&
           (filter_policy->included_preference.value == OW_STEER_CANDIDATE_PREFERENCE_NONE) &&
           (filter_policy->excluded_preference.override == false) &&
           (filter_policy->excluded_preference.value == OW_STEER_CANDIDATE_PREFERENCE_NONE) &&
           (ds_tree_is_empty(&filter_policy->band_tree) == true);
}

static void
ow_steer_policy_band_filter_reset(struct ow_steer_policy_band_filter *filter_policy,
                                  const struct ow_steer_policy_band_filter_config *config)
{
    ASSERT(filter_policy != NULL, "");

    const bool need_reset = (ow_steer_policy_band_filter_is_cleared(filter_policy) == false) ||
                            (config != NULL);
    if (need_reset == false)
        return;

    filter_policy->included_preference.override = false;
    filter_policy->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    filter_policy->excluded_preference.override = false;
    filter_policy->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;

    struct ow_steer_policy_band_filter_band *entry;
    struct ow_steer_policy_band_filter_band *tmp_entry;
    ds_tree_foreach_safe(&filter_policy->band_tree, entry, tmp_entry) {
        ds_tree_remove(&filter_policy->band_tree, entry);
        FREE(entry);
    }

    if (config != NULL) {
        memcpy(&filter_policy->included_preference, &config->included_preference, sizeof(filter_policy->included_preference));
        memcpy(&filter_policy->excluded_preference, &config->excluded_preference, sizeof(filter_policy->excluded_preference));

        size_t i;
        for (i = 0; i < config->band_list_len; i++) {
            const enum osw_band band = config->band_list[i];
            struct ow_steer_policy_band_filter_band *entry = ds_tree_find(&filter_policy->band_tree, &band);
            if (entry != NULL)
                continue;

            entry = CALLOC(1, sizeof(*entry));
            entry->band = band;
            ds_tree_insert(&filter_policy->band_tree, entry, &entry->band);
        }
    }

    LOGI("%s config changed", ow_steer_policy_get_prefix(filter_policy->base));

    ow_steer_policy_schedule_stack_recalc(filter_policy->base);
}

static void
ow_steer_policy_band_filter_recalc_cb(struct ow_steer_policy *policy,
                                      struct ow_steer_candidate_list *candidate_list)
{
    assert(policy != NULL);
    assert(candidate_list != NULL);

    struct ow_steer_policy_band_filter *filter_policy = ow_steer_policy_get_priv(policy);

    const bool filter_is_cleared = ow_steer_policy_band_filter_is_cleared(filter_policy);
    if (filter_is_cleared == true)
        return;

    size_t i = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const struct osw_channel *channel = ow_steer_candidate_get_channel(candidate);
        const enum osw_band band = osw_freq_to_band(channel->control_freq_mhz);

        const struct ow_steer_policy_band_filter_band *entry = ds_tree_find(&filter_policy->band_tree, &band);
        const struct ow_steer_policy_band_filter_preference *preference = entry != NULL ? &filter_policy->included_preference : &filter_policy->excluded_preference;

        if (preference->override == false)
            continue;

        ow_steer_candidate_set_preference(candidate, preference->value);

        LOGD("%s bssid: "OSW_HWADDR_FMT" preference: %s", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(bssid),
             ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate)));
     }
}

static void
ow_steer_policy_band_filter_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                            struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_band_filter *filter_policy = ow_steer_policy_get_priv(policy);

    osw_diag_pipe_writef(pipe, "ow: steer:         included_preference:");
    osw_diag_pipe_writef(pipe, "ow: steer:           override: %s", filter_policy->included_preference.override == true ? "true" : "false");
    osw_diag_pipe_writef(pipe, "ow: steer:           preference: %s", ow_steer_candidate_preference_to_cstr(filter_policy->included_preference.value));
    osw_diag_pipe_writef(pipe, "ow: steer:         excluded_preference:");
    osw_diag_pipe_writef(pipe, "ow: steer:           override: %s", filter_policy->excluded_preference.override == true ? "true" : "false");
    osw_diag_pipe_writef(pipe, "ow: steer:           preference: %s", ow_steer_candidate_preference_to_cstr(filter_policy->excluded_preference.value));
    osw_diag_pipe_writef(pipe, "ow: steer:         bands:%s", ds_tree_is_empty(&filter_policy->band_tree) == true ? " (none)" : "");

    struct ow_steer_policy_band_filter_band *entry;
    ds_tree_foreach(&filter_policy->band_tree, entry)
        osw_diag_pipe_writef(pipe, "ow: steer:           band: %s", osw_band_to_str(entry->band));
}

struct ow_steer_policy_band_filter*
ow_steer_policy_band_filter_create(const char *name,
                                   const struct osw_hwaddr *sta_addr,
                                   const struct ow_steer_policy_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(name != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops ops = {
        .sigusr1_dump_fn = ow_steer_policy_band_filter_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_band_filter_recalc_cb,
    };

    struct ow_steer_policy_band_filter *filter_policy = CALLOC(1, sizeof(*filter_policy));
    filter_policy->base = ow_steer_policy_create(name, sta_addr, &ops, mediator, filter_policy);
    ds_tree_init(&filter_policy->band_tree, (ds_key_cmp_t*) ds_int_cmp, struct ow_steer_policy_band_filter_band, node);

    return filter_policy;
}

void
ow_steer_policy_band_filter_set_config(struct ow_steer_policy_band_filter *filter_policy,
                                       struct ow_steer_policy_band_filter_config *config)
{
    ASSERT(filter_policy != NULL, "");

    if (config == NULL) {
        ow_steer_policy_band_filter_reset(filter_policy, config);
        return;
    }

    if (memcmp(&filter_policy->included_preference, &config->included_preference, sizeof(filter_policy->included_preference)) != 0) {
        ow_steer_policy_band_filter_reset(filter_policy, config);
        return;
    }

    if (memcmp(&filter_policy->excluded_preference, &config->excluded_preference, sizeof(filter_policy->excluded_preference)) != 0) {
        ow_steer_policy_band_filter_reset(filter_policy, config);
        return;
    }

    if (ds_tree_len(&filter_policy->band_tree) != config->band_list_len) {
        ow_steer_policy_band_filter_reset(filter_policy, config);
        return;
    }

    size_t i;
    for (i = 0; i < config->band_list_len; i++) {
        const enum osw_band band = config->band_list[i];
        const struct ow_steer_policy_band_filter_band *entry = ds_tree_find(&filter_policy->band_tree, &band);
        if (entry == NULL) {
            ow_steer_policy_band_filter_reset(filter_policy, config);
            return;
        }
    }

    FREE(config);
}

void
ow_steer_policy_band_filter_free(struct ow_steer_policy_band_filter *filter_policy)
{
    ASSERT(filter_policy != NULL, "");

    struct ow_steer_policy_band_filter_band *entry;
    struct ow_steer_policy_band_filter_band *tmp;
    ds_tree_foreach_safe(&filter_policy->band_tree, entry, tmp) {
        ds_tree_remove(&filter_policy->band_tree, entry);
        FREE(entry);
    }

    ow_steer_policy_free(filter_policy->base);
    FREE(filter_policy);
}

struct ow_steer_policy*
ow_steer_policy_band_filter_get_base(struct ow_steer_policy_band_filter *filter_policy)
{
    ASSERT(filter_policy != NULL, "");
    return filter_policy->base;
}

#include "ow_steer_policy_band_filter_ut.c"
