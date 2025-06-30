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

#ifndef OSW_WNM_H
#define OSW_WNM_H

#include <osw_types.h>

typedef struct osw_wnm osw_wnm_t;
typedef struct osw_wnm_sta osw_wnm_sta_t;
typedef struct osw_wnm_sta_params osw_wnm_sta_params_t;
typedef struct osw_wnm_sta_observer osw_wnm_sta_observer_t;
typedef struct osw_wnm_sta_observer_params osw_wnm_sta_observer_params_t;

typedef void osw_wnm_sta_observer_notify_fn_t(void *priv, const osw_wnm_sta_t *sta);

bool osw_wnm_sta_is_mbo_capable(const osw_wnm_sta_t *sta);
enum osw_sta_cell_cap osw_wnm_sta_get_mbo_cell_cap(const osw_wnm_sta_t *sta);

/* observer API */
osw_wnm_sta_observer_params_t *osw_wnm_sta_observer_params_alloc(void);
void osw_wnm_sta_observer_params_set_changed_fn(
        osw_wnm_sta_observer_params_t *p,
        osw_wnm_sta_observer_notify_fn_t *fn,
        void *priv);
void osw_wnm_sta_observer_params_set_addr(osw_wnm_sta_observer_params_t *p, const struct osw_hwaddr *sta_addr);

osw_wnm_sta_observer_t *osw_wnm_sta_observer_alloc(osw_wnm_t *m, osw_wnm_sta_observer_params_t *p);
void osw_wnm_sta_observer_drop(osw_wnm_sta_observer_t *obs);

#endif /* OSW_WNM_H */
