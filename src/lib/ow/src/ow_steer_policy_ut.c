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

static void
ow_steer_policy_ut_medaitor_sched_stack_recalc(struct ow_steer_policy *policy,
                                               void *priv)
{
    assert(priv != NULL);
    unsigned int *stack_recalc_cnt = priv;
    *stack_recalc_cnt = *stack_recalc_cnt + 1;
}

OSW_UT(ow_steer_policy_ut_methods)
{
    const char *name = "policy_ut";
    const char *prefix = "policy: policy_ut: ";
    const struct osw_hwaddr sta_addr = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA }, };
    const struct osw_hwaddr bssid = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB }, };
    const struct ow_steer_policy_ops ops = { 0 };
    unsigned int stack_recalc_cnt = 0;
    void *priv = NULL;
    struct ow_steer_policy *policy = NULL;
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_ut_medaitor_sched_stack_recalc,
        .priv = &stack_recalc_cnt,
    };

    policy = ow_steer_policy_create(name, &sta_addr, &ops, &mediator, "", priv);
    ow_steer_policy_set_bssid(policy, &bssid);
    OSW_UT_EVAL(policy != NULL);
    OSW_UT_EVAL(ow_steer_policy_get_priv(policy) == priv);
    OSW_UT_EVAL(osw_hwaddr_cmp(ow_steer_policy_get_sta_addr(policy), &sta_addr) == 0);
    OSW_UT_EVAL(osw_hwaddr_cmp(ow_steer_policy_get_bssid(policy), &bssid) == 0);
    OSW_UT_EVAL(strcmp(ow_steer_policy_get_prefix(policy), prefix) == 0);

    OSW_UT_EVAL(stack_recalc_cnt == 0);
    ow_steer_policy_schedule_stack_recalc(policy);
    OSW_UT_EVAL(stack_recalc_cnt == 1);

    /* Cleanup */
    ow_steer_policy_free(policy);
}
