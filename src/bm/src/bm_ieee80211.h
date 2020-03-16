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

#ifndef BM_IEEE80211_H_INCLUDED
#define BM_IEEE80211_H_INCLUDED

#include <linux/types.h>

#define STRUCT_PACKED __attribute__ ((packed))

/* Types we will use in headers */
typedef __u8 u8;
typedef __u16 u16;
typedef __le16 le16;
typedef __le32 le32;
typedef __le64 le64;

#define IEEE80211_HDRLEN (sizeof(struct ieee80211_hdr))

struct element {
        u8 id;
        u8 datalen;
        u8 data[];
} STRUCT_PACKED;

/* element iteration helpers */
#define for_each_element(_elem, _data, _datalen)                        \
        for (_elem = (const struct element *) (_data);                  \
             (const u8 *) (_data) + (_datalen) - (const u8 *) _elem >=  \
                (int) sizeof(*_elem) &&                                 \
             (const u8 *) (_data) + (_datalen) - (const u8 *) _elem >=  \
                (int) sizeof(*_elem) + _elem->datalen;                  \
             _elem = (const struct element *) (_elem->data + _elem->datalen))

#define for_each_element_id(element, _id, data, datalen)                \
        for_each_element(element, data, datalen)                        \
                if (element->id == (_id))

/* Action frame categories (IEEE Std 802.11-2016, 9.4.1.11, Table 9-76) */
#define WLAN_ACTION_SPECTRUM_MGMT 0
#define WLAN_ACTION_QOS 1
#define WLAN_ACTION_DLS 2
#define WLAN_ACTION_BLOCK_ACK 3
#define WLAN_ACTION_PUBLIC 4
#define WLAN_ACTION_RADIO_MEASUREMENT 5
#define WLAN_ACTION_FT 6
#define WLAN_ACTION_HT 7
#define WLAN_ACTION_SA_QUERY 8
#define WLAN_ACTION_PROTECTED_DUAL 9
#define WLAN_ACTION_WNM 10
#define WLAN_ACTION_UNPROTECTED_WNM 11
#define WLAN_ACTION_TDLS 12
#define WLAN_ACTION_MESH 13
#define WLAN_ACTION_MULTIHOP 14
#define WLAN_ACTION_SELF_PROTECTED 15
#define WLAN_ACTION_DMG 16
#define WLAN_ACTION_WMM 17 /* WMM Specification 1.1 */
#define WLAN_ACTION_FST 18
#define WLAN_ACTION_ROBUST_AV_STREAMING 19
#define WLAN_ACTION_UNPROTECTED_DMG 20
#define WLAN_ACTION_VHT 21
#define WLAN_ACTION_FILS 26
#define WLAN_ACTION_VENDOR_SPECIFIC_PROTECTED 126
#define WLAN_ACTION_VENDOR_SPECIFIC 127

/* IEEE 802.11v - WNM Action field values */
enum wnm_action {
        WNM_EVENT_REQ = 0,
        WNM_EVENT_REPORT = 1,
        WNM_DIAGNOSTIC_REQ = 2,
        WNM_DIAGNOSTIC_REPORT = 3,
        WNM_LOCATION_CFG_REQ = 4,
        WNM_LOCATION_CFG_RESP = 5,
        WNM_BSS_TRANS_MGMT_QUERY = 6,
        WNM_BSS_TRANS_MGMT_REQ = 7,
        WNM_BSS_TRANS_MGMT_RESP = 8,
        WNM_FMS_REQ = 9,
        WNM_FMS_RESP = 10,
        WNM_COLLOCATED_INTERFERENCE_REQ = 11,
        WNM_COLLOCATED_INTERFERENCE_REPORT = 12,
        WNM_TFS_REQ = 13,
        WNM_TFS_RESP = 14,
        WNM_TFS_NOTIFY = 15,
        WNM_SLEEP_MODE_REQ = 16,
        WNM_SLEEP_MODE_RESP = 17,
        WNM_TIM_BROADCAST_REQ = 18,
        WNM_TIM_BROADCAST_RESP = 19,
        WNM_QOS_TRAFFIC_CAPAB_UPDATE = 20,
        WNM_CHANNEL_USAGE_REQ = 21,
        WNM_CHANNEL_USAGE_RESP = 22,
        WNM_DMS_REQ = 23,
        WNM_DMS_RESP = 24,
        WNM_TIMING_MEASUREMENT_REQ = 25,
        WNM_NOTIFICATION_REQ = 26,
        WNM_NOTIFICATION_RESP = 27
};

/* IEEE Std 802.11-2012 - Table 8-253 */
enum bss_trans_mgmt_status_code {
        WNM_BSS_TM_ACCEPT = 0,
        WNM_BSS_TM_REJECT_UNSPECIFIED = 1,
        WNM_BSS_TM_REJECT_INSUFFICIENT_BEACON = 2,
        WNM_BSS_TM_REJECT_INSUFFICIENT_CAPABITY = 3,
        WNM_BSS_TM_REJECT_UNDESIRED = 4,
        WNM_BSS_TM_REJECT_DELAY_REQUEST = 5,
        WNM_BSS_TM_REJECT_STA_CANDIDATE_LIST_PROVIDED = 6,
        WNM_BSS_TM_REJECT_NO_SUITABLE_CANDIDATES = 7,
        WNM_BSS_TM_REJECT_LEAVING_ESS = 8
};

/*
 * IEEE P802.11-REVmc/D5.0 Table 9-81 - Measurement type definitions for
 * measurement requests
 */
enum measure_type {
        MEASURE_TYPE_BASIC = 0,
        MEASURE_TYPE_CCA = 1,
        MEASURE_TYPE_RPI_HIST = 2,
        MEASURE_TYPE_CHANNEL_LOAD = 3,
        MEASURE_TYPE_NOISE_HIST = 4,
        MEASURE_TYPE_BEACON = 5,
        MEASURE_TYPE_FRAME = 6,
        MEASURE_TYPE_STA_STATISTICS = 7,
        MEASURE_TYPE_LCI = 8,
        MEASURE_TYPE_TRANSMIT_STREAM = 9,
        MEASURE_TYPE_MULTICAST_DIAG = 10,
        MEASURE_TYPE_LOCATION_CIVIC = 11,
        MEASURE_TYPE_LOCATION_ID = 12,
        MEASURE_TYPE_DIRECTIONAL_CHAN_QUALITY = 13,
        MEASURE_TYPE_DIRECTIONAL_MEASURE = 14,
        MEASURE_TYPE_DIRECTIONAL_STATS = 15,
        MEASURE_TYPE_FTM_RANGE = 16,
        MEASURE_TYPE_MEASURE_PAUSE = 255,
};

/* Radio Measurement Action codes */
#define WLAN_RRM_RADIO_MEASUREMENT_REQUEST 0
#define WLAN_RRM_RADIO_MEASUREMENT_REPORT 1
#define WLAN_RRM_LINK_MEASUREMENT_REQUEST 2
#define WLAN_RRM_LINK_MEASUREMENT_REPORT 3
#define WLAN_RRM_NEIGHBOR_REPORT_REQUEST 4
#define WLAN_RRM_NEIGHBOR_REPORT_RESPONSE 5

struct supp_chan {
    uint8_t first;
    uint8_t range;
};

#define WLAN_EID_SUPPORTED_CHANNELS 36
#define WLAN_EID_MEASURE_REPORT 39
#define WLAN_EID_SUPPORTED_OPERATING_CLASSES 59

/* IEEE Std 802.11-2016, 9.4.2.22 - Measurement Report element */
struct rrm_measurement_report_element {
        u8 eid; /* Element ID */
        u8 len; /* Length */
        u8 token; /* Measurement Token */
        u8 mode; /* Measurement Report Mode */
        u8 type; /* Measurement Type */
        u8 variable[0]; /* Measurement Report */
} STRUCT_PACKED;

/* IEEE Std 802.11-2016, Figure 9-192 - Measurement Report Mode field */
#define MEASUREMENT_REPORT_MODE_ACCEPT 0
#define MEASUREMENT_REPORT_MODE_REJECT_LATE BIT(0)
#define MEASUREMENT_REPORT_MODE_REJECT_INCAPABLE BIT(1)
#define MEASUREMENT_REPORT_MODE_REJECT_REFUSED BIT(2)

/* IEEE Std 802.11-2016, 9.4.2.22.7 - Beacon report */
struct rrm_measurement_beacon_report {
        u8 op_class; /* Operating Class */
        u8 channel; /* Channel Number */
        le64 start_time; /* Actual Measurement Start Time
                          * (in TSF of the BSS requesting the measurement) */
        le16 duration; /* in TUs */
        u8 report_info; /* Reported Frame Information */
        u8 rcpi; /* RCPI */
        u8 rsni; /* RSNI */
        u8 bssid[ETH_ALEN]; /* BSSID */
        u8 antenna_id; /* Antenna ID */
        le32 parent_tsf; /* Parent TSF */
        u8 variable[0]; /* Optional Subelements */
} STRUCT_PACKED;


struct ieee80211_hdr {
        le16 frame_control;
        le16 duration_id;
        u8 addr1[6];
        u8 addr2[6];
        u8 addr3[6];
        le16 seq_ctrl;
        /* followed by 'u8 addr4[6];' if ToDS and FromDS is set in data frame
         */
} STRUCT_PACKED;

struct ieee80211_mgmt {
        le16 frame_control;
        le16 duration;
        u8 da[6];
        u8 sa[6];
        u8 bssid[6];
        le16 seq_ctrl;
        union {
                struct {
                        u8 category;
                        union {
                                struct {
                                        u8 action_code;
                                        u8 dialog_token;
                                        u8 status_code;
                                        u8 variable[];
                                } STRUCT_PACKED wmm_action;
                                struct {
                                        u8 action;
                                        u8 sta_addr[ETH_ALEN];
                                        u8 target_ap_addr[ETH_ALEN];
                                        u8 variable[]; /* FT Request */
                                } STRUCT_PACKED ft_action_req;
                                struct {
                                        u8 action;
                                        u8 sta_addr[ETH_ALEN];
                                        u8 target_ap_addr[ETH_ALEN];
                                        le16 status_code;
                                        u8 variable[]; /* FT Request */
                                } STRUCT_PACKED ft_action_resp;
                                struct {
                                        u8 action;
                                        u8 dialogtoken;
                                        u8 variable[];
                                } STRUCT_PACKED wnm_sleep_req;
                                struct {
                                        u8 action;
                                        u8 dialogtoken;
                                        le16 keydata_len;
                                        u8 variable[];
                                } STRUCT_PACKED wnm_sleep_resp;
                                struct {
                                        u8 action;
                                        u8 variable[];
                                } STRUCT_PACKED public_action;
                                struct {
                                        u8 action; /* 9 */
                                        u8 oui[3];
                                        /* Vendor-specific content */
                                        u8 variable[];
                                } STRUCT_PACKED vs_public_action;
                                struct {
                                        u8 action; /* 7 */
                                        u8 dialog_token;
                                        u8 req_mode;
                                        le16 disassoc_timer;
                                        u8 validity_interval;
                                        /* BSS Termination Duration (optional),
                                         * Session Information URL (optional),
                                         * BSS Transition Candidate List
                                         * Entries */
                                        u8 variable[];
                                } STRUCT_PACKED bss_tm_req;
                                struct {
                                        u8 action; /* 8 */
                                        u8 dialog_token;
                                        u8 status_code;
                                        u8 bss_termination_delay;
                                        /* Target BSSID (optional),
                                         * BSS Transition Candidate List
                                         * Entries (optional) */
                                        u8 variable[];
                                } STRUCT_PACKED bss_tm_resp;
                                struct {
                                        u8 action; /* 6 */
                                        u8 dialog_token;
                                        u8 query_reason;
                                        /* BSS Transition Candidate List
                                         * Entries (optional) */
                                        u8 variable[];
                                } STRUCT_PACKED bss_tm_query;
                                struct {
                                        u8 action;
                                        u8 dialog_token;
                                        u8 variable[];
                                } STRUCT_PACKED rrm;
                        } u;
                } STRUCT_PACKED action;
        } u;
} STRUCT_PACKED;

#endif /* BM_IEEE80211_H_INCLUDED */
