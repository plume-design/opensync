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

#ifndef OW_STEER_BM_MLO_H_INCLUDED
#define OW_STEER_BM_MLO_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <osw_types.h>
#include <osw_module.h>

struct ow_steer_bm_mlo;
typedef struct ow_steer_bm_mlo ow_steer_bm_mlo_t;

typedef enum
{
    OW_STEER_BM_MLO_STEERING_CLIENT_BS_BTM,
    OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_BTM,
    OW_STEER_BM_MLO_STEERING_CLIENT_BTM,
    OW_STEER_BM_MLO_STEERING_CLIENT_BTM_STATUS,
    OW_STEER_BM_MLO_STEERING_CLIENT_BS_BTM_RETRY,
    OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_BTM_RETRY,
    OW_STEER_BM_MLO_STEERING_CLIENT_BTM_RETRY,
    OW_STEER_BM_MLO_STEERING_CLIENT_KICKED,
    OW_STEER_BM_MLO_STEERING_CLIENT_BS_KICK,
    OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_KICK,
    OW_STEER_BM_MLO_STEERING_CLIENT_SPECULATIVE_KICK,
    OW_STEER_BM_MLO_STEERING_CLIENT_DIRECTED_KICK,
    OW_STEER_BM_MLO_STEERING_CLIENT_GHOST_DEVICE_KICK,
    OW_STEER_BM_MLO_STEERING_BAND_STEERING_ATTEMPT,
    OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_ATTEMPT,
    OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_STARTED,
    OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_DISABLED,
    OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_EXPIRED,
    OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_FAILED,
    OW_STEER_BM_MLO_STEERING_AUTH_BLOCK,
} ow_steer_bm_mlo_steering_type_e;

typedef enum
{
    OW_STEER_BM_MLO_MISC_BACKOFF,
    OW_STEER_BM_MLO_MISC_ACTIVITY,
} ow_steer_bm_mlo_misc_type_e;

typedef struct
{
    ow_steer_bm_mlo_steering_type_e type;
    bool btm_response_code_valid;
    uint32_t btm_response_code;
    bool allow_acl_valid;
    bool allow_acl;
    const struct osw_hwaddr *target_bssids;
    size_t n_target_bssids;
} ow_steer_bm_mlo_steering_params_t;

static inline ow_steer_bm_mlo_t *ow_steer_bm_mlo_load(void)
{
    return OSW_MODULE_LOAD(ow_steer_bm_mlo);
}

void ow_steer_bm_mlo_set_topic(ow_steer_bm_mlo_t *m, const char *topic);

void ow_steer_bm_mlo_client_track(ow_steer_bm_mlo_t *m, const struct osw_hwaddr *assoc);
void ow_steer_bm_mlo_client_untrack(ow_steer_bm_mlo_t *m, const struct osw_hwaddr *assoc);

void ow_steer_bm_mlo_report_steering(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *assoc,
        const ow_steer_bm_mlo_steering_params_t *params);

void ow_steer_bm_mlo_report_misc(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *sta_addr,
        ow_steer_bm_mlo_misc_type_e type);

#endif /* OW_STEER_BM_MLO_H_INCLUDED */
