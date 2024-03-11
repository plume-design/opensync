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

#ifndef NL_CMD_H_INCLUDED
#define NL_CMD_H_INCLUDED

/**
 * @file nl_cmd.h
 * @brief Bi-directional debounce message helper
 *
 * This allows request submission deduplication. For example
 * when a program has multiple event entrypoints that should
 * end up with a command getting issued. Most of the time
 * what is necessary is the guarantee that after the last
 * intent submission an exchange is initiated.
 *
 * The expected use is to maintain a long-running nl_cmd and
 * using nl_cmd_set_msg() to submit the freshest command
 * intent.
 *
 * This module expects the control flow handling tx/rx
 * processing to be called explicitly someplace else.
 * Eventually nl_conn_poll() needs to be called to handle
 * tx/rx. Recommendation is to use nl_ev.
 */

#include <stdbool.h>
#include <netlink/msg.h>

struct nl_cmd;

typedef void
nl_cmd_response_fn_t(struct nl_cmd *cmd,
                     struct nl_msg *msg,
                     void *priv);


typedef void
nl_cmd_completed_fn_t(struct nl_cmd *cmd,
                      void *priv);

typedef void
nl_cmd_failed_fn_t(struct nl_cmd *cmd,
                   struct nlmsgerr *err,
                   void *priv);

void
nl_cmd_set_response_fn(struct nl_cmd *cmd,
                       nl_cmd_response_fn_t *fn,
                       void *priv);

void
nl_cmd_set_completed_fn(struct nl_cmd *cmd,
                        nl_cmd_completed_fn_t *fn,
                        void *priv);

void
nl_cmd_set_failed_fn(struct nl_cmd *cmd,
                     nl_cmd_failed_fn_t *fn,
                     void *priv);

void
nl_cmd_set_name(struct nl_cmd *cmd,
                const char *name);

void
nl_cmd_free(struct nl_cmd *cmd);

bool
nl_cmd_is_pending(const struct nl_cmd *cmd);

bool
nl_cmd_is_in_flight(const struct nl_cmd *cmd);

bool
nl_cmd_is_completed(const struct nl_cmd *cmd);

bool
nl_cmd_is_failed(const struct nl_cmd *cmd);

void
nl_cmd_set_msg(struct nl_cmd *cmd,
               struct nl_msg *msg);

bool
nl_cmd_wait(struct nl_cmd *cmd,
            struct timeval *tv);

#endif /* NL_CMD_H_INCLUDED */
