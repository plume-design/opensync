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

#ifndef OW_STEER_STA_PRIV_H
#define OW_STEER_STA_PRIV_H

struct ow_steer_candidate_list*
ow_steer_sta_get_candidate_list(struct ow_steer_sta *sta);

struct ow_steer_policy_stack*
ow_steer_sta_get_policy_stack(struct ow_steer_sta *sta);

struct ow_steer_executor*
ow_steer_sta_get_executor(struct ow_steer_sta *sta);

void
ow_steer_sta_set_candidate_assessor(struct ow_steer_sta *sta,
                                    struct ow_steer_candidate_assessor *candidate_assessor);

void
ow_steer_sta_schedule_executor_call(struct ow_steer_sta *sta);

void
ow_steer_sta_schedule_policy_stack_recalc(struct ow_steer_sta *sta);

void
ow_steer_sta_conf_mutate(struct ow_steer_sta *sta,
                         struct ds_tree *phy_tree);

#endif /* OW_STEER_STA_PRIV_H */
