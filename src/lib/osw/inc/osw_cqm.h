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

#ifndef OSW_CQM_H_INCLUDED
#define OSW_CQM_H_INCLUDED

#include <osw_types.h>

enum osw_cqm_link_state {
    OSW_CQM_LINK_DECONFIGURED,
    OSW_CQM_LINK_DISCONNECTED,
    OSW_CQM_LINK_CONNECTED,
    OSW_CQM_LINK_RECOVERING,
    OSW_CQM_LINK_TIMING_OUT,
    OSW_CQM_LINK_TIMED_OUT,
};

struct osw_cqm;
struct osw_cqm_notify;
struct osw_cqm_ops;

typedef void osw_cqm_notify_fn_t(const char *vif_name,
                                 enum osw_cqm_link_state state,
                                 const struct osw_channel *last_channel,
                                 void *priv);

typedef struct osw_cqm *osw_cqm_alloc_fn_t(struct osw_cqm_ops *ops);
typedef void osw_cqm_set_timeout_sec_fn_t(struct osw_cqm *cqm, float seconds);
typedef struct osw_cqm_notify *osw_cqm_add_notify_fn_t(struct osw_cqm *cqm,
                                                       const char *name,
                                                       osw_cqm_notify_fn_t *fn,
                                                       void *fn_priv);
typedef void osw_cqm_del_notify_fn_t(struct osw_cqm_notify *n);
typedef void osw_cqm_free_fn_t(struct osw_cqm *cqm);

struct osw_cqm_ops {
    osw_cqm_alloc_fn_t *alloc_fn;
    osw_cqm_free_fn_t *free_fn;
    osw_cqm_set_timeout_sec_fn_t *set_timeout_sec_fn;
    osw_cqm_add_notify_fn_t *add_notify_fn;
    osw_cqm_del_notify_fn_t *del_notify_fn;
};

#endif /* OSW_CQM_H_INCLUDED */
