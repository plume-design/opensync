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

#ifndef OSW_DRV_COMMON_H
#define OSW_DRV_COMMON_H

#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define OSW_DRV_FRAME_TX_DESC_BUF_SIZE 4096

#define DOT11_RRM_MEAS_REP_IE_CATEGORY_CODE 0x05
#define DOT11_RRM_MEAS_REQ_IE_ACTION_CODE 0x00
#define DOT11_RRM_MEAS_REP_IE_ACTION_CODE 0x01
#define DOT11_RRM_MEAS_REP_IE_TAG 0x27
#define DOT11_RRM_MEAS_REP_IE_REP_MODE_REFUSED_MSK 0x04

struct osw_drv;
struct osw_drv_frame;
struct osw_drv_frame_tx_desc;

enum osw_frame_tx_result {
    OSW_FRAME_TX_RESULT_SUBMITTED,
    OSW_FRAME_TX_RESULT_FAILED,
    OSW_FRAME_TX_RESULT_DROPPED,
};

typedef void
osw_drv_frame_tx_result_fn_t(const struct osw_drv_frame_tx_desc *desc,
                             enum osw_frame_tx_result result,
                             void *caller_priv);

struct osw_drv_dot11_frame_header {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t da[6];
    uint8_t sa[6];
    uint8_t bssid[6];
    uint16_t seq_ctrl;
} __attribute__((__packed__));

struct osw_drv_dot11_frame_action_bss_tm_req {
    uint8_t action;
    uint8_t dialog_token;
    uint8_t options;
    uint16_t disassoc_timer;
    uint8_t validity_interval;
    /*
     * Variable and optional IE:
     * - BSS Termination Duration
     * - Session Information URL
     * - BSS Transition Candidate List Entries
     */
    uint8_t variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_frame_action_bss_tm_resp {
    uint8_t action;
    uint8_t dialog_token;
    uint8_t status_code;
    uint8_t bss_termination_delay;
    /*
     * Variable and optional IE:
     * - Target BSSID
     * - BSS Transition Candidate List Entries
     */
    uint8_t variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_frame_action_rrm_meas_req {
    uint8_t action;
    uint8_t dialog_token;
    uint16_t repetitions;
    /*
     * Variable and required IE:
     * - Measurement Request IE (>=1)
     */
    uint8_t variable[];
} __attribute__((__packed__));


struct osw_drv_dot11_frame_action_rrm_meas_rep {
    uint8_t action;
    uint8_t dialog_token;
    uint8_t variable[];
} __attribute__((__packed__));


struct osw_drv_dot11_frame_action {
    uint8_t category;
    union {
        struct osw_drv_dot11_frame_action_bss_tm_req bss_tm_req;
        struct osw_drv_dot11_frame_action_bss_tm_resp bss_tm_resp;
        struct osw_drv_dot11_frame_action_rrm_meas_req rrm_meas_req;
        struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep;
    } u;
} __attribute__((__packed__));

struct osw_drv_dot11_frame {
    struct osw_drv_dot11_frame_header header;
    union {
        struct osw_drv_dot11_frame_action action;
    } u;
} __attribute__((__packed__));

struct osw_drv_dot11_neighbor_report {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t bssid[6];
    uint32_t bssid_info;
    uint8_t op_class;
    uint8_t channel;
    uint8_t phy_type;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_req_ie {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t token;
    uint8_t req_mode;
    uint8_t req_type;
    uint8_t op_class;
    uint8_t channel;
    uint16_t rand_interval;
    uint16_t duration;
    uint8_t meas_mode;
    uint8_t bssid[6];
    /*
     * SubElements
     * - SSID
     * - Beacon Reporting Information
     * - Reporting Detail
     */
    uint8_t variable[];
} __attribute__((__packed__));

/* SSID SubElement  */
struct osw_drv_dot11_meas_req_ie_subel_ssid {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t ssid[];
} __attribute__((__packed__));

/* Beacon Reporting Information SubElement */
struct osw_drv_dot11_meas_req_ie_subel_bri {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t reporting_condition;
    /* Threshold / Offset*/
    uint8_t thr_offs;
} __attribute__((__packed__));

/* Reporting Detail */
struct osw_drv_dot11_meas_req_ie_subel_rep_det {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t reporting_detail;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_rep_ie {
    uint8_t tag;
    uint8_t tag_len;
    uint8_t token;
    uint8_t report_mode;
    uint8_t report_type;
    uint8_t op_class;
    uint8_t channel;
    uint64_t start_time;
    uint16_t duration;
    uint8_t frame_info;
    uint8_t rcpi;
    uint8_t rsni;
    uint8_t bssid[6];
    uint8_t antenna_id;
    uint32_t parent_tsf;
    /*
     * SubElements
     * - Reported Frame Body Fragment ID
     */
    uint8_t variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_meas_rep_ie_subel_rep_frag_id {
    uint8_t tag;
    uint8_t tag_len;
    uint16_t fragment_id;
} __attribute__((__packed__));

const char*
osw_frame_tx_result_to_cstr(enum osw_frame_tx_result result);

const uint8_t*
osw_drv_frame_tx_desc_get_frame(const struct osw_drv_frame_tx_desc *desc);

size_t
osw_drv_frame_tx_desc_get_frame_len(const struct osw_drv_frame_tx_desc *desc);

bool
osw_drv_frame_tx_desc_has_channel(const struct osw_drv_frame_tx_desc *desc);

const struct osw_channel *
osw_drv_frame_tx_desc_get_channel(const struct osw_drv_frame_tx_desc *desc);

#endif /* OSW_DRV_COMMON_H */
