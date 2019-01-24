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

#ifndef __TARGET_BSAL_H__
#define __TAREGT_BSAL_H__

/*****************************************************************************/

#define BSAL_IFNAME_LEN         17
#define BSAL_MAC_ADDR_LEN       6

#define BSAL_MAX_TM_NEIGHBORS   3

/*****************************************************************************/

typedef enum {
    BSAL_BAND_24G               = 0,
    BSAL_BAND_5G,
    BSAL_BAND_COUNT
} bsal_band_t;

/*****************************************************************************/

typedef struct {
    char                    ifname[BSAL_IFNAME_LEN];

    uint8_t                 chan_util_check_sec;
    uint8_t                 chan_util_avg_count;

    uint8_t                 inact_check_sec;
    uint8_t                 inact_tmout_sec_normal;
    uint8_t                 inact_tmout_sec_overload;

    uint8_t                 def_rssi_inact_xing;
    uint8_t                 def_rssi_low_xing;
    uint8_t                 def_rssi_xing;

    struct {
        bool                raw_chan_util;
        bool                raw_rssi;
    } debug;
} bsal_ifconfig_t;

typedef struct {
    bool                    blacklist;
    uint8_t                 rssi_probe_hwm;
    uint8_t                 rssi_probe_lwm;
    uint8_t                 rssi_auth_hwm;
    uint8_t                 rssi_auth_lwm;
    uint8_t                 rssi_inact_xing;
    uint8_t                 rssi_high_xing;
    uint8_t                 rssi_low_xing;
    uint8_t                 auth_reject_reason;
} bsal_client_config_t;

typedef struct {
    uint8_t                 bssid[BSAL_MAC_ADDR_LEN];

    uint32_t                bssid_info;
    uint8_t                 op_class;
    uint8_t                 channel;
    uint8_t                 phy_type;
} bsal_neigh_info_t;

typedef struct {
    bsal_neigh_info_t       neigh[BSAL_MAX_TM_NEIGHBORS];

    int                     num_neigh;
    uint8_t                 valid_int;
    uint8_t                 abridged;
    uint8_t                 pref;
    uint8_t                 disassoc_imminent;
    uint16_t                bss_term;
    int                     num_retries;
    int                     max_retries;
    int                     retry_interval;
    bool                    inc_neigh;
} bsal_btm_params_t;

typedef struct {
    uint8_t                 op_class;
    uint8_t                 channel;
    uint8_t                 rand_ivl;
    uint8_t                 meas_dur;
    uint8_t                 meas_mode;
    uint8_t                 req_ssid;
    uint8_t                 rep_cond;
    uint8_t                 rpt_detail;
    uint8_t                 req_ie;
    uint8_t                 chanrpt_mode;
} bsal_rrm_params_t;

/*****************************************************************************/

typedef enum {
    BSAL_EVENT_PROBE_REQ        = 1,
    BSAL_EVENT_CLIENT_CONNECT,
    BSAL_EVENT_CLIENT_DISCONNECT,
    BSAL_EVENT_CLIENT_ACTIVITY,
    BSAL_EVENT_CHAN_UTILIZATION,
    BSAL_EVENT_RSSI_XING,
    BSAL_EVENT_RSSI,
    BSAL_EVENT_STEER_CLIENT,
    BSAL_EVENT_STEER_SUCCESS,
    BSAL_EVENT_STEER_FAILURE,
    BSAL_EVENT_AUTH_FAIL,

    BSAL_EVENT_DEBUG_CHAN_UTIL  = 128,
    BSAL_EVENT_DEBUG_RSSI
} bsal_ev_type_t;

typedef enum {
    BSAL_DISC_SOURCE_LOCAL      = 0,
    BSAL_DISC_SOURCE_REMOTE
} bsal_disc_source_t;

typedef enum {
    BSAL_DISC_TYPE_DISASSOC     = 0,
    BSAL_DISC_TYPE_DEAUTH
} bsal_disc_type_t;

typedef enum {
    BSAL_RSSI_UNCHANGED         = 0,
    BSAL_RSSI_HIGHER,
    BSAL_RSSI_LOWER
} bsal_rssi_change_t;

typedef struct {
    uint8_t                 max_chwidth;
    uint8_t                 max_streams;
    uint8_t                 phy_mode;
    uint8_t                 max_MCS;
    uint8_t                 max_txpower;
    uint8_t                 is_static_smps;
    uint8_t                 is_mu_mimo_supported;
} bsal_datarate_info_t;

typedef struct {
    bool                    link_meas;
    bool                    neigh_rpt;
    bool                    bcn_rpt_passive;
    bool                    bcn_rpt_active;
    bool                    bcn_rpt_table;
    bool                    lci_meas;
    bool                    ftm_range_rpt;
} bsal_rrm_caps_t;

typedef struct {
    uint8_t                 client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t                 rssi;
    bool                    ssid_null;
    bool                    blocked;
} bsal_ev_probe_req_t;

typedef struct {
    uint8_t                 client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t                 is_BTM_supported;
    uint8_t                 is_RRM_supported;
    bool                    band_cap_2G;
    bool                    band_cap_5G;
    bsal_datarate_info_t    datarate_info;
    bsal_rrm_caps_t         rrm_caps;
} bsal_ev_connect_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t             reason;
    bsal_disc_source_t  source;
    bsal_disc_type_t    type;
} bsal_ev_disconnect_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    bool                active;
} bsal_ev_activity_t;

typedef struct {
    uint8_t             utilization;
} bsal_ev_chan_util_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t             rssi;
    bsal_rssi_change_t  inact_xing;
    bsal_rssi_change_t  high_xing;
    bsal_rssi_change_t  low_xing;
} bsal_ev_rssi_xing_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t             rssi;
} bsal_ev_rssi_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    int                 from_ch;
    int                 to_ch;
    uint8_t             rssi;
    bool                connected;
} bsal_ev_steer_t;

typedef struct {
    uint8_t             client_addr[BSAL_MAC_ADDR_LEN];
    uint8_t             rssi;
    uint8_t             reason;
    uint8_t             bs_blocked;
    uint8_t             bs_rejected;
} bsal_ev_auth_fail_t;

typedef struct {
    bsal_ev_type_t      type;
    char                ifname[BSAL_IFNAME_LEN];
    bsal_band_t         band;
    uint64_t            timestamp_ms;
    union {
        bsal_ev_probe_req_t         probe_req;
        bsal_ev_connect_t           connect;
        bsal_ev_disconnect_t        disconnect;
        bsal_ev_activity_t          activity;
        bsal_ev_chan_util_t         chan_util;
        bsal_ev_rssi_xing_t         rssi_change;
        bsal_ev_rssi_t              rssi;
        bsal_ev_steer_t             steer;
        bsal_ev_auth_fail_t         auth_fail;
    } data;
} bsal_event_t;

/*****************************************************************************/

typedef void (*bsal_event_cb_t)(bsal_event_t *event);

/*****************************************************************************/

int     target_bsal_init(bsal_event_cb_t event_cb, struct ev_loop* loop);
int     target_bsal_cleanup(void);

int	target_bsal_iface_add(const bsal_ifconfig_t *ifcfg);
int	target_bsal_iface_update(const bsal_ifconfig_t *ifcfg);
int	target_bsal_iface_remove(const bsal_ifconfig_t *ifcfg);

int     target_bsal_client_add(const char *ifname, const uint8_t *mac_addr, const bsal_client_config_t *conf);
int     target_bsal_client_update(const char *ifname, const uint8_t *mac_addr, const bsal_client_config_t *conf);
int     target_bsal_client_remove(const char *ifname, const uint8_t *mac_addr);

int     target_bsal_client_measure(const char *ifname, const uint8_t *mac_addr, int num_samples);
int     target_bsal_client_disconnect(const char *ifname, const uint8_t *mac_addr, bsal_disc_type_t type, uint8_t reason);
int     target_bsal_client_is_connected(const char *ifname, const uint8_t *mac_addr);

int     target_bsal_bss_tm_request(const char *ifname, const uint8_t *mac_addr, const bsal_btm_params_t *btm_params);
int     target_bsal_rrm_beacon_report_request(const char *ifname, const uint8_t *mac_addr, const bsal_rrm_params_t *rrm_params);

bool    target_client_disconnect(const char *ifname, const char *disc_type, const char *mac_str, uint8_t reason );

#endif /* __TARGET_BSAL_H__ */
