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

#ifndef OSW_STA_SNR_H_INCLUDED
#define OSW_STA_SNR_H_INCLUDED

#include <stdint.h>

#include <osw_module.h>
#include <osw_types.h>

struct osw_sta_snr;
struct osw_sta_snr_params;
struct osw_sta_snr_observer;

typedef struct osw_sta_snr osw_sta_snr_t;
typedef struct osw_sta_snr_params osw_sta_snr_params_t;
typedef struct osw_sta_snr_observer osw_sta_snr_observer_t;
typedef void osw_sta_snr_notify_fn_t(void *priv, const uint8_t *snr_db);

osw_sta_snr_params_t *osw_sta_snr_params_alloc(void);
void osw_sta_snr_params_drop(osw_sta_snr_params_t *p);
void osw_sta_snr_params_set_sta_addr(osw_sta_snr_params_t *p, const struct osw_hwaddr *sta_addr);
void osw_sta_snr_params_set_vif_name(osw_sta_snr_params_t *p, const char *vif_name);
void osw_sta_snr_params_set_vif_addr(osw_sta_snr_params_t *p, const struct osw_hwaddr *vif_addr);
void osw_sta_snr_params_set_ageout_sec(osw_sta_snr_params_t *p, unsigned int seconds);
void osw_sta_snr_params_set_notify_fn(osw_sta_snr_params_t *p, osw_sta_snr_notify_fn_t *fn, void *priv);

osw_sta_snr_observer_t *osw_sta_snr_observer_alloc(osw_sta_snr_t *m, osw_sta_snr_params_t *p);
const uint8_t *osw_sta_snr_observer_get_last(osw_sta_snr_observer_t *obs);
void osw_sta_snr_observer_drop(osw_sta_snr_observer_t *obs);

static inline osw_sta_snr_t *osw_sta_snr_load(void)
{
    return OSW_MODULE_LOAD(osw_sta_snr);
}

#endif /* OSW_STA_SNR_H_INCLUDED */
