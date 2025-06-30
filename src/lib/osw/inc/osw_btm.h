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

#ifndef OSW_BTM_H
#define OSW_BTM_H

#include <osw_module.h>

#define OSW_BTM_REQ_NEIGH_SIZE 16 /* TODO Check! */

struct osw_btm;
struct osw_btm_sta;
struct osw_btm_req;
struct osw_btm_req_neigh;
struct osw_btm_req_params;
struct osw_btm_obs;

typedef struct osw_btm osw_btm_t;
typedef struct osw_btm_sta osw_btm_sta_t;
typedef struct osw_btm_req osw_btm_req_t;
typedef struct osw_btm_obs osw_btm_obs_t;

enum osw_btm_req_result {
    OSW_BTM_REQ_RESULT_SENT,
    OSW_BTM_REQ_RESULT_FAILED,
};

typedef struct osw_btm_resp osw_btm_resp_t;

uint8_t
osw_btm_resp_get_status(const osw_btm_resp_t *resp);

typedef void
osw_btm_req_completed_fn_t(void *priv, enum osw_btm_req_result result);

typedef void
osw_btm_req_response_fn_t(void *priv, const osw_btm_resp_t *resp);

struct osw_btm_req_neigh {
    struct osw_hwaddr bssid;
    uint32_t bssid_info;
    uint8_t op_class;
    uint8_t channel;
    uint8_t btmpreference;
    uint8_t phy_type;
    bool disassoc_imminent;
    uint16_t disassoc_timer;
};

struct osw_btm_retry_neigh {
    struct osw_btm_req_neigh neigh;
    uint8_t preference;
};

enum osw_btm_mbo_cell_preference {
    OSW_BTM_MBO_CELL_PREF_NONE,           /* Do not include preference in BTM */
    OSW_BTM_MBO_CELL_PREF_EXCLUDE_CELL,   /* STA shall not use Cellular */
    OSW_BTM_MBO_CELL_PREF_AVOID_CELL,     /* STA shall avoid Cellular */
    OSW_BTM_MBO_CELL_PREF_RECOMMEND_CELL, /* STA shall prefer Cellular */
};

enum osw_btm_mbo_reason {
    OSW_BTM_MBO_REASON_NONE,         /* Do not include reason in BTM */
    OSW_BTM_MBO_REASON_LOW_RSSI,     /* AP considers STA's signal low */
    /* More can be added later */
};

struct osw_btm_req_params {
    struct osw_btm_req_neigh neigh[OSW_BTM_REQ_NEIGH_SIZE];
    size_t neigh_len;
    uint8_t valid_int;
    bool abridged;
    bool bss_term;
    bool disassoc_imminent;
    uint16_t disassoc_timer;
    struct {
        enum osw_btm_mbo_cell_preference cell_preference;
        enum osw_btm_mbo_reason reason;
    } mbo;
};

typedef void
osw_btm_obs_received_fn_t(void *priv,
                          const int response_code,
                          const struct osw_btm_retry_neigh *list,
                          size_t count);

osw_btm_obs_t *
osw_btm_obs_alloc(osw_btm_t *m);

void
osw_btm_obs_set_sta_addr(osw_btm_obs_t *obs,
                              const struct osw_hwaddr *addr);

void
osw_btm_obs_set_received_fn(osw_btm_obs_t *obs,
                            osw_btm_obs_received_fn_t *fn,
                            void *priv);

void
osw_btm_obs_drop(osw_btm_obs_t *obs);

osw_btm_sta_t *
osw_btm_sta_alloc(osw_btm_t *m,
                  const struct osw_hwaddr *addr);

void
osw_btm_sta_drop(osw_btm_sta_t *sta);

void
osw_btm_req_params_log(const struct osw_btm_req_params *params);

osw_btm_req_t  *
osw_btm_req_alloc(osw_btm_sta_t *sta);

void
osw_btm_req_drop(osw_btm_req_t *r);

bool
osw_btm_req_set_completed_fn(osw_btm_req_t *r, osw_btm_req_completed_fn_t *fn, void *priv);

bool
osw_btm_req_set_response_fn(osw_btm_req_t *r, osw_btm_req_response_fn_t *fn, void *priv);

bool
osw_btm_req_set_params(osw_btm_req_t *r,
                       const struct osw_btm_req_params *params);

bool
osw_btm_req_submit(osw_btm_req_t *r);

static inline osw_btm_t *
osw_btm(void)
{
    return OSW_MODULE_LOAD(osw_btm);
}

#endif /* OSW_BTM_H */
