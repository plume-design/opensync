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

#ifndef OW_STEER_EXECUTOR_ACTION_ACL_H
#define OW_STEER_EXECUTOR_ACTION_ACL_H

struct ow_steer_executor_action_acl;

struct ow_steer_executor_action_acl*
ow_steer_executor_action_acl_create(const struct osw_hwaddr *sta_addr,
                                    const struct ow_steer_executor_action_mediator *mediator,
                                    const char *log_prefix);

void
ow_steer_executor_action_acl_set_enabled(struct ow_steer_executor_action_acl *acl_action,
                                         const bool enabled);

void
ow_steer_executor_action_acl_free(struct ow_steer_executor_action_acl *acl_action);

struct ow_steer_executor_action*
ow_steer_executor_action_acl_get_base(struct ow_steer_executor_action_acl *acl_action);

#endif /* OW_STEER_EXECUTOR_ACTION_ACL_H */
