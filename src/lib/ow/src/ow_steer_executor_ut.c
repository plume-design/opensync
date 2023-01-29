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
ow_steer_executor_ut_action_conf_mutate(struct ow_steer_executor_action *action,
                                              const struct ow_steer_candidate_list *candidate_list,
                                              struct ds_tree *phy_tree)
{
    ASSERT(action != NULL, "");
    ASSERT(action->priv != NULL, "");

    unsigned int *cnt = action->priv;
    *cnt = *cnt + 1;
}

static void
ow_steer_executor_ut_action_call(struct ow_steer_executor_action *action,
                                       const struct ow_steer_candidate_list *candidate_list,
                                       struct osw_conf_mutator *conf_mutator)
{
    ASSERT(action != NULL, "");
    ASSERT(action->priv != NULL, "");

    unsigned int *cnt = action->priv;
    *cnt = *cnt + 1;
}

static void
ow_steer_executor_ut_action_mediator_sched_recall(struct ow_steer_executor_action *action,
                                                              void *mediator_priv)
{
    /* nop */
}

OSW_UT(ow_steer_executor_call)
{
    const struct osw_hwaddr sta_addr = {
        .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
    };
    struct ow_steer_executor_action_ops call_ops = {
        .call_fn = ow_steer_executor_ut_action_call,
    };
    struct ow_steer_executor_action_mediator mediator = {
        .sched_recall_fn = ow_steer_executor_ut_action_mediator_sched_recall,
        .priv = NULL,
    };
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_executor_action* action1;
    struct ow_steer_executor_action* action2;
    struct ow_steer_executor *executor;
    struct osw_conf_mutator conf_mutator;
    unsigned int action1_call_cnt;
    unsigned int action2_call_cnt;

    /*
     * Setup
     */
    candidate_list = ow_steer_candidate_list_new();
    executor = ow_steer_executor_create();
    action1 = ow_steer_executor_action_create("action1", &sta_addr, &call_ops, &mediator, &action1_call_cnt);
    action2 = ow_steer_executor_action_create("action2", &sta_addr, &call_ops, &mediator, &action2_call_cnt);
    action1_call_cnt = 0;
    action2_call_cnt = 0;

    /*
     * Actions:
     * - action1
     * - action2
     */
    ow_steer_executor_add(executor, action1);
    ow_steer_executor_add(executor, action2);
    ow_steer_executor_call(executor, &sta_addr, candidate_list, &conf_mutator);
    ow_steer_executor_call(executor, &sta_addr, candidate_list, &conf_mutator);

    OSW_UT_EVAL(action1_call_cnt == 2);
    OSW_UT_EVAL(action2_call_cnt == 2);

    ow_steer_executor_free(executor);
    ow_steer_candidate_list_free(candidate_list);
}

OSW_UT(ow_steer_executor_mutate)
{
    const struct osw_hwaddr sta_addr = {
        .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
    };
    struct ow_steer_executor_action_ops in_progress_ops = {
        .conf_mutate_fn = ow_steer_executor_ut_action_conf_mutate,
    };
    struct ow_steer_executor_action_mediator mediator = {
        .sched_recall_fn = ow_steer_executor_ut_action_mediator_sched_recall,
        .priv = NULL,
    };
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_executor_action* action1;
    struct ow_steer_executor_action* action2;
    struct ow_steer_executor *executor;
    struct ds_tree phy_tree;
    unsigned int action1_mutate_cnt;
    unsigned int action2_mutate_cnt;

    /*
     * Setup
     */
    candidate_list = ow_steer_candidate_list_new();
    executor = ow_steer_executor_create();
    action1 = ow_steer_executor_action_create("action1", &sta_addr, &in_progress_ops, &mediator, &action1_mutate_cnt);
    action2 = ow_steer_executor_action_create("action2", &sta_addr, &in_progress_ops, &mediator, &action2_mutate_cnt);
    action1_mutate_cnt = 0;
    action2_mutate_cnt = 0;

    /*
     * Actions:
     * - action1
     * - action2
     */
    ow_steer_executor_add(executor, action1);
    ow_steer_executor_add(executor, action2);
    ow_steer_executor_conf_mutate(executor, &sta_addr, candidate_list, &phy_tree);

    OSW_UT_EVAL(action1_mutate_cnt == 1);
    OSW_UT_EVAL(action2_mutate_cnt == 1);

    /*
     * Cleanup
     */
    ow_steer_executor_free(executor);
    ow_steer_candidate_list_free(candidate_list);
}
