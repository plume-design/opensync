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

#ifndef OSW_RRM_MEAS_H_INCLUDED
#define OSW_RRM_MEAS_H_INCLUDED

struct osw_rrm_meas_sta;
struct osw_rrm_meas_sta_observer;
struct osw_rrm_meas_desc;
struct osw_rrm_meas_req_params;
struct osw_rrm_meas_rep_neigh;

/* FIXME use desc and void*priv */
typedef
void osw_rrm_meas_req_tx_complete_fn_t(const struct osw_rrm_meas_sta_observer *observer);

typedef
void osw_rrm_meas_req_tx_error_fn_t(const struct osw_rrm_meas_sta_observer *observer);

struct osw_rrm_meas_sta_observer {
    osw_rrm_meas_req_tx_complete_fn_t * req_tx_complete_fn;
    osw_rrm_meas_req_tx_error_fn_t * req_tx_error_fn;
};

/* rrm measurement request parameters */
struct osw_rrm_meas_req_params {
    uint8_t op_class;
    uint8_t channel;
    struct osw_hwaddr bssid;
    struct osw_ssid ssid;
};

/* rrm measurement report neighbor */
struct osw_rrm_meas_rep_neigh {
    struct osw_hwaddr bssid;
    uint8_t op_class;
    uint8_t channel;
    uint8_t rcpi;
    uint64_t scan_start_time;

    uint64_t last_update_tstamp_nsec;
    ds_tree_node_t node;
};

void
osw_rrm_meas_init(void);

struct osw_rrm_meas_desc*
osw_rrm_meas_get_desc(const struct osw_hwaddr *sta_addr,
                      const struct osw_rrm_meas_sta_observer *observer,
                      const char *phy_name,
                      const char *vif_name);

bool
osw_rrm_meas_desc_set_req_params(struct osw_rrm_meas_desc *desc,
                                 const struct osw_rrm_meas_req_params *req_params);

struct osw_rrm_meas_sta*
osw_rrm_meas_desc_get_sta(struct osw_rrm_meas_desc *desc);

void
osw_rrm_meas_sta_set_throttle(struct osw_rrm_meas_sta *rrm_meas_sta,
                              struct osw_throttle *throttle);

const struct osw_rrm_meas_rep_neigh *
osw_rrm_meas_get_neigh(const struct osw_hwaddr *sta_addr,
                       const struct osw_hwaddr *bssid);

void
osw_rrm_meas_desc_free(struct osw_rrm_meas_desc *desc);

/* FIXME create function returning only neighbor list for a station.
 * Make sure a neighbor list returned is separate from sta as
 * otherwise it is going to be freed when freeing sta.
 * For now neigh_tree fetched from get_sta can be used as
 * long as sta is not freed.
 */

#endif /* OSW_RRM_MEAS_H_INCLUDED */
