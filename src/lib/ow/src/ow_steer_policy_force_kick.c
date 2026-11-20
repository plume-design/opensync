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
#include <const.h>
#include <util.h>
#include <log.h>
#include <os.h>
#include <module.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_sta_assoc.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_diag.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_force_kick.h"

#define OW_STEER_POLICY_FORCE_KICK_ENFORCE_PERIOD_SEC 20

#define LOG_PREFIX(fmt, ...) "ow: steer: " fmt, ##__VA_ARGS__
#define LOG_WITH_PREFIX(prefix, fmt, ...) \
    LOG_PREFIX(                           \
        "%s " fmt,                        \
        prefix,                           \
        ##__VA_ARGS__)

#define LOG_WITH_POLICY_PREFIX(policy, fmt, ...) \
    LOG_WITH_PREFIX(                             \
        ow_steer_policy_get_prefix(policy),      \
        fmt,                                     \
        ##__VA_ARGS__)

struct ow_steer_policy_force_kick {
    struct ow_steer_policy *base;
    struct ow_steer_policy_force_kick_config *next_config;
    struct ow_steer_policy_force_kick_config *config;
    struct osw_timer reconf_timer;
    struct osw_timer enforce_timer;
    osw_sta_assoc_observer_t *assoc_obs;
    osw_sta_assoc_links_t assoc_links;
};

static const char *g_policy_name = "force_kick";

static void
ow_steer_policy_force_kick_reset(struct ow_steer_policy_force_kick *force_policy)
{
    ASSERT(force_policy != NULL, "");

    const bool enforce_period = osw_timer_is_armed(&force_policy->enforce_timer);

    if (enforce_period == true)
        LOGI(LOG_WITH_POLICY_PREFIX(force_policy->base, "enforce period stopped"));

    osw_timer_disarm(&force_policy->enforce_timer);
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, NULL);

    if (enforce_period == true)
        ow_steer_policy_dismiss_executor(force_policy->base);
}

static void
ow_steer_policy_force_kick_recalc_cb(struct ow_steer_policy *policy,
                                     struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = ow_steer_policy_get_priv(policy);

    const struct ow_steer_policy_force_kick_config *config = force_policy->config;
    if (config == NULL)
        return;

    const bool enforce_period = osw_timer_is_armed(&force_policy->enforce_timer);
    if (enforce_period == false)
        return;

    const size_t n_links = force_policy->assoc_links.count;
    const size_t n_candidates = ow_steer_candidate_list_get_length(candidate_list);
    size_t n_candidates_associated = 0;
    size_t n_candidates_associated_blocked = 0;
    size_t n_candidates_associated_blockable = 0;
    size_t n_candidates_unassociated_undecided = 0;
    size_t n_candidates_unassociated_available = 0;
    size_t i;

    for (i = 0; i < n_candidates; i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const bool associated = osw_sta_assoc_links_lookup(&force_policy->assoc_links, bssid, NULL);
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(candidate);

        n_candidates_associated += associated ? 1 : 0;

        switch (pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                n_candidates_associated_blocked += associated ? 1 : 0;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
                if (associated) {
                    LOGW(LOG_WITH_POLICY_PREFIX(policy, "candidate "OSW_HWADDR_FMT" is out of scope, associated: %d"
                         "; suspected high-level misconfiguration",
                         OSW_HWADDR_ARG(bssid), associated));
                }
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                if (associated) {
                    LOGW(LOG_WITH_POLICY_PREFIX(policy, "candidate "OSW_HWADDR_FMT" is soft-blocked, associated: %d"
                         "; suspected high-level misconfiguration",
                         OSW_HWADDR_ARG(bssid), associated));
                }
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                n_candidates_associated_blockable += associated ? 1 : 0;
                n_candidates_unassociated_undecided += associated ? 0 : 1;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                n_candidates_unassociated_available += associated ? 0 : 1;
                break;
        }
    }

    const bool blocking_permissible =
        (n_links == n_candidates_associated) &&
        (n_candidates_associated == (n_candidates_associated_blockable + n_candidates_associated_blocked)) &&
        ((n_candidates_unassociated_undecided + n_candidates_unassociated_available) > 0);

    if (blocking_permissible == false) {
        LOGI(LOG_WITH_POLICY_PREFIX(policy, "aborted force kick, blocking not permissible"
             ": n_links: %zu"
             ", n_candidates: %zu"
             ", n_candidates_associated: %zu"
             ", n_candidates_associated_blockable: %zu"
             ", n_candidates_associated_blocked: %zu"
             ", n_candidates_unassociated_undecided: %zu"
             ", n_candidates_unassociated_available: %zu",
             n_links,
             n_candidates,
             n_candidates_associated,
             n_candidates_associated_blockable,
             n_candidates_associated_blocked,
             n_candidates_unassociated_undecided,
             n_candidates_unassociated_available));
        ow_steer_policy_force_kick_reset(force_policy);
        return;
    }

    for (i = 0; i < n_links; i++) {
        const osw_sta_assoc_link_t *link = &force_policy->assoc_links.links[i];
        const struct osw_hwaddr *bssid = &link->local_sta_addr;
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_lookup(candidate_list, bssid);
        if (WARN_ON(candidate == NULL)) continue;

        /* This early `continue` avoids unnecessary sanity
         * warning in ow_steer_candidate_set_preference() */
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(candidate);
        const bool hard_blocked_already = (pref == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
        if (hard_blocked_already == true) continue;

        const char *reason = ow_steer_policy_get_name(policy);
        ow_steer_candidate_set_preference(candidate, reason, OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    }
}

static void
ow_steer_policy_force_kick_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                           struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_force_kick_config *config = force_policy->config;

    osw_diag_pipe_writef(pipe, LOG_PREFIX("        config: %s", config != NULL ? "" : "(nil)"));

    const uint64_t now_nsec = osw_time_mono_clk();
    const char *enforce_timer_buf = osw_timer_is_armed(&force_policy->enforce_timer) == true?
        strfmta("%.2lf sec remaining", OSW_TIME_TO_DBL(osw_timer_get_remaining_nsec(&force_policy->enforce_timer, now_nsec))) : "inactive";

    osw_diag_pipe_writef(pipe, LOG_PREFIX("        enforce_timer: %s", enforce_timer_buf));
}

static void
ow_steer_policy_force_kick_set_assoc_links(struct ow_steer_policy_force_kick *force_policy,
                                           const osw_sta_assoc_links_t *assoc_links,
                                           osw_sta_assoc_event_e ev)
{
    MEMZERO(force_policy->assoc_links);
    if (assoc_links != NULL) {
        force_policy->assoc_links = *assoc_links;
    }

    switch (ev) {
        case OSW_STA_ASSOC_UNDEFINED:
            break;
        case OSW_STA_ASSOC_CONNECTED:
            break;
        case OSW_STA_ASSOC_RECONNECTED:
            if (force_policy->config != NULL) {
                ow_steer_policy_force_kick_reset(force_policy);
            }
            break;
        case OSW_STA_ASSOC_DISCONNECTED:
            if (force_policy->config != NULL) {
                ow_steer_policy_force_kick_reset(force_policy);
            }
            break;
    }
}

static void
ow_steer_policy_force_kick_assoc_changed_cb(void *priv,
                                            const osw_sta_assoc_entry_t *entry,
                                            osw_sta_assoc_event_e ev)
{
    struct ow_steer_policy_force_kick *force_policy = priv;
    const osw_sta_assoc_links_t *active_links = osw_sta_assoc_entry_get_active_links(entry);
    ow_steer_policy_force_kick_set_assoc_links(force_policy, active_links, ev);
}

static osw_sta_assoc_observer_t *
ow_steer_policy_force_kick_assoc_observer(struct ow_steer_policy_force_kick *force_policy,
                                          const struct osw_hwaddr *sta_addr)
{
    osw_sta_assoc_t *m_sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    struct osw_sta_assoc_observer_params *params = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_addr(params, sta_addr);
    osw_sta_assoc_observer_params_set_changed_fn(params, ow_steer_policy_force_kick_assoc_changed_cb, force_policy);
    return osw_sta_assoc_observer_alloc(m_sta_assoc, params);
}

static void
ow_steer_policy_force_kick_reconf_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = container_of(timer, struct ow_steer_policy_force_kick, reconf_timer);

    FREE(force_policy->config);
    force_policy->config = NULL;

    force_policy->config = force_policy->next_config;
    force_policy->next_config = NULL;

    if (force_policy->config != NULL) {
        ow_steer_policy_trigger_executor(force_policy->base);

        const uint64_t enforce_period_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_POLICY_FORCE_KICK_ENFORCE_PERIOD_SEC);
        osw_timer_arm_at_nsec(&force_policy->enforce_timer, enforce_period_nsec);
        LOGI(LOG_WITH_POLICY_PREFIX(force_policy->base, "enforce period started"));

        if (force_policy->assoc_links.count == 0) {
            LOGI(LOG_WITH_POLICY_PREFIX(force_policy->base, "aborted force kick, sta disconnected"));
            ow_steer_policy_force_kick_reset(force_policy);
            return;
        }
    }

    ow_steer_policy_schedule_stack_recalc(force_policy->base);
}

static void
ow_steer_policy_force_kick_enforce_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = container_of(timer, struct ow_steer_policy_force_kick, enforce_timer);

    LOGI(LOG_WITH_POLICY_PREFIX(force_policy->base, "enforce period finished"));

    ow_steer_policy_force_kick_reset(force_policy);
    ow_steer_policy_dismiss_executor(force_policy->base);
}

struct ow_steer_policy_force_kick*
ow_steer_policy_force_kick_create(const struct osw_hwaddr *sta_addr,
                                  const struct ow_steer_policy_mediator *mediator,
                                  const char *log_prefix)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops ops = {
        .sigusr1_dump_fn = ow_steer_policy_force_kick_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_force_kick_recalc_cb,
    };
    struct ow_steer_policy_force_kick *force_policy = CALLOC(1, sizeof(*force_policy));
    osw_timer_init(&force_policy->reconf_timer, ow_steer_policy_force_kick_reconf_timer_cb);
    osw_timer_init(&force_policy->enforce_timer, ow_steer_policy_force_kick_enforce_timer_cb);

    force_policy->base = ow_steer_policy_create(g_policy_name, sta_addr, &ops, mediator, log_prefix, force_policy);
    force_policy->assoc_obs = ow_steer_policy_force_kick_assoc_observer(force_policy, sta_addr);

    return force_policy;
}

void
ow_steer_policy_force_kick_free(struct ow_steer_policy_force_kick *force_policy)
{
    if (force_policy == NULL)
        return;

    ow_steer_policy_force_kick_reset(force_policy);
    osw_sta_assoc_observer_drop(force_policy->assoc_obs);
    force_policy->assoc_obs = NULL;
    osw_timer_disarm(&force_policy->reconf_timer);
    FREE(force_policy->next_config);
    force_policy->next_config = NULL;
    FREE(force_policy->config);
    force_policy->config = NULL;
    ow_steer_policy_free(force_policy->base);
    FREE(force_policy);
}

struct ow_steer_policy*
ow_steer_policy_force_kick_get_base(struct ow_steer_policy_force_kick *force_policy)
{
    ASSERT(force_policy != NULL, "");
    return force_policy->base;
}

void
ow_steer_policy_force_kick_set_oneshot_config(struct ow_steer_policy_force_kick *force_policy,
                                              struct ow_steer_policy_force_kick_config *config)
{
    ASSERT(force_policy != NULL, "");

    FREE(force_policy->next_config);
    force_policy->next_config = config;

    osw_timer_arm_at_nsec(&force_policy->reconf_timer, 0);
}

#include "ow_steer_policy_force_kick_ut.c"
