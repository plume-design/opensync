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

#ifndef OSW_MLD_VIF_H_INCLUDED
#define OSW_MLD_VIF_H_INCLUDED

#include <osw_state.h>

struct osw_mld_vif;
struct osw_mld_vif_observer;
typedef struct osw_mld_vif osw_mld_vif_t;
typedef struct osw_mld_vif_observer osw_mld_vif_observer_t;

typedef void osw_mld_vif_mld_fn_t(void *priv, const char *mld_if_name);

typedef void osw_mld_vif_link_fn_t(void *priv, const char *mld_if_name, const struct osw_state_vif_info *info);

osw_mld_vif_observer_t *osw_mld_vif_observer_alloc(osw_mld_vif_t *m);

void osw_mld_vif_observer_drop(osw_mld_vif_observer_t *obs);

void osw_mld_vif_observer_set_mld_added_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_mld_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_mld_connected_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_mld_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_mld_disconnected_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_mld_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_mld_removed_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_mld_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_link_added_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_link_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_link_changed_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_link_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_link_connected_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_link_fn_t *fn, void *fn_priv);

void osw_mld_vif_observer_set_link_disconnected_fn(
        osw_mld_vif_observer_t *obs,
        osw_mld_vif_link_fn_t *fn,
        void *fn_priv);

void osw_mld_vif_observer_set_link_removed_fn(osw_mld_vif_observer_t *obs, osw_mld_vif_link_fn_t *fn, void *fn_priv);

#endif /* OSW_MLD_VIF_H_INCLUDED */
