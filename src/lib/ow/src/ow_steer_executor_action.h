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

#ifndef OW_STEER_EXECUTOR_ACTION_H
#define OW_STEER_EXECUTOR_ACTION_H

struct ow_steer_executor_action;

typedef bool
ow_steer_executor_action_call_fn_t(struct ow_steer_executor_action *action,
                                   const struct ow_steer_candidate_list *candidate_list,
                                   struct osw_conf_mutator *conf_mutator);

typedef void
ow_steer_executor_action_conf_mutate_fn_t(struct ow_steer_executor_action *action,
                                          const struct ow_steer_candidate_list *candidate_list,
                                          struct ds_tree *phy_tree);

typedef void
ow_steer_executor_action_mediator_sched_recall_fn_t(struct ow_steer_executor_action *action,
                                                    void *mediator_priv);

typedef void
ow_steer_executor_action_mediator_notify_going_busy_fn_t(struct ow_steer_executor_action *action,
                                                         void *mediator_priv);

typedef void
ow_steer_executor_action_mediator_notify_data_sent_fn_t(struct ow_steer_executor_action *action,
                                                        void *mediator_priv);

typedef void
ow_steer_executor_action_mediator_notify_going_idle_fn_t(struct ow_steer_executor_action *action,
                                                         void *mediator_priv);

struct ow_steer_executor_action_ops {
    ow_steer_executor_action_call_fn_t *call_fn;
    ow_steer_executor_action_conf_mutate_fn_t *conf_mutate_fn;
};

struct ow_steer_executor_action_mediator {
    ow_steer_executor_action_mediator_sched_recall_fn_t *sched_recall_fn;
    ow_steer_executor_action_mediator_notify_going_busy_fn_t *notify_going_busy_fn;
    ow_steer_executor_action_mediator_notify_data_sent_fn_t *notify_data_sent_fn;
    ow_steer_executor_action_mediator_notify_going_idle_fn_t *notify_going_idle_fn;
    void *priv;
};

struct ow_steer_executor_action*
ow_steer_executor_action_create(const char *name,
                                const struct osw_hwaddr *sta_addr,
                                const struct ow_steer_executor_action_ops *ops,
                                const struct ow_steer_executor_action_mediator *mediator,
                                void *priv);

void
ow_steer_executor_action_sched_recall(struct ow_steer_executor_action *action);

void
ow_steer_executor_action_notify_going_busy(struct ow_steer_executor_action *action);

void
ow_steer_executor_action_notify_data_sent(struct ow_steer_executor_action *action);

void
ow_steer_executor_action_notify_going_idle(struct ow_steer_executor_action *action);

void*
ow_steer_executor_action_get_priv(struct ow_steer_executor_action *action);

const char*
ow_steer_executor_action_get_name(struct ow_steer_executor_action *action);

const struct osw_hwaddr*
ow_steer_executor_action_get_sta_addr(const struct ow_steer_executor_action *action);


#endif /* OW_STEER_EXECUTOR_ACTION_H */
