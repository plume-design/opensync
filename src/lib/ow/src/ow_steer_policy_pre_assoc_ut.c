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

#include <osw_ut.h>
#include <osw_conf.h>
#include "ow_steer_policy_i.h"

struct ow_steer_policy_pre_assoc_ut_ctx {
    struct osw_drv_vif_state drv_vif_state_0;
    struct osw_state_vif_info vif_0;
    struct osw_drv_vif_state drv_vif_state_1;
    struct osw_state_vif_info vif_1;
    struct osw_state_sta_info sta_info;
    struct osw_drv_report_vif_probe_req probe_req;
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_policy_pre_assoc_config config;
    struct osw_drv_dot11_frame_header auth_frame;
};

static struct ow_steer_policy_pre_assoc_ut_ctx*
ow_steer_policy_pre_assoc_ut_ctx_get(void)
{
    static struct ow_steer_policy_pre_assoc_ut_ctx ctx = {
        .drv_vif_state_0 = {
            .mac_addr = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, },
        },
        .vif_0 = {
            .vif_name = "vif_0",
            .drv_state = &ctx.drv_vif_state_0,
        },
        .drv_vif_state_1 = {
            .mac_addr = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, },
        },
        .vif_1 = {
            .vif_name = "vif_1",
            .drv_state = &ctx.drv_vif_state_1,
        },
        .probe_req = {
            .sta_addr = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, },
        },
        .sta_info = {
            .mac_addr = &ctx.probe_req.sta_addr,
            .vif = &ctx.vif_1,
        },
        .config = {
            .bssid = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, },
            .backoff_timeout_sec = 60,
            .backoff_exp_base = 3,
            .reject_condition = {
                .type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER,
                .params.counter = {
                    .reject_limit = 3,
                    .reject_timeout_sec = 120,
                },
            },
            .backoff_condition = {
                .type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_NONE,
            },
        },
        .auth_frame = {
            .sa = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, },
            .da = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, },
            .bssid = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, },
        },
    };

    ctx.auth_frame.frame_control = htole16(DOT11_FRAME_CTRL_SUBTYPE_AUTH);
    ctx.candidate_list = ow_steer_candidate_list_new();

    return &ctx;
}

struct ow_steer_policy_pre_assoc_ut_mediator_cnt {
    unsigned int schedule_recalc_cnt;
    unsigned int trigger_executor_cnt;
    unsigned int dismiss_executor_cnt;
};

static void
ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc(struct ow_steer_policy *policy,
                                                        void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_pre_assoc_ut_mediator_cnt *cnt = priv;
    cnt->schedule_recalc_cnt++;
}

static bool
ow_steer_policy_pre_assoc_ut_mediator_trigger_executor(struct ow_steer_policy *policy,
                                                       void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_pre_assoc_ut_mediator_cnt *cnt = priv;
    cnt->trigger_executor_cnt++;
    return true;
}

static void
ow_steer_policy_pre_assoc_ut_mediator_dismis_executor(struct ow_steer_policy *policy,
                                                             void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_pre_assoc_ut_mediator_cnt *cnt = priv;
    cnt->dismiss_executor_cnt++;
}

OSW_UT(ow_steer_policy_pre_assoc_ut_always_success_steer)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };

    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    /* Create policy */
    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);

    ow_steer_candidate_list_clear(ctx->candidate_list);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Configure policy */
    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(ctx->candidate_list);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Start */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA sends few probes first */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to second VIF (preferred) */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_always_backoff_connect_blocked_vif)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA many few probes to initiate backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to first (blocked) VIF */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 4);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_always_backoff_connect_preferred_vif)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA sends few probes to initiate backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to second VIF (preferred) */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 4);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_always_backoff_no_connect)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA sends few probes to initiate backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Wait until backoff expire */
    osw_ut_time_advance(OSW_TIME_SEC(60));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 4);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Start sending probes again */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 5);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 5);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_threshold_snr_success_steer)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();
    ctx->config.backoff_condition.type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR;
    ctx->config.backoff_condition.params.threshold_snr.threshold_snr = 35;

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA sends few probes first */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->probe_req.snr = 38;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to second VIF (preferred) */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_threshold_snr_enter_backoff)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();
    ctx->config.backoff_condition.type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR;
    ctx->config.backoff_condition.params.threshold_snr.threshold_snr = 35;

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA sends few probes first */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Backoff should start with this probe req */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ctx->probe_req.snr = 32;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->probe_req.snr = 31;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_dormant_when_connected_to_desired_band)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();
    ctx->config.backoff_condition.type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR;
    ctx->config.backoff_condition.params.threshold_snr.threshold_snr = 35;

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA connects to second VIF (preferred) */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA sends probes on blocked band */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Next probe reqs shouldn't trigger backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Disconnect STA */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_disconnected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Now probe requests received on blocked VIF should trigger backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 4);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 4);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Next probe req shouldn't trigger backoff */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    ctx->probe_req.snr = 40;
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 5);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_blocked)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send probe to initiate reject period */
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Send few more probes */
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);

    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Wait until backoff */
    osw_ut_time_advance(OSW_TIME_SEC(4));
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to blocked VIF */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_preferred)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send probe to initiate reject period */
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Send few more probes */
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);

    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to preferred VIF */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_1;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_blocked_auth_after_probe)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;
    ctx->config.immediate_backoff_on_auth_req = true;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send probe to initiate reject period */
    counter_policy->state_observer.vif_probe_req_fn(&counter_policy->state_observer, &ctx->vif_0, &ctx->probe_req);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Give it some time, should still be rejecting */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Send auth, no probes */
    counter_policy->state_observer.vif_frame_rx_fn(&counter_policy->state_observer,
                                                   &ctx->vif_0,
                                                   (const void *)&ctx->auth_frame,
                                                   sizeof(ctx->auth_frame));

    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to blocked VIF */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_blocked_auth_only)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel1 = { .width = OSW_CHANNEL_80MHZ, .control_freq_mhz = 5300, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;
    ctx->config.immediate_backoff_on_auth_req = true;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel1);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send auth to initiate backoff immediately period */
    counter_policy->state_observer.vif_frame_rx_fn(&counter_policy->state_observer,
                                                   &ctx->vif_0,
                                                   (const void *)&ctx->auth_frame,
                                                   sizeof(ctx->auth_frame));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA send auth again, should already be in backoff anyway and nothing should change */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    counter_policy->state_observer.vif_frame_rx_fn(&counter_policy->state_observer,
                                                   &ctx->vif_0,
                                                   (const void *)&ctx->auth_frame,
                                                   sizeof(ctx->auth_frame));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* STA connects to blocked VIF */
    osw_ut_time_advance(OSW_TIME_SEC(1));
    ctx->sta_info.vif = &ctx->vif_0;
    counter_policy->state_observer.sta_connected_fn(&counter_policy->state_observer, &ctx->sta_info);
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_blocked_auth_only_negative)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;
    ctx->config.immediate_backoff_on_auth_req= false;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send auth - but it should not cause backoff because auth_block==true */
    counter_policy->state_observer.vif_frame_rx_fn(&counter_policy->state_observer,
                                                   &ctx->vif_0,
                                                   (const void *)&ctx->auth_frame,
                                                   sizeof(ctx->auth_frame));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_timer_mode_connect_to_blocked_auth_only_bypass_failed)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();

    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_pre_assoc *counter_policy;
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup internal bits */
    ctx->config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
    ctx->config.reject_condition.params.timer.reject_timeout_msec = 4000;
    ctx->config.immediate_backoff_on_auth_req = true;

    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* STA send auth to initiate backoff immediately period */
    counter_policy->state_observer.vif_frame_rx_fn(&counter_policy->state_observer,
                                                   &ctx->vif_0,
                                                   (const void *)&ctx->auth_frame,
                                                   sizeof(ctx->auth_frame));
    ow_steer_candidate_list_clear(ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* This simulates possible failure to unblock a client
     * quickly enough through auth unblocking. It simply
     * checks if the report will be generated.
     */
    osw_ut_time_advance(OSW_TIME_SEC(10));
    OSW_UT_EVAL(counter_policy->pstate.auth_bypass_fail_cnt == 1);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_add_remove_local_bssids)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();
    struct ow_steer_policy_pre_assoc *counter_policy;
    const struct osw_channel channel2g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel5g = { .width = OSW_CHANNEL_80MHZ, .control_freq_mhz = 5290, };
    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };

    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel2g);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel5g);

    /* Create policy */
    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);

    /* Add potential candidates to the list of local_bssids */
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, &channel2g);
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_1.mac_addr, &channel5g);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 2);

    /* Change channel of already known local_bssid */
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, &channel5g);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 2);

    /* Remove local_bssid */
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, NULL);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 1);

    /* Re-add local_bssid */
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, &channel2g);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 2);

    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, NULL);
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_1.mac_addr, NULL);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 0);
}

OSW_UT(ow_steer_policy_pre_assoc_ut_prevent_blocking_last_candidate)
{
    struct ow_steer_policy_pre_assoc_ut_ctx *ctx = ow_steer_policy_pre_assoc_ut_ctx_get();
    struct ow_steer_policy_pre_assoc_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_pre_assoc_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_pre_assoc_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };

    struct ow_steer_policy_pre_assoc *counter_policy;
    const struct osw_channel channel2g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel5g = { .width = OSW_CHANNEL_80MHZ, .control_freq_mhz = 5290, };
    struct ow_steer_candidate *candidate_0 = NULL;
    struct ow_steer_candidate *candidate_1 = NULL;

    /* Setup */
    osw_ut_time_init();
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr, &channel2g);
    ow_steer_candidate_list_bss_set(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr, &channel5g);
    candidate_0 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_0.mac_addr);
    candidate_1 = ow_steer_candidate_list_lookup(ctx->candidate_list, &ctx->drv_vif_state_1.mac_addr);

    /* Create policy */
    counter_policy = ow_steer_policy_pre_assoc_create("pre_assoc_test", NULL, &ctx->probe_req.sta_addr, &mediator, "");
    OSW_UT_EVAL(counter_policy != NULL);

    /* Add potential candidates to the list of local_bssids */
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_0.mac_addr, &channel2g);
    ow_steer_policy_pre_assoc_set_bss(&counter_policy->local_bssids, &ctx->drv_vif_state_1.mac_addr, &channel5g);
    OSW_UT_EVAL(ds_dlist_len(&counter_policy->local_bssids) == 2);

    /* Add candidates with preferences NONE */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    /* Configure policy */
    ow_steer_policy_pre_assoc_set_config(counter_policy, MEMNDUP(&ctx->config, sizeof(ctx->config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* CASE 1 - Both candidates NONE at input */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    /* 2.4GHz candidate soft blocked - 5GHz not changed */
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /* CASE 2 - 2.4GHz NONE, 5GHz AVAILABLE */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    ow_steer_candidate_set_preference(candidate_1, "", OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /* CASE 3 - 2.4GHz NONE, 5GHz SOFT_BLOCKED */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    ow_steer_candidate_set_preference(candidate_1, "", OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);

    /* CASE 4 - 2.4GHz SOFT_BLOCKED, 5GHz AVAILABLE */
    ow_steer_candidate_list_clear(ctx->candidate_list);
    ow_steer_candidate_set_preference(candidate_0, "", OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    ow_steer_candidate_set_preference(candidate_1, "", OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    counter_policy->base->ops.recalc_fn(counter_policy->base, ctx->candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_0) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);


}
