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

#include <osw_throttle.h>

#define OSW_BTM_REQ_NEIGH_SIZE 16 /* TODO Check! */

struct osw_btm_sta_observer;
struct osw_btm_sta;
struct osw_btm_desc;
struct osw_btm_req_neigh;
struct osw_btm_req_params;

typedef void
osw_btm_req_tx_complete_fn_t(struct osw_btm_sta_observer *observer);

typedef void
osw_btm_req_tx_error_fn_t(struct osw_btm_sta_observer *observer);

struct osw_btm_sta_observer {
    osw_btm_req_tx_complete_fn_t *req_tx_complete_fn;
    osw_btm_req_tx_error_fn_t *req_tx_error_fn;
};

struct osw_btm_req_neigh {
    struct osw_hwaddr bssid;
    uint32_t bssid_info;
    uint8_t op_class;
    uint8_t channel;
    uint8_t phy_type;
};

struct osw_btm_retry_neigh {
    struct osw_btm_req_neigh neigh;
    int preference;
};

struct osw_btm_retry_neigh_list {
    struct osw_btm_retry_neigh neigh[OSW_BTM_REQ_NEIGH_SIZE];
    unsigned int neigh_len;
};

struct osw_btm_response_observer;

typedef void
osw_btm_response_fn_t(struct osw_btm_response_observer *observer,
                      const int response_code,
                      const struct osw_btm_retry_neigh_list *retry_neigh_list);

struct osw_btm_req_params {
    struct osw_btm_req_neigh neigh[OSW_BTM_REQ_NEIGH_SIZE];
    size_t neigh_len;
    uint8_t valid_int;
    bool abridged;
    bool disassoc_imminent;
    bool bss_term;
};

struct osw_btm_response_observer {
    struct ds_dlist_node node;
    struct osw_hwaddr sta_addr;
    osw_btm_response_fn_t *btm_response_fn;
};

void
osw_btm_register_btm_response_observer(struct osw_btm_response_observer *observer);

void
osw_btm_unregister_btm_response_observer(struct osw_btm_response_observer *observer);

struct osw_btm_desc*
osw_btm_get_desc(const struct osw_hwaddr *sta_addr,
                 struct osw_btm_sta_observer *observer);

void
osw_btm_desc_free(struct osw_btm_desc *desc);

bool
osw_btm_desc_set_req_params(struct osw_btm_desc *desc,
                            const struct osw_btm_req_params *req_params);

struct osw_btm_sta*
osw_btm_desc_get_sta(struct osw_btm_desc *desc);

void
osw_btm_sta_set_throttle(struct osw_btm_sta *btm_sta,
                         struct osw_throttle *throttle);

#endif /* OSW_BTM_H */
