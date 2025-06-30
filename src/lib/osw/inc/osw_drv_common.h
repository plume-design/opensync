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

#define DOT11_FRAME_CTRL_TYPE_MASK 0x000C
#define DOT11_FRAME_CTRL_TYPE_MGMT 0x0000
#define DOT11_FRAME_CTRL_SUBTYPE_MASK 0x00F0
#define DOT11_FRAME_CTRL_SUBTYPE_ASSOC_REQ 0x0000
#define DOT11_FRAME_CTRL_SUBTYPE_REASSOC_REQ 0x0020
#define DOT11_FRAME_CTRL_SUBTYPE_PROBE_REQ 0x0040
#define DOT11_FRAME_CTRL_SUBTYPE_AUTH 0x00B0
#define DOT11_FRAME_CTRL_SUBTYPE_ACTION 0x00D0
#define DOT11_FRAME_CTRL_SUBTYPE_DEAUTH 0x00C0

#define DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE 0x05
#define DOT11_ACTION_CATEGORY_WNM_CODE 0x0A

#define DOT11_RRM_MEAS_REQ_IE_ACTION_CODE 0x00
#define DOT11_RRM_MEAS_REQ_IE_ELEM_ID 0x26
#define DOT11_RRM_MEAS_REQ_IE_TYPE_BEACON 0x05
#define DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_MODE_ACTIVE 0x01
#define DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_SSID 0x00
#define DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_BEACON_REPORTING 0x01
#define DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_REPORTING_DETAIL 0x02

#define DOT11_RRM_MEAS_REP_IE_ACTION_CODE 0x01
#define DOT11_RRM_MEAS_REP_IE_ELEM_ID 0x27
#define DOT11_RRM_MEAS_REP_IE_TYPE_BEACON 0x05
#define DOT11_RRM_MEAS_REP_IE_REP_MODE_REFUSED_MSK 0x04

#define DOT11_BTM_QUERY_IE_ACTION_CODE 0x06
#define DOT11_BTM_REQUEST_IE_ACTION_CODE 0x07
#define DOT11_BTM_RESPONSE_IE_ACTION_CODE 0x08
#define DOT11_BTM_RESPONSE_CODE_REJECT_CAND_LIST_PROVIDED 6
#define DOT11_NEIGHBOR_REPORT_IE_TAG 0x34
#define DOT11_NEIGHBOR_REPORT_CANDIDATE_PREFERENCE 0x03

#define DOT11_WNM_NOTIF_REQUEST_IE_ACTION_CODE 0x1A
#define DOT11_WNM_NOTIF_REQUEST_TYPE_VENDOR_SPECIFIC 0xDD

#define DOT11_VENDOR_SPECIFIC_IE_WFA_MBO_OCE 0x16
#define WFA_MBO_ATTR_ID_NON_PREF_CHAN_REP 0x02
#define WFA_MBO_ATTR_ID_CELL_DATA_PREF 0x03

struct osw_drv;
struct osw_drv_frame;
struct osw_drv_frame_tx_desc;

enum osw_frame_tx_result {
    OSW_FRAME_TX_RESULT_SUBMITTED,
    OSW_FRAME_TX_RESULT_FAILED,
    OSW_FRAME_TX_RESULT_DROPPED,
};

typedef void
osw_drv_frame_tx_result_fn_t(struct osw_drv_frame_tx_desc *desc,
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

struct osw_drv_dot11_frame_action_wnm_req {
    uint8_t action;
    uint8_t dialog_token;
    uint8_t type;
    /*
     * Optional subelements
     */
    uint8_t variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_frame_action {
    uint8_t category;
    union {
        struct osw_drv_dot11_frame_action_bss_tm_req bss_tm_req;
        struct osw_drv_dot11_frame_action_bss_tm_resp bss_tm_resp;
        struct osw_drv_dot11_frame_action_rrm_meas_req rrm_meas_req;
        struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep;
        struct osw_drv_dot11_frame_action_wnm_req wnm_notif_req;
    } u;
} __attribute__((__packed__));

struct osw_drv_dot11_frame_deauth {
    uint16_t reason_code;
    char variable[0];
} __attribute__((__packed__));

struct osw_drv_dot11_frame_assoc_req {
    uint16_t capability_info;
    uint16_t listen_interval;
    char variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_frame_reassoc_req {
    uint16_t capability_info;
    uint16_t listen_interval;
    char bssid[6];
    char variable[];
} __attribute__((__packed__));

#define ieee80211_frame_into_header(buf, len, rem) \
    C_FIELD_GET_REM(buf, len, rem, struct osw_drv_dot11_frame, header)

#define ieee80211_frame_into_variable(buf, len, rem) \
    C_FIELD_GET_REM(buf, len, rem, struct osw_drv_dot11_frame, u.variable)

#define ieee80211_frame_into_assoc_req(buf, len, rem) \
    C_FIELD_GET_REM(buf, len, rem, struct osw_drv_dot11_frame, u.assoc_req)

#define ieee80211_frame_into_reassoc_req(buf, len, rem) \
    C_FIELD_GET_REM(buf, len, rem, struct osw_drv_dot11_frame, u.reassoc_req)

struct osw_drv_dot11_frame {
    struct osw_drv_dot11_frame_header header;
    union {
        struct osw_drv_dot11_frame_assoc_req assoc_req;
        struct osw_drv_dot11_frame_reassoc_req reassoc_req;
        struct osw_drv_dot11_frame_action action;
        struct osw_drv_dot11_frame_deauth deauth;
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
    /*
     * SubElements
     * - Neighbor Preference
     */
    uint8_t variable[];
} __attribute__((__packed__));

struct osw_drv_dot11_neighbor_preference {
    uint8_t eid;
    uint8_t len;
    uint8_t preference;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_req_beacon {
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

struct osw_drv_dot11_meas_req_ie_header {
    uint8_t tag;
    uint8_t tag_len;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_req_ie_fixed {
    uint8_t token;
    uint8_t req_mode;
    uint8_t req_type;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_req_ie {
    struct osw_drv_dot11_meas_req_ie_header h;
    struct osw_drv_dot11_meas_req_ie_fixed f;
    union {
        struct osw_drv_dot11_meas_req_beacon beacon;
    } u;
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

struct osw_drv_dot11_meas_rep_ie_header {
    uint8_t tag;
    uint8_t tag_len;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_rep_ie_fixed {
    uint8_t token;
    uint8_t report_mode;
    uint8_t report_type;
} __attribute__((__packed__));

struct osw_drv_dot11_meas_rpt_ie_beacon {
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

struct osw_drv_dot11_meas_rep_ie {
    struct osw_drv_dot11_meas_rep_ie_header h;
    struct osw_drv_dot11_meas_rep_ie_fixed f;
    union {
        struct osw_drv_dot11_meas_rpt_ie_beacon beacon;
    } u;
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
