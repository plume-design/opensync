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

/*
 * Band Steering Manager - Clients
 */
#ifndef BM_CLIENT_H_INCLUDED
#define BM_CLIENT_H_INCLUDED

#ifndef OVSDB_UUID_LEN
#define OVSDB_UUID_LEN                      37
#endif /* OVSDB_UUID_LEN */

#ifndef MAC_STR_LEN
#define MAC_STR_LEN                         18
#endif /* MAC_STR_LEN */

#define BM_CLIENT_MIN_HWM                   1
#define BM_CLIENT_MAX_HWM                   128

#define BM_CLIENT_MIN_LWM                   1
#define BM_CLIENT_MAX_LWM                   128

#define BM_CLIENT_ROGUE_SNR_LEVEL           5
#define BM_CLIENT_RSSI_HYSTERESIS           2

#define BTM_DEFAULT_VALID_INT               255 // TBTT count
#define BTM_DEFAULT_ABRIDGED                1   // Yes
#define BTM_DEFAULT_PREF                    1   // Yes
#define BTM_DEFAULT_DISASSOC_IMMINENT       1   // Yes
#define BTM_DEFAULT_BSS_TERM                0   // Disabled
#define BTM_DEFAULT_NEIGH_BSS_INFO          0x8f  // Reachable, secure, key scope

#define BM_CLIENT_MAX_TM_NEIGHBORS          3

#define BTM_DEFAULT_MAX_RETRIES             3
#define BTM_DEFAULT_RETRY_INTERVAL          10  // In seconds

#define RRM_BCN_RPT_DEFAULT_SCAN_INTERVAL   1   // In seconds

#define BM_CLIENT_DEFAULT_BACKOFF_EXP_BASE 2  // In seconds

#define BM_CLIENT_DEFAULT_STEER_DURING_BACKOFF false
#define BM_CLIENT_DEFAULT_PREQ_SNR_THR 0

#define BM_CLIENT_PREQ_SNR_TH 3
#define BM_CLIENT_PREQ_TIME_TH 3

#define BM_CLIENT_SNR_XING_DIFF         5

#define BM_CLIENT_STICKY_KICK_GUARD_TIME    30
#define BM_CLIENT_STEERING_KICK_GUARD_TIME  30
#define BM_CLIENT_STICKY_KICK_BACKOFF_TIME    120
#define BM_CLIENT_STEERING_KICK_BACKOFF_TIME  120
#define BM_CLIENT_SETTLING_BACKOFF_TIME 0

#define BM_CLIENT_D_EVENTS_MAX 128

#define BM_CLIENT_RRM_NEIGHBOR_MAX 16
#define BM_CLIENT_RRM_REQ_MAX   8
#define BM_CLIENT_RRM_ACTIVE_MEASUREMENT_DURATION 30
#define BM_CLIENT_RRM_PASIVE_MEASUREMENT_DURATION 100

#define BM_CLIENT_DEFAULT_ACTIVITY_BPS_TH 2000

/*****************************************************************************/

typedef enum {
    BM_CLIENT_KICK_NONE             = 0,
    BM_CLIENT_KICK_DISASSOC,
    BM_CLIENT_KICK_DEAUTH,
    BM_CLIENT_KICK_BSS_TM_REQ,
    BM_CLIENT_KICK_RRM_BR_REQ,
    BM_CLIENT_KICK_BTM_DISASSOC,
    BM_CLIENT_KICK_BTM_DEAUTH,
    BM_CLIENT_KICK_RRM_DISASSOC,
    BM_CLIENT_KICK_RRM_DEAUTH
} bm_client_kick_t;

typedef enum {
    BM_CLIENT_REJECT_NONE           = 0,
    BM_CLIENT_REJECT_PROBE_ALL,
    BM_CLIENT_REJECT_PROBE_NULL,
    BM_CLIENT_REJECT_PROBE_DIRECT,
    BM_CLIENT_REJECT_AUTH_BLOCKED
} bm_client_reject_t;

typedef enum {
    BM_CLIENT_STATE_DISCONNECTED    = 0,
    BM_CLIENT_STATE_CONNECTED,
    BM_CLIENT_STATE_STEERING,
    BM_CLIENT_STATE_BACKOFF
} bm_client_state_t;

typedef enum {
    BM_CLIENT_CS_MODE_OFF           = 0,
    BM_CLIENT_CS_MODE_AWAY,
    BM_CLIENT_CS_MODE_HOME
} bm_client_cs_mode_t;

typedef enum {
    BM_CLIENT_CS_STATE_NONE         = 0,
    BM_CLIENT_CS_STATE_STEERING,
    BM_CLIENT_CS_STATE_EXPIRED,
    BM_CLIENT_CS_STATE_FAILED,
    BM_CLIENT_CS_STATE_XING_LOW,
    BM_CLIENT_CS_STATE_XING_HIGH,
    BM_CLIENT_CS_STATE_XING_DISABLED
} bm_client_cs_state_t;

typedef enum {
    BM_CLIENT_STEERING_NONE         = 0,
    BM_CLIENT_BAND_STEERING,
    BM_CLIENT_CLIENT_STEERING
} bm_client_steering_state_t;

typedef enum {
    BM_CLIENT_BTM_PARAMS_STEERING   = 0,
    BM_CLIENT_BTM_PARAMS_STICKY,
    BM_CLIENT_BTM_PARAMS_SC
} bm_client_btm_params_type_t;

typedef enum {
    BM_CLIENT_PREF_ALLOWED_NEVER              = 0,
    BM_CLIENT_PREF_ALLOWED_HWM,
    BM_CLIENT_PREF_ALLOWED_ALWAYS,
    BM_CLIENT_PREF_ALLOWED_NON_DFS
} bm_client_pref_allowed;

typedef enum {
    BM_CLIENT_FORCE_KICK_NONE       = 0,
    BM_CLIENT_SPECULATIVE_KICK,
    BM_CLIENT_DIRECTED_KICK,
    BM_CLIENT_GHOST_DEVICE_KICK
} bm_client_force_kick_t;

typedef enum {
    BM_CLIENT_RRM_ALL_CHANNELS     = 0,
    BM_CLIENT_RRM_2G_ONLY,
    BM_CLIENT_RRM_5G_ONLY,
    BM_CLIENT_RRM_OWN_CHANNEL_ONLY,
    BM_CLIENT_RRM_OWN_BAND_ONLY
} bm_client_rrm_req_type_t;

/*
 * Used to store kick information:
 *   - To kick client upon idle
 *   - To retry BSS TM requests multiple times
*/
typedef struct {
    bool                        kick_pending;
    uint8_t                     kick_type;
    uint8_t                     rssi;
} bm_client_kick_info_t;

typedef struct {
    uint32_t                    rejects;
    uint32_t                    connects;
    uint32_t                    disconnects;
    uint32_t                    activity_changes;

    uint32_t                    steering_success_cnt;
    uint32_t                    steering_fail_cnt;
    uint32_t                    steering_kick_cnt;
    uint32_t                    sticky_kick_cnt;

    struct {
        uint32_t                null_cnt;
        uint32_t                null_blocked;
        uint32_t                direct_cnt;
        uint32_t                direct_blocked;
        time_t                  last;
        time_t                  last_null;
        time_t                  last_direct;
        time_t                  last_blocked;
        uint32_t                last_snr;
    } probe;

    struct {
        uint32_t                higher;
        uint32_t                lower;
        uint32_t                inact_higher;
        uint32_t                inact_lower;
    } rssi;

    struct {
        bsal_disc_source_t      source;
        bsal_disc_type_t        type;
        uint8_t                 reason;
    } last_disconnect;
} bm_client_stats_t;

typedef struct {
    time_t                      last_kick;
    time_t                      last_connect;
    time_t                      last_disconnect;
    time_t                      last_state_change;
    time_t                      last_activity_change;
    time_t                      last_sticky_kick;
    time_t                      last_steering_kick;

    struct {
        time_t                  first;
        time_t                  last;
    } reject;
} bm_client_times_t;

/* max 4 ifaces per group and max 4 groups */
#define BM_CLIENT_IFCFG_MAX     16

typedef struct {
    char                        ifname[BSAL_IFNAME_LEN];
    radio_type_t                radio_type;
    bool                        bs_allowed;
    bm_group_t                  *group;
    bm_client_stats_t           stats;
    bsal_client_info_t          info;
    bsal_client_config_t        conf;
} bm_client_ifcfg_t;

typedef struct {
    dpp_bs_client_event_record_t dpp_event;
    bool dpp_event_set;
    char ifname[BSAL_IFNAME_LEN];
    radio_type_t radio_type;
} bm_event_stat_t;

typedef struct {
    os_macaddr_t                bssid;
    char                        bssid_str[MAC_STR_LEN];

    unsigned char               channel;
    unsigned char               snr;
    int                         rssi;

    unsigned char               rcpi;
    unsigned char               rsni;

    time_t                      time;
} bm_rrm_neighbor_t;

typedef struct {
    evsched_task_t              rrm_task;
    bsal_rrm_params_t           rrm_params;
    void                        *client;
} bm_rrm_req_t;

typedef struct {
    char                        mac_addr[MAC_STR_LEN];
    os_macaddr_t                macaddr;

    bm_client_ifcfg_t           ifcfg[BM_CLIENT_IFCFG_MAX];
    unsigned int                ifcfg_num;

    bm_rrm_neighbor_t           rrm_neighbor[BM_CLIENT_RRM_NEIGHBOR_MAX];
    unsigned int                rrm_neighbor_num;

    bm_rrm_req_t                rrm_req[BM_CLIENT_RRM_REQ_MAX];

    bm_client_reject_t          reject_detection;

    bm_client_kick_t            kick_type;
    bm_client_kick_t            sc_kick_type;
    bm_client_kick_t            sticky_kick_type;

    bm_client_force_kick_t      force_kick_type;

    uint8_t                     kick_reason;
    uint8_t                     sc_kick_reason;
    uint8_t                     sticky_kick_reason;

    uint8_t                     hwm;
    uint8_t                     lwm;

    uint8_t                     prev_xing_snr;

    int                         max_rejects;
    int                         max_rejects_period;

    int                         backoff_period;
    int                         backoff_exp_base;
    bool                        steer_during_backoff;
    int                         backoff_period_used;
    int                         backoff_connect_counter;
    bool                        backoff_connect_calculated;
    bool                        backoff;

    uint16_t                    kick_debounce_period;
    uint16_t                    sc_kick_debounce_period;
    uint16_t                    sticky_kick_debounce_period;

    bool                        pre_assoc_auth_block;
    bm_client_pref_allowed      pref_allowed;

    bool                        kick_upon_idle;
    bm_client_kick_info_t       kick_info;

    // Client steering specific variables
    bm_client_reject_t          cs_reject_detection;
    uint8_t                     cs_hwm;
    uint8_t                     cs_lwm;
    int                         cs_max_rejects;
    int                         cs_max_rejects_period;
    int                         cs_enforce_period;
    radio_type_t                cs_radio_type;
    bool                        cs_probe_block;
    bool                        cs_auth_block;
    int                         cs_auth_reject_reason;
    bm_client_cs_mode_t         cs_mode;
    bm_client_cs_state_t        cs_state;
    bool                        cs_auto_disable;

    int                         num_rejects;
    int                         num_rejects_copy;
    bool                        connected;
    bm_group_t                  *group;
    char                        ifname[BSAL_IFNAME_LEN];
    bsal_neigh_info_t           self_neigh;
    bm_client_state_t           state;
    bm_client_times_t           times;
    bm_client_steering_state_t  steering_state;

    // BSS Transition Management variables
    bsal_btm_params_t           steering_btm_params;
    bsal_btm_params_t           sticky_btm_params;
    bsal_btm_params_t           sc_btm_params;

    // Client BTM and RRM capabilities
    bool                        band_cap_2G;
    bool                        band_cap_5G;
    bsal_client_info_t          *info;

    bool                        enable_ch_scan;
    uint8_t                     ch_scan_interval;

    evsched_task_t              backoff_task;
    evsched_task_t              cs_task;
    evsched_task_t              rssi_xing_task;
    evsched_task_t              state_task;
    evsched_task_t              btm_retry_task;
    evsched_task_t              xing_bs_sticky_task;
    bool                        cancel_btm;
    time_t                      skip_sticky_kick_till;
    time_t                      skip_steering_kick_till;

    int                         sticky_kick_guard_time;
    int                         steering_kick_guard_time;
    int                         sticky_kick_backoff_time;
    int                         steering_kick_backoff_time;

    time_t                      settling_skip_xing_till;
    int                         settling_backoff_time;

    uint8_t                     preq_snr_thr;

    char                        uuid[OVSDB_UUID_LEN];

    bm_event_stat_t             d_events[BM_CLIENT_D_EVENTS_MAX];
    unsigned int                d_events_idx;

    bool                        send_rrm_after_assoc;
    bool                        send_rrm_after_xing;
    int                         rrm_better_factor;
    unsigned int                rrm_age_time;

    uint8_t                     xing_bs_rssi;

    /* Connected with client ACTIVE/INACTIVE detection */
    uint64_t                    tx_bytes;
    uint64_t                    rx_bytes;
    time_t                      bytes_report_time;

    bool                        is_active;
    time_t                      is_active_time;

    unsigned int                active_treshold_bps;

    ds_tree_node_t              dst_node;
} bm_client_t;

static inline bm_client_ifcfg_t *
bm_client_get_ifcfg(bm_client_t *client, const char *ifname)
{
    unsigned int i;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname)) {
            return &client->ifcfg[i];
        }
    }

    return NULL;
}

static inline bm_client_stats_t *
bm_client_get_stats(bm_client_t *client, const char *ifname)
{
    unsigned int i;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname)) {
            return &client->ifcfg[i].stats;
        }
    }

    return NULL;
}

static inline bool
bm_client_is_dfs_channel(uint32_t channel)
{
    bool result = false;

    switch (channel) {
    case 52:
    case 56:
    case 60:
    case 64:
    case 100:
    case 104:
    case 108:
    case 112:
    case 116:
    case 120:
    case 124:
    case 128:
    case 132:
    case 136:
    case 140:
        result = true;
        break;
    default:
        break;
    }

    return result;
}

/*****************************************************************************/
extern bool                 bm_client_init(void);
extern bool                 bm_client_cleanup(void);
extern bool                 bm_client_add_all_to_group(bm_group_t *group);
extern bool                 bm_client_update_all_from_group(bm_group_t *group);
extern bool                 bm_client_remove_all_from_group(bm_group_t *group);
extern bool                 bm_client_ovsdb_update(bm_client_t *client);
extern void                 bm_client_connected(bm_client_t *client, bm_group_t *group, const char *ifname);
extern void                 bm_client_disconnected(bm_client_t *client);
extern void                 bm_client_rejected(bm_client_t *client, bsal_event_t *event);
extern void                 bm_client_success(bm_client_t *client, const char *ifname);
extern void                 bm_client_cs_connect(bm_client_t *client, const char *ifname);
extern void                 bm_client_preassoc_backoff_recalc(bm_group_t *group,bm_client_t *client, const char *ifname);
extern void                 bm_client_bs_connect(bm_group_t *group, bm_client_t *client, const char *ifname);
extern void                 bm_client_set_state(bm_client_t *client, bm_client_state_t state);
extern bool                 bm_client_update_cs_state( bm_client_t *client );

extern bm_client_reject_t   bm_client_get_reject_detection( bm_client_t *client );
extern void                 bm_client_cs_check_rssi_xing( bm_client_t *client, bsal_event_t *event );
extern void                 bm_client_disable_client_steering( bm_client_t *client );

extern ds_tree_t *          bm_client_get_tree(void);
extern bm_client_t *        bm_client_find_by_uuid(const char *uuid);
extern bm_client_t *        bm_client_find_by_macstr(char *mac_str);
extern bm_client_t *        bm_client_find_by_macaddr(os_macaddr_t mac_addr);
extern bm_client_t *        bm_client_find_or_add_by_macaddr(os_macaddr_t *mac_addr);
extern bool                 bm_client_ifcfg_set(bm_group_t *group, bm_client_t *client, const char *ifname, radio_type_t radio_type, bool bs_allowed, bsal_client_config_t *conf);
extern bool                 bm_client_ifcfg_remove(bm_client_t *client, const char *ifname);
extern void                 bm_client_ifcfg_clean(bm_client_t *client);
extern bool                 bm_client_bs_ifname_allowed(bm_client_t *client, const char *ifname);
extern void                 bm_client_check_connected(bm_client_t *client, bm_group_t *group, const char *ifname);
extern void                 bm_client_add_dbg_event(bm_client_t *client, const char *ifname, dpp_bs_client_event_record_t *event);
extern void                 bm_client_dump_dbg_events(void);
extern void                 bm_client_reset_last_probe_snr(bm_client_t *client);
extern void                 bm_client_reset_rrm_neighbors(bm_client_t *client);
extern bool                 bm_client_set_rrm_neighbor(bm_client_t *client, const unsigned char *bssid, unsigned char channel, unsigned char rcpi, unsigned char rsni);
extern void                 bm_client_update_rrm_neighbors(void);
extern void                 bm_client_send_rrm_req(bm_client_t *client, bm_client_rrm_req_type_t rrm_req_type, int delay);
extern void                 bm_client_parse_assoc_ies(bm_client_t *client, const uint8_t *ies, size_t ies_len);
extern void                 bm_client_sta_info_update_callback(void);
extern void                 bm_client_handle_ext_activity(bm_client_t *client, const char *ifname, bool active);
extern void                 bm_client_handle_ext_xing(bm_client_t *client, const char *ifname, bsal_event_t *event);
#endif /* BM_CLIENT_H_INCLUDED */
