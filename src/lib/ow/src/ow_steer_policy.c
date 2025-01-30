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

#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <util.h>
#include <osw_state.h>
#include <osw_types.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_policy_stack.h"
#include "ow_steer_policy_i.h"

#define LOG_PREFIX(fmt, ...) \
    "ow: steer:" fmt, ##__VA_ARGS__


struct ow_steer_policy*
ow_steer_policy_create(const char *name,
                       const struct osw_hwaddr *sta_addr,
                       const struct ow_steer_policy_ops *ops,
                       const struct ow_steer_policy_mediator *mediator,
                       const char *log_prefix,
                       void *priv)
{
    assert(name != NULL);
    assert(sta_addr != NULL);
    assert(ops != NULL);
    assert(mediator != NULL);

    struct ow_steer_policy *policy = CALLOC(1, sizeof(*policy));

    policy->name = STRDUP(name);
    memcpy(&policy->sta_addr, sta_addr, sizeof(policy->sta_addr));
    memcpy(&policy->ops, ops, sizeof(policy->ops));
    memcpy(&policy->mediator, mediator, sizeof(policy->mediator));
    policy->priv = priv;
    policy->log_prefix = strfmt("%spolicy: %s: ", log_prefix, name);

    return policy;
}

void
ow_steer_policy_free(struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    FREE(policy->name);
    FREE(policy->log_prefix);
    FREE(policy);
}

void*
ow_steer_policy_get_priv(struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    return policy->priv;
}

const struct osw_hwaddr*
ow_steer_policy_get_sta_addr(const struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    return &policy->sta_addr;
}

const struct osw_hwaddr*
ow_steer_policy_get_bssid(const struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    return &policy->bssid;
}

void
ow_steer_policy_set_bssid(struct ow_steer_policy *policy,
                          const struct osw_hwaddr *bssid)
{
    assert(policy != NULL);

    if (bssid != NULL)
        memcpy(&policy->bssid, bssid, sizeof(policy->bssid));
    else
        memset(&policy->bssid, 0, sizeof(policy->bssid));
}

const char*
ow_steer_policy_get_name(const struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    return policy->name;
}

const char*
ow_steer_policy_get_prefix(struct ow_steer_policy *policy)
{
    assert(policy != NULL);
    return policy->log_prefix;
}

void
ow_steer_policy_schedule_stack_recalc(struct ow_steer_policy *policy)
{
    assert(policy != NULL);

    if (WARN_ON(policy->mediator.sched_recalc_stack_fn == NULL))
        return;

    policy->mediator.sched_recalc_stack_fn(policy, policy->mediator.priv);
}

bool
ow_steer_policy_trigger_executor(struct ow_steer_policy *policy)
{
    assert(policy != NULL);

    if (policy->mediator.trigger_executor_fn == NULL)
        return true;

    return policy->mediator.trigger_executor_fn(policy, policy->mediator.priv);
}

void
ow_steer_policy_dismiss_executor(struct ow_steer_policy *policy)
{
    assert(policy != NULL);

    if (policy->mediator.dismiss_executor_fn == NULL)
        return;

    policy->mediator.dismiss_executor_fn(policy, policy->mediator.priv);
}

void
ow_steer_policy_notify_backoff(struct ow_steer_policy *policy,
                               const bool enabled,
                               const unsigned int period)
{
    assert(policy != NULL);

    if (policy->mediator.notify_backoff_fn == NULL)
        return;

    policy->mediator.notify_backoff_fn(policy,
                                       policy->mediator.priv,
                                       enabled,
                                       period);
}

void
ow_steer_policy_notify_steering_attempt(struct ow_steer_policy *policy,
                                        const char *vif_name)
{
    assert(policy != NULL);
    assert(vif_name != NULL);

    if (policy->mediator.notify_steering_attempt_fn == NULL)
        return;

    policy->mediator.notify_steering_attempt_fn(policy,
                                                vif_name,
                                                policy->mediator.priv);
}
#include "ow_steer_policy_ut.c"
