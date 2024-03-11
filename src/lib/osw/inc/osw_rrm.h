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

#ifndef OSW_RRM_H
#define OSW_RRM_H

#include <osw_drv_common.h>

struct osw_rrm;
struct osw_rrm_desc_observer;
struct osw_rrm_rpt_observer;
struct osw_rrm_sta;
struct osw_rrm_desc;
struct osw_rrm_radio_meas_req;

enum osw_rrm_radio_status {
    OSW_RRM_RADIO_STATUS_SENT,
    OSW_RRM_RADIO_STATUS_REPLIED,
    OSW_RRM_RADIO_STATUS_TIMEOUT,
};

typedef void
osw_rrm_desc_radio_meas_req_status_fn_t(void *priv,
                                        enum osw_rrm_radio_status status,
                                        const uint8_t *dialog_token,
                                        const uint8_t *meas_token,
                                        const struct osw_rrm_radio_meas_req *radio_meas_req);

typedef void
osw_rrm_radio_rpt_bcn_meas_t(struct osw_rrm_rpt_observer *observer,
                             const struct osw_drv_dot11_frame_header *frame_header,
                             const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep,
                             const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed,
                             const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon);

enum osw_rrm_radio_meas_type {
    OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
};

struct osw_rrm_desc_observer {
    const char *name;
    osw_rrm_desc_radio_meas_req_status_fn_t *radio_meas_req_status_fn;

    struct ds_dlist_node node;
};

struct osw_rrm_rpt_observer {
    const char *name;
    osw_rrm_radio_rpt_bcn_meas_t *bcn_meas_fn;

    struct ds_dlist_node node;
};

struct osw_rrm_radio_meas_beacon_req {
    uint8_t op_class;
    uint8_t channel;
    struct osw_hwaddr bssid;
    struct osw_ssid ssid;
};

struct osw_rrm_radio_meas_req {
    enum osw_rrm_radio_meas_type type;
    union {
       struct osw_rrm_radio_meas_beacon_req beacon;
    } u;
};

struct osw_rrm_desc*
osw_rrm_get_desc(struct osw_rrm *rrm,
                 const struct osw_hwaddr *sta_addr,
                 struct osw_rrm_desc_observer *observer,
                 void *priv);

bool
osw_rrm_desc_schedule_radio_meas_req(struct osw_rrm_desc *desc,
                                     const struct osw_rrm_radio_meas_req *radio_meas_req);

void
osw_rrm_desc_free(struct osw_rrm_desc *desc);

void
osw_rrm_register_rpt_observer(struct osw_rrm *rrm,
                              struct osw_rrm_rpt_observer *observer);

void
osw_rrm_unregister_rpt_observer(struct osw_rrm *rrm,
                                struct osw_rrm_rpt_observer *observer);

#endif /* OSW_RRM_H */
