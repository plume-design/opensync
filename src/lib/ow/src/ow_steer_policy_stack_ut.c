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

#include <osw_conf.h>
#include <osw_ut.h>
#include <os.h>
#include "ow_steer_sta_i.h"

struct ow_steer_policy_stack_ut_ctx {
    struct ow_steer_policy policy_low_0;
    struct ow_steer_policy policy_mid_0;
    struct ow_steer_policy policy_high_0;
};

struct ow_steer_policy_stack_ut_policy {
    unsigned int recalc_cnt;
};

static const char *g_ow_steer_policy_stack_ut_policy_name = "ow_steer_policy_stack_ut_policy";

static struct ow_steer_policy*
ow_steer_policy_stack_ut_policy_new(const struct osw_hwaddr *sta_addr,
                                    const struct ow_steer_policy_ops *ops,
                                    const struct ow_steer_policy_mediator *mediator)
{
    struct ow_steer_policy_stack_ut_policy *priv = CALLOC(1, sizeof(*priv));
    return ow_steer_policy_create(g_ow_steer_policy_stack_ut_policy_name, sta_addr, ops, mediator, "", priv);
}

static void
ow_steer_policy_stack_ut_ctx_init(struct ow_steer_policy_stack_ut_ctx* ctx)
{
    assert(ctx != NULL);

    memset(ctx, 0, sizeof(*ctx));

    ctx->policy_low_0.name = "policy_low_0";
    ctx->policy_mid_0.name = "policy_policy_mid_0";
    ctx->policy_high_0.name = "policy_policy_high_0";
}

static void
ow_steer_policy_stack_ut_policy_recalc_cb(struct ow_steer_policy *policy,
                                          struct ow_steer_candidate_list *candidate_list)
{
    assert(policy != NULL);
    struct ow_steer_policy_stack_ut_policy *priv = ow_steer_policy_get_priv(policy);
    priv->recalc_cnt++;
}

static void
ow_steer_policy_stack_ut_sorting_policies_1_exec_cb(struct osw_timer *timer)
{
}

OSW_UT(ow_steer_policy_stack_ut_sorting_policies_1)
{
    struct ow_steer_policy_stack_ut_ctx ctx;
    struct ow_steer_policy_stack *policy_stack;
    struct ow_steer_policy *policy;
    struct ow_steer_sta sta;
    MEMZERO(sta);

    osw_timer_init(&sta.executor_timer, ow_steer_policy_stack_ut_sorting_policies_1_exec_cb);

    osw_ut_time_init();
    ow_steer_policy_stack_ut_ctx_init(&ctx);

    sta.candidate_list = ow_steer_candidate_list_new();
    policy_stack = ow_steer_policy_stack_create(&sta, "");
    ow_steer_policy_stack_add(policy_stack, &ctx.policy_high_0);
    ow_steer_policy_stack_add(policy_stack, &ctx.policy_mid_0);
    ow_steer_policy_stack_add(policy_stack, &ctx.policy_low_0);
    osw_ut_time_advance(0);

    /* Expected policies order:
     * - policy_high_0
     * - policy_mid_0
     * - policy_low_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_high_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_mid_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_low_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    ow_steer_policy_stack_remove(policy_stack, &ctx.policy_mid_0);
    osw_ut_time_advance(0);
    /* Expected policies order:
     * - policy_high_0
     * - policy_low_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_high_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_low_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    ow_steer_policy_stack_remove(policy_stack, &ctx.policy_high_0);
    osw_ut_time_advance(0);
    /* Expected policies order:
     * - policy_low_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_low_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    ow_steer_policy_stack_remove(policy_stack, &ctx.policy_low_0);
    osw_ut_time_advance(0);
    /* Expected policies order:
     * (empty)
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == NULL);

    ow_steer_policy_stack_free(policy_stack);
}

OSW_UT(ow_steer_policy_stack_ut_sorting_policies_2)
{
    struct ow_steer_policy_stack_ut_ctx ctx;
    struct ow_steer_policy_stack *policy_stack;
    struct ow_steer_policy *policy;
    struct ow_steer_sta sta;
    MEMZERO(sta);

    ow_steer_policy_stack_ut_ctx_init(&ctx);

    policy_stack = ow_steer_policy_stack_create(&sta, "");
    ow_steer_policy_stack_add(policy_stack, &ctx.policy_high_0);

    /* Expected policies oerder:
     * - policy_high_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_high_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    ow_steer_policy_stack_add(policy_stack, &ctx.policy_mid_0);
    /* Expected policies oerder:
     * - policy_high_0
     * - policy_mid_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_high_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_mid_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    ow_steer_policy_stack_add(policy_stack, &ctx.policy_low_0);
    /* Expected policies oerder:
     * - policy_high_0
     * - policy_mid_0
     * - policy_low_0
     */
    policy = (struct ow_steer_policy*) ds_dlist_head(&policy_stack->policy_list);
    assert(policy == &ctx.policy_high_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_mid_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == &ctx.policy_low_0);
    policy = (struct ow_steer_policy*) ds_dlist_next(&policy_stack->policy_list, policy);
    assert(policy == NULL);

    assert(ow_steer_policy_get_more_important(&ctx.policy_low_0, &ctx.policy_low_0) == &ctx.policy_low_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_low_0, &ctx.policy_mid_0) == &ctx.policy_mid_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_low_0, &ctx.policy_high_0) == &ctx.policy_high_0);

    assert(ow_steer_policy_get_more_important(&ctx.policy_mid_0, &ctx.policy_low_0) == &ctx.policy_mid_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_mid_0, &ctx.policy_mid_0) == &ctx.policy_mid_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_mid_0, &ctx.policy_high_0) == &ctx.policy_high_0);

    assert(ow_steer_policy_get_more_important(&ctx.policy_high_0, &ctx.policy_low_0) == &ctx.policy_high_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_high_0, &ctx.policy_mid_0) == &ctx.policy_high_0);
    assert(ow_steer_policy_get_more_important(&ctx.policy_high_0, &ctx.policy_high_0) == &ctx.policy_high_0);
}

static void
ow_steer_policy_stack_ut_lifecycle_exec_cb(struct osw_timer *timer)
{
}

OSW_UT(ow_steer_policy_stack_ut_lifecycle)
{
    const struct osw_hwaddr sta_addr = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA }, };
    const struct ow_steer_policy_ops ops = {
        .recalc_fn = ow_steer_policy_stack_ut_policy_recalc_cb,
    };
    struct ow_steer_policy_stack *policy_stack;
    struct ow_steer_policy *policy;
    struct ow_steer_policy_stack_ut_policy *priv; /* Hold struct ow_steer_policy_stack_ut_policy ptrs to free them at the end */
    struct ow_steer_policy_mediator mediator;
    struct ow_steer_sta sta = {
        .candidate_list = ow_steer_candidate_list_new(),
    };

    /* Setup */
    osw_timer_init(&sta.executor_timer, ow_steer_policy_stack_ut_lifecycle_exec_cb);
    memset(&mediator, 0, sizeof(mediator));
    policy = ow_steer_policy_stack_ut_policy_new(&sta_addr, &ops, &mediator);
    priv = ow_steer_policy_get_priv(policy);
    policy_stack = ow_steer_policy_stack_create(&sta, "");

    assert(priv->recalc_cnt == 0);

    /* Add policy to stack */
    ow_steer_policy_stack_add(policy_stack, policy);
    assert(priv->recalc_cnt == 0);

    /* Schedule recalc */
    ow_steer_policy_stack_schedule_recalc(policy_stack);
    osw_ut_time_advance(0);
    assert(priv->recalc_cnt == 1);

    /* Cleanup */
    ow_steer_policy_stack_remove(policy_stack, policy);
    ow_steer_policy_free(policy);
    FREE(priv);
}
