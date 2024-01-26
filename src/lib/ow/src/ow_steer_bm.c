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

#include <inttypes.h>
#include <string.h>
#include <inttypes.h>
#include <memutil.h>
#include <os.h>
#include <ds_dlist.h>
#include <util.h>
#include <const.h>
#include <log.h>
#include <qm_conn.h>
#include <dpp_types.h>
#include <dpp_bs_client.h>
#include <dppline.h>
#include <osw_types.h>
#include <osw_conf.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_bss_map.h>
#include <osw_throttle.h>
#include <osw_btm.h>
#include <osw_rrm_meas.h>
#include <osw_util.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include <osw_stats_subscriber.h>
#include <osw_defer_vif_down.h>
#include <osw_diag.h>
#include <ow_steer_bm.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_sta.h"
#include "ow_steer_sta_priv.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_bss_filter.h"
#include "ow_steer_policy_chan_cap.h"
#include "ow_steer_policy_pre_assoc.h"
#include "ow_steer_policy_force_kick.h"
#include "ow_steer_policy_snr_xing.h"
#include "ow_steer_policy_stack.h"
#include "ow_steer_policy_btm_response.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_acl.h"
#include "ow_steer_executor_action_btm.h"
#include "ow_steer_executor_action_deauth.h"
#include "ow_steer_executor.h"
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_candidate_assessor_i.h"
#include "ow_steer_bm_policy_hwm_2g.h"
#include "ow_steer_bm_priv.h"

#define OW_STEER_BM_DEFAULT_REPORTING_TIME 60
#define OW_STEER_BM_MAX_EVENT_CNT 60
#define OW_STEER_BM_DPP_MAX_BS_BANDS 5
#define OW_STEER_BM_BITRATE_STATS_INTERVAL 10

/* BM constants */
#define OW_STEER_BM_RRM_REP_VALIDITY_PERIOD_SEC 15
#define OW_STEER_BM_RRM_REP_BETTER_FACTOR 3
#define OW_STEER_BM_RRM_MEAS_AFTER_CONNECT_DELAY_SEC 5
#define OW_STEER_BM_RRM_MEAS_DELAY_SEC 1
#define OW_STEER_BM_DEFAULT_BITRATE_THRESHOLD 2000

#define OW_STEER_BM_ATTR_DECL(type, name)   \
    struct {                                \
        bool valid;                         \
        type *next;                         \
        type *cur;                          \
    } name;

struct ow_steer_bm_sta_rrm;

struct ow_steer_bm_event_stats {
    dpp_bs_client_event_type_t type;
    uint64_t timestamp_ms;
    uint32_t rssi;
    uint32_t probe_bcast;
    uint32_t probe_blocked;
    dpp_bs_client_disconnect_src_t disconnect_src;
    dpp_bs_client_disconnect_type_t disconnect_type;
    uint32_t disconnect_reason;
    bool backoff_enabled;
    bool active;
    bool rejected;
    bool is_BTM_supported;
    bool is_RRM_supported;
    bool band_cap_2G;
    bool band_cap_5G;
    bool band_cap_6G;
    uint32_t max_chwidth;
    uint32_t max_streams;
    uint32_t phy_mode;
    uint32_t max_MCS;
    uint32_t max_txpower;
    bool is_static_smps;
    bool is_mu_mimo_supported;
    bool rrm_caps_link_meas;
    bool rrm_caps_neigh_rpt;
    bool rrm_caps_bcn_rpt_passive;
    bool rrm_caps_bcn_rpt_active;
    bool rrm_caps_bcn_rpt_table;
    bool rrm_caps_lci_meas;
    bool rrm_caps_ftm_range_rpt;
    uint32_t backoff_period;
    uint8_t *assoc_ies;
    size_t assoc_ies_len;
    uint32_t btm_status;
};

struct ow_steer_bm_vif_stats {
    struct osw_ifname vif_name;
    bool connected;
    uint32_t rejects;
    uint32_t connects;
    uint32_t disconnects;
    uint32_t activity_changes;
    uint32_t steering_success_cnt;
    uint32_t steering_fail_cnt;
    uint32_t steering_kick_cnt;
    uint32_t sticky_kick_cnt;
    uint32_t probe_bcast_cnt;
    uint32_t probe_bcast_blocked;
    uint32_t probe_direct_cnt;
    uint32_t probe_direct_blocked;
    uint32_t event_stats_count;
    struct ow_steer_bm_event_stats event_stats[OW_STEER_BM_MAX_EVENT_CNT];

    struct ds_tree_node node;
};

struct ow_steer_bm_neighbor {
    struct osw_hwaddr bssid;
    bool removed;

    OW_STEER_BM_ATTR_DECL(struct osw_ifname, vif_name);
    OW_STEER_BM_ATTR_DECL(uint8_t, channel_number);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_neighbor_ht_mode, ht_mode);
    OW_STEER_BM_ATTR_DECL(uint8_t, op_class);
    OW_STEER_BM_ATTR_DECL(unsigned int, priority);
    OW_STEER_BM_ATTR_DECL(struct osw_channel, channel);
    OW_STEER_BM_ATTR_DECL(struct ow_steer_bm_vif, vif);

    struct ow_steer_bm_bss *bss;
    struct ow_steer_bm_observer observer;

    struct ds_tree_node node;
};

struct ow_steer_bm_client {
    struct osw_hwaddr addr;
    bool removed;

    OW_STEER_BM_ATTR_DECL(unsigned int, hwm);
    OW_STEER_BM_ATTR_DECL(unsigned int, lwm);
    OW_STEER_BM_ATTR_DECL(unsigned int, bottom_lwm);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_pref_5g, pref_5g);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_kick_type, kick_type);
    OW_STEER_BM_ATTR_DECL(bool, pre_assoc_auth_block);
    OW_STEER_BM_ATTR_DECL(bool, kick_upon_idle);
    OW_STEER_BM_ATTR_DECL(bool, send_rrm_after_assoc);
    OW_STEER_BM_ATTR_DECL(unsigned int, backoff_secs);
    OW_STEER_BM_ATTR_DECL(unsigned int, backoff_exp_base);
    OW_STEER_BM_ATTR_DECL(unsigned int, max_rejects);
    OW_STEER_BM_ATTR_DECL(unsigned int, rejects_tmout_secs);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_force_kick, force_kick);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_sc_kick_type, sc_kick_type);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_sticky_kick_type, sticky_kick_type);
    OW_STEER_BM_ATTR_DECL(bool, neighbor_list_filter_by_beacon_report);
    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_client_cs_mode, cs_mode);
    OW_STEER_BM_ATTR_DECL(unsigned int, pref_5g_pre_assoc_block_timeout_msecs);

    ow_steer_bm_client_set_cs_state_mutate_fn_t *cs_state_mutate_fn;

    struct ow_steer_bm_btm_params *sc_btm_params;
    struct ow_steer_bm_btm_params *steering_btm_params;
    struct ow_steer_bm_btm_params *sticky_btm_params;
    struct ow_steer_bm_cs_params *cs_params;

    struct ds_dlist sta_list;
    struct ds_tree stats_tree; /* struct ow_steer_bm_vif_stats */

    struct ds_tree_node node;
};

enum ow_steer_bm_kick_source {
    OW_STEER_BM_KICK_SOURCE_STEERING,
    OW_STEER_BM_KICK_SOURCE_STICKY,
    OW_STEER_BM_KICK_SOURCE_FORCE,
    OW_STEER_BM_KICK_SOURCE_NON_POLICY,
    OW_STEER_BM_KICK_SOURCE_INVALID
};

struct ow_steer_bm_kick_state_monitor {
    bool force_trig;
    bool steering_trig;
    bool sticky_trig;
    unsigned int btm_send_cnt;
};

struct ow_steer_bm_sta {
    struct osw_hwaddr addr;
    bool removed;

    const struct osw_state_sta_info *sta_info;

    struct ow_steer_sta *steer_sta;
    struct ow_steer_policy_bss_filter *bss_filter_policy;
    struct ow_steer_policy_chan_cap *chan_cap_policy;
    struct ow_steer_policy_bss_filter *defer_vif_down_deny_policy;
    struct ow_steer_policy_bss_filter *defer_vif_down_allow_policy;
    struct ow_steer_policy_pre_assoc *pre_assoc_2g_policy;
    struct ow_steer_bm_policy_hwm_2g *hwm_2g_policy;
    struct ow_steer_policy_snr_xing *lwm_2g_xing_policy;
    struct ow_steer_policy_snr_xing *lwm_5g_xing_policy;
    struct ow_steer_policy_snr_xing *lwm_6g_xing_policy;
    struct ow_steer_policy_snr_xing *bottom_lwm_2g_xing_policy;
    struct ow_steer_policy_force_kick *force_kick_policy;
    struct ow_steer_policy_bss_filter *cs_allow_filter_policy;
    struct ow_steer_policy_bss_filter *cs_deny_filter_policy;
    struct ow_steer_policy_bss_filter *cs_kick_filter_policy;
    struct ow_steer_policy_btm_response *btm_response_policy;

    struct ow_steer_policy *active_policy;
    struct ow_steer_executor_action_acl *acl_executor_action;
    struct ow_steer_executor_action_btm *btm_executor_action;
    struct ow_steer_executor_action_deauth *deauth_executor_action;

    /*
     * FIXME
     * This is temporary solution, we should handle RRM Requests in separate\
     * dedicated module.
     */
    struct ds_tree vif_rrm_tree;
    struct osw_state_observer state_observer;
    struct osw_assoc_req_info assoc_req_info;

    bool issue_force_kick;
    bool client_steering_recalc;
    struct osw_timer client_steering_timer;
    bool bps_activity;
    struct ow_steer_bm_kick_state_monitor kick_state_mon;

    struct ow_steer_bm_client *client;
    struct ds_dlist_node client_node;
    struct ow_steer_bm_group *group;
    struct ds_tree_node group_node;

    struct ds_dlist_node node;
};

struct ow_steer_bm_btm_params {
    const char *name;
    struct osw_hwaddr sta_addr;

    OW_STEER_BM_ATTR_DECL(struct osw_hwaddr, bssid);
    OW_STEER_BM_ATTR_DECL(bool, disassoc_imminent);
};

struct ow_steer_bm_cs_params {
    struct osw_hwaddr sta_addr;

    OW_STEER_BM_ATTR_DECL(enum ow_steer_bm_cs_params_band, band);
    OW_STEER_BM_ATTR_DECL(unsigned int, enforce_period_secs);
};

struct ow_steer_bm_attr_state {
    bool changed;
    bool present;
};

struct ow_steer_bm_candidate_assessor {
    struct ow_steer_candidate_assessor *base;
};

struct ow_steer_bm_sta_rrm {
    const struct ow_steer_bm_sta *sta;
    const struct osw_state_sta_info *sta_info;

    struct osw_timer timer;
    struct osw_rrm_meas_sta_observer desc_observer;
    struct osw_rrm_meas_desc *desc;
    struct ds_dlist request_list;

    struct ds_tree_node node;
    bool handled;
};

struct ow_steer_bm_sta_rrm_req {
    struct osw_rrm_meas_req_params params;

    struct ds_dlist_node node;
};

static void
ow_steer_bm_state_obs_vif_added_cb(struct osw_state_observer *self,
                                   const struct osw_state_vif_info *vif_info);

static void
ow_steer_bm_state_obs_vif_changed_cb(struct osw_state_observer *self,
                                     const struct osw_state_vif_info *vif_info);

static void
ow_steer_bm_state_obs_vif_removed_cb(struct osw_state_observer *self,
                                     const struct osw_state_vif_info *vif_info);

static void
ow_steer_bm_state_obs_vif_probe_cb(struct osw_state_observer *self,
                                   const struct osw_state_vif_info *vif,
                                   const struct osw_drv_report_vif_probe_req *probe_req);

static void
ow_steer_bm_sta_stop_client_steering(struct ow_steer_bm_sta *sta);

static void
ow_steer_bm_sta_clear_client_steering(struct ow_steer_bm_sta *sta);

static struct osw_bss_provider *g_bss_provider = NULL;
static struct ds_dlist g_observer_list = DS_DLIST_INIT(struct ow_steer_bm_observer, node);
static struct ds_tree g_vif_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_ifname_cmp, struct ow_steer_bm_vif, node);
static struct ds_tree g_group_tree = DS_TREE_INIT(ds_str_cmp, struct ow_steer_bm_group, node);
static struct ds_tree g_neighbor_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct ow_steer_bm_neighbor, node);
static struct ds_tree g_client_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct ow_steer_bm_client, node);
static struct ds_dlist g_sta_list = DS_DLIST_INIT(struct ow_steer_bm_sta, node);
static struct ds_dlist g_bss_list = DS_DLIST_INIT(struct ow_steer_bm_bss, node);
static struct osw_state_observer g_state_observer = {
    .name = "ow_steer_bm",
    .vif_probe_req_fn = ow_steer_bm_state_obs_vif_probe_cb,
};
static struct osw_stats_subscriber *g_stats_subscriber;
static struct osw_timer g_work_timer;
static struct osw_timer g_stats_timer;

static bool
ow_steer_bm_vif_is_ready(const struct ow_steer_bm_vif *vif);

static bool
ow_steer_bm_vif_is_up(const struct ow_steer_bm_vif *vif);

static bool
ow_steer_bm_neighbor_is_ready(const struct ow_steer_bm_neighbor *neighbor);

static bool
ow_steer_bm_neighbor_is_up(const struct ow_steer_bm_neighbor *neighbor);

static const struct ow_steer_bm_vif*
ow_steer_bm_group_lookup_vif_by_band(struct ow_steer_bm_group *group,
                                     enum osw_band band);

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_group_create_bss_filter_policy_config(struct ow_steer_bm_group *group);

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_sta_create_defer_vif_down_deny_policy_config(struct ow_steer_bm_sta *sta);

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_sta_create_defer_vif_down_allow_policy_config(struct ow_steer_bm_sta *sta);

static void
ow_steer_bm_policy_mediator_sched_stack_recalc_cb(struct ow_steer_policy *policy,
                                                  void *priv);

static bool
ow_steer_bm_policy_mediator_trigger_executor_cb(struct ow_steer_policy *policy,
                                                void *priv);

static void
ow_steer_bm_policy_mediator_dismiss_executor_cb(struct ow_steer_policy *policy,
                                                void *priv);

static void
ow_steer_bm_policy_mediator_notify_backoff_cb(struct ow_steer_policy *policy,
                                              void *priv,
                                              const bool enabled,
                                              const unsigned int period);

static void
ow_steer_bm_policy_mediator_notify_steering_attempt_cb(struct ow_steer_policy *policy,
                                                       const char *vif_name,
                                                       void *priv);
static void
ow_steer_bm_executor_action_mediator_sched_recall_cb(struct ow_steer_executor_action *action,
                                                     void *mediator_priv);

static void
ow_steer_bm_executor_action_mediator_going_busy_cb(struct ow_steer_executor_action *action,
                                                   void *mediator_priv);

static void
ow_steer_bm_executor_action_mediator_data_sent_cb(struct ow_steer_executor_action *action,
                                                  void *mediator_priv);

static void
ow_steer_bm_executor_action_mediator_going_idle_cb(struct ow_steer_executor_action *action,
                                                   void *mediator_priv);

static struct ow_steer_bm_vif_stats*
ow_steer_bm_get_client_vif_stats(const struct osw_hwaddr *addr,
                                 const char *vif_name);

static struct ow_steer_bm_event_stats*
ow_steer_bm_get_new_client_event_stats(struct ow_steer_bm_vif_stats *vif_stats);

#define OW_STEER_BM_SCHEDULE_WORK                                                                       \
    do {                                                                                                \
        LOGD("ow: steer: bm: schedule work at line: %u", __LINE__); ow_steer_bm_schedule_work_impl();   \
    } while(0)

#define OW_STEER_BM_MEM_ATTR_FREE(object, attr) \
    FREE(object->attr.next);                    \
    FREE(object->attr.cur);                     \
    object->attr.next = NULL;                   \
    object->attr.cur = NULL;

#define OW_STEER_BM_MEM_ATTR_SET_BODY(object, attr)                 \
    ASSERT(object != NULL, "");                                     \
    if (ow_steer_bm_mem_attr_cmp(object->attr.next, attr) == true)  \
        return;                                                     \
                                                                    \
    object->attr.valid = false;                                     \
    FREE(object->attr.next);                                        \
    object->attr.next = NULL;                                       \
    if (attr != NULL)                                               \
        object->attr.next = MEMNDUP(attr, sizeof(*attr));           \
                                                                    \
    OW_STEER_BM_SCHEDULE_WORK;

#define OW_STEER_BM_MEM_ATTR_UNSET_BODY(object, attr)   \
    ASSERT(object != NULL, "");                         \
    if (object->attr.next == NULL)                      \
        return;                                         \
                                                        \
    object->attr.valid = false;                         \
    FREE(object->attr.next);                            \
    object->attr.next = NULL;                           \
                                                        \
    OW_STEER_BM_SCHEDULE_WORK;

#define OW_STEER_BM_ATTR_PRINT_CHANGE_BOOL(prefix, object, attr)                                                    \
    const char *cur_str = object->attr.cur == NULL ? "(nil)" : (*object->attr.cur == true ? "true" : "false");      \
    const char *next_str = object->attr.next == NULL ? "(nil)" : (*object->attr.next == true ? "true" : "false");   \
    LOGD("%s %s: %s -> %s", prefix, #attr, cur_str, next_str)

#define OW_STEER_BM_ATTR_PRINT_CHANGE_NUM(prefix, object, attr, fmt)                                   \
    const char *cur_str = object->attr.cur == NULL ? "(nil)" : strfmta("%"fmt, *object->attr.cur);     \
    const char *next_str = object->attr.next == NULL ? "(nil)" : strfmta("%"fmt, *object->attr.next);  \
    LOGD("%s %s: %s -> %s", prefix, #attr, cur_str, next_str)

#define OW_STEER_BM_ATTR_PRINT_CHANGE_ENUM(prefix, object, attr, to_str_fn)                                     \
    const char *cur_str = object->attr.cur == NULL ? "(nil)" : strfmta("%s", to_str_fn(*object->attr.cur));     \
    const char *next_str = object->attr.next == NULL ? "(nil)" : strfmta("%s", to_str_fn(*object->attr.next));  \
    LOGD("%s %s: %s -> %s", prefix, #attr, cur_str, next_str)

#define OW_STEER_BM_ATTR_PRINT_CHANGE_HWADDR(prefix, object, attr)                                                              \
    const char *cur_str = object->attr.cur == NULL ? "(nil)" : strfmta(OSW_HWADDR_FMT, OSW_HWADDR_ARG(object->attr.cur));       \
    const char *next_str = object->attr.next == NULL ? "(nil)" : strfmta(OSW_HWADDR_FMT, OSW_HWADDR_ARG(object->attr.next));    \
    LOGD("%s %s: %s -> %s", prefix, #attr, cur_str, next_str)

#define OW_STEER_BM_ATTR_PRINT_CHANGE_IFNAME(prefix, object, attr)                          \
    const char *cur_str = object->attr.cur == NULL ? "(nil)" : object->attr.cur->buf;       \
    const char *next_str = object->attr.next == NULL ? "(nil)" : object->attr.next->buf;    \
    LOGD("%s %s: %s -> %s", prefix, #attr, cur_str, next_str)

#define OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(params)                                                           \
    strfmta("ow: steer: bm: %s: sta_addr: "OSW_HWADDR_FMT, params->name, OSW_HWADDR_ARG(&params->sta_addr))

#define OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_NUM(params, attr, fmt)                             \
    OW_STEER_BM_ATTR_PRINT_CHANGE_NUM(OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(params), params, attr, fmt)

#define OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_BOOL(params, attr)                             \
    OW_STEER_BM_ATTR_PRINT_CHANGE_BOOL(OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(params), params, attr)

#define OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_HWADDR(params, attr)                               \
    OW_STEER_BM_ATTR_PRINT_CHANGE_HWADDR(OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(params), params, attr)

#define OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_DISASSOC_IMMINENT(params, attr)                               \
    OW_STEER_BM_ATTR_PRINT_CHANGE_BOOL(OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(params), params, attr)

#define OW_STEER_BM_CLIENT_LOG_PREFIX(client)                                               \
    strfmta("ow: steer: bm: client: addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&client->addr))

#define OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, attr, fmt)                             \
    OW_STEER_BM_ATTR_PRINT_CHANGE_NUM(OW_STEER_BM_CLIENT_LOG_PREFIX(client), client, attr, fmt)

#define OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_BOOL(client, attr)                             \
    OW_STEER_BM_ATTR_PRINT_CHANGE_BOOL(OW_STEER_BM_CLIENT_LOG_PREFIX(client), client, attr)

#define OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, attr, to_str_fn)                              \
    OW_STEER_BM_ATTR_PRINT_CHANGE_ENUM(OW_STEER_BM_CLIENT_LOG_PREFIX(client), client, attr, to_str_fn)

#define OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor)                                               \
    strfmta("ow: steer: bm: neighbor: bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&neighbor->bssid))

#define OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_NUM(neighbor, attr, fmt)                                 \
    OW_STEER_BM_ATTR_PRINT_CHANGE_NUM(OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor), neighbor, attr, fmt)

#define OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_IFNAME(neighbor, attr)                               \
    OW_STEER_BM_ATTR_PRINT_CHANGE_IFNAME(OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor), neighbor, attr)

#define OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_ENUM(neighbor, attr, to_str_fn)                                  \
    OW_STEER_BM_ATTR_PRINT_CHANGE_ENUM(OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor), neighbor, attr, to_str_fn)

#define OW_STEER_BM_MEM_ATTR_UPDATE(object, attr_name)                  \
    struct ow_steer_bm_attr_state attr_name ## _state;                  \
    ow_steer_bm_update_mem_attr_impl((void **) &object->attr_name.cur,  \
                                     (void **) &object->attr_name.next, \
                                     &object->attr_name.valid,          \
                                     &attr_name ## _state,              \
                                     sizeof(*object->attr_name.cur))

#define OW_STEER_BM_PTR_ATTR_UPDATE(object, attr_name)                          \
    struct ow_steer_bm_attr_state attr_name ## _state;                          \
    ow_steer_bm_update_ptr_attr_impl((const void **) &object->attr_name.cur,    \
                                     (const void **) &object->attr_name.next,   \
                                     &object->attr_name.valid,                  \
                                     &attr_name ## _state,                      \
                                     sizeof(*object->attr_name.cur))

#define OW_STEER_BM_CS_PARAMS_LOG_PREFIX(cs_parms)                                               \
    strfmta("ow: steer: bm: cs_params: addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&cs_parms->sta_addr))

#define OW_STEER_BM_CS_PARAMS_ATTR_PRINT_CHANGE_NUM(cs_parms, attr, fmt)                                 \
    OW_STEER_BM_ATTR_PRINT_CHANGE_NUM(OW_STEER_BM_CS_PARAMS_LOG_PREFIX(cs_parms), cs_parms, attr, fmt)

#define OW_STEER_BM_CS_PARAMS_ATTR_PRINT_CHANGE_ENUM(cs_parms, attr, to_str_fn)                                  \
    OW_STEER_BM_ATTR_PRINT_CHANGE_ENUM(OW_STEER_BM_CS_PARAMS_LOG_PREFIX(cs_parms), cs_parms, attr, to_str_fn)

#define ow_steer_bm_mem_attr_cmp(a, b) ow_steer_bm_mem_attr_cmp_impl(a, b, sizeof(*a))

static const char*
ow_steer_bm_client_pref_5g_to_str(const enum ow_steer_bm_client_pref_5g pref_5g)
{
    switch (pref_5g) {
        case OW_STEER_BM_CLIENT_PREF_5G_NEVER:
            return "never";
        case OW_STEER_BM_CLIENT_PREF_5G_ALWAYS:
            return "always";
        case OW_STEER_BM_CLIENT_PREF_5G_HWM:
            return "hwm";
        case OW_STEER_BM_CLIENT_PREF_5G_NON_DFS:
            return "nonDFS";
    }

    return "unknown";
}

static const char*
ow_steer_bm_client_kick_type_to_str(const enum ow_steer_bm_client_kick_type kick_type)
{
    switch (kick_type) {
        case OW_STEER_BM_CLIENT_KICK_TYPE_DEAUTH:
            return "deauth";
        case OW_STEER_BM_CLIENT_KICK_TYPE_BTM_DEAUTH:
            return "btm-deauth";
    }

    return "unknown";
}

static const char*
ow_steer_bm_client_sc_kick_type_to_str(const enum ow_steer_bm_client_sc_kick_type kick_type)
{
    switch (kick_type) {
        case OW_STEER_BM_CLIENT_SC_KICK_TYPE_DEAUTH:
            return "deauth";
        case OW_STEER_BM_CLIENT_SC_KICK_TYPE_BTM_DEAUTH:
            return "btm-deauth";
    }

    return "unknown";
}

static const char*
ow_steer_bm_client_sticky_kick_type_to_str(const enum ow_steer_bm_client_sticky_kick_type kick_type)
{
    switch (kick_type) {
        case OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_DEAUTH:
            return "deauth";
        case OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_BTM_DEAUTH:
            return "btm-deauth";
    }

    return "unknown";
}

static const char*
ow_steer_bm_client_force_kick_to_str(const enum ow_steer_bm_client_force_kick force_kick)
{
    switch (force_kick) {
        case OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE:
            return "speculative";
        case OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED:
            return "directed";
    }

    return "unknown";
}

static const char*
ow_steer_bm_neighbor_ht_mode_to_str(enum ow_steer_bm_neighbor_ht_mode ht_mode)
{
    switch (ht_mode) {
        case OW_STEER_BM_NEIGHBOR_HT20:
            return "ht20";
        case OW_STEER_BM_NEIGHBOR_HT2040:
            return "ht20_40";
        case OW_STEER_BM_NEIGHBOR_HT40:
            return "ht40";
        case OW_STEER_BM_NEIGHBOR_HT40P:
            return "ht40+";
        case OW_STEER_BM_NEIGHBOR_HT40M:
            return "ht40-";
        case OW_STEER_BM_NEIGHBOR_HT80:
            return "ht80";
        case OW_STEER_BM_NEIGHBOR_HT160:
            return "ht160";
        case OW_STEER_BM_NEIGHBOR_HT80P80:
            return "ht80+80";
        case OW_STEER_BM_NEIGHBOR_HT320:
            return "ht320";
    }

    return "unknown";
}

static const char*
ow_steer_bm_client_cs_mode_to_str(enum ow_steer_bm_client_cs_mode mode)
{
    switch (mode) {
        case OW_STEER_BM_CLIENT_CS_MODE_OFF:
            return "off";
        case OW_STEER_BM_CLIENT_CS_MODE_HOME:
            return "home";
        case OW_STEER_BM_CLIENT_CS_MODE_AWAY:
            return "away";
    }

    return "unknown";
}

static const char*
ow_steer_bm_cs_params_band_to_str(enum ow_steer_bm_cs_params_band band)
{
    switch (band) {
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_2G:
            return "2.4ghz";
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5G:
            return "5ghz";
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GL:
            return "5ghz lower";
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GU:
            return "5ghz upper";
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_6G:
            return "6ghz";
    }

    return "unknown";
}

static enum osw_channel_width
ow_steer_bm_neighbor_ht_mode_to_channel_width(enum ow_steer_bm_neighbor_ht_mode ht_mode)
{
    switch (ht_mode) {
        case OW_STEER_BM_NEIGHBOR_HT20:
            return OSW_CHANNEL_20MHZ;
        case OW_STEER_BM_NEIGHBOR_HT2040:
            return OSW_CHANNEL_40MHZ;
        case OW_STEER_BM_NEIGHBOR_HT40:
            return OSW_CHANNEL_40MHZ;
        case OW_STEER_BM_NEIGHBOR_HT40P:
            return OSW_CHANNEL_40MHZ;
        case OW_STEER_BM_NEIGHBOR_HT40M:
            return OSW_CHANNEL_40MHZ;
        case OW_STEER_BM_NEIGHBOR_HT80:
            return OSW_CHANNEL_80MHZ;
        case OW_STEER_BM_NEIGHBOR_HT160:
            return OSW_CHANNEL_160MHZ;
        case OW_STEER_BM_NEIGHBOR_HT80P80:
            return OSW_CHANNEL_80P80MHZ;
        case OW_STEER_BM_NEIGHBOR_HT320:
            return OSW_CHANNEL_320MHZ;
    }

    WARN_ON(0);
    return OSW_CHANNEL_20MHZ;
}

static bool
ow_steer_bm_mem_attr_cmp_impl(const void *a,
                              const void *b,
                              size_t size)
{
    if (a != NULL && b != NULL)
        return memcmp(a, b, size) == 0;
    else if (a != b)
        return false;
    else
        return true;
}

static void
ow_steer_bm_update_mem_attr_impl(void **cur,
                                 void **next,
                                 bool *valid,
                                 struct ow_steer_bm_attr_state *state,
                                 size_t size)
{
    ASSERT(cur != NULL, "");
    ASSERT(next != NULL, "");
    ASSERT(valid != NULL, "");
    ASSERT(state != NULL, "");

    memset(state, 0, sizeof(*state));

    if (*valid == true)
        goto end;

    *valid = true;

    state->changed = ow_steer_bm_mem_attr_cmp_impl(*cur, *next, size) == false;
    if (state->changed == false)
        goto end;

    FREE(*cur);
    *cur = NULL;

    if (*next == NULL)
        goto end;

    *cur = MEMNDUP(*next, size);

end:
    state->present = *cur != NULL;
}

static void
ow_steer_bm_update_ptr_attr_impl(const void **cur,
                                 const void **next,
                                 bool *valid,
                                 struct ow_steer_bm_attr_state *state,
                                 size_t size)
{
    ASSERT(cur != NULL, "");
    ASSERT(next != NULL, "");
    ASSERT(valid != NULL, "");
    ASSERT(state != NULL, "");

    memset(state, 0, sizeof(*state));

    if (*valid == true)
        goto end;

    *valid = true;
    state->changed = *cur != *next;

    *cur = *next;

end:
    state->present = *cur != NULL;
}

/* get/alloc client stats for current vif */
static struct ow_steer_bm_vif_stats*
ow_steer_bm_get_client_vif_stats(const struct osw_hwaddr *addr, const char *vif_name)
{
    struct ow_steer_bm_client *client = ds_tree_find(&g_client_tree, addr);
    if (client == NULL) {
        LOGT("ow: steer: bm: stats: no client found with addr "OSW_HWADDR_FMT"", OSW_HWADDR_ARG(addr));
        return NULL;
    }

    struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, vif_name);
    if (client_vif_stats == NULL) {
        LOGT("ow: steer: bm: stats: no vif stats found for client addr "OSW_HWADDR_FMT
             " - now allocating", OSW_HWADDR_ARG(addr));
        client_vif_stats = CALLOC(1, sizeof(*client_vif_stats));
        memset(client_vif_stats, 0, sizeof(*client_vif_stats));
        STRSCPY_WARN(client_vif_stats->vif_name.buf, vif_name);
        ds_tree_insert(&client->stats_tree, client_vif_stats, &client_vif_stats->vif_name);
    }

    return client_vif_stats;
}

static struct ow_steer_bm_event_stats*
ow_steer_bm_get_new_client_event_stats(struct ow_steer_bm_vif_stats *vif_stats)
{
    if (vif_stats->event_stats_count >= OW_STEER_BM_MAX_EVENT_CNT) {
        LOGW( "ow: steer: bm: stats: max events limit was already reached, not adding any more events");
        return NULL;
    }

    struct ow_steer_bm_event_stats *event_stats = &vif_stats->event_stats[vif_stats->event_stats_count];

    vif_stats->event_stats_count++;
    if (vif_stats->event_stats_count == OW_STEER_BM_MAX_EVENT_CNT) {
        LOGW( "ow: steer: bm: stats: max events limit reached, adding OVERRUN event");
        event_stats->type = OVERRUN;
        return NULL;
    }

    event_stats->timestamp_ms = OSW_TIME_TO_MS(osw_time_wall_clk());
    return event_stats;
}

static void
ow_steer_bm_stats_set_probe(const struct osw_hwaddr *sta_addr,
                            const char *vif_name,
                            const bool probe_bcast,
                            const bool probe_blocked,
                            const uint32_t rssi)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    /* This is an exception - do not warn about probes from clients we don't know */
    if (client_vif_stats == NULL) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: PROBE event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s"
         " probe_bcast: "OSW_BOOL_FMT
         " probe_blocked: "OSW_BOOL_FMT
         " rssi: %u",
         OSW_HWADDR_ARG(sta_addr),
         vif_name,
         OSW_BOOL_ARG(probe_bcast),
         OSW_BOOL_ARG(probe_blocked),
         rssi);

    if (probe_blocked == true) client_vif_stats->rejects++;

    if (probe_bcast == true) client_vif_stats->probe_bcast_cnt++;
    else client_vif_stats->probe_direct_cnt++;

    client_event_stats->type = PROBE;
    client_event_stats->rssi = rssi;
    client_event_stats->probe_bcast = probe_bcast;
    client_event_stats->probe_blocked = probe_blocked;
}

static void
ow_steer_bm_stats_set_connect(const struct osw_hwaddr *sta_addr,
                              const char *vif_name)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (client_vif_stats == NULL) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: CONNECT event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s",
         OSW_HWADDR_ARG(sta_addr),
         vif_name);

    client_vif_stats->connects++;
    client_vif_stats->connected = true;

    client_event_stats->type = CONNECT;
}

static void
ow_steer_bm_stats_set_disconnect(const struct osw_hwaddr *sta_addr,
                                 const char *vif_name,
                                 const dpp_bs_client_disconnect_src_t disconnect_src,
                                 const dpp_bs_client_disconnect_type_t disconnect_type,
                                 const uint32_t disconnect_reason)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (client_vif_stats == NULL) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;
    char *disconnect_src_name;
    char *disconnect_type_name;

    switch (disconnect_src) {
        case LOCAL:
            disconnect_src_name = "LOCAL";
            break;
        case REMOTE:
            disconnect_src_name = "REMOTE";
            break;
        case MAX_DISCONNECT_SOURCES:
            disconnect_src_name = "MAX_DISCONNECT_SOURCES";
            break;
        default:
            disconnect_src_name = "INVALID";
            break;
    }

    switch (disconnect_type) {
        case DISASSOC:
            disconnect_type_name = "DISASSOC";
            break;
        case DEAUTH:
            disconnect_type_name = "DEAUTH";
            break;
        case MAX_DISCONNECT_TYPES:
            disconnect_type_name = "MAX_DISCONNECT_TYPES";
            break;
        default:
            disconnect_type_name = "INVALID";
            break;
    }

    LOGT("ow: steer: bm: stats: DISCONNECT event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s"
         " disconnect_src: %s"
         " disconnect_type: %s"
         " disconnect_reason: %u",
         OSW_HWADDR_ARG(sta_addr),
         vif_name,
         disconnect_src_name,
         disconnect_type_name,
         disconnect_reason);

    client_vif_stats->disconnects++;
    client_vif_stats->connected = false;

    client_event_stats->type = DISCONNECT;
    client_event_stats->disconnect_src = disconnect_src;
    client_event_stats->disconnect_type = disconnect_type;
    client_event_stats->disconnect_reason = disconnect_reason;
}

static void
ow_steer_bm_stats_set_client_btm(const struct osw_hwaddr *sta_addr,
                                 const char *vif_name,
                                 const enum ow_steer_bm_kick_source kick_source,
                                 const bool retry)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (WARN_ON(client_vif_stats == NULL)) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;
    char *event_name = NULL;

    if (retry == true) {
        switch (kick_source) {
            case OW_STEER_BM_KICK_SOURCE_STEERING:
                client_event_stats->type = CLIENT_BS_BTM_RETRY;
                client_vif_stats->steering_kick_cnt++;
                event_name = "CLIENT_BTM_RETRY";
                break;
            case OW_STEER_BM_KICK_SOURCE_STICKY:
                client_event_stats->type = CLIENT_STICKY_BTM_RETRY;
                client_vif_stats->sticky_kick_cnt++;
                event_name = "CLIENT_STICKY_BTM_RETRY";
                break;
            case OW_STEER_BM_KICK_SOURCE_FORCE:
                client_event_stats->type = CLIENT_BTM_RETRY;
                event_name = "CLIENT_BTM_RETRY";
                break;
            case OW_STEER_BM_KICK_SOURCE_NON_POLICY:
                LOGD("ow: steer: bm: stats: sta_addr: "OSW_HWADDR_FMT" vif_name: %s"
                     " non-policy triggered btm, skip event",
                     OSW_HWADDR_ARG(sta_addr), vif_name);
                return;
            default:
                LOGE("ow: steer: bm: unhandled kick source enum value %d", kick_source);
                return;
        }
    }
    else {
        switch (kick_source) {
            case OW_STEER_BM_KICK_SOURCE_STEERING:
                client_event_stats->type = CLIENT_BS_BTM;
                client_vif_stats->steering_kick_cnt++;
                event_name = "CLIENT_BS_BTM";
                break;
            case OW_STEER_BM_KICK_SOURCE_STICKY:
                client_event_stats->type = CLIENT_STICKY_BTM;
                client_vif_stats->sticky_kick_cnt++;
                event_name = "CLIENT_STICKY_BTM";
                break;
            case OW_STEER_BM_KICK_SOURCE_FORCE:
                client_event_stats->type = CLIENT_BTM;
                event_name = "CLIENT_BTM";
                break;
            case OW_STEER_BM_KICK_SOURCE_NON_POLICY:
                LOGD("ow: steer: bm: stats: sta_addr: "OSW_HWADDR_FMT" vif_name: %s"
                     " non-policy triggered btm, skip event",
                     OSW_HWADDR_ARG(sta_addr), vif_name);
                return;
            default:
                LOGE("ow: steer: bm: unhandled kick source enum value %d", kick_source);
                return;
        }
    }

    LOGT("ow: steer: bm: stats: %s event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s",
         event_name,
         OSW_HWADDR_ARG(sta_addr),
         vif_name);
}

struct ow_steer_bm_stats_set_client_capabilities_params {
    bool is_BTM_supported;
    bool is_RRM_supported;
    bool band_cap_2G;
    bool band_cap_5G;
    bool band_cap_6G;
    uint32_t max_chwidth;
    uint32_t max_streams;
    uint32_t phy_mode;
    uint32_t max_MCS;
    uint32_t max_txpower;
    bool is_static_smps;
    bool is_mu_mimo_supported;
    bool rrm_caps_link_meas;
    bool rrm_caps_neigh_rpt;
    bool rrm_caps_bcn_rpt_passive;
    bool rrm_caps_bcn_rpt_active;
    bool rrm_caps_bcn_rpt_table;
    bool rrm_caps_lci_meas;
    bool rrm_caps_ftm_range_rpt;
    const uint8_t *assoc_ies;
    size_t assoc_ies_len;
};

static void
ow_steer_bm_stats_set_client_capabilities(const struct osw_hwaddr *sta_addr,
                                          const char *vif_name,
                                          const struct ow_steer_bm_stats_set_client_capabilities_params *params)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (WARN_ON(client_vif_stats == NULL)) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: CLIENT_CAPABILITIES event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s"
         " is_BTM_supported: "OSW_BOOL_FMT
         " is_RRM_supported: "OSW_BOOL_FMT
         " band_cap_2G: "OSW_BOOL_FMT
         " band_cap_5G: "OSW_BOOL_FMT
         " band_cap_6G: "OSW_BOOL_FMT
         " max_chwidth: %u"
         " max_streams: %u"
         " phy_mode: %u"
         " mac_MCS: %u"
         " max_txpower: %u"
         " is_static_smps: "OSW_BOOL_FMT
         " is_mu_mimo_supported: "OSW_BOOL_FMT
         " rrm_caps_link_meas: "OSW_BOOL_FMT
         " rrm_caps_neigh_rpt: "OSW_BOOL_FMT
         " rrm_caps_bcn_rpt_passive: "OSW_BOOL_FMT
         " rrm_caps_bcn_rpt_active: "OSW_BOOL_FMT
         " rrm_caps_bcn_rpt_table: "OSW_BOOL_FMT
         " rrm_caps_lci_meas: "OSW_BOOL_FMT
         " rrm_caps_ftm_range_rpt: "OSW_BOOL_FMT
         " assoc_ies: %s",
         OSW_HWADDR_ARG(sta_addr),
         vif_name,
         OSW_BOOL_ARG(params->is_BTM_supported),
         OSW_BOOL_ARG(params->is_RRM_supported),
         OSW_BOOL_ARG(params->band_cap_2G),
         OSW_BOOL_ARG(params->band_cap_5G),
         OSW_BOOL_ARG(params->band_cap_6G),
         params->max_chwidth,
         params->max_streams,
         params->phy_mode,
         params->max_MCS,
         params->max_txpower,
         OSW_BOOL_ARG(params->is_static_smps),
         OSW_BOOL_ARG(params->is_mu_mimo_supported),
         OSW_BOOL_ARG(params->rrm_caps_link_meas),
         OSW_BOOL_ARG(params->rrm_caps_neigh_rpt),
         OSW_BOOL_ARG(params->rrm_caps_bcn_rpt_passive),
         OSW_BOOL_ARG(params->rrm_caps_bcn_rpt_active),
         OSW_BOOL_ARG(params->rrm_caps_bcn_rpt_table),
         OSW_BOOL_ARG(params->rrm_caps_lci_meas),
         OSW_BOOL_ARG(params->rrm_caps_ftm_range_rpt),
         params->assoc_ies == NULL ? "not present" : "present");

    client_event_stats->type = CLIENT_CAPABILITIES;
    client_event_stats->is_BTM_supported = params->is_BTM_supported;
    client_event_stats->is_RRM_supported = params->is_RRM_supported;
    client_event_stats->band_cap_2G = params->band_cap_2G;
    client_event_stats->band_cap_5G = params->band_cap_5G;
    client_event_stats->band_cap_6G = params->band_cap_6G;
    client_event_stats->max_chwidth = params->max_chwidth;
    client_event_stats->max_streams = params->max_streams;
    client_event_stats->phy_mode = params->phy_mode;
    client_event_stats->max_MCS = params->max_MCS;
    client_event_stats->max_txpower = params->max_txpower;
    client_event_stats->is_static_smps = params->is_static_smps;
    client_event_stats->is_mu_mimo_supported = params->is_mu_mimo_supported;
    client_event_stats->rrm_caps_link_meas = params->rrm_caps_link_meas;
    client_event_stats->rrm_caps_neigh_rpt = params->rrm_caps_neigh_rpt;
    client_event_stats->rrm_caps_bcn_rpt_passive = params->rrm_caps_bcn_rpt_passive;
    client_event_stats->rrm_caps_bcn_rpt_active = params->rrm_caps_bcn_rpt_active;
    client_event_stats->rrm_caps_bcn_rpt_table = params->rrm_caps_bcn_rpt_table;
    client_event_stats->rrm_caps_lci_meas = params->rrm_caps_lci_meas;
    client_event_stats->rrm_caps_ftm_range_rpt = params->rrm_caps_ftm_range_rpt;
    if (params->assoc_ies != NULL) {
        client_event_stats->assoc_ies = MEMNDUP(params->assoc_ies, params->assoc_ies_len);
        client_event_stats->assoc_ies_len = params->assoc_ies_len;
    }
}

static void
ow_steer_bm_stats_set_client_backoff(const struct osw_hwaddr *sta_addr,
                                     const char *vif_name,
                                     const bool backoff_enabled,
                                     const uint32_t backoff_period)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (WARN_ON(client_vif_stats == NULL)) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: BACKOFF event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s"
         " backoff_enabled: %s"
         " backoff_period: %u",
         OSW_HWADDR_ARG(sta_addr),
         vif_name,
         backoff_enabled ? "true" : "false",
         backoff_period);

    client_event_stats->type = BACKOFF;
    client_event_stats->backoff_enabled = backoff_enabled;
    client_event_stats->backoff_period = backoff_period;
}

static void
ow_steer_bm_stats_set_client_band_steering_attempt(const struct osw_hwaddr *sta_addr,
                                                   const char *vif_name)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (WARN_ON(client_vif_stats == NULL)) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: BAND_STEERING_ATTEMPT event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s",
         OSW_HWADDR_ARG(sta_addr),
         vif_name);

    client_event_stats->type = BAND_STEERING_ATTEMPT;
}

static void
ow_steer_bm_stats_set_client_activity(const struct osw_hwaddr *sta_addr,
                                      const char *vif_name,
                                      const bool active)
{
    if (WARN_ON(sta_addr == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_steer_bm_vif_stats *client_vif_stats = ow_steer_bm_get_client_vif_stats(sta_addr, vif_name);
    if (WARN_ON(client_vif_stats == NULL)) return;
    struct ow_steer_bm_event_stats *client_event_stats = ow_steer_bm_get_new_client_event_stats(client_vif_stats);
    if (WARN_ON(client_event_stats == NULL)) return;

    LOGT("ow: steer: bm: stats: ACTIVITY event,"
         " mac: "OSW_HWADDR_FMT
         " vif_name: %s"
         " active: "OSW_BOOL_FMT,
         OSW_HWADDR_ARG(sta_addr),
         vif_name,
         OSW_BOOL_ARG(active));

    client_vif_stats->activity_changes++;
    client_event_stats->type = ACTIVITY;
    client_event_stats->active = active;
}

static bool
ow_steer_bm_report_gen(uint8_t **buf,
                       uint32_t *len)
{
    const size_t max = 128 * 1024;
#ifndef DPP_FAST_PACK
    if (dpp_get_queue_elements() == 0)
        return false;

    *buf = MALLOC(max);
    return dpp_get_report(*buf, max, len);
#else
    return dpp_get_report2(buf, max, len);
#endif
}

static void
ow_steer_bm_report_send(void)
{
    uint8_t *buf = NULL;
    uint32_t len;
    const bool generated = ow_steer_bm_report_gen(&buf, &len);
    if (generated == false) goto free_report;
    if (len <= 0) goto free_report;
    qm_response_t res;
    LOGI("ow: steer: bm: stats: sending report: len=%u", len);
    const bool sent = qm_conn_send_stats(buf, len, &res);
    WARN_ON(sent == false);
    /* FIXME: Should probably store the buffer if sending
     * failed and re-try later without re-generating reports
     */
free_report:
    FREE(buf);
}

#define OW_STEER_BM_STA_KICK_SET_DISASSOC_IMMINENT(STA, NAME) \
    do { \
        if ((STA) == NULL) break; \
        const bool *b = (((STA)->client != NULL) && \
                         ((STA)->client->NAME != NULL)) \
                      ? (STA)->client->NAME->disassoc_imminent.cur \
                      : NULL; \
        ow_steer_executor_action_btm_set_disassoc_imminent((STA)->btm_executor_action, b); \
    } while (0)

static void
ow_steer_bm_sta_kick_state_force_trig(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    struct ow_steer_bm_kick_state_monitor *kick_state_mon = &sta->kick_state_mon;

    LOGT("ow: steer: bm: kick_state_monitor: force kick trigger,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr));
    kick_state_mon->force_trig = true;
    OW_STEER_BM_STA_KICK_SET_DISASSOC_IMMINENT(sta, sc_btm_params);
}

static void
ow_steer_bm_sta_kick_state_steering_trig(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    struct ow_steer_bm_kick_state_monitor *kick_state_mon = &sta->kick_state_mon;

    LOGT("ow: steer: bm: kick_state_monitor: steering kick trigger,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr));
    kick_state_mon->steering_trig = true;
    OW_STEER_BM_STA_KICK_SET_DISASSOC_IMMINENT(sta, steering_btm_params);
}

static void
ow_steer_bm_sta_kick_state_sticky_trig(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    struct ow_steer_bm_kick_state_monitor *kick_state_mon = &sta->kick_state_mon;

    LOGT("ow: steer: bm: kick_state_monitor: sticky kick trigger,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr));
    kick_state_mon->sticky_trig = true;
    OW_STEER_BM_STA_KICK_SET_DISASSOC_IMMINENT(sta, sticky_btm_params);
}

static void
ow_steer_bm_sta_kick_state_reset(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    struct ow_steer_bm_kick_state_monitor *kick_state_mon = &sta->kick_state_mon;

    LOGT("ow: steer: bm: kick_state_monitor: state reset,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr));

    kick_state_mon->force_trig = false;
    kick_state_mon->sticky_trig = false;
    kick_state_mon->steering_trig = false;
    kick_state_mon->btm_send_cnt = 0;
    ow_steer_executor_action_btm_set_disassoc_imminent(sta->btm_executor_action, NULL);
}

static const char*
ow_steer_bm_sta_kick_state_source_to_str(const enum ow_steer_bm_kick_source kick_source)
{
    switch (kick_source) {
        case OW_STEER_BM_KICK_SOURCE_STEERING:
            return "OW_STEER_BM_KICK_SOURCE_STEERING";
        case OW_STEER_BM_KICK_SOURCE_STICKY:
            return "OW_STEER_BM_KICK_SOURCE_STICKY";
        case OW_STEER_BM_KICK_SOURCE_FORCE:
            return "OW_STEER_BM_KICK_SOURCE_FORCE";
        case OW_STEER_BM_KICK_SOURCE_INVALID:
            return "OW_STEER_BM_KICK_SOURCE_INVALID";
        case OW_STEER_BM_KICK_SOURCE_NON_POLICY:
            return "OW_STEER_BM_KICK_SOURCE_NON_POLICY";
    }
    return "INVALID_ENUM";
}

static enum ow_steer_bm_kick_source
ow_steer_bm_sta_kick_state_get_kick_type(struct ow_steer_bm_kick_state_monitor *kick_state_mon)
{
    LOGD("ow: steer: bm: kick_state_monitor: trying to determine kick source,"
         " force_trig: "OSW_BOOL_FMT
         " steering_trig: "OSW_BOOL_FMT
         " sticky_trig: "OSW_BOOL_FMT,
         OSW_BOOL_ARG(kick_state_mon->force_trig),
         OSW_BOOL_ARG(kick_state_mon->steering_trig),
         OSW_BOOL_ARG(kick_state_mon->sticky_trig));

    const bool force_trig = (kick_state_mon->force_trig == true) &&
                            (kick_state_mon->steering_trig == false) &&
                            (kick_state_mon->sticky_trig == false);
    const bool steering_trig = (kick_state_mon->force_trig == false) &&
                               (kick_state_mon->steering_trig == true) &&
                               (kick_state_mon->sticky_trig == false);
    const bool sticky_trig = (kick_state_mon->force_trig == false) &&
                             (kick_state_mon->steering_trig == false) &&
                             (kick_state_mon->sticky_trig == true);
    const bool non_policy_trig = (force_trig == false) &&
                                 (steering_trig == false) &&
                                 (sticky_trig == false);

    LOGT("ow: steer: bm: kick_state_monitor: final triggers, "
         " force_trig: "OSW_BOOL_FMT
         " steering_trig: "OSW_BOOL_FMT
         " sticky_trig: "OSW_BOOL_FMT
         " non_policy_trig: "OSW_BOOL_FMT,
         OSW_BOOL_ARG(force_trig),
         OSW_BOOL_ARG(steering_trig),
         OSW_BOOL_ARG(sticky_trig),
         OSW_BOOL_ARG(non_policy_trig));

    enum ow_steer_bm_kick_source kick_source = OW_STEER_BM_KICK_SOURCE_INVALID;
    if (force_trig == true)
        kick_source = OW_STEER_BM_KICK_SOURCE_FORCE;
    else if (steering_trig == true)
        kick_source = OW_STEER_BM_KICK_SOURCE_STEERING;
    else if (sticky_trig == true)
        kick_source = OW_STEER_BM_KICK_SOURCE_STICKY;
    else if (non_policy_trig == true)
        kick_source = OW_STEER_BM_KICK_SOURCE_NON_POLICY;
    else
        LOGW("ow: steer: bm: kick_state_monitor: ambiguous or invalid kick trigger");

    LOGD("ow: steer: bm: kick_state_monitor:"
         " determined kick source: %s ",
         ow_steer_bm_sta_kick_state_source_to_str(kick_source));

    return kick_source;
}

static void
ow_steer_bm_sta_kick_state_send_btm_event(struct ow_steer_bm_sta *sta,
                                          const char* vif_name)
{
    ASSERT(sta != NULL, "");

    struct ow_steer_bm_kick_state_monitor *kick_state_mon = &sta->kick_state_mon;
    kick_state_mon->btm_send_cnt++;

    if (sta->active_policy == NULL) {
        LOGD("ow: steer: bm: kick_state_monitor: sta_addr: "OSW_HWADDR_FMT
             " non-policy triggered, skip send_btm_event",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    LOGD("ow: steer: bm: kick_state_monitor:"
         " send_btm_event"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr));

    const bool retry = (kick_state_mon->btm_send_cnt > 1);
    enum ow_steer_bm_kick_source kick_source = ow_steer_bm_sta_kick_state_get_kick_type(kick_state_mon);
    ow_steer_bm_stats_set_client_btm(&sta->addr,
                                     vif_name,
                                     kick_source,
                                     retry);
}

static void
ow_steer_bm_candidate_assessor_assess_by_rrm(const struct ow_steer_bm_candidate_assessor *bm_assessor,
                                             struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(candidate_list != NULL, "");

    const struct osw_hwaddr *sta_addr = ow_steer_candidate_assessor_get_sta_addr(bm_assessor->base);
    const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(sta_addr);
    if (sta_info == NULL)
        return;
    if (WARN_ON(sta_info->vif == NULL))
        return;
    if (WARN_ON(sta_info->vif->drv_state == NULL))
        return;
    if (WARN_ON(sta_info->vif->drv_state->vif_type != OSW_VIF_AP))
        return;

    const struct osw_hwaddr *self_bssid = &sta_info->vif->drv_state->mac_addr;
    const struct osw_rrm_meas_rep_neigh *self_rrm_rep = osw_rrm_meas_get_neigh(sta_addr, self_bssid);
    if (self_rrm_rep == NULL)
        return;

    const uint64_t now_nsec = osw_time_mono_clk();
    const uint64_t validity_period_nsec = OSW_TIME_SEC(OW_STEER_BM_RRM_REP_VALIDITY_PERIOD_SEC);
    if ((now_nsec - self_rrm_rep->last_update_tstamp_nsec) > validity_period_nsec)
        return;

    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *candidate_bssid = ow_steer_candidate_get_bssid(candidate);
        const struct osw_rrm_meas_rep_neigh *candidate_rrm_rep = osw_rrm_meas_get_neigh(sta_addr, candidate_bssid);
        if (candidate_rrm_rep == NULL)
            continue;
        if ((now_nsec - candidate_rrm_rep->last_update_tstamp_nsec) > validity_period_nsec)
            continue;

        const uint8_t rcpi_diff = OW_STEER_BM_RRM_REP_BETTER_FACTOR * 2;
        if (candidate_rrm_rep->rcpi < (self_rrm_rep->rcpi + rcpi_diff))
            continue;

        /*
         * Make sure that "better" candidates (according to RRM Reports)
         * will end up a the beginning after sorting the list.
         */
        const unsigned int bias = 1000;
        const unsigned int metric = candidate_rrm_rep->rcpi + bias;
        ow_steer_candidate_set_metric(candidate, metric);
    }
}

static void
ow_steer_bm_candidate_assessor_assess_by_ovsdb_priority(const struct ow_steer_bm_candidate_assessor *bm_assessor,
                                                        struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(candidate_list != NULL, "");

    unsigned int max_priority_value = 0; /* The highest value -> the lowest priority */
    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const struct ow_steer_bm_neighbor *neighbor = ds_tree_find(&g_neighbor_tree, bssid);
        if (neighbor == NULL)
            continue;
        if (neighbor->priority.cur == NULL)
            continue;

        if (*neighbor->priority.cur > max_priority_value)
            max_priority_value = *neighbor->priority.cur;
    }

    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        if (ow_steer_candidate_get_metric(candidate) != 0)
            continue; /* Candidate was already assessed */

        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const struct ow_steer_bm_neighbor *neighbor = ds_tree_find(&g_neighbor_tree, bssid);
        if (neighbor == NULL)
            continue;
        if (neighbor->priority.cur == NULL)
            continue;

        if (WARN_ON(max_priority_value < *neighbor->priority.cur))
            continue;

        const unsigned int metric = max_priority_value - *neighbor->priority.cur;
        ow_steer_candidate_set_metric(candidate, metric);
    }
}

static bool
ow_steer_bm_candidate_assessor_assess_cb(struct ow_steer_candidate_assessor *assessor,
                                         struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(assessor != NULL, "");
    ASSERT(assessor->priv != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_bm_candidate_assessor *bm_assessor = assessor->priv;
    const struct osw_hwaddr *sta_addr = ow_steer_candidate_assessor_get_sta_addr(bm_assessor->base);

    const struct ow_steer_bm_client *client = ds_tree_find(&g_client_tree, sta_addr);
    if (client == NULL)
        return false;

    /* Reset metrics first */
    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        ow_steer_candidate_set_metric(candidate, 0);
    }

    const bool sort_by_rrm_report = client->neighbor_list_filter_by_beacon_report.cur != NULL ? *client->neighbor_list_filter_by_beacon_report.cur : false;
    if (sort_by_rrm_report == true)
        ow_steer_bm_candidate_assessor_assess_by_rrm(bm_assessor, candidate_list);

    const bool sort_by_ovsdb_priority = true;
    if (sort_by_ovsdb_priority == true)
        ow_steer_bm_candidate_assessor_assess_by_ovsdb_priority(bm_assessor, candidate_list);

    return true;
}

static void
ow_steer_bm_candidate_assessor_free_priv_cb(struct ow_steer_candidate_assessor *assessor)
{
    ASSERT(assessor != NULL, "");
    ASSERT(assessor->priv != NULL, "");

    struct ow_steer_bm_candidate_assessor *bm_assessor = assessor->priv;
    FREE(bm_assessor);
}

static struct ow_steer_candidate_assessor*
ow_steer_bm_candidate_assessor_create(const struct osw_hwaddr *sta_addr)
{
    ASSERT(sta_addr != NULL, "");

    const struct ow_steer_candidate_assessor_ops ops = {
        .assess_fn = ow_steer_bm_candidate_assessor_assess_cb,
        .free_priv_fn = ow_steer_bm_candidate_assessor_free_priv_cb,
    };
    struct ow_steer_bm_candidate_assessor *bm_assessor = CALLOC(1, sizeof(*bm_assessor));
    bm_assessor->base = ow_steer_candidate_assessor_create("bm_candidate_assessor", sta_addr, &ops, bm_assessor);

    return bm_assessor->base;
}

static void
ow_steer_bm_schedule_work_impl(void)
{
    osw_timer_arm_at_nsec(&g_work_timer, 0);
}

static void
ow_steer_bm_btm_params_free(struct ow_steer_bm_btm_params *btm_params)
{
    ASSERT(btm_params != NULL, "");

    OW_STEER_BM_MEM_ATTR_FREE(btm_params, bssid);
    OW_STEER_BM_MEM_ATTR_FREE(btm_params, disassoc_imminent);
    FREE(btm_params);
}

static void
ow_steer_bm_btm_params_update(struct ow_steer_bm_btm_params *btm_params,
                              struct ow_steer_bm_attr_state *state)
{
    ASSERT(state != NULL, "");

    memset(state, 0, sizeof(*state));

    if (btm_params == NULL)
        return;

    OW_STEER_BM_MEM_ATTR_UPDATE(btm_params, bssid);
    OW_STEER_BM_MEM_ATTR_UPDATE(btm_params, disassoc_imminent);

    state->changed = false;
    state->changed |= (bssid_state.changed == true);
    state->changed |= (disassoc_imminent_state.changed == true);
    state->present = true;
}

static void
ow_steer_bm_cs_params_free(struct ow_steer_bm_cs_params *cs_params)
{
    ASSERT(cs_params != NULL, "");

    OW_STEER_BM_MEM_ATTR_FREE(cs_params, band);
    OW_STEER_BM_MEM_ATTR_FREE(cs_params, enforce_period_secs);
    FREE(cs_params);
}

static void
ow_steer_bm_cs_params_update(struct ow_steer_bm_cs_params *cs_params,
                             struct ow_steer_bm_attr_state *state)
{
    ASSERT(state != NULL, "");

    memset(state, 0, sizeof(*state));

    if (cs_params == NULL)
        return;

    OW_STEER_BM_MEM_ATTR_UPDATE(cs_params, band);
    OW_STEER_BM_MEM_ATTR_UPDATE(cs_params, enforce_period_secs);

    state->changed = band_state.changed == true ||
                     enforce_period_secs_state.changed == true;

    state->present = true;
}

static void
ow_steer_bm_bss_update_channel(struct ow_steer_bm_bss *bss,
                               const struct osw_channel *channel)
{
    ASSERT(bss != NULL, "");

    struct osw_channel c;
    const struct osw_channel *old_channel = osw_bss_get_channel(&bss->bssid);
    if (old_channel != NULL) {
        /* Once osw_bss_entry_set_channel() is called, the
         * old_channel would be rendered invalid. Make a
         * snapshot and use that instead.
         */
        c = *old_channel;
        old_channel = &c;
    }

    osw_bss_entry_set_channel(bss->bss_entry, channel);

    if (bss->neighbor != NULL) {
        struct ow_steer_bm_observer *observer;
        ds_dlist_foreach(&g_observer_list, observer) {
            if (observer->neighbor_changed_channel_fn != NULL) {
                observer->neighbor_changed_channel_fn(observer,
                                                      bss->neighbor,
                                                      old_channel,
                                                      channel);
            }
        }
    }

    if (bss->vif == NULL) return;

    struct ow_steer_bm_observer *observer;
    ds_dlist_foreach(&g_observer_list, observer) {
        if (observer->vif_changed_fn != NULL) {
            observer->vif_changed_fn(observer, bss->vif);
        }

        if (channel != NULL && old_channel != NULL) {
            if (observer->vif_changed_channel_fn != NULL) {
                observer->vif_changed_channel_fn(observer,
                                                 bss->vif,
                                                 old_channel,
                                                 channel);
            }
        }
    }
}

static void
ow_steer_bm_bss_update_op_class(struct ow_steer_bm_bss *bss,
                                uint8_t *op_class)
{
    ASSERT(bss != NULL, "");
    osw_bss_entry_set_op_class(bss->bss_entry, op_class);

    if (bss->vif == NULL) return;

    struct ow_steer_bm_observer *observer;
    ds_dlist_foreach(&g_observer_list, observer)
        if (observer->vif_changed_fn != NULL)
            observer->vif_changed_fn(observer, bss->vif);
}

static void
ow_steer_bm_bss_unset(struct ow_steer_bm_bss *bss)
{
    ASSERT(bss != NULL, "");

    if (bss->removed == true)
        return;

    bss->removed = true;
    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_get_bss(struct ow_steer_bm_vif *vif,
                    struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT((neighbor != NULL) != (vif != NULL), ""); /* XOR */

    struct ow_steer_bm_bss *bss = CALLOC(1, sizeof(*bss));

    if (vif != NULL) {
        if (ow_steer_bm_vif_is_up(vif) == true) {
            vif->bss->removed = false;
            FREE(bss);
            return;
        }

        ASSERT(ow_steer_bm_vif_is_ready(vif) == true, "");
        memcpy(&bss->bssid, &vif->vif_info->drv_state->mac_addr, sizeof(bss->bssid));
        bss->vif = vif;
        bss->bss_entry = osw_bss_map_entry_new(g_bss_provider, &bss->bssid);
        ow_steer_bm_bss_update_channel(bss, &vif->vif_info->drv_state->u.ap.channel);
        uint8_t op_class;
        const bool result = osw_channel_to_op_class(&vif->vif_info->drv_state->u.ap.channel, &op_class);
        if (result == true)
            ow_steer_bm_bss_update_op_class(bss, &op_class);
        else
            LOGW("ow: steer: bm: vif vif_name: %s failed to infer op_class from channel: "OSW_CHANNEL_FMT,
                 vif->vif_name.buf, OSW_CHANNEL_ARG(&vif->vif_info->drv_state->u.ap.channel));
        bss->group = vif->group;
        ds_tree_insert(&vif->group->bss_tree, bss, &bss->bssid);

        vif->bss = bss;

        LOGD("ow: steer: bm: bss vif bssid: "OSW_HWADDR_FMT" vif_name: %s added group id: %s",
             OSW_HWADDR_ARG(&bss->bssid), vif->vif_name.buf, vif->group->id);

        OW_STEER_BM_SCHEDULE_WORK;

        struct ow_steer_bm_observer *observer;
        ds_dlist_foreach(&g_observer_list, observer)
            if (observer->vif_up_fn != NULL)
                observer->vif_up_fn(observer, vif);
    }

    if (neighbor != NULL) {
        if (ow_steer_bm_neighbor_is_up(neighbor) == true) {
            neighbor->bss->removed = false;
            FREE(bss);
            return;
        }

        ASSERT(ow_steer_bm_neighbor_is_ready(neighbor) == true, "");
        memcpy(&bss->bssid, &neighbor->bssid, sizeof(bss->bssid));
        bss->neighbor = neighbor;
        bss->bss_entry = osw_bss_map_entry_new(g_bss_provider, &bss->bssid);
        ow_steer_bm_bss_update_channel(bss, bss->neighbor->channel.cur);
        ow_steer_bm_bss_update_op_class(bss, bss->neighbor->op_class.cur);
        bss->group = neighbor->vif.cur->group;
        ds_tree_insert(&bss->group->bss_tree, bss, &bss->bssid);

        neighbor->bss = bss;

        LOGD("ow: steer: bm: bss neighbor bssid: "OSW_HWADDR_FMT" added to group id: %s",
             OSW_HWADDR_ARG(&bss->bssid), bss->group->id);

        OW_STEER_BM_SCHEDULE_WORK;

        struct ow_steer_bm_observer *observer;
        ds_dlist_foreach(&g_observer_list, observer)
            if (observer->neighbor_up_fn != NULL)
                observer->neighbor_up_fn(observer, neighbor);
    }

    ds_dlist_insert_tail(&g_bss_list, bss);

    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_bss_free(struct ow_steer_bm_bss *bss)
{
    ASSERT(bss != NULL, "");

    ASSERT((bss->neighbor != NULL) != (bss->vif != NULL), ""); /* XOR */
    if (bss->neighbor != NULL) {
        LOGD("ow: steer: bm: bss bssid: "OSW_HWADDR_FMT" removed from neighbor",
             OSW_HWADDR_ARG(&bss->neighbor->bssid));

        struct ow_steer_bm_observer *observer;
        ds_dlist_foreach(&g_observer_list, observer)
            if (observer->neighbor_down_fn != NULL)
                observer->neighbor_down_fn(observer, bss->neighbor);

        bss->neighbor->bss = NULL;
    }

    if (bss->vif != NULL) {
        LOGD("ow: steer: bm: bss bssid: "OSW_HWADDR_FMT" removed from vif vif_name: %s",
             OSW_HWADDR_ARG(&bss->bssid), bss->vif->vif_name.buf);

        struct ow_steer_bm_observer *observer;
        ds_dlist_foreach(&g_observer_list, observer)
            if (observer->vif_down_fn != NULL)
                observer->vif_down_fn(observer, bss->vif);

        bss->vif->bss = NULL;
    }
    ASSERT(bss->bss_entry != NULL, "");
    osw_bss_map_entry_free(g_bss_provider, bss->bss_entry);
    ASSERT(bss->group != NULL, "");
    ds_tree_remove(&bss->group->bss_tree, bss);
    FREE(bss);

    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_sta_rrm_req_tx_complete_cb(const struct osw_rrm_meas_sta_observer *observer)
{
    struct ow_steer_bm_sta_rrm *rrm = container_of(observer, struct ow_steer_bm_sta_rrm, desc_observer);

    LOGD("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" successfully issued rrm req", OSW_HWADDR_ARG(&rrm->sta->addr));
    if (ds_dlist_is_empty(&rrm->request_list) == true)
        return;

    const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_BM_RRM_MEAS_DELAY_SEC);
    osw_timer_arm_at_nsec(&rrm->timer, tstamp);
}

static void
ow_steer_bm_sta_rrm_req_tx_error_cb(const struct osw_rrm_meas_sta_observer *observer)
{
    struct ow_steer_bm_sta_rrm *rrm = container_of(observer, struct ow_steer_bm_sta_rrm, desc_observer);

    LOGD("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" failed to issue rrm req", OSW_HWADDR_ARG(&rrm->sta->addr));
    if (ds_dlist_is_empty(&rrm->request_list) == true)
        return;

    const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_BM_RRM_MEAS_DELAY_SEC);
    osw_timer_arm_at_nsec(&rrm->timer, tstamp);
}

static void
ow_steer_bm_sta_rrm_timer_cb(struct osw_timer* timer)
{
    struct ow_steer_bm_sta_rrm *rrm = container_of(timer, struct ow_steer_bm_sta_rrm, timer);

    struct ow_steer_bm_sta_rrm_req *request = ds_dlist_head(&rrm->request_list);
    if (WARN_ON(request == NULL))
        return;

    const bool rrm_req_set = osw_rrm_meas_desc_set_req_params(rrm->desc, &request->params);
    if (rrm_req_set == true) {
        LOGD("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" started issuing rrm req op_class: %u channel: %u",
             OSW_HWADDR_ARG(&rrm->sta->addr), request->params.op_class, request->params.channel);
    }
    else {
        LOGD("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" failed to start issuing rrm req op_class: %u channel: %u",
             OSW_HWADDR_ARG(&rrm->sta->addr), request->params.op_class, request->params.channel);
    }

    ds_dlist_remove(&rrm->request_list, request);
    FREE(request);

    const bool rearm_timer = (rrm_req_set == false) &&
                             (ds_dlist_is_empty(&rrm->request_list) == false);
    if (rearm_timer == true) {
        const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_BM_RRM_MEAS_DELAY_SEC);
        osw_timer_arm_at_nsec(&rrm->timer, tstamp);
    }
}

static void
ow_steer_bm_sta_rrm_add_request(struct ow_steer_bm_sta_rrm *rrm,
                                const struct osw_rrm_meas_req_params *rrm_req_params)
{
    ASSERT(rrm != NULL, "");
    ASSERT(rrm_req_params != NULL, "");

    struct ow_steer_bm_sta_rrm_req *entry;
    ds_dlist_foreach(&rrm->request_list, entry)
        if (memcmp(&entry->params, rrm_req_params, sizeof(entry->params)) == 0)
            return;

    struct ow_steer_bm_sta_rrm_req *request = CALLOC(1, sizeof(*request));
    memcpy(&request->params, rrm_req_params, sizeof(request->params));
    ds_dlist_insert_tail(&rrm->request_list, request);

    LOGI("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" scheduling rrm req op_class: %u channel: %u ssid: "OSW_SSID_FMT,
         OSW_HWADDR_ARG(&rrm->sta->addr), request->params.op_class, request->params.channel,
         OSW_SSID_ARG(&request->params.ssid));
}

static void
ow_steer_bm_sta_rrm_reset(struct ow_steer_bm_sta_rrm *rrm)
{
    ASSERT(rrm != NULL, "");

    osw_timer_disarm(&rrm->timer);
    osw_rrm_meas_desc_set_req_params(rrm->desc, NULL);

    struct ow_steer_bm_sta_rrm_req *req;
    struct ow_steer_bm_sta_rrm_req *tmp_req;
    ds_dlist_foreach_safe(&rrm->request_list, req, tmp_req) {
        ds_dlist_remove(&rrm->request_list, req);
        FREE(req);
    }
}

static void
ow_steer_bm_sta_rrm_scan_link_band(struct ow_steer_bm_sta_rrm *rrm)
{
    ASSERT(rrm != NULL, "");

    const struct osw_state_vif_info *vif_info = rrm->sta_info->vif;
    const struct osw_drv_vif_state *vif_drv = vif_info->drv_state;
    const enum osw_band vif_band = osw_channel_to_band(&vif_drv->u.ap.channel);
    if (vif_band == OSW_BAND_UNDEFINED) {
        LOGW("ow: steer: bm: sta: rrm: addr: "OSW_HWADDR_FMT" bssid: "OSW_HWADDR_FMT" failed to scan current band, cannot determine band",
             OSW_HWADDR_ARG(&rrm->sta->addr), OSW_HWADDR_ARG(&vif_drv->mac_addr));
        return;
    }

    ow_steer_bm_sta_rrm_reset(rrm);

    struct ow_steer_bm_group *group = rrm->sta->group;
    struct ow_steer_bm_bss *bss;
    ds_tree_foreach(&group->bss_tree, bss) {
        const struct osw_channel *bss_channel = osw_bss_get_channel(&bss->bssid);
        if (bss_channel == NULL)
            continue;

        const enum osw_band bss_band = osw_channel_to_band(bss_channel);
        if (vif_band == bss_band)
            continue;

        const int bss_channel_number = osw_freq_to_chan(bss_channel->control_freq_mhz);
        if (bss_channel_number == 0)
            continue;

        uint8_t bss_op_class;
        const bool found_op_class = osw_channel_to_op_class(bss_channel, &bss_op_class);
        if (found_op_class == false)
            continue;

        uint8_t bss_op_class_20mhz;
        const bool found_op_class_20mhz = osw_op_class_to_20mhz(bss_op_class, bss_channel_number, &bss_op_class_20mhz);
        if (found_op_class_20mhz == false)
            continue;

        struct osw_rrm_meas_req_params rrm_req_params;
        memset(&rrm_req_params, 0, sizeof(rrm_req_params));
        rrm_req_params.op_class = bss_op_class_20mhz;
        rrm_req_params.channel = bss_channel_number;
        memcpy(&rrm_req_params.ssid, &vif_drv->u.ap.ssid, sizeof(rrm_req_params.ssid));
        ow_steer_bm_sta_rrm_add_request(rrm, &rrm_req_params);
    }

    const bool rrm_requests_pending = (ds_dlist_is_empty(&rrm->request_list) == false);
    if (rrm_requests_pending == false)
        return;

    const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_BM_RRM_MEAS_AFTER_CONNECT_DELAY_SEC);
    osw_timer_arm_at_nsec(&rrm->timer, tstamp);
}

static void
ow_steer_bm_sta_rrm_scan_link_band_try(struct ow_steer_bm_sta_rrm *rrm)
{
    if (rrm == NULL) return;

    const struct ow_steer_bm_sta *sta = rrm->sta;
    if (sta == NULL) return;

    const struct ow_steer_bm_client *client = sta->client;
    if (client == NULL) return;

    const bool enabled = (client->send_rrm_after_assoc.cur != NULL)
                      && (*client->send_rrm_after_assoc.cur == true);
    const bool supported_by_sta = rrm->sta != NULL
                                ? rrm->sta->assoc_req_info.rrm_neighbor_bcn_act_meas
                                : false;
    const bool not_handled_yet = (rrm->handled == false);
    const bool handle = (enabled && supported_by_sta && not_handled_yet);
    const bool dont_handle = !handle;

    if (dont_handle) return;

    ow_steer_bm_sta_rrm_scan_link_band(rrm);
    rrm->handled = true;
}

static struct ow_steer_bm_sta_rrm*
ow_steer_bm_sta_rrm_create(struct ow_steer_bm_sta *sta,
                           const struct osw_state_sta_info *sta_info)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta_info != NULL, "");
    ASSERT(sta_info->vif != NULL, "");
    ASSERT(sta_info->vif->phy != NULL, "");

    const struct osw_state_vif_info *vif_info = sta_info->vif;
    struct ow_steer_bm_sta_rrm *rrm = CALLOC(1, sizeof(*rrm));

    rrm->sta = sta;
    rrm->sta_info = sta_info;
    osw_timer_init(&rrm->timer, ow_steer_bm_sta_rrm_timer_cb);
    rrm->desc_observer.req_tx_complete_fn = ow_steer_bm_sta_rrm_req_tx_complete_cb;
    rrm->desc_observer.req_tx_error_fn = ow_steer_bm_sta_rrm_req_tx_error_cb;
    rrm->desc = osw_rrm_meas_get_desc(&sta->addr, &rrm->desc_observer, vif_info->phy->phy_name, vif_info->vif_name);
    ds_dlist_init(&rrm->request_list, struct ow_steer_bm_sta_rrm_req, node);

    return rrm;
}

static void
ow_steer_bm_sta_rrm_free(struct ow_steer_bm_sta_rrm *rrm)
{
    if (rrm == NULL)
        return;

    ASSERT(rrm != NULL, "");

    osw_timer_disarm(&rrm->timer);
    osw_rrm_meas_desc_free(rrm->desc);

    struct ow_steer_bm_sta_rrm_req *req;
    struct ow_steer_bm_sta_rrm_req *tmp_req;
    ds_dlist_foreach_safe(&rrm->request_list, req, tmp_req) {
        ds_dlist_remove(&rrm->request_list, req);
        FREE(req);
    }

    FREE(rrm);
}

static bool
ow_steer_bm_assoc_req_is_band_capable(const struct osw_assoc_req_info *info,
                                      const enum osw_band band)
{
    ASSERT(info != NULL, "");

    if (info->op_class_cnt != 0) {
        unsigned int i;
        for (i = 0; i < info->op_class_cnt; i++) {
            const unsigned int op_class = info->op_class_list[i];
            enum osw_band curr_band;
            curr_band = osw_op_class_to_band(op_class);
            if (curr_band == OSW_BAND_UNDEFINED) {
                LOGN("ow: steer: bm: could not convert op_class number"
                     " to band, op_class: %d",
                     op_class);
                continue;
            }
            if (curr_band == band) return true;
        }
        return false;
    }

    if (info->channel_cnt != 0) {
        unsigned int i;
        for (i = 0; i < info->channel_cnt; i++) {
            struct osw_channel osw_chan;
            const unsigned int chan = info->channel_list[i];
            const bool ok = osw_channel_from_channel_num_width(info->channel_list[i],
                                                               OSW_CHANNEL_20MHZ,
                                                               &osw_chan);
            if (ok == false) {
                LOGN("ow: steer: bm: could not convert channel number"
                     " to 20MHz osw_channel, channel: %d",
                     chan);
                continue;
            }
            enum osw_band curr_band = osw_channel_to_band(&osw_chan);

            /* The Supported Channels Element _cannot_
             * represent 6GHz channels. The spec requires
             * 6GHz clients to use Operating Classes
             * Element. This prevents mis-reporting some 5GHz
             * channels as 6GHz channels.
             */
            if (curr_band == OSW_BAND_6GHZ) continue;

            if (curr_band == band) return true;
        }
        return false;
    }

    LOGN("ow: steer: bm: assoc_req_is_band_capable: no channels nor op_classes in assoc request");
    return false;
}

static int
ow_steer_bm_chwidth_to_idx(const enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return 0;
        case OSW_CHANNEL_40MHZ: return 1;
        case OSW_CHANNEL_80MHZ: return 2;
        case OSW_CHANNEL_160MHZ: return 3;
        case OSW_CHANNEL_80P80MHZ: return 4;
        case OSW_CHANNEL_320MHZ: return 5;
    }
    /* unreachable */
    return 0;
}

static bool
ow_steer_bm_sta_set_assoc_req(struct ow_steer_bm_sta *sta,
                              const void *ies,
                              const size_t len)
{
    struct osw_assoc_req_info info;
    MEMZERO(info);

    const bool parse_ok = osw_parse_assoc_req_ies(ies, len, &info);
    if (parse_ok == false) {
        LOGT("ow: steer: bm: parsing assoc req ies did not succeed");
        MEMZERO(info);
    }

    if (info.op_class_parse_errors > 0) {
        LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" associated with %u broken op_classes",
             OSW_HWADDR_ARG(&sta->addr),
             info.op_class_parse_errors);
    }

    const bool info_changed = (memcmp(&info, &sta->assoc_req_info, sizeof(info)) != 0);
    if (info_changed) {
        sta->assoc_req_info = info;
    }

    return info_changed;
}

static enum osw_channel_width
ow_steer_bm_sta_calc_width(const struct ow_steer_bm_sta *sta)
{
    const struct osw_state_vif_info *vif_info = (sta->sta_info != NULL)
                                              ? sta->sta_info->vif
                                              : NULL;
    const bool vif_is_ap = (vif_info != NULL)
                        && (vif_info->drv_state->vif_type == OSW_VIF_AP);
    const enum osw_channel_width sta_width = osw_assoc_req_to_max_chwidth(&sta->assoc_req_info);
    const enum osw_channel_width ap_width = vif_is_ap
                                          ? vif_info->drv_state->u.ap.channel.width
                                          : OSW_CHANNEL_20MHZ;
    const enum osw_channel_width width = osw_channel_width_min(sta_width, ap_width);
    return width;
}

static void
ow_steer_bm_sta_stats_fill_client_capabilities(struct ow_steer_bm_stats_set_client_capabilities_params *params,
                                               const struct ow_steer_bm_sta *sta)
{
    const enum osw_channel_width width = ow_steer_bm_sta_calc_width(sta);
    const struct osw_state_sta_info *sta_info = sta->sta_info;
    const struct osw_assoc_req_info *info = &sta->assoc_req_info;

    MEMZERO(*params);
    params->is_BTM_supported = info->wnm_bss_trans;
    params->is_RRM_supported = info->rrm_neighbor_bcn_act_meas;
    params->band_cap_2G = ow_steer_bm_assoc_req_is_band_capable(info, OSW_BAND_2GHZ);
    params->band_cap_5G = ow_steer_bm_assoc_req_is_band_capable(info, OSW_BAND_5GHZ);
    params->band_cap_6G = ow_steer_bm_assoc_req_is_band_capable(info, OSW_BAND_6GHZ);
    params->max_chwidth = ow_steer_bm_chwidth_to_idx(width);
    params->max_streams = osw_assoc_req_to_max_nss(info);
    params->phy_mode = 0;
    params->max_MCS = osw_assoc_req_to_max_mcs(info);
    params->max_txpower = info->max_tx_power > 0 ? info->max_tx_power : 0;
    params->is_static_smps = (info->ht_caps_smps == 0);
    params->is_mu_mimo_supported = info->vht_caps_mu_beamformee;
    params->rrm_caps_link_meas = info->rrm_neighbor_link_meas;
    params->rrm_caps_neigh_rpt = false;
    params->rrm_caps_bcn_rpt_passive = info->rrm_neighbor_bcn_pas_meas;
    params->rrm_caps_bcn_rpt_active = info->rrm_neighbor_bcn_act_meas;
    params->rrm_caps_bcn_rpt_table = info->rrm_neighbor_bcn_tab_meas;
    params->rrm_caps_lci_meas = info->rrm_neighbor_lci_meas;
    params->rrm_caps_ftm_range_rpt = info->rrm_neighbor_ftm_range_rep;
    params->assoc_ies = sta_info->assoc_req_ies;
    params->assoc_ies_len = sta_info->assoc_req_ies_len;
}

static void
ow_steer_bm_sta_update_info(struct ow_steer_bm_sta *sta,
                            const struct osw_state_sta_info *sta_info)
{
    const bool is_new = (sta->sta_info == NULL)
                     && (sta_info != NULL);

    sta->sta_info = sta_info;
    if (sta_info == NULL) return;

    const bool changed = ow_steer_bm_sta_set_assoc_req(sta,
                                                       sta_info->assoc_req_ies,
                                                       sta_info->assoc_req_ies_len);
    if (changed || is_new) {
        const bool assoc_ies_missing = (sta_info->assoc_req_ies_len == 0);
        if (is_new && assoc_ies_missing) {
            LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" is missing assoc ies on connect",
                 OSW_HWADDR_ARG(&sta->addr));
        }

        const struct osw_state_vif_info *vif_info = sta_info->vif;
        struct ow_steer_bm_stats_set_client_capabilities_params params;
        ow_steer_bm_sta_stats_fill_client_capabilities(&params, sta);
        ow_steer_bm_stats_set_client_capabilities(sta_info->mac_addr,
                                                  vif_info->vif_name,
                                                  &params);

        struct ow_steer_bm_sta_rrm *rrm = ds_tree_find(&sta->vif_rrm_tree, sta_info);
        ow_steer_bm_sta_rrm_scan_link_band_try(rrm);

        /* FIXME: This should probably be controlled more
         * tightly with regards to kick_type but that is
         * almost never really changed and makes little
         * sense to change.
         */
        ow_steer_executor_action_btm_set_enabled(sta->btm_executor_action,
                                                 sta->assoc_req_info.wnm_bss_trans);
        ow_steer_executor_action_deauth_set_delay_sec(sta->deauth_executor_action,
                                                      sta->assoc_req_info.wnm_bss_trans
                                                      ? 10
                                                      : 0);
    }
}

static uint64_t
ow_steer_bm_sta_get_shutdown_deadline_nsec_cb(void *priv)
{
    struct ow_steer_bm_sta *sta = priv;
    if (sta->sta_info == NULL) return 0;

    const char *vif_name = sta->sta_info->vif->vif_name;
    osw_defer_vif_down_t *defer_mod = OSW_MODULE_LOAD(osw_defer_vif_down);
    const uint64_t remaining_nsec = osw_defer_vif_down_get_remaining_nsec(defer_mod, vif_name);

    return remaining_nsec;
}

static void
ow_steer_bm_sta_state_sta_connected_cb(struct osw_state_observer *self,
                                       const struct osw_state_sta_info *sta_info)
{
    ASSERT(self != NULL, "");

    const struct osw_state_vif_info *vif_info = sta_info->vif;
    struct ow_steer_bm_sta *sta = container_of(self, struct ow_steer_bm_sta, state_observer);
    if (osw_hwaddr_cmp(&sta->addr, sta_info->mac_addr) != 0)
        return;

    ASSERT(sta_info != NULL, "");
    ASSERT(sta_info->vif != NULL, "");
    ASSERT(sta_info->vif->drv_state != NULL, "");
    ASSERT(sta_info->vif->drv_state->vif_type == OSW_VIF_AP, "");

    ow_steer_bm_stats_set_connect(sta_info->mac_addr,
                                  vif_info->vif_name);

    struct ow_steer_bm_sta_rrm *rrm = ds_tree_find(&sta->vif_rrm_tree, sta_info);
    if (WARN_ON(rrm != NULL)) {
        ds_tree_remove(&sta->vif_rrm_tree, rrm);
        ow_steer_bm_sta_rrm_free(rrm);
    }

    rrm = ow_steer_bm_sta_rrm_create(sta, sta_info);
    ds_tree_insert(&sta->vif_rrm_tree, rrm, rrm->sta_info);

    ow_steer_bm_sta_update_info(sta, sta_info);

    /* Sanity check for above ap_width assignment. It should
     * never really happen, but better safe and log.
     */
    WARN_ON(vif_info->drv_state->vif_type != OSW_VIF_AP);

    ow_steer_bm_stats_set_client_activity(sta_info->mac_addr,
                                          vif_info->vif_name,
                                          true);

    LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" connected to bssid: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr), OSW_HWADDR_ARG(&vif_info->drv_state->mac_addr));
}


static void
ow_steer_bm_sta_state_sta_changed_cb(struct osw_state_observer *self,
                                     const struct osw_state_sta_info *sta_info)
{
    ASSERT(self != NULL, "");

    struct ow_steer_bm_sta *sta = container_of(self, struct ow_steer_bm_sta, state_observer);
    if (osw_hwaddr_cmp(&sta->addr, sta_info->mac_addr) != 0)
        return;

    ow_steer_bm_sta_update_info(sta, sta_info);
}

static void
ow_steer_bm_sta_state_sta_disconnected_cb(struct osw_state_observer *self,
                                          const struct osw_state_sta_info *sta_info)
{
    ASSERT(self != NULL, "");
    ASSERT(sta_info != NULL, "");

    struct ow_steer_bm_sta *sta = container_of(self, struct ow_steer_bm_sta, state_observer);
    if (osw_hwaddr_cmp(&sta->addr, sta_info->mac_addr) != 0)
        return;

    ow_steer_bm_sta_update_info(sta, NULL);

    struct ow_steer_bm_sta_rrm *rrm = ds_tree_find(&sta->vif_rrm_tree, sta_info);
    if (rrm != NULL) {
        ds_tree_remove(&sta->vif_rrm_tree, rrm);
        ow_steer_bm_sta_rrm_free(rrm);
    }

    ow_steer_bm_sta_kick_state_reset(sta);
    ow_steer_policy_bss_filter_set_config(sta->cs_kick_filter_policy, NULL);

    /* FIXME */
    const struct osw_state_vif_info *vif_info = sta_info->vif;
    const dpp_bs_client_disconnect_type_t disconnect_type = DEAUTH;
    const dpp_bs_client_disconnect_src_t disconnect_src = LOCAL;
    const uint32_t disconnect_reason = 0;
    ow_steer_bm_stats_set_disconnect(sta_info->mac_addr,
                                     vif_info->vif_name,
                                     disconnect_src,
                                     disconnect_type,
                                     disconnect_reason);

    LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" disconnected from bssid: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->addr), OSW_HWADDR_ARG(&vif_info->drv_state->mac_addr));
}

static void
ow_steer_bm_sta_client_steering_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_bm_sta *sta = container_of(timer, struct ow_steer_bm_sta, client_steering_timer);
    ow_steer_bm_sta_clear_client_steering(sta);

    struct ow_steer_bm_client *client = sta->client;
    ASSERT(client != NULL, "");
    ASSERT(client->cs_state_mutate_fn != NULL, "");
    client->cs_state_mutate_fn(&sta->addr, OW_STEER_BM_CS_STATE_EXPIRED);

    LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" client steering timer expired", OSW_HWADDR_ARG(&sta->addr));
}

static void
ow_steer_bm_sta_create(struct ow_steer_bm_group *group,
                       struct ow_steer_bm_client *client)
{
    ASSERT(group != NULL, "");
    ASSERT(client != NULL, "");

    struct ow_steer_bm_sta *sta = CALLOC(1, sizeof(*sta));
    const struct ow_steer_policy_mediator policy_mediator = {
        .sched_recalc_stack_fn = ow_steer_bm_policy_mediator_sched_stack_recalc_cb,
        .trigger_executor_fn = ow_steer_bm_policy_mediator_trigger_executor_cb,
        .dismiss_executor_fn = ow_steer_bm_policy_mediator_dismiss_executor_cb,
        .notify_backoff_fn = ow_steer_bm_policy_mediator_notify_backoff_cb,
        .notify_steering_attempt_fn = ow_steer_bm_policy_mediator_notify_steering_attempt_cb,
        .priv = sta,
    };
    const struct ow_steer_executor_action_mediator action_mediator = {
        .sched_recall_fn = ow_steer_bm_executor_action_mediator_sched_recall_cb,
        .notify_going_busy_fn = ow_steer_bm_executor_action_mediator_going_busy_cb,
        .notify_data_sent_fn = ow_steer_bm_executor_action_mediator_data_sent_cb,
        .notify_going_idle_fn = ow_steer_bm_executor_action_mediator_going_idle_cb,
        .priv = sta,
    };

    memcpy(&sta->addr, &client->addr, sizeof(sta->addr));

    sta->steer_sta = ow_steer_sta_create(&sta->addr);
    sta->bss_filter_policy = ow_steer_policy_bss_filter_create("bss_filter", &sta->addr, &policy_mediator);
    sta->chan_cap_policy = ow_steer_policy_chan_cap_alloc("chan_cap", &sta->addr, &policy_mediator);
    sta->defer_vif_down_deny_policy = ow_steer_policy_bss_filter_create("defer_vif_down_deny", &sta->addr, &policy_mediator);
    sta->defer_vif_down_allow_policy = ow_steer_policy_bss_filter_create("defer_vif_down_allow", &sta->addr, &policy_mediator);
    sta->cs_kick_filter_policy = ow_steer_policy_bss_filter_create("cs_kick_filter", &sta->addr, &policy_mediator);
    sta->cs_allow_filter_policy = ow_steer_policy_bss_filter_create("cs_allow_filter", &sta->addr, &policy_mediator);
    sta->force_kick_policy = ow_steer_policy_force_kick_create(&sta->addr, &policy_mediator);
    sta->cs_deny_filter_policy = ow_steer_policy_bss_filter_create("cs_deny_filter", &sta->addr, &policy_mediator);
    sta->hwm_2g_policy = ow_steer_bm_policy_hwm_2g_alloc("hwm_2g", group, &sta->addr, &policy_mediator);
    sta->lwm_2g_xing_policy = ow_steer_policy_snr_xing_create("lwm_2g", &sta->addr, &policy_mediator);
    sta->lwm_5g_xing_policy = ow_steer_policy_snr_xing_create("lwm_5g", &sta->addr, &policy_mediator);
    sta->lwm_6g_xing_policy = ow_steer_policy_snr_xing_create("lwm_6g", &sta->addr, &policy_mediator);
    sta->bottom_lwm_2g_xing_policy = ow_steer_policy_snr_xing_create("bottom_lwm", &sta->addr, &policy_mediator);
    sta->pre_assoc_2g_policy = ow_steer_policy_pre_assoc_create("pre_assoc_2g", group, &sta->addr, &policy_mediator);
    sta->btm_response_policy = ow_steer_policy_btm_response_create("btm_response", &sta->addr, &policy_mediator);

    struct ow_steer_policy_stack *policy_stack = ow_steer_sta_get_policy_stack(sta->steer_sta);
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->bss_filter_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_chan_cap_get_base(sta->chan_cap_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->defer_vif_down_deny_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->defer_vif_down_allow_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_kick_filter_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_allow_filter_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_force_kick_get_base(sta->force_kick_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_deny_filter_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_2g_xing_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_5g_xing_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_6g_xing_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_bm_policy_hwm_2g_get_base(sta->hwm_2g_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_snr_xing_get_base(sta->bottom_lwm_2g_xing_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_pre_assoc_get_base(sta->pre_assoc_2g_policy));
    ow_steer_policy_stack_add(policy_stack, ow_steer_policy_btm_response_get_base(sta->btm_response_policy));

    sta->acl_executor_action = ow_steer_executor_action_acl_create(&sta->addr, &action_mediator);
    sta->btm_executor_action = ow_steer_executor_action_btm_create(&sta->addr, &action_mediator);
    sta->deauth_executor_action = ow_steer_executor_action_deauth_create(&sta->addr,&action_mediator);

    ow_steer_executor_action_btm_set_get_shutdown_deadline_nsec_fn(sta->btm_executor_action,
                                                                   ow_steer_bm_sta_get_shutdown_deadline_nsec_cb,
                                                                   sta);

    struct ow_steer_executor *executor = ow_steer_sta_get_executor(sta->steer_sta);
    ow_steer_executor_add(executor, ow_steer_executor_action_acl_get_base(sta->acl_executor_action));
    ow_steer_executor_add(executor, ow_steer_executor_action_btm_get_base(sta->btm_executor_action));
    ow_steer_executor_add(executor, ow_steer_executor_action_deauth_get_base(sta->deauth_executor_action));

    struct ow_steer_candidate_assessor *candidate_assessor = ow_steer_bm_candidate_assessor_create(&sta->addr);
    ow_steer_sta_set_candidate_assessor(sta->steer_sta, candidate_assessor);

    ds_tree_init(&sta->vif_rrm_tree, ds_void_cmp, struct ow_steer_bm_sta_rrm, node);
    sta->state_observer.sta_connected_fn = ow_steer_bm_sta_state_sta_connected_cb;
    sta->state_observer.sta_changed_fn = ow_steer_bm_sta_state_sta_changed_cb;
    sta->state_observer.sta_disconnected_fn = ow_steer_bm_sta_state_sta_disconnected_cb;

    osw_timer_init(&sta->client_steering_timer, ow_steer_bm_sta_client_steering_timer_cb);

    sta->client = client;
    ds_dlist_insert_tail(&client->sta_list, sta);
    ASSERT(ds_tree_find(&group->sta_tree, &sta->addr) == NULL, "");
    sta->group = group;
    ds_tree_insert(&group->sta_tree, sta, &sta->addr);

    ds_dlist_insert_tail(&g_sta_list, sta);

    osw_state_register_observer(&sta->state_observer);

    LOGD("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" added to group id: %s", OSW_HWADDR_ARG(&client->addr), group->id);

    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_sta_remove(struct ow_steer_bm_group *group,
                       struct ow_steer_bm_client *client)
{
    ASSERT(group != NULL, "");
    ASSERT(client != NULL, "");

    struct ow_steer_bm_sta *sta = ds_tree_find(&group->sta_tree, &client->addr);
    if (sta == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot remove from group id: %s, it does not exist",
              OSW_HWADDR_ARG(&client->addr),
              group->id);
        return;
    }

    const char *reason = (group->removed
                       ? " because group is being removed"
                       : (client->removed
                          ? " because client entry was removed"
                          : " but why "));
    LOGD("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" removing from group id: %s%s",
         OSW_HWADDR_ARG(&client->addr),
         group->id,
         reason);

    sta->removed = true;

    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_sta_free(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct ow_steer_bm_sta_rrm *rrm;
    struct ow_steer_bm_sta_rrm *tmp_rrm;
    ds_tree_foreach_safe(&sta->vif_rrm_tree, rrm, tmp_rrm) {
        ds_tree_remove(&sta->vif_rrm_tree, rrm);
        ow_steer_bm_sta_rrm_free(rrm);
    }

    osw_state_unregister_observer(&sta->state_observer);

    struct ow_steer_policy_stack *policy_stack = ow_steer_sta_get_policy_stack(sta->steer_sta);
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->bss_filter_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_chan_cap_get_base(sta->chan_cap_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->defer_vif_down_deny_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->defer_vif_down_allow_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_pre_assoc_get_base(sta->pre_assoc_2g_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_snr_xing_get_base(sta->bottom_lwm_2g_xing_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_2g_xing_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_5g_xing_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_snr_xing_get_base(sta->lwm_6g_xing_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_bm_policy_hwm_2g_get_base(sta->hwm_2g_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_force_kick_get_base(sta->force_kick_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_allow_filter_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_deny_filter_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_bss_filter_get_base(sta->cs_kick_filter_policy));
    ow_steer_policy_stack_remove(policy_stack, ow_steer_policy_btm_response_get_base(sta->btm_response_policy));

    ow_steer_policy_bss_filter_free(sta->bss_filter_policy);
    ow_steer_policy_chan_cap_free(sta->chan_cap_policy);
    ow_steer_policy_bss_filter_free(sta->defer_vif_down_deny_policy);
    ow_steer_policy_bss_filter_free(sta->defer_vif_down_allow_policy);
    ow_steer_policy_pre_assoc_free(sta->pre_assoc_2g_policy);
    ow_steer_policy_snr_xing_free(sta->bottom_lwm_2g_xing_policy);
    ow_steer_policy_snr_xing_free(sta->lwm_2g_xing_policy);
    ow_steer_policy_snr_xing_free(sta->lwm_5g_xing_policy);
    ow_steer_policy_snr_xing_free(sta->lwm_6g_xing_policy);
    ow_steer_bm_policy_hwm_2g_free(sta->hwm_2g_policy);
    ow_steer_policy_force_kick_free(sta->force_kick_policy);
    ow_steer_policy_bss_filter_free(sta->cs_allow_filter_policy);
    ow_steer_policy_bss_filter_free(sta->cs_deny_filter_policy);
    ow_steer_policy_bss_filter_free(sta->cs_kick_filter_policy);
    ow_steer_policy_btm_response_free(sta->btm_response_policy);

    struct ow_steer_executor *executor = ow_steer_sta_get_executor(sta->steer_sta);
    ow_steer_executor_remove(executor, ow_steer_executor_action_acl_get_base(sta->acl_executor_action));
    ow_steer_executor_remove(executor, ow_steer_executor_action_btm_get_base(sta->btm_executor_action));
    ow_steer_executor_remove(executor, ow_steer_executor_action_deauth_get_base(sta->deauth_executor_action));

    ow_steer_executor_action_acl_free(sta->acl_executor_action);
    ow_steer_executor_action_btm_free(sta->btm_executor_action);
    ow_steer_executor_action_deauth_free(sta->deauth_executor_action);

    osw_timer_disarm(&sta->client_steering_timer);

    ASSERT(sta->client != NULL, "");
    ds_dlist_remove(&sta->client->sta_list, sta);
    ASSERT(sta->group != NULL, "");
    ds_tree_remove(&sta->group->sta_tree, sta);
    ow_steer_sta_free(sta->steer_sta);

    FREE(sta);
}

static bool
ow_steer_bm_vif_would_use_dfs(struct ow_steer_bm_vif *vif,
                              const struct osw_channel *c)
{
    if (c == NULL) return false;
    const enum osw_band band = osw_channel_to_band(c);
    if (band != OSW_BAND_5GHZ) return false;
    if (vif->vif_info->phy == NULL) return false;
    if (vif->vif_info->phy->drv_state == NULL) return false;
    const struct osw_channel_state *cs = vif->vif_info->phy->drv_state->channel_states;
    const size_t n_cs = vif->vif_info->phy->drv_state->n_channel_states;

    return osw_cs_chan_is_control_dfs(cs, n_cs, c);
}

static const struct osw_channel *
ow_steer_bm_vif_get_channel(struct ow_steer_bm_vif *vif)
{
    if (vif->vif_info == NULL) return NULL;
    if (vif->vif_info->drv_state == NULL) return NULL;

    return &vif->vif_info->drv_state->u.ap.channel;
}

static bool
ow_steer_bm_vif_uses_dfs(struct ow_steer_bm_vif *vif)
{
    const struct osw_channel *c = ow_steer_bm_vif_get_channel(vif);
    return ow_steer_bm_vif_would_use_dfs(vif, c);
}

static bool
ow_steer_bm_group_uses_dfs(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");

    struct ow_steer_bm_vif *vif;
    ds_tree_foreach(&group->vif_tree, vif) {
        if (ow_steer_bm_vif_uses_dfs(vif)) {
            return true;
        }
    }

    return false;
}

static struct ow_steer_policy_pre_assoc_config*
ow_steer_bm_sta_recalc_pre_assoc_2g_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");
    ASSERT(sta->group != NULL, "");

    struct ow_steer_policy_pre_assoc_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    const struct ow_steer_bm_vif *vif_2g = ow_steer_bm_group_lookup_vif_by_band(sta->group, OSW_BAND_2GHZ);
    if (vif_2g == NULL)
        return NULL;
    if (ow_steer_bm_vif_is_up(vif_2g) == false)
        return NULL;

    memcpy(&policy_config.bssid, &vif_2g->vif_info->drv_state->mac_addr, sizeof(policy_config.bssid));

    const struct ow_steer_bm_client *client = sta->client;
    if (client->backoff_secs.cur == NULL)
        return NULL;
    if (client->backoff_exp_base.cur == NULL)
        return NULL;

    policy_config.backoff_timeout_sec = *client->backoff_secs.cur;
    policy_config.backoff_exp_base = *client->backoff_exp_base.cur;

    if (client->max_rejects.cur == NULL)
        return NULL;
    if (client->rejects_tmout_secs.cur == NULL)
        return NULL;
    if (client->pref_5g_pre_assoc_block_timeout_msecs.cur == NULL)
        return NULL;

    const bool max_rejects_is_nonzero = (*client->max_rejects.cur > 0);
    const bool pref_5g_pre_assoc_block_timeout_msecs_is_nonzero = (*client->pref_5g_pre_assoc_block_timeout_msecs.cur > 0);

    if (max_rejects_is_nonzero == true && pref_5g_pre_assoc_block_timeout_msecs_is_nonzero == false) {
        policy_config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER;
        policy_config.reject_condition.params.counter.reject_limit = *client->max_rejects.cur;
        policy_config.reject_condition.params.counter.reject_timeout_sec = *client->rejects_tmout_secs.cur;
    }
    else if (max_rejects_is_nonzero == false && pref_5g_pre_assoc_block_timeout_msecs_is_nonzero == true) {
        policy_config.reject_condition.type = OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER;
        policy_config.reject_condition.params.timer.reject_timeout_msec = *client->pref_5g_pre_assoc_block_timeout_msecs.cur;
    }
    else {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" failed to configure 2g pre-assoc policy, "
             "ambiguous cfg max_rejects: %u pref_5g_pre_assoc_block_timeout_msecs: %u",
             OSW_HWADDR_ARG(&sta->addr), *client->max_rejects.cur, *client->pref_5g_pre_assoc_block_timeout_msecs.cur);
        return NULL;
    }

    if (client->pref_5g.cur == NULL)
        return NULL;

    switch (*client->pref_5g.cur) {
        case OW_STEER_BM_CLIENT_PREF_5G_NEVER:
            return NULL;
        case OW_STEER_BM_CLIENT_PREF_5G_NON_DFS:
            if (ow_steer_bm_group_uses_dfs(sta->group))  {
                return NULL;
            }
            /* FALLTHROUGH */
            /* non-DFS behaves like ALWAYS if
             * none of the 5G radios use DFS channels.
             */
        case OW_STEER_BM_CLIENT_PREF_5G_ALWAYS:
            policy_config.backoff_condition.type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_NONE;
            break;
        case OW_STEER_BM_CLIENT_PREF_5G_HWM:
            if (client->hwm.cur == NULL)
                return NULL;

            policy_config.backoff_condition.type = OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR;
            policy_config.backoff_condition.params.threshold_snr.threshold_snr = *client->hwm.cur;
            break;
    }

    if (client->pre_assoc_auth_block.cur != NULL) {
        policy_config.immediate_backoff_on_auth_req = *client->pre_assoc_auth_block.cur
                                                    ? false
                                                    : true;
    }
    else {
        policy_config.immediate_backoff_on_auth_req = false;
    }

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_snr_xing_config*
ow_steer_bm_sta_recalc_lwm_2g_xing_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");
    ASSERT(sta->group != NULL, "");

    struct ow_steer_policy_snr_xing_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    const struct ow_steer_bm_vif *vif_2g = ow_steer_bm_group_lookup_vif_by_band(sta->group, OSW_BAND_2GHZ);
    if (vif_2g == NULL)
        return NULL;
    if (ow_steer_bm_vif_is_up(vif_2g) == false)
        return NULL;

    const struct ow_steer_bm_client *client = sta->client;
    if (client->lwm.cur == NULL)
        return NULL;

    if (client->kick_upon_idle.cur == NULL)
        return NULL;

    policy_config.snr = *client->lwm.cur;
    policy_config.mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;
    policy_config.mode_config.lwm.txrx_bytes_limit.active = *client->kick_upon_idle.cur;
    policy_config.mode_config.lwm.txrx_bytes_limit.delta = OW_STEER_BM_DEFAULT_BITRATE_THRESHOLD / 8;
    memcpy(&policy_config.bssid, &vif_2g->vif_info->drv_state->mac_addr, sizeof(policy_config.bssid));

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_snr_xing_config*
ow_steer_bm_sta_recalc_lwm_5g_xing_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");
    ASSERT(sta->group != NULL, "");

    struct ow_steer_policy_snr_xing_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    const struct ow_steer_bm_vif *vif_5g = ow_steer_bm_group_lookup_vif_by_band(sta->group, OSW_BAND_5GHZ);
    if (vif_5g == NULL)
        return NULL;
    if (ow_steer_bm_vif_is_up(vif_5g) == false)
        return NULL;

    const struct ow_steer_bm_client *client = sta->client;
    if (client->lwm.cur == NULL)
        return NULL;

    if (client->kick_upon_idle.cur == NULL)
        return NULL;

    policy_config.snr = *client->lwm.cur;
    policy_config.mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;
    policy_config.mode_config.lwm.txrx_bytes_limit.active = *client->kick_upon_idle.cur;
    policy_config.mode_config.lwm.txrx_bytes_limit.delta = OW_STEER_BM_DEFAULT_BITRATE_THRESHOLD / 8;
    memcpy(&policy_config.bssid, &vif_5g->vif_info->drv_state->mac_addr, sizeof(policy_config.bssid));

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_snr_xing_config*
ow_steer_bm_sta_recalc_lwm_6g_xing_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");
    ASSERT(sta->group != NULL, "");

    struct ow_steer_policy_snr_xing_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    const struct ow_steer_bm_vif *vif_6g = ow_steer_bm_group_lookup_vif_by_band(sta->group, OSW_BAND_6GHZ);
    if (vif_6g == NULL)
        return NULL;
    if (ow_steer_bm_vif_is_up(vif_6g) == false)
        return NULL;

    const struct ow_steer_bm_client *client = sta->client;
    if (client->lwm.cur == NULL)
        return NULL;

    if (client->kick_upon_idle.cur == NULL)
        return NULL;

    policy_config.snr = *client->lwm.cur;
    policy_config.mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;
    policy_config.mode_config.lwm.txrx_bytes_limit.active = *client->kick_upon_idle.cur;
    policy_config.mode_config.lwm.txrx_bytes_limit.delta = OW_STEER_BM_DEFAULT_BITRATE_THRESHOLD / 8;
    memcpy(&policy_config.bssid, &vif_6g->vif_info->drv_state->mac_addr, sizeof(policy_config.bssid));

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_snr_xing_config*
ow_steer_bm_sta_recalc_bottom_lwm_2g_xing_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");
    ASSERT(sta->group != NULL, "");

    struct ow_steer_policy_snr_xing_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    const struct ow_steer_bm_vif *vif_2g = ow_steer_bm_group_lookup_vif_by_band(sta->group, OSW_BAND_2GHZ);
    if (vif_2g == NULL)
        return NULL;
    if (ow_steer_bm_vif_is_up(vif_2g) == false)
        return NULL;

    const struct ow_steer_bm_client *client = sta->client;
    if (client->bottom_lwm.cur == NULL)
        return NULL;

    policy_config.snr = *client->bottom_lwm.cur;
    policy_config.mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM;
    memcpy(&policy_config.bssid, &vif_2g->vif_info->drv_state->mac_addr, sizeof(policy_config.bssid));

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_force_kick_config*
ow_steer_bm_sta_get_force_kick_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct ow_steer_policy_force_kick_config config;
    memset(&config, 0, sizeof(config));

    struct ow_steer_bm_client *client = sta->client;
    if (client->force_kick.cur == NULL)
        return NULL;

    return MEMNDUP(&config, sizeof(config));
}

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_sta_get_directed_kick_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const struct ow_steer_bm_client *client = sta->client;
    const bool client_steering_in_progress = osw_timer_is_armed(&sta->client_steering_timer);
    if (client_steering_in_progress == false) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" attempt to issue directed kick outside of client steering, aborting",
             OSW_HWADDR_ARG(&sta->addr));
        return NULL;
    }

    ASSERT(sta->client != NULL, "");
    const struct ow_steer_bm_btm_params *sc_btm_params = client->sc_btm_params;
    if (sc_btm_params == NULL) {
        LOGD("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot issue directed kick, no sc_btm_params",
             OSW_HWADDR_ARG(&sta->addr));
        return NULL;
    }

    const struct osw_hwaddr *bssid = sc_btm_params->bssid.cur;
    if (bssid == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot issue directed kick, no bssid in sc_btm_params",
             OSW_HWADDR_ARG(&sta->addr));
        return NULL;
    }

    if (client->sc_kick_type.cur == NULL) {
        LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" implying deauth sc_kick_type",
             OSW_HWADDR_ARG(&sta->addr));
    }

    const enum ow_steer_bm_client_sc_kick_type sc_kick_type = (client->sc_kick_type.cur == NULL)
                                                            ? OW_STEER_BM_CLIENT_SC_KICK_TYPE_DEAUTH
                                                            : *client->sc_kick_type.cur;

    switch (sc_kick_type) {
        case OW_STEER_BM_CLIENT_SC_KICK_TYPE_DEAUTH:
            {
                if (sta->sta_info == NULL) {
                    LOGD("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot issue directed kick (deauth), no sta link",
                         OSW_HWADDR_ARG(&sta->addr));
                    return NULL;
                }

                /* This basically expresses "don't be here, go someplace else" */
                struct ow_steer_policy_bss_filter_config *bss_filter_policy_config = CALLOC(1, sizeof(*bss_filter_policy_config));
                const struct osw_hwaddr *bssid = &sta->sta_info->vif->drv_state->mac_addr;
                bss_filter_policy_config->included_preference.override = true;
                bss_filter_policy_config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
                memcpy(&bss_filter_policy_config->bssid_list[0], bssid, sizeof(bss_filter_policy_config->bssid_list[0]));
                bss_filter_policy_config->bssid_list_len = 1;
                return bss_filter_policy_config;
            }
            break;
        case OW_STEER_BM_CLIENT_SC_KICK_TYPE_BTM_DEAUTH:
            {
                struct ow_steer_policy_bss_filter_config *bss_filter_policy_config = CALLOC(1, sizeof(*bss_filter_policy_config));
                bss_filter_policy_config->included_preference.override = true;
                bss_filter_policy_config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
                bss_filter_policy_config->excluded_preference.override = true;
                bss_filter_policy_config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
                memcpy(&bss_filter_policy_config->bssid_list[0], bssid, sizeof(bss_filter_policy_config->bssid_list[0]));
                bss_filter_policy_config->bssid_list_len = 1;
                return bss_filter_policy_config;
            }
            break;
    }

    return NULL;
}

static void
ow_steer_bm_sta_clear_client_steering(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    ow_steer_policy_bss_filter_set_config(sta->cs_allow_filter_policy, NULL);
    ow_steer_policy_bss_filter_set_config(sta->cs_deny_filter_policy, NULL);
    osw_timer_disarm(&sta->client_steering_timer);
}

static void
ow_steer_bm_sta_stop_client_steering(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    const bool client_steering_in_progress = osw_timer_is_armed(&sta->client_steering_timer);

    ow_steer_bm_sta_clear_client_steering(sta);

    const struct ow_steer_bm_client *client = sta->client;
    ASSERT(sta->client != NULL, "");
    ASSERT(client->cs_state_mutate_fn != NULL, "");

    if (client_steering_in_progress == true) {
        /* FIXME: This should report FAILED based on reject
         * threshold. Reject thresholds are not counted
         * currently.
         */
        client->cs_state_mutate_fn(&sta->addr, OW_STEER_BM_CS_STATE_NONE);
    }
    else {
        /* This is handled in ow_steer_bm_sta_client_steering_timer_cb() */
    }

    LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" stopped client steering", OSW_HWADDR_ARG(&sta->addr));
}

static bool
ow_steer_bm_chan_is_5gl(const struct osw_channel *c)
{
    const int b5ch36 = 5180;
    const int b5ch100 = 5500;
    const int freq = c->control_freq_mhz;
    return (freq >= b5ch36) && (freq < b5ch100);
}

static bool
ow_steer_bm_chan_is_5gu(const struct osw_channel *c)
{
    const int b5ch100 = 5500;
    const int b5ch177 = 5885;
    const int freq = c->control_freq_mhz;
    return (freq >= b5ch100) && (freq <= b5ch177);
}

static void
ow_steer_bm_sta_start_inbound_client_steering(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const bool client_steering_in_progress = osw_timer_is_armed(&sta->client_steering_timer);
    if (client_steering_in_progress == true) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, steering already in progress",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    const struct ow_steer_bm_client *client = sta->client;
    const struct ow_steer_bm_cs_params *cs_params = client->cs_params;
    if (cs_params == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, no cs_params",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    const enum ow_steer_bm_cs_params_band *cs_band = cs_params->band.cur;
    if (cs_band == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, undefined band",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    bool bss_must_be_5gl = false;
    bool bss_must_be_5gu = false;

    enum osw_band band = OSW_BAND_UNDEFINED;
    switch (*cs_band) {
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_2G:
            band = OSW_BAND_2GHZ;
            break;
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5G:
            band = OSW_BAND_5GHZ;
            break;
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GL:
            band = OSW_BAND_5GHZ;
            bss_must_be_5gl = true;
            break;
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GU:
            band = OSW_BAND_5GHZ;
            bss_must_be_5gu = true;
            break;
        case OW_STEER_BM_CLIENT_CS_PARAMS_BAND_6G:
            band = OSW_BAND_6GHZ;
            break;
    }

    if (WARN_ON(band == OSW_BAND_UNDEFINED))
        return;

    const unsigned int *cs_enforce_period_secs = cs_params->enforce_period_secs.cur;
    if (cs_enforce_period_secs == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, no cs_enforce_period_secs",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    struct ow_steer_policy_bss_filter_config *allow_filter_policy_config = CALLOC(1, sizeof(*allow_filter_policy_config));
    struct ow_steer_policy_bss_filter_config *deny_filter_policy_config = CALLOC(1, sizeof(*deny_filter_policy_config));

    allow_filter_policy_config->included_preference.override = true;
    allow_filter_policy_config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;

    deny_filter_policy_config->excluded_preference.override = true;
    deny_filter_policy_config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED;

    struct ow_steer_bm_group *group = sta->group;
    struct ow_steer_bm_bss *bss = NULL;
    ds_tree_foreach(&group->bss_tree, bss) {
        if (bss->vif == NULL)
            continue;

        const struct osw_channel *channel = osw_bss_get_channel(&bss->bssid);
        if (WARN_ON(channel == NULL))
            continue;

        const enum osw_band bss_band = osw_freq_to_band(channel->control_freq_mhz);
        if (bss_band != band)
            continue;

        if (bss_must_be_5gl && ow_steer_bm_chan_is_5gl(channel) == false)
            continue;

        if (bss_must_be_5gu && ow_steer_bm_chan_is_5gu(channel) == false)
            continue;

        const size_t i = allow_filter_policy_config->bssid_list_len;
        if (WARN_ON(i >= ARRAY_SIZE(allow_filter_policy_config->bssid_list)))
            break;

        memcpy(&allow_filter_policy_config->bssid_list[i], &bss->bssid, sizeof(allow_filter_policy_config->bssid_list[i]));
        memcpy(&deny_filter_policy_config->bssid_list[i], &bss->bssid, sizeof(deny_filter_policy_config->bssid_list[i]));
        allow_filter_policy_config->bssid_list_len++;
        deny_filter_policy_config->bssid_list_len++;
    }

    if (allow_filter_policy_config->bssid_list_len == 0) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, no band: %s vap found",
             OSW_HWADDR_ARG(&sta->addr), osw_band_into_cstr(band));
        FREE(allow_filter_policy_config);
        FREE(deny_filter_policy_config);
        return;
    }

    ow_steer_policy_bss_filter_set_config(sta->cs_allow_filter_policy, allow_filter_policy_config);
    ow_steer_policy_bss_filter_set_config(sta->cs_deny_filter_policy, deny_filter_policy_config);

    const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(*cs_enforce_period_secs);
    osw_timer_arm_at_nsec(&sta->client_steering_timer, tstamp);

    ASSERT(client->cs_state_mutate_fn != NULL, "");
    client->cs_state_mutate_fn(&sta->addr, OW_STEER_BM_CS_STATE_STEERING);

    LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" started inbound client steering", OSW_HWADDR_ARG(&sta->addr));
}

static void
ow_steer_bm_sta_start_outbound_client_steering(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    struct ow_steer_bm_group *group = sta->group;
    ASSERT(group != NULL, "");

    const bool client_steering_in_progress = osw_timer_is_armed(&sta->client_steering_timer);
    if (client_steering_in_progress == true) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start outbound client steering, steering already in progress",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    const struct ow_steer_bm_client *client = sta->client;
    const struct ow_steer_bm_cs_params *cs_params = client->cs_params;
    if (cs_params == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start inbound client steering, no cs_params",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    const unsigned int *cs_enforce_period_secs = cs_params->enforce_period_secs.cur;
    if (cs_enforce_period_secs == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" cannot start outbound client steering, no cs_enforce_period_secs",
             OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    const uint64_t tstamp = osw_time_mono_clk() + OSW_TIME_SEC(*cs_enforce_period_secs);
    osw_timer_arm_at_nsec(&sta->client_steering_timer, tstamp);

    struct ow_steer_policy_bss_filter_config *bss_filter_policy_config = CALLOC(1, sizeof(*bss_filter_policy_config));
    bss_filter_policy_config->included_preference.override = true;
    bss_filter_policy_config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED;

    struct ow_steer_bm_bss *bss;
    ds_tree_foreach(&group->bss_tree, bss) {
        const bool not_a_local_vif = (bss->vif == NULL);
        if (not_a_local_vif) continue;

        size_t *i = &bss_filter_policy_config->bssid_list_len;
        const size_t max = ARRAY_SIZE(bss_filter_policy_config->bssid_list);
        if (WARN_ON(*i >= max)) break;

        struct osw_hwaddr *slot = &bss_filter_policy_config->bssid_list[*i];
        memcpy(slot, &bss->bssid, sizeof(*slot));
        (*i)++;
    }

    ow_steer_policy_bss_filter_set_config(sta->cs_allow_filter_policy, NULL);
    ow_steer_policy_bss_filter_set_config(sta->cs_deny_filter_policy, bss_filter_policy_config);

    ASSERT(client->cs_state_mutate_fn != NULL, "");
    client->cs_state_mutate_fn(&sta->addr, OW_STEER_BM_CS_STATE_STEERING);

    LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" started outbound client steering", OSW_HWADDR_ARG(&sta->addr));
}

static void
ow_steer_bm_sta_recalc_client_steering(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const struct ow_steer_bm_client *client = sta->client;
    const enum ow_steer_bm_client_cs_mode client_steering_mode = client->cs_mode.cur != NULL ? *client->cs_mode.cur : OW_STEER_BM_CLIENT_CS_MODE_OFF;

    switch (client_steering_mode) {
        case OW_STEER_BM_CLIENT_CS_MODE_OFF:
            ow_steer_bm_sta_stop_client_steering(sta);
            break;
        case OW_STEER_BM_CLIENT_CS_MODE_HOME:
            ow_steer_bm_sta_start_inbound_client_steering(sta);
            break;
        case OW_STEER_BM_CLIENT_CS_MODE_AWAY:
            ow_steer_bm_sta_start_outbound_client_steering(sta);
            break;
    }
}

static void
ow_steer_bm_sta_recalc(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    /*
     * FIXME
     * Recalculating policies' configs on each run is heavy and unnecessary in most
     * cases. However, it makes sure all policies will be reconfigured and we won't
     * miss any change in config source.
     *
     * This code should be improved once steering get stabilized.
     */

    struct ow_steer_policy_bss_filter_config *bss_filter_policy_config = ow_steer_bm_group_create_bss_filter_policy_config(sta->group);
    ow_steer_policy_bss_filter_set_config(sta->bss_filter_policy, bss_filter_policy_config);
    struct ow_steer_policy_bss_filter_config *defer_vif_down_deny_policy_config = ow_steer_bm_sta_create_defer_vif_down_deny_policy_config(sta);
    ow_steer_policy_bss_filter_set_config(sta->defer_vif_down_deny_policy, defer_vif_down_deny_policy_config);
    struct ow_steer_policy_bss_filter_config *defer_vif_down_allow_policy_config = ow_steer_bm_sta_create_defer_vif_down_allow_policy_config(sta);
    ow_steer_policy_bss_filter_set_config(sta->defer_vif_down_allow_policy, defer_vif_down_allow_policy_config);
    struct ow_steer_policy_pre_assoc_config *pre_assoc_2g_policy_config = ow_steer_bm_sta_recalc_pre_assoc_2g_policy_config(sta);
    ow_steer_policy_pre_assoc_set_config(sta->pre_assoc_2g_policy, pre_assoc_2g_policy_config);
    struct ow_steer_policy_snr_xing_config *lwm_2g_xing_policy_config = ow_steer_bm_sta_recalc_lwm_2g_xing_policy_config(sta);
    ow_steer_policy_snr_xing_set_config(sta->lwm_2g_xing_policy, lwm_2g_xing_policy_config);
    struct ow_steer_policy_snr_xing_config *lwm_5g_xing_policy_config = ow_steer_bm_sta_recalc_lwm_5g_xing_policy_config(sta);
    ow_steer_policy_snr_xing_set_config(sta->lwm_5g_xing_policy, lwm_5g_xing_policy_config);
    struct ow_steer_policy_snr_xing_config *lwm_6g_xing_policy_config = ow_steer_bm_sta_recalc_lwm_6g_xing_policy_config(sta);
    ow_steer_policy_snr_xing_set_config(sta->lwm_6g_xing_policy, lwm_6g_xing_policy_config);
    struct ow_steer_policy_snr_xing_config *bottom_lwm_2g_xing_policy_config = ow_steer_bm_sta_recalc_bottom_lwm_2g_xing_policy_config(sta);
    ow_steer_policy_snr_xing_set_config(sta->bottom_lwm_2g_xing_policy, bottom_lwm_2g_xing_policy_config);

    ow_steer_bm_policy_hwm_2g_update(sta->hwm_2g_policy,
                                     sta->client->hwm.cur,
                                     sta->client->kick_upon_idle.cur);

    if (sta->issue_force_kick == true) {
        const struct ow_steer_bm_client *client = sta->client;
        ASSERT(client->force_kick.cur != NULL, "");

        switch (*client->force_kick.cur) {
            case OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE:
                {
                    struct ow_steer_policy_force_kick_config *force_kick_policy_config = ow_steer_bm_sta_get_force_kick_policy_config(sta);
                    ow_steer_policy_force_kick_set_oneshot_config(sta->force_kick_policy, force_kick_policy_config);
                }
                break;
            case OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED:
                {
                    struct ow_steer_policy_bss_filter_config *cs_kick_filter_policy_config = ow_steer_bm_sta_get_directed_kick_policy_config(sta);
                    if (cs_kick_filter_policy_config != NULL) {
                        LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" issuing force directed kick to bssid: "OSW_HWADDR_FMT" using cs_kick_filter_policy",
                             OSW_HWADDR_ARG(&sta->addr), OSW_HWADDR_ARG(&cs_kick_filter_policy_config->bssid_list[0]));
                        ow_steer_policy_bss_filter_set_config(sta->cs_kick_filter_policy, cs_kick_filter_policy_config);
                    }
                    else {
                        LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" issuing force directed kick without bssid",
                             OSW_HWADDR_ARG(&sta->addr));
                        struct ow_steer_policy_force_kick_config *force_kick_policy_config = ow_steer_bm_sta_get_force_kick_policy_config(sta);
                        ow_steer_policy_force_kick_set_oneshot_config(sta->force_kick_policy, force_kick_policy_config);
                    }
                }
                break;
        }

        sta->issue_force_kick = false;
    }

    if (sta->client_steering_recalc == true) {
        ow_steer_bm_sta_recalc_client_steering(sta);
        sta->client_steering_recalc = false;
    }

    if (sta->sta_info != NULL) {
        struct ow_steer_bm_sta_rrm *rrm = ds_tree_find(&sta->vif_rrm_tree, sta->sta_info);
        ow_steer_bm_sta_rrm_scan_link_band_try(rrm);
    }
}

static void
ow_steer_bm_sta_execute_force_speculative_kick(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const struct ow_steer_bm_client *client = sta->client;

    if (client->sc_kick_type.cur == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" attempt to issue force speculative kick with (nil) type, aborting", OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    LOGN("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" executing force speculative %s kick", OSW_HWADDR_ARG(&sta->addr),
         ow_steer_bm_client_sc_kick_type_to_str(*client->sc_kick_type.cur));

    ow_steer_sta_schedule_executor_call(sta->steer_sta);
}

static void
ow_steer_bm_sta_execute_force_directed_kick(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ow_steer_sta_schedule_executor_call(sta->steer_sta);
}

static void
ow_steer_bm_sta_execute_hwm_kick(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const struct ow_steer_bm_client *client = sta->client;

    if (client->kick_type.cur == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" attempt to issue hwm kick with (nil) kick_type, aborting", OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    LOGI("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" executing hwm %s kick", OSW_HWADDR_ARG(&sta->addr),
         ow_steer_bm_client_kick_type_to_str(*client->kick_type.cur));

    ow_steer_sta_schedule_executor_call(sta->steer_sta);
}

static void
ow_steer_bm_sta_execute_force_kick(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta->client != NULL, "");

    const struct ow_steer_bm_client *client = sta->client;

    if (client->force_kick.cur == NULL) {
        LOGW("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" attempt to issue (nil) force kick, aborting", OSW_HWADDR_ARG(&sta->addr));
        return;
    }

    switch (*client->force_kick.cur) {
        case OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE:
            ow_steer_bm_sta_execute_force_speculative_kick(sta);
            break;
        case OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED:
            ow_steer_bm_sta_execute_force_directed_kick(sta);
            break;
    }
}

static void
ow_steer_bm_vif_set_vif_info(struct ow_steer_bm_vif *vif,
                             const struct osw_state_vif_info *vif_info)
{
    ASSERT(vif != NULL, "");

    const bool adding = (vif->vif_info == NULL)
                     && (vif_info != NULL);
    const bool changing = (vif->vif_info != NULL)
                       && (vif_info != NULL);
    const bool removing = (vif->vif_info != NULL)
                       && (vif_info == NULL);

    if (adding) {
        LOGD("ow: steer: bm: vif: %s claimed to group %s",
             vif->vif_name.buf,
             vif->group->id);
    }

    if (changing) {
        LOGD("ow: steer: bm: vif: %s still claimed to group %s",
             vif->vif_name.buf,
             vif->group->id);
    }

    if (removing) {
        LOGD("ow: steer: bm: vif: %s unclaimed from group %s",
             vif->vif_name.buf,
             vif->group->id);

        const bool unset_bss = ow_steer_bm_vif_is_up(vif) == true;
        if (unset_bss == true)
            ow_steer_bm_bss_unset(vif->bss);
    }

    vif->vif_info = vif_info;

    OW_STEER_BM_SCHEDULE_WORK;
}

static bool
ow_steer_bm_vif_is_ready(const struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");
    return vif->vif_info != NULL;
}

static bool
ow_steer_bm_vif_is_up(const struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");
    return vif->bss != NULL;
}

static void
ow_steer_bm_vif_free(struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");

    LOGD("ow: steer: bm: vif vif_name: %s removed from group id: %s", vif->vif_name.buf, vif->group->id);

    osw_defer_vif_down_observer_free(vif->defer_vif_down_obs);
    osw_defer_vif_down_rule_free(vif->defer_vif_down_rule);
    osw_state_unregister_observer(&vif->state_obs);

    struct ow_steer_bm_observer *observer;
    ds_dlist_foreach(&g_observer_list, observer)
        if (observer->vif_removed_fn != NULL)
            observer->vif_removed_fn(observer, vif);

    ASSERT(vif->bss == NULL, "");
    ASSERT(vif->group != NULL, "");
    ds_tree_remove(&vif->group->vif_tree, vif);
    FREE(vif);
}

static void
ow_steer_bm_vif_recalc(struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");

    const struct osw_drv_vif_state *state = vif->vif_info != NULL
                                          ? vif->vif_info->drv_state
                                          : NULL;
    const struct osw_drv_vif_state_ap *ap = state != NULL
                                          ? &state->u.ap
                                          : NULL;
    const bool is_enabled = state != NULL
                          ? (state->status == OSW_VIF_ENABLED)
                          : false;
    const bool is_ap = (state != NULL && state->vif_type == OSW_VIF_AP);

    const bool not_created_yet = (vif->bss == NULL);
    const bool create_bss = (not_created_yet && is_ap);
    if (create_bss) {
        ow_steer_bm_get_bss(vif, NULL);
    }

    const bool can_update = (vif->bss != NULL);
    if (can_update) {
        const struct osw_channel *channel = (is_ap && is_enabled)
                                          ? &ap->channel
                                          : NULL;
        uint8_t op_class = 0;

        if (channel != NULL) {
            const bool op_class_found = osw_channel_to_op_class(channel, &op_class);
            const bool op_class_not_found = !op_class_found;
            WARN_ON(op_class_not_found);
        }

        ow_steer_bm_bss_update_channel(vif->bss, channel);
        ow_steer_bm_bss_update_op_class(vif->bss, &op_class);
    }
}

static bool
ow_steer_bm_neighbor_is_ready(const struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");

    const bool vif_name_ready = neighbor->vif_name.valid && neighbor->vif_name.cur != NULL;
    const bool channel_ready = neighbor->channel.valid && neighbor->channel.cur != NULL;
    const bool op_class_ready = neighbor->op_class.valid && neighbor->op_class.cur != NULL;
    const bool vif_present = neighbor->vif.cur != NULL;

    return vif_name_ready &&
           channel_ready &&
           op_class_ready &&
           vif_present;
}

static bool
ow_steer_bm_neighbor_is_up(const struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");
    return neighbor->bss != NULL;
}

static void
ow_steer_bm_neighbor_free(struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");

    LOGD("ow: steer: bm: neighbor: bssid: "OSW_HWADDR_FMT" removed", OSW_HWADDR_ARG(&neighbor->bssid));

    OW_STEER_BM_MEM_ATTR_FREE(neighbor, vif_name);
    OW_STEER_BM_MEM_ATTR_FREE(neighbor, channel_number);
    OW_STEER_BM_MEM_ATTR_FREE(neighbor, ht_mode);
    OW_STEER_BM_MEM_ATTR_FREE(neighbor, op_class);
    OW_STEER_BM_MEM_ATTR_FREE(neighbor, priority);
    OW_STEER_BM_MEM_ATTR_FREE(neighbor, channel);
    ASSERT(neighbor->bss == NULL, "");
    ow_steer_bm_observer_unregister(&neighbor->observer);
    FREE(neighbor);
}

static void
ow_steer_bm_neighbor_set_channel(struct ow_steer_bm_neighbor *neighbor,
                                 const struct osw_channel *channel)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(neighbor, channel);
}

static void
ow_steer_bm_neighbor_set_vif(struct ow_steer_bm_neighbor *neighbor,
                             struct ow_steer_bm_vif *vif)
{
    ASSERT(neighbor != NULL, "");

    if (neighbor->vif.next == vif)
        return;

    neighbor->vif.valid = false;
    neighbor->vif.next = vif;

    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_neighbor_recalc(struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");

    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, vif_name);
    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, channel_number);
    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, ht_mode);
    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, op_class);
    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, priority);
    OW_STEER_BM_MEM_ATTR_UPDATE(neighbor, channel);
    OW_STEER_BM_PTR_ATTR_UPDATE(neighbor, vif);

    const bool invalidate_vif = vif_name_state.changed == true;
    if (invalidate_vif == true)
        ow_steer_bm_neighbor_set_vif(neighbor, NULL);

    const bool invalidate_channel = channel_number_state.changed == true ||
                                    op_class_state.changed == true ||
                                    ht_mode_state.changed == true;
    if (invalidate_channel == true)
        ow_steer_bm_neighbor_set_channel(neighbor, NULL);

    const bool invalidate_bss = ow_steer_bm_neighbor_is_up(neighbor) == true &&
                                (vif_name_state.present == false ||
                                 vif_state.present == false);
    if (invalidate_bss == true)
        ow_steer_bm_bss_unset(neighbor->bss);

    const bool lookup_vif = vif_state.present == false &&
                            vif_name_state.present == true;
    if (lookup_vif == true) {
        struct ow_steer_bm_vif *vif = ds_tree_find(&g_vif_tree, neighbor->vif_name.cur);
        if (vif != NULL)
            if (ow_steer_bm_vif_is_up(vif) == true)
                ow_steer_bm_neighbor_set_vif(neighbor, vif);
    }

    const bool create_channel = channel_state.present == false &&
                                (channel_number_state.present == true &&
                                 (op_class_state.present == true ||
                                  ht_mode_state.present == true));
    if (create_channel == true) {
        struct osw_channel channel;
        const uint8_t chan = *neighbor->channel_number.cur;

        LOGD("ow: steer: bm: neighbor: bssid: "OSW_HWADDR_FMT" channel/ht_mode/op_class: %"PRIu8"/%d/%"PRIu8,
             OSW_HWADDR_ARG(&neighbor->bssid),
             neighbor->op_class.cur ? *neighbor->op_class.cur : 0,
             neighbor->ht_mode.cur ? *neighbor->ht_mode.cur : 0,
             neighbor->channel_number.cur ? *neighbor->channel_number.cur : 0);

        if (op_class_state.present) {
            const uint8_t op_class = *neighbor->op_class.cur;
            const bool ok = osw_channel_from_op_class(op_class, chan, &channel);
            WARN_ON(!ok);
            if (ok) {
                ow_steer_bm_neighbor_set_channel(neighbor, &channel);
            }
        }
        else if (ht_mode_state.present) {
            const enum ow_steer_bm_neighbor_ht_mode ht_mode = *neighbor->ht_mode.cur;
            const enum osw_channel_width width = ow_steer_bm_neighbor_ht_mode_to_channel_width(ht_mode);
            const bool ok = osw_channel_from_channel_num_width(chan, width, &channel);
            WARN_ON(!ok);
            if (ok) {
                ow_steer_bm_neighbor_set_channel(neighbor, &channel);
            }
        }
        else {
            WARN_ON(1);
        }
    }

    const bool create_bss = ow_steer_bm_neighbor_is_up(neighbor) == false &&
                            ow_steer_bm_neighbor_is_ready(neighbor) == true;
    if (create_bss == true)
        ow_steer_bm_get_bss(NULL, neighbor);

    const bool update_bss_channel = ow_steer_bm_neighbor_is_up(neighbor) == true &&
                                    ow_steer_bm_neighbor_is_ready(neighbor) == true &&
                                    channel_state.changed == true;
    if (update_bss_channel == true)
        ow_steer_bm_bss_update_channel(neighbor->bss, neighbor->channel.cur);

    const bool update_op_class = ow_steer_bm_neighbor_is_up(neighbor) == true &&
                                 ow_steer_bm_neighbor_is_ready(neighbor) == true &&
                                 op_class_state.changed == true;
    if (update_op_class == true)
        ow_steer_bm_bss_update_op_class(neighbor->bss, neighbor->op_class.cur);
}

static const struct ow_steer_bm_vif*
ow_steer_bm_group_lookup_vif_by_band(struct ow_steer_bm_group *group,
                                     enum osw_band band)
{
    ASSERT(group != NULL, "");

    struct ow_steer_bm_vif *vif;
    ds_tree_foreach(&group->vif_tree, vif) {
        if (vif->vif_info == NULL)
            continue;

        const enum osw_band vif_band = osw_channel_to_band(&vif->vif_info->drv_state->u.ap.channel);
        if (vif_band == band)
            break;
    }

    return vif;
}

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_group_create_bss_filter_policy_config(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");

    struct ow_steer_policy_bss_filter_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    policy_config.included_preference.override = false;
    policy_config.included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    policy_config.excluded_preference.override = true;
    policy_config.excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;

    struct ow_steer_bm_bss *bss = NULL;
    ds_tree_foreach(&group->bss_tree, bss) {
        const size_t i = policy_config.bssid_list_len;
        memcpy(&policy_config.bssid_list[i], &bss->bssid, sizeof(policy_config.bssid_list[i]));
        policy_config.bssid_list_len++;
    }

    if (policy_config.bssid_list_len == 0)
        return NULL;

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_sta_create_defer_vif_down_deny_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct ow_steer_bm_group *group = sta->group;
    ASSERT(group != NULL, "");

    struct ow_steer_policy_bss_filter_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    policy_config.included_preference.override = true;
    policy_config.included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    policy_config.excluded_preference.override = false;
    policy_config.excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;

    struct ow_steer_bm_bss *bss;
    ds_tree_foreach(&group->bss_tree, bss) {
        const bool is_not_shutting_down = (bss->vif == NULL)
                                       || (bss->vif->shutting_down == false);
        if (is_not_shutting_down) continue;

        const size_t i = policy_config.bssid_list_len;
        const bool not_enough_room = (i >= ARRAY_SIZE(policy_config.bssid_list));
        if (WARN_ON(not_enough_room)) continue;

        LOGT("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" excluding %s ("OSW_HWADDR_FMT"): it is shutting down",
             OSW_HWADDR_ARG(&sta->addr),
             bss->vif->vif_name.buf,
             OSW_HWADDR_ARG(&bss->bssid));

        struct osw_hwaddr *slot = &policy_config.bssid_list[i];
        memcpy(slot, &bss->bssid, sizeof(*slot));
        policy_config.bssid_list_len++;
    }

    const bool no_local_vifs_are_shutting_down = (policy_config.bssid_list_len == 0);
    if (no_local_vifs_are_shutting_down)
        return NULL;

    ds_tree_foreach(&group->bss_tree, bss) {
        const bool is_not_neighbor = (bss->neighbor == NULL);
        if (is_not_neighbor) continue;

        const size_t i = policy_config.bssid_list_len;
        const bool not_enough_room = (i >= ARRAY_SIZE(policy_config.bssid_list));
        if (WARN_ON(not_enough_room)) continue;

        LOGT("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" excluding %s ("OSW_HWADDR_FMT"): it is a neighbor",
             OSW_HWADDR_ARG(&sta->addr),
             bss->vif->vif_name.buf,
             OSW_HWADDR_ARG(&bss->bssid));

        struct osw_hwaddr *slot = &policy_config.bssid_list[i];
        memcpy(slot, &bss->bssid, sizeof(*slot));
        policy_config.bssid_list_len++;
    }

    if (policy_config.bssid_list_len == 0)
        return NULL;

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static struct ow_steer_policy_bss_filter_config*
ow_steer_bm_sta_create_defer_vif_down_allow_policy_config(struct ow_steer_bm_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct ow_steer_bm_group *group = sta->group;
    ASSERT(group != NULL, "");

    struct ow_steer_policy_bss_filter_config policy_config;
    memset(&policy_config, 0, sizeof(policy_config));

    policy_config.included_preference.override = true;
    policy_config.included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
    policy_config.excluded_preference.override = false;
    policy_config.excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;

    struct ow_steer_bm_bss *bss;
    size_t num_of_vifs_shutting_down = 0;
    ds_tree_foreach(&group->bss_tree, bss) {
        const bool is_shutting_down = (bss->vif != NULL)
                                   && (bss->vif->shutting_down == true);
        if (is_shutting_down) {
            num_of_vifs_shutting_down++;
        }
    }

    if (num_of_vifs_shutting_down == 0)
        return NULL;

    ds_tree_foreach(&group->bss_tree, bss) {
        const bool is_neighbor = (bss->neighbor != NULL);
        const bool is_shutting_down = (bss->vif != NULL)
                                   && (bss->vif->shutting_down == true);
        if (is_neighbor || is_shutting_down) continue;

        const size_t i = policy_config.bssid_list_len;
        const bool not_enough_room = (i >= ARRAY_SIZE(policy_config.bssid_list));
        if (WARN_ON(not_enough_room)) continue;

        LOGT("ow: steer: bm: sta addr: "OSW_HWADDR_FMT" including %s ("OSW_HWADDR_FMT"): it is local fallback",
             OSW_HWADDR_ARG(&sta->addr),
             bss->vif->vif_name.buf,
             OSW_HWADDR_ARG(&bss->bssid));

        struct osw_hwaddr *slot = &policy_config.bssid_list[i];
        memcpy(slot, &bss->bssid, sizeof(*slot));
        policy_config.bssid_list_len++;
    }

    if (policy_config.bssid_list_len == 0)
        return NULL;

    return MEMNDUP(&policy_config, sizeof(policy_config));
}

static void
ow_steer_bm_group_free_pending_vifs(struct ow_steer_bm_group *group)
{
    struct ow_steer_bm_vif_pending *vif;
    while ((vif = ds_tree_remove_head(&group->pending_vifs)) != NULL) {
        FREE(vif);
    }
}

static void
ow_steer_bm_group_free(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");

    ow_steer_bm_group_free_pending_vifs(group);
    FREE(group->id);
    ASSERT(ds_tree_is_empty(&group->vif_tree) == true, "");
    ASSERT(ds_tree_is_empty(&group->sta_tree) == true, "");
    ASSERT(ds_tree_is_empty(&group->bss_tree) == true, "");
    FREE(group);
}

static void
ow_steer_bm_group_try_adding_pending_vifs(struct ow_steer_bm_group *group)
{
    struct ow_steer_bm_vif_pending *vif;
    struct ds_tree head = group->pending_vifs;
    ds_tree_init(&group->pending_vifs,
                 (ds_key_cmp_t *)osw_ifname_cmp,
                 struct ow_steer_bm_vif_pending,
                 group_node);

    while ((vif = ds_tree_remove_head(&head)) != NULL) {
        struct ow_steer_bm_vif *v = ow_steer_bm_group_get_vif(group, vif->vif_name.buf);
        if (v != NULL) {
            LOGI("ow: steer: bm: vif vif_name: %s added pending to group: %s",
                    vif->vif_name.buf,
                    group->id);
        }
        FREE(vif);
    }
}

static void
ow_steer_bm_group_recalc(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");

    ow_steer_bm_group_try_adding_pending_vifs(group);
}

static void
ow_steer_bm_client_free(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    OW_STEER_BM_MEM_ATTR_FREE(client, hwm);
    OW_STEER_BM_MEM_ATTR_FREE(client, lwm);
    OW_STEER_BM_MEM_ATTR_FREE(client, bottom_lwm);
    OW_STEER_BM_MEM_ATTR_FREE(client, pref_5g);
    OW_STEER_BM_MEM_ATTR_FREE(client, kick_type);
    OW_STEER_BM_MEM_ATTR_FREE(client, kick_upon_idle);
    OW_STEER_BM_MEM_ATTR_FREE(client, pre_assoc_auth_block);
    OW_STEER_BM_MEM_ATTR_FREE(client, send_rrm_after_assoc);
    OW_STEER_BM_MEM_ATTR_FREE(client, backoff_secs);
    OW_STEER_BM_MEM_ATTR_FREE(client, backoff_exp_base);
    OW_STEER_BM_MEM_ATTR_FREE(client, max_rejects);
    OW_STEER_BM_MEM_ATTR_FREE(client, rejects_tmout_secs);
    OW_STEER_BM_MEM_ATTR_FREE(client, force_kick);
    OW_STEER_BM_MEM_ATTR_FREE(client, sc_kick_type);
    OW_STEER_BM_MEM_ATTR_FREE(client, sticky_kick_type);
    OW_STEER_BM_MEM_ATTR_FREE(client, neighbor_list_filter_by_beacon_report);
    OW_STEER_BM_MEM_ATTR_FREE(client, pref_5g_pre_assoc_block_timeout_msecs);
    ow_steer_bm_btm_params_free(client->sc_btm_params);
    ow_steer_bm_btm_params_free(client->steering_btm_params);
    ow_steer_bm_btm_params_free(client->sticky_btm_params);
    ow_steer_bm_cs_params_free(client->cs_params);
    ASSERT(ds_dlist_is_empty(&client->sta_list) == true, "");
    FREE(client);
}

static void
ow_steer_bm_stats_free_dpp_report(dpp_bs_client_report_data_t *dpp_report)
{
    dpp_bs_client_record_list_t *client;
    ds_dlist_foreach(&dpp_report->list, client)
    {
        dpp_bs_client_record_t *client_entry = &client->entry;

        unsigned int i;
        for (i = 0; i < client_entry->num_band_records; i++)
        {
            dpp_bs_client_band_record_t *band_rec = &client_entry->band_record[i];

            unsigned int j;
            for (j = 0; j < band_rec->num_event_records; j++)
            {
                dpp_bs_client_event_record_t *event_rec = &band_rec->event_record[j];
                FREE(event_rec->assoc_ies);
            }
        }
    }

    while ((client = ds_dlist_remove_head(&dpp_report->list)) != NULL) {
        dpp_bs_client_record_free(client);
    }

    FREE(dpp_report);
}

static void
ow_steer_bm_stats_free_client_stats(void)
{
    struct ow_steer_bm_client *client;

    ds_tree_foreach(&g_client_tree, client) {
        struct ow_steer_bm_vif_stats *stats;
        struct ow_steer_bm_vif_stats *tmp_stats;

        ds_tree_foreach_safe(&client->stats_tree, stats, tmp_stats) {
            unsigned int i;
            for (i = 0; i < stats->event_stats_count; i++) {
                FREE(stats->event_stats[i].assoc_ies);
            }
            ds_tree_remove(&client->stats_tree, stats);
            FREE(stats);
        }
    }
}

static void
ow_steer_bm_client_recalc(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    OW_STEER_BM_MEM_ATTR_UPDATE(client, hwm);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, lwm);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, bottom_lwm);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, pref_5g);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, kick_type);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, kick_upon_idle);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, pre_assoc_auth_block);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, send_rrm_after_assoc);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, backoff_secs);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, backoff_exp_base);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, max_rejects);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, rejects_tmout_secs);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, force_kick);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, sc_kick_type);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, sticky_kick_type);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, neighbor_list_filter_by_beacon_report);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, cs_mode);
    OW_STEER_BM_MEM_ATTR_UPDATE(client, pref_5g_pre_assoc_block_timeout_msecs);
    struct ow_steer_bm_attr_state sc_btm_params_state;
    ow_steer_bm_btm_params_update(client->sc_btm_params, &sc_btm_params_state);
    struct ow_steer_bm_attr_state steering_btm_params_state;
    ow_steer_bm_btm_params_update(client->steering_btm_params, &steering_btm_params_state);
    struct ow_steer_bm_attr_state sticky_btm_params_state;
    ow_steer_bm_btm_params_update(client->sticky_btm_params, &sticky_btm_params_state);
    struct ow_steer_bm_attr_state cs_params_state;
    ow_steer_bm_cs_params_update(client->cs_params, &cs_params_state);

    const bool schedule_force_kick = (force_kick_state.changed == true) &&
                                     (force_kick_state.present == true) &&
                                     ((*client->force_kick.cur == OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE) ||
                                      (*client->force_kick.cur == OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED));

    const bool schedule_client_steering_recalc = (cs_mode_state.changed == true);

    struct ow_steer_bm_sta *sta;
    ds_dlist_foreach(&client->sta_list, sta) {
        if (schedule_force_kick == true)
            sta->issue_force_kick = true;
        if (schedule_client_steering_recalc == true)
            sta->client_steering_recalc = true;
    }

    const bool any_attr_changed = hwm_state.changed == true ||
                                  lwm_state.changed == true ||
                                  bottom_lwm_state.changed == true ||
                                  pref_5g_state.changed == true ||
                                  kick_type_state.changed == true ||
                                  kick_upon_idle_state.changed == true ||
                                  pre_assoc_auth_block_state.changed == true ||
                                  send_rrm_after_assoc_state.changed == true ||
                                  backoff_secs_state.changed == true ||
                                  backoff_exp_base_state.changed == true ||
                                  max_rejects_state.changed == true ||
                                  rejects_tmout_secs_state.changed == true ||
                                  force_kick_state.changed == true ||
                                  sc_kick_type_state.changed == true ||
                                  sticky_kick_type_state.changed == true ||
                                  neighbor_list_filter_by_beacon_report_state.changed == true ||
                                  cs_mode_state.changed == true ||
                                  pref_5g_pre_assoc_block_timeout_msecs_state.changed == true ||
                                  sc_btm_params_state.changed == true ||
                                  steering_btm_params_state.changed == true ||
                                  sticky_btm_params_state.changed == true ||
                                  cs_params_state.changed == true;

    const bool schedule_work = any_attr_changed == true ||
                               schedule_force_kick == true ||
                               schedule_client_steering_recalc == true;
    if (schedule_work == true)
        OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_recalc_vifs(void)
{
    struct ow_steer_bm_vif *vif;
    struct ow_steer_bm_vif *tmp_vif;

    ds_tree_foreach_safe(&g_vif_tree, vif, tmp_vif) {
        if (vif->removed == true) {
            ds_tree_remove(&g_vif_tree, vif);
            ow_steer_bm_vif_free(vif);
            continue;
        }

        ow_steer_bm_vif_recalc(vif);
    }
}

static void
ow_steer_bm_recalc_bsses(void)
{
    struct ow_steer_bm_bss *bss;
    struct ow_steer_bm_bss *tmp_bss;

    ds_dlist_foreach_safe(&g_bss_list, bss, tmp_bss) {
        if (bss->removed == true) {
            ds_dlist_remove(&g_bss_list, bss);
            ow_steer_bm_bss_free(bss);
            continue;
        }
    }
}

static void
ow_steer_bm_recalc_stas(void)
{
    struct ow_steer_bm_sta *sta;
    struct ow_steer_bm_sta *tmp_sta;

    ds_dlist_foreach_safe(&g_sta_list, sta, tmp_sta) {
        if (sta->removed == true) {
            ds_dlist_remove(&g_sta_list, sta);
            ow_steer_bm_sta_free(sta);
            continue;
        }

        ow_steer_bm_sta_recalc(sta);
    }
}

static void
ow_steer_bm_recalc_groups(void)
{
    struct ow_steer_bm_group *group;
    struct ow_steer_bm_group *tmp_group;

    ds_tree_foreach_safe(&g_group_tree, group, tmp_group) {
        const bool remove = group->removed &&
                            ds_tree_is_empty(&group->vif_tree) == true &&
                            ds_tree_is_empty(&group->sta_tree) == true &&
                            ds_tree_is_empty(&group->bss_tree) == true;
        if (remove == true) {
            ds_tree_remove(&g_group_tree, group);
            ow_steer_bm_group_free(group);
            continue;
        }

        ow_steer_bm_group_recalc(group);
    }
}

static void
ow_steer_bm_recalc_neighbors(void)
{
    struct ow_steer_bm_neighbor *neighbor;
    struct ow_steer_bm_neighbor *tmp_neighbor;

    ds_tree_foreach_safe(&g_neighbor_tree, neighbor, tmp_neighbor) {
        if (neighbor->removed == true) {
            ds_tree_remove(&g_neighbor_tree, neighbor);
            ow_steer_bm_neighbor_free(neighbor);
            continue;
        }

        ow_steer_bm_neighbor_recalc(neighbor);
    }
}

static void
ow_steer_bm_recalc_clients(void)
{
    struct ow_steer_bm_client *client;
    struct ow_steer_bm_client *tmp_client;

    ds_tree_foreach_safe(&g_client_tree, client, tmp_client) {
        if (client->removed == true) {
            ds_tree_remove(&g_client_tree, client);
            ow_steer_bm_client_free(client);
            continue;
        }

        ow_steer_bm_client_recalc(client);
    }
}

static void
ow_steer_bm_work(void)
{
    LOGD("ow: steer: bm: do work");
    ow_steer_bm_recalc_bsses();
    ow_steer_bm_recalc_stas();
    ow_steer_bm_recalc_neighbors();
    ow_steer_bm_recalc_vifs();
    ow_steer_bm_recalc_clients();
    ow_steer_bm_recalc_groups();
}

static void
ow_steer_bm_work_timer_cb(struct osw_timer *timer)
{
    ow_steer_bm_work();
}

enum ow_steer_bm_vif_usability {
    OW_STEER_BM_VIF_NOT_MATCHING,
    OW_STEER_BM_VIF_NOT_USABLE,
    OW_STEER_BM_VIF_USABLE,
};

static void
ow_steer_bm_state_obs_vif_update_cb(struct osw_state_observer *self,
                                    const struct osw_state_vif_info *vif_info,
                                    const bool exists)
{
    struct ow_steer_bm_vif *vif = container_of(self, struct ow_steer_bm_vif, state_obs);
    const bool other_vif = (strcmp(vif->vif_name.buf, vif_info->vif_name) != 0);
    if (other_vif) return;

    if ((vif_info->drv_state == NULL)
    ||  (vif_info->drv_state->vif_type != OSW_VIF_AP)
    ||  (exists == false)) {
        vif_info = NULL;
    }

    ow_steer_bm_vif_set_vif_info(vif, vif_info);
}

static void
ow_steer_bm_state_obs_vif_added_cb(struct osw_state_observer *self,
                                   const struct osw_state_vif_info *vif_info)
{
    ow_steer_bm_state_obs_vif_update_cb(self, vif_info, true);
}

static void
ow_steer_bm_state_obs_vif_changed_cb(struct osw_state_observer *self,
                                     const struct osw_state_vif_info *vif_info)
{
    ow_steer_bm_state_obs_vif_update_cb(self, vif_info, true);
}

static void
ow_steer_bm_state_obs_vif_removed_cb(struct osw_state_observer *self,
                                     const struct osw_state_vif_info *vif_info)
{
    ow_steer_bm_state_obs_vif_update_cb(self, vif_info, false);
}

static bool
ow_steer_bm_probe_was_blocked(const struct osw_drv_vif_state *vif,
                              const struct osw_drv_report_vif_probe_req *probe_req)
{
    if (vif->vif_type != OSW_VIF_AP) return true;
    const bool on_list = osw_hwaddr_list_contains(vif->u.ap.acl.list,
                                                  vif->u.ap.acl.count,
                                                  &probe_req->sta_addr);
    const bool not_on_list = !on_list;
    const bool probe_allowed = vif->u.ap.acl_policy == OSW_ACL_ALLOW_LIST ? on_list
                             : vif->u.ap.acl_policy == OSW_ACL_DENY_LIST ? not_on_list
                             : true;
    /* This is not guaranteed to be correct but the current
     * thinking is it doesn't have to be. There's no
     * explicit way to learn if a probe response was sent or
     * not. The expectation is that the underlying WLAN
     * driver will block probe responses when ACL implies
     * it.
     */
    return !probe_allowed;
}

static void
ow_steer_bm_state_obs_vif_probe_cb(struct osw_state_observer *self,
                                   const struct osw_state_vif_info *vif,
                                   const struct osw_drv_report_vif_probe_req *probe_req)
{
    ASSERT(self != NULL, "");
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(probe_req == NULL)) return;

    const bool probe_bcast = (probe_req->ssid.len == 0);
    const bool probe_blocked = ow_steer_bm_probe_was_blocked(vif->drv_state, probe_req);

    ow_steer_bm_stats_set_probe(&probe_req->sta_addr,
                                vif->vif_name,
                                probe_bcast,
                                probe_blocked,
                                probe_req->snr);
}

static void
ow_steer_bm_recalc_sta_bitrate(struct ow_steer_bm_sta *sta,
                               const char *vif_name,
                               const unsigned int data_rx,
                               const unsigned int data_tx)
{
    unsigned int data_bits = (data_rx + data_tx) * 8;
    unsigned int bitrate = data_bits / OW_STEER_BM_BITRATE_STATS_INTERVAL;
    bool activity = false;

    if (bitrate > OW_STEER_BM_DEFAULT_BITRATE_THRESHOLD) activity = true;
    else activity = false;

    if (activity != sta->bps_activity)
        ow_steer_bm_stats_set_client_activity(&sta->addr,
                                              vif_name,
                                              activity);

    sta->bps_activity = activity;
}

static void
ow_steer_bm_stats_report_cb(enum osw_stats_id id,
                            const struct osw_tlv *data,
                            const struct osw_tlv *last,
                            void *priv)
{
    const struct osw_stats_defs *stats_defs = osw_stats_defs_lookup(OSW_STATS_STA);
    const struct osw_tlv_hdr *tb[OSW_STATS_STA_MAX__] = {0};
    const struct osw_hwaddr *sta_addr;
    const char *vif_name;

    if (id != OSW_STATS_STA)
        return;

    osw_tlv_parse(data->data, data->used, stats_defs->tpolicy, tb, OSW_STATS_STA_MAX__);

    if (WARN_ON(tb[OSW_STATS_STA_MAC_ADDRESS] == NULL) ||
        WARN_ON(tb[OSW_STATS_STA_VIF_NAME] == NULL))
        return;
    if (tb[OSW_STATS_STA_TX_BYTES] == NULL &&
        tb[OSW_STATS_STA_RX_BYTES] == NULL)
        return;

    sta_addr = osw_tlv_get_data(tb[OSW_STATS_STA_MAC_ADDRESS]);
    vif_name = osw_tlv_get_string(tb[OSW_STATS_STA_VIF_NAME]);

    if (tb[OSW_STATS_STA_TX_BYTES] != NULL || tb[OSW_STATS_STA_RX_BYTES] != NULL) {
        struct ow_steer_bm_sta *sta;
        const unsigned int data_rx = tb[OSW_STATS_STA_RX_BYTES] != NULL ? osw_tlv_get_u32(tb[OSW_STATS_STA_RX_BYTES]) : 0;
        const unsigned int data_tx = tb[OSW_STATS_STA_TX_BYTES] != NULL ? osw_tlv_get_u32(tb[OSW_STATS_STA_TX_BYTES]) : 0;

        ds_dlist_foreach(&g_sta_list, sta) {
            if (osw_hwaddr_cmp(&sta->addr, sta_addr) != 0)
                continue;

            ow_steer_bm_recalc_sta_bitrate(sta,
                                           vif_name,
                                           data_rx,
                                           data_tx);
        }
    }

}

static char*
ow_steer_bm_dpp_radio_type_to_str(radio_type_t radio_type) {
    switch(radio_type) {
        case RADIO_TYPE_2G:
            return "RADIO_TYPE_2G";
        case RADIO_TYPE_5G:
            return "RADIO_TYPE_5G";
        case RADIO_TYPE_5GL:
            return "RADIO_TYPE_5GL";
        case RADIO_TYPE_5GU:
            return "RADIO_TYPE_5GU";
        case RADIO_TYPE_6G:
            return "RADIO_TYPE_6G";
        case RADIO_TYPE_NONE:
            break;
    }
    return "RADIO_TYPE_NONE";
}

static radio_type_t
ow_steer_bm_vif_to_radio_type(const struct ow_steer_bm_vif *vif)
{
    bool is_2g = false;
    bool is_5gl = false;
    bool is_5gu = false;
    bool is_6g = false;
    bool is_unspec = false;

    if (WARN_ON(vif == NULL)) return RADIO_TYPE_NONE;
    if (WARN_ON(vif->vif_info->phy == NULL)) return RADIO_TYPE_NONE;
    if (WARN_ON(vif->vif_info->phy->drv_state == NULL)) return RADIO_TYPE_NONE;
    const struct osw_drv_phy_state *phy_drv_state = vif->vif_info->phy->drv_state;

    size_t i;
    for (i = 0; i < phy_drv_state->n_channel_states; i++) {
        const int b2ch1 = 2412;
        const int b2ch13 = 2472;
        const int b2ch14 = 2484;
        const int b5ch36 = 5180;
        const int b5ch96 = 5480;
        const int b5ch100 = 5500;
        const int b5ch177 = 5885;
        const int b6ch1 = 5955;
        const int b6ch2 = 5935;
        const int b6ch233 = 7115;
        const int mhz = phy_drv_state->channel_states[i].channel.control_freq_mhz;

        LOGT("ow: steer: bm: for vif: %s available control_freq_mhz=%d", vif->vif_name.buf, mhz);

        if ((mhz >= b2ch1 && mhz <= b2ch13) || mhz == b2ch14) is_2g = true;
        else if (mhz >= b5ch36 && mhz <= b5ch96) is_5gl = true;
        else if (mhz >= b5ch100 && mhz <= b5ch177) is_5gu = true;
        else if ((mhz >= b6ch1 && mhz <= b6ch233) || mhz == b6ch2) is_6g = true;
        else is_unspec = true;
    }

    bool is_5g = false;
    if (is_5gl && is_5gu) {
        is_5gl = false;
        is_5gu = false;
        is_5g = true;
    }

    radio_type_t radio_type = RADIO_TYPE_NONE;
    const int band_flags_cnt = (is_2g + is_5gl + is_5gu + is_5g + is_6g + is_unspec);
    if (band_flags_cnt == 1) {
        if (is_2g) radio_type = RADIO_TYPE_2G;
        if (is_5gl) radio_type = RADIO_TYPE_5GL;
        if (is_5gu) radio_type = RADIO_TYPE_5GU;
        if (is_5g) radio_type = RADIO_TYPE_5G;
        if (is_6g) radio_type = RADIO_TYPE_6G;
        if (is_unspec) radio_type = RADIO_TYPE_NONE;
    }
    else LOGE("ow: steer: bm: for vif: %s incoherent available channels", vif->vif_name.buf);

    LOGD("ow: steer: bm: for vif: %s determined radio type %s", vif->vif_name.buf, ow_steer_bm_dpp_radio_type_to_str(radio_type));

    return radio_type;
}

static int
ow_steer_bm_radio_type_to_band_idx(radio_type_t radio_type)
{
    switch (radio_type) {
        case RADIO_TYPE_2G:
            return 0;
        case RADIO_TYPE_5G:
            return 1;
        case RADIO_TYPE_5GL:
            return 2;
        case RADIO_TYPE_5GU:
            return 3;
        case RADIO_TYPE_6G:
            return 4;
        case RADIO_TYPE_NONE:
            return -1;
    }
    LOGE("ow: steer: bm: radio type to band idx conversion failed");
    return -1;
}

dpp_bs_client_record_list_t*
ow_steer_bm_get_dpp_client_record(struct osw_hwaddr mac_addr, dpp_bs_client_report_data_t *dpp_report)

{
    ASSERT(dpp_report != NULL, "");

    /* find client datapipeline record if available */
    dpp_bs_client_record_list_t *dpp_client_record_l = NULL;
    dpp_bs_client_record_list_t *r;
    ds_dlist_foreach(&dpp_report->list, r) {
        if (memcmp(r->entry.mac, &mac_addr, OSW_HWADDR_LEN) == 0) {
            LOGT("ow: steer: bm: stats: found already allocated datapipeline record for client");
            dpp_client_record_l = r;
            break;
        }
    }

    /* allocate datapipeline client record and append to reports list */
    if (dpp_client_record_l == NULL) {
        LOGT("ow: steer: bm: stats: allocating a new datapipeline record for client");
        dpp_client_record_l = dpp_bs_client_record_alloc();
        if (dpp_client_record_l == NULL) {
            LOGE("ow: steer: bm: stats: datapipeline client record allocation failed");
            return NULL;
        }
        ds_dlist_insert_tail(&dpp_report->list, dpp_client_record_l);
    }

    return dpp_client_record_l;
}

/* This function is meant to replicate BM behavior.
 * It utilizes a datapipeline structure that has a
 * limitation - only one slot per band for a client
 * (2G/5G/5GL/5GU/6G). That means a group should not
 * contain multiple vifs from the same band. Setting
 * them in such way would result in undefined vif
 * being reported for a band.
 * Example:
 *     Group contains vif1-24 and vif2-24
 *     Reported for 2.4GHz band is vif1-24 or vif2-24
 *     Undefined which one. The other report will be
 *     lost.
 */
static void
ow_steer_bm_build_report(void)
{
    /* for every group */
    struct ow_steer_bm_group *group;
    ds_tree_foreach(&g_group_tree, group) {
        dpp_bs_client_report_data_t *dpp_report = CALLOC(1, sizeof(*dpp_report));
        ds_dlist_init(&dpp_report->list,
                      dpp_bs_client_record_list_t,
                      node);
        LOGD("ow: steer: bm: stats: building report for group id: %s", group->id);

        /* for every vif in group */
        struct ow_steer_bm_vif *vif;
        ds_tree_foreach(&group->vif_tree, vif) {
            LOGD("ow: steer: bm: stats: building report for vif: %s", vif->vif_name.buf);

            /* for every client */
            struct ow_steer_bm_client *client;
            ds_tree_foreach(&g_client_tree, client) {

                LOGD("ow: steer: bm: stats: building report for mac: "
                     OSW_HWADDR_FMT"", OSW_HWADDR_ARG(&client->addr));

                /* client stats for current vif */
                struct ow_steer_bm_vif_stats *client_vif_stats = ds_tree_find(&client->stats_tree, &vif->vif_name);
                if (client_vif_stats == NULL) {
                    LOGT("ow: steer: bm: stats: client stats entry not found");
                    continue;
                }

                /* calculate band index used in datapipeline for the current band */
                if (WARN_ON(vif->vif_info == NULL)) continue;
                if (WARN_ON(vif->vif_info->drv_state == NULL)) continue;
                if (WARN_ON(vif->vif_info->drv_state->vif_type != OSW_VIF_AP)) continue;

                const radio_type_t radio_type = ow_steer_bm_vif_to_radio_type(vif);
                const int band_idx = ow_steer_bm_radio_type_to_band_idx(radio_type);
                if (band_idx < 0) {
                    LOGE("ow: steer: bm: stats: could not determine band idx");
                    continue;
                }

                /* find client datapipeline record if available */
                dpp_bs_client_record_list_t *dpp_client_record_l =
                    ow_steer_bm_get_dpp_client_record(client->addr, dpp_report);
                if (WARN_ON(dpp_client_record_l == NULL)) continue;

                /* fill in client record */
                dpp_bs_client_record_t *dpp_client_record = &dpp_client_record_l->entry;
                memcpy(&dpp_client_record->mac, &client->addr, OSW_HWADDR_LEN);
                dpp_client_record->num_band_records = OW_STEER_BM_DPP_MAX_BS_BANDS;

                /* fill in band record */
                dpp_bs_client_band_record_t *dpp_band_record = &dpp_client_record->band_record[band_idx];
                dpp_band_record->type = radio_type;
                STRSCPY_WARN(dpp_band_record->ifname, client_vif_stats->vif_name.buf);
                dpp_band_record->connected = client_vif_stats->connected;
                dpp_band_record->rejects = client_vif_stats->rejects;
                dpp_band_record->connects = client_vif_stats->connects;
                dpp_band_record->disconnects = client_vif_stats->disconnects;
                dpp_band_record->activity_changes = client_vif_stats->activity_changes;
                dpp_band_record->steering_success_cnt = client_vif_stats->steering_success_cnt;
                dpp_band_record->steering_fail_cnt = client_vif_stats->steering_fail_cnt;
                dpp_band_record->steering_kick_cnt = client_vif_stats->steering_kick_cnt;
                dpp_band_record->sticky_kick_cnt = client_vif_stats->sticky_kick_cnt;
                dpp_band_record->probe_bcast_cnt = client_vif_stats->probe_bcast_cnt;
                dpp_band_record->probe_bcast_blocked = client_vif_stats->probe_bcast_blocked;
                dpp_band_record->probe_direct_cnt = client_vif_stats->probe_direct_cnt;
                dpp_band_record->probe_direct_blocked = client_vif_stats->probe_direct_blocked;
                dpp_band_record->num_event_records = client_vif_stats->event_stats_count;

                /* fill in events */
                unsigned int i;
                for (i = 0; i < client_vif_stats->event_stats_count; i++) {
                    struct ow_steer_bm_event_stats *client_event_stats = &client_vif_stats->event_stats[i];
                    dpp_bs_client_event_record_t *dpp_event_record = &dpp_band_record->event_record[i];

                    if (client_event_stats->assoc_ies_len > 0) {
                        if (client_event_stats->assoc_ies != NULL) {
                            dpp_event_record->assoc_ies = MEMNDUP(client_event_stats->assoc_ies, client_event_stats->assoc_ies_len);
                            dpp_event_record->assoc_ies_len = client_event_stats->assoc_ies_len;
                        } else {
                            LOGE("ow: steer: bm: stats: assoc_ies_len > 0 but no pointer assigned");
                        }
                    }

                    dpp_event_record->type = client_event_stats->type;
                    dpp_event_record->timestamp_ms = client_event_stats->timestamp_ms;
                    dpp_event_record->rssi = client_event_stats->rssi;
                    dpp_event_record->probe_bcast = client_event_stats->probe_bcast;
                    dpp_event_record->probe_blocked = client_event_stats->probe_blocked;
                    dpp_event_record->disconnect_src = client_event_stats->disconnect_src;
                    dpp_event_record->disconnect_type = client_event_stats->disconnect_type;
                    dpp_event_record->disconnect_reason = client_event_stats->disconnect_reason;
                    dpp_event_record->backoff_enabled = client_event_stats->backoff_enabled;
                    dpp_event_record->active = client_event_stats->active;
                    dpp_event_record->rejected = client_event_stats->rejected;
                    dpp_event_record->is_BTM_supported = client_event_stats->is_BTM_supported;
                    dpp_event_record->is_RRM_supported = client_event_stats->is_RRM_supported;
                    dpp_event_record->band_cap_2G = client_event_stats->band_cap_2G;
                    dpp_event_record->band_cap_5G = client_event_stats->band_cap_5G;
                    dpp_event_record->band_cap_6G = client_event_stats->band_cap_6G;
                    dpp_event_record->max_chwidth = client_event_stats->max_chwidth;
                    dpp_event_record->max_streams = client_event_stats->max_streams;
                    dpp_event_record->phy_mode = client_event_stats->phy_mode;
                    dpp_event_record->max_MCS = client_event_stats->max_MCS;
                    dpp_event_record->max_txpower = client_event_stats->max_txpower;
                    dpp_event_record->is_static_smps = client_event_stats->is_static_smps;
                    dpp_event_record->is_mu_mimo_supported = client_event_stats->is_mu_mimo_supported;
                    dpp_event_record->rrm_caps_link_meas = client_event_stats->rrm_caps_link_meas;
                    dpp_event_record->rrm_caps_neigh_rpt = client_event_stats->rrm_caps_neigh_rpt;
                    dpp_event_record->rrm_caps_bcn_rpt_passive = client_event_stats->rrm_caps_bcn_rpt_passive;
                    dpp_event_record->rrm_caps_bcn_rpt_active = client_event_stats->rrm_caps_bcn_rpt_active;
                    dpp_event_record->rrm_caps_bcn_rpt_table = client_event_stats->rrm_caps_bcn_rpt_table;
                    dpp_event_record->rrm_caps_lci_meas = client_event_stats->rrm_caps_lci_meas;
                    dpp_event_record->rrm_caps_ftm_range_rpt = client_event_stats->rrm_caps_ftm_range_rpt;
                    dpp_event_record->backoff_period = client_event_stats->backoff_period;
                    dpp_event_record->btm_status = client_event_stats->btm_status;
                }
            }
        }

        dpp_report->timestamp_ms = OSW_TIME_TO_MS(osw_time_wall_clk());
        LOGD("ow: steer: bm: stats: queuing report to send to QM with timestamp: %"PRIu64"ms", dpp_report->timestamp_ms);
        dpp_put_bs_client(dpp_report);
        ow_steer_bm_stats_free_dpp_report(dpp_report);
    }
    ow_steer_bm_stats_free_client_stats();
}

static void
ow_steer_bm_proc_stats_timer_cb(struct osw_timer *timer)
{
    ow_steer_bm_build_report();
    ow_steer_bm_report_send();
    /* this timer makes up for drift */
    const uint64_t at = timer->at_nsec + OSW_TIME_SEC(OW_STEER_BM_DEFAULT_REPORTING_TIME);
    osw_timer_arm_at_nsec(&g_stats_timer, at);
}

static void
ow_steer_bm_register_stats_subscriber(struct osw_stats_subscriber *sub)
{
    LOGT("ow: steer: bm: allocating stats subscriber");
    sub = osw_stats_subscriber_alloc();

    LOGT("ow: steer: bm: setting stats subscriber");
    osw_stats_subscriber_set_report_seconds(sub, OW_STEER_BM_BITRATE_STATS_INTERVAL);
    osw_stats_subscriber_set_poll_seconds(sub, OW_STEER_BM_BITRATE_STATS_INTERVAL);
    osw_stats_subscriber_set_sta(sub, true);
    osw_stats_subscriber_set_report_fn(sub, ow_steer_bm_stats_report_cb, sub);

    LOGT("ow: steer: bm: registering stats subscriber");
    osw_stats_register_subscriber(sub);
}

static void
ow_steer_bm_init(void)
{
    osw_state_register_observer(&g_state_observer);
    ow_steer_bm_register_stats_subscriber(g_stats_subscriber);
    g_bss_provider = osw_bss_map_register_provider();
    osw_timer_init(&g_work_timer, ow_steer_bm_work_timer_cb);
    osw_timer_init(&g_stats_timer, ow_steer_bm_proc_stats_timer_cb);

    LOGI("ow: steer: bm: initialized");

    osw_timer_arm_at_nsec(&g_stats_timer, osw_time_mono_clk());
}

void
ow_steer_bm_observer_register(struct ow_steer_bm_observer *observer)
{
    ASSERT(observer != NULL, "");

    if (observer->vif_added_fn != NULL) {
        struct ow_steer_bm_vif *vif;
        ds_tree_foreach(&g_vif_tree, vif)
            observer->vif_added_fn(observer, vif);
    }

    if (observer->neighbor_up_fn != NULL) {
        struct ow_steer_bm_neighbor *neighbor;
        ds_tree_foreach(&g_neighbor_tree, neighbor)
            if (ow_steer_bm_neighbor_is_up(neighbor) == true)
                observer->neighbor_up_fn(observer, neighbor);
    }

    if (observer->client_added_fn != NULL) {
        struct ow_steer_bm_client *client;
        ds_tree_foreach(&g_client_tree, client)
            observer->client_added_fn(observer, client);
    }

    ds_dlist_insert_tail(&g_observer_list, observer);
}

void
ow_steer_bm_observer_unregister(struct ow_steer_bm_observer *observer)
{
    ASSERT(observer != NULL, "");
    ds_dlist_remove(&g_observer_list, observer);

    if (observer->vif_removed_fn != NULL) {
        struct ow_steer_bm_vif *vif;
        ds_tree_foreach(&g_vif_tree, vif)
            observer->vif_removed_fn(observer, vif);
    }

    if (observer->neighbor_down_fn != NULL) {
        struct ow_steer_bm_neighbor *neighbor;
        ds_tree_foreach(&g_neighbor_tree, neighbor)
            if (ow_steer_bm_neighbor_is_up(neighbor) == true)
                observer->neighbor_down_fn(observer, neighbor);
    }

    if (observer->client_removed_fn != NULL) {
        struct ow_steer_bm_client *client;
        ds_tree_foreach(&g_client_tree, client)
            observer->client_removed_fn(observer, client);
    }
}

static void
ow_steer_bm_neighbor_vif_added_cb(struct ow_steer_bm_observer *observer,
                                  struct ow_steer_bm_vif *vif)
{
    struct ow_steer_bm_neighbor *neighbor = container_of(observer, struct ow_steer_bm_neighbor, observer);
    const bool set_vif = neighbor->vif_name.cur != NULL &&
                         osw_ifname_cmp(neighbor->vif_name.cur, &vif->vif_name) == 0;
    if (set_vif == true)
        ow_steer_bm_neighbor_set_vif(neighbor, vif);
}

static void
ow_steer_bm_neighbor_vif_changed_channel_cb(struct ow_steer_bm_observer *observer,
                                            struct ow_steer_bm_vif *vif,
                                            const struct osw_channel *old_channel,
                                            const struct osw_channel *new_channel)
{
    struct osw_channel zero = {0};
    const bool dfs_non_dfs_change = ((old_channel == NULL) && (new_channel != NULL))
                                ||  ((old_channel != NULL) && (new_channel == NULL))
                                ||  (ow_steer_bm_vif_would_use_dfs(vif, old_channel) !=
                                     ow_steer_bm_vif_would_use_dfs(vif, new_channel));
    if (dfs_non_dfs_change) {
        LOGD("ow: steer: bm: vif vif_name: %s dfs/nondfs channel change: "
             OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             vif->vif_name.buf,
             OSW_CHANNEL_ARG(old_channel ?: &zero),
             OSW_CHANNEL_ARG(new_channel ?: &zero));
        OW_STEER_BM_SCHEDULE_WORK;
    }
}

static void
ow_steer_bm_neighbor_vif_removed_cb(struct ow_steer_bm_observer *observer,
                                    struct ow_steer_bm_vif *vif)
{
    struct ow_steer_bm_neighbor *neighbor = container_of(observer, struct ow_steer_bm_neighbor, observer);
    const bool unset_vif = neighbor->vif_name.cur != NULL &&
                           osw_ifname_cmp(neighbor->vif_name.cur, &vif->vif_name) == 0;
    if (unset_vif == true)
        ow_steer_bm_neighbor_set_vif(neighbor, NULL);
}

static void
ow_steer_bm_group_vif_toggled_cb(struct ow_steer_bm_observer *observer,
                                 struct ow_steer_bm_vif *vif)
{
    struct ow_steer_bm_group *self = container_of(observer, struct ow_steer_bm_group, observer);
    if (self == vif->group)
        OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_group_neighbor_toggled_cb(struct ow_steer_bm_observer *observer,
                                      struct ow_steer_bm_neighbor *neighbor)
{
    struct ow_steer_bm_group *self = container_of(observer, struct ow_steer_bm_group, observer);
    if (self == neighbor->bss->group)
        OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_group_client_added_cb(struct ow_steer_bm_observer *observer,
                                  struct ow_steer_bm_client *client)
{
    struct ow_steer_bm_group *group = container_of(observer, struct ow_steer_bm_group, observer);
    ow_steer_bm_sta_create(group, client);
}

static void
ow_steer_bm_group_client_removed_cb(struct ow_steer_bm_observer *observer,
                                    struct ow_steer_bm_client *client)
{
    struct ow_steer_bm_group *group = container_of(observer, struct ow_steer_bm_group, observer);
    ow_steer_bm_sta_remove(group, client);
}

static void
ow_steer_bm_sigusr1_dump_neighbors(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();

    osw_diag_pipe_writef(pipe, "ow: steer: bm: neighbors:");

    struct ow_steer_bm_neighbor *neighbor;
    ds_tree_foreach(&g_neighbor_tree, neighbor) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   neighbors:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", neighbor);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&neighbor->bssid));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", neighbor->removed == true ? "true" : "false");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif_name: %s", neighbor->vif_name.cur == NULL ? "(nil)" : neighbor->vif_name.cur->buf);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     channel_number: %s", neighbor->channel_number.cur == NULL ? "(nil)" : strfmta("%u", *neighbor->channel_number.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ht_mode: %s", neighbor->ht_mode.cur == NULL ? "(nil)" : ow_steer_bm_neighbor_ht_mode_to_str(*neighbor->ht_mode.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     op_class: %s", neighbor->op_class.cur == NULL ? "(nil)" : strfmta("%u", *neighbor->op_class.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     channel: %s", neighbor->channel.cur == NULL ? "(nil)" : strfmta(OSW_CHANNEL_FMT, OSW_CHANNEL_ARG(neighbor->channel.cur)));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif: %s", neighbor->vif.cur == NULL ? "(nil)" : strfmta("%s", neighbor->vif.cur->vif_name.buf));

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bss: %s", neighbor->bss == NULL ? "(nil)" : "");
        if (neighbor->bss != NULL) {
            osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", neighbor->bss);
            osw_diag_pipe_writef(pipe, "ow: steer: bm:         bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&neighbor->bss->bssid));
         }
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_sigusr1_dump_vifs(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();

    osw_diag_pipe_writef(pipe, "ow: steer: bm: vifs:");

    struct ow_steer_bm_vif *vif;
    ds_tree_foreach(&g_vif_tree, vif) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   vif:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", vif);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif_name: %s", vif->vif_name.buf);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", vif->removed == true ? "true" : "false");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif_info: %s", vif->vif_info == NULL ? "(nil)" : "(set)");

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bss: %s", vif->bss == NULL ? "(nil)" : "");
        if (vif->bss != NULL) {
            osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", vif->bss);
            osw_diag_pipe_writef(pipe, "ow: steer: bm:         bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&vif->bss->bssid));
         }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     group: %s", vif->group == NULL ? "(nil)" : "");
        if (vif->group != NULL)
            osw_diag_pipe_writef(pipe, "ow: steer: bm:       id: %s", vif->group->id);
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_btm_params_sigusr1_dump(const struct ow_steer_bm_btm_params *btm_params)
{
    if (btm_params == NULL)
        return;

    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm:       bssid: %s", btm_params->bssid.cur == NULL ? "(nil)" : strfmta(OSW_HWADDR_FMT, OSW_HWADDR_ARG(btm_params->bssid.cur)));
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_cs_params_sigusr1_dump(const struct ow_steer_bm_cs_params *cs_params)
{
    if (cs_params == NULL)
        return;

    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm:       band: %s", cs_params->band.cur == NULL ? "(nil)" : ow_steer_bm_cs_params_band_to_str(*cs_params->band.cur));
    osw_diag_pipe_writef(pipe, "ow: steer: bm:       enforce_period_secs: %s", cs_params->enforce_period_secs.cur == NULL ? "(nil)" : strfmta("%u", *cs_params->enforce_period_secs.cur));
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_sigusr1_dump_clients(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm: clients:");

    struct ow_steer_bm_client *client;
    ds_tree_foreach(&g_client_tree, client) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   client:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", client);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&client->addr));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", client->removed == true ? "true" : "false");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     lwm: %s", client->lwm.cur == NULL ? "(nil)" : strfmta("%u", *client->lwm.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     hwm: %s", client->hwm.cur == NULL ? "(nil)" : strfmta("%u", *client->hwm.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bottom_lwm: %s", client->bottom_lwm.cur == NULL ? "(nil)" : strfmta("%u", *client->bottom_lwm.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     pref_5g: %s", client->pref_5g.cur == NULL ? "(nil)" : ow_steer_bm_client_pref_5g_to_str(*client->pref_5g.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     kick_type: %s", client->kick_type.cur == NULL ? "(nil)" : ow_steer_bm_client_kick_type_to_str(*client->kick_type.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     pre_assoc_auth_block: %s", client->pre_assoc_auth_block.cur == NULL ? "(nil)" : (*client->pre_assoc_auth_block.cur ? "true" : "false"));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     kick_upon_idle: %s", client->kick_upon_idle.cur == NULL ? "(nil)" : (*client->kick_upon_idle.cur ? "true" : "false"));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     send_rrm_after_assoc: %s", client->send_rrm_after_assoc.cur == NULL ? "(nil)" : (*client->send_rrm_after_assoc.cur ? "true" : "false"));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     backoff_secs: %s", client->backoff_secs.cur == NULL ? "(nil)":  strfmta("%u", *client->backoff_secs.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     backoff_exp_base: %s", client->backoff_exp_base.cur == NULL ? "(nil)" : strfmta("%u", *client->backoff_exp_base.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     max_rejects: %s", client->max_rejects.cur == NULL ? "(nil)" : strfmta("%u", *client->max_rejects.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     rejects_tmout_secs: %s", client->rejects_tmout_secs.cur == NULL ? "(nil)" : strfmta("%u", *client->rejects_tmout_secs.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     force_kick: %s", client->force_kick.cur == NULL ? "(nil)" : ow_steer_bm_client_force_kick_to_str(*client->force_kick.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     sc_kick_type: %s", client->sc_kick_type.cur == NULL ? "(nil)" : ow_steer_bm_client_sc_kick_type_to_str(*client->sc_kick_type.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     sticky_kick_type: %s", client->sticky_kick_type.cur == NULL ? "(nil)" :
                                                        ow_steer_bm_client_sticky_kick_type_to_str(*client->sticky_kick_type.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     neighbor_list_filter_by_beacon_report: %s", client->neighbor_list_filter_by_beacon_report.cur == NULL ? "(nil)" :
                                                                             strfmta("%s", *client->neighbor_list_filter_by_beacon_report.cur == true ? "true" : "false"));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     cs_mode: %s", client->cs_mode.cur == NULL ? "(nil)" : ow_steer_bm_client_cs_mode_to_str(*client->cs_mode.cur));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     sc_btm_params: %s", client->sc_btm_params == NULL ? "(nil)" : "");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     pref_5g_pre_assoc_block_timeout_msecs: %s", client->pref_5g_pre_assoc_block_timeout_msecs.cur == NULL ? "(nil)" :
                                                                                  strfmta("%u", *client->pref_5g_pre_assoc_block_timeout_msecs.cur));
        ow_steer_bm_btm_params_sigusr1_dump(client->sc_btm_params);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     steering_btm_params: %s", client->steering_btm_params == NULL ? "(nil)" : "");
        ow_steer_bm_btm_params_sigusr1_dump(client->steering_btm_params);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     cs_params: %s", client->cs_params == NULL ? "(nil)" : "");
        ow_steer_bm_cs_params_sigusr1_dump(client->cs_params);

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     sta_tree:");
        struct ow_steer_bm_sta *sta;
        ds_dlist_foreach(&client->sta_list, sta) {
             osw_diag_pipe_writef(pipe, "ow: steer: bm:       sta:");
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", sta);
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->addr));
        }
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_sigusr1_dump_groups(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm: groups:");

    struct ow_steer_bm_group *group;
    ds_tree_foreach(&g_group_tree, group) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   group:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", group);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     id: %s", group->id);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", group->removed == true ? "true" : "false");

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif_tree:");
        struct ow_steer_bm_vif *vif;
        ds_tree_foreach(&group->vif_tree, vif) {
             osw_diag_pipe_writef(pipe, "ow: steer: bm:       vif:");
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", vif);
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         vif_name: %s", vif->vif_name.buf);
        }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     sta_tree:");
        struct ow_steer_bm_sta *sta;
        ds_tree_foreach(&group->sta_tree, sta) {
             osw_diag_pipe_writef(pipe, "ow: steer: bm:       sta:");
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", sta);
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->addr));
        }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bss_tree:");
        struct ow_steer_bm_bss *bss;
        ds_tree_foreach(&group->bss_tree, bss) {
             osw_diag_pipe_writef(pipe, "ow: steer: bm:       bss:");
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         ptr: %p", bss);
             osw_diag_pipe_writef(pipe, "ow: steer: bm:         bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&bss->bssid));
        }
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_sigusr1_dump_bsses(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm: bsses:");

    struct ow_steer_bm_bss *bss;
    ds_dlist_foreach(&g_bss_list, bss) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   bss:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", bss);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&bss->bssid));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", bss->removed == true ? "true" : "false");

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     neighbor: %s", bss->neighbor != NULL ? "" : "(nill)");
        if (bss->neighbor != NULL) {
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        ptr: %p", bss->neighbor);
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&bss->neighbor->bssid));
        }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     vif: %s", bss->vif != NULL ? "" : "(nill)");
        if (bss->vif != NULL) {
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        ptr: %p", bss->vif);
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        vif_name: %s", bss->vif->vif_name.buf);
        }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bss_entry: %s", bss->bss_entry != NULL ? "(set)" : "(nill)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     group:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:       id: %s", bss->group->id);
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_sigusr1_dump_stas(void)
{
    osw_diag_pipe_t *pipe = osw_diag_pipe_open();
    osw_diag_pipe_writef(pipe, "ow: steer: bm: stas:");

    struct ow_steer_bm_sta *sta;
    ds_dlist_foreach(&g_sta_list, sta) {
        osw_diag_pipe_writef(pipe, "ow: steer: bm:   sta:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     ptr: %p", sta);
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->addr));
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     removed: %s", sta->removed == true ? "true" : "false");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     steer_sta: %s", sta->steer_sta == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bss_filter_policy: %s", sta->bss_filter_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     chan_cap_policy: %s", sta->chan_cap_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     defer_vif_down_deny_policy: %s", sta->defer_vif_down_deny_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     defer_vif_down_allow_policy: %s", sta->defer_vif_down_allow_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     pre_assoc_2g_policy: %s", sta->pre_assoc_2g_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     bottom_lwm_2g_xing_policy: %s", sta->bottom_lwm_2g_xing_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     lwm_2g_xing_policy: %s", sta->lwm_2g_xing_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     lwm_5g_xing_policy: %s", sta->lwm_5g_xing_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     lwm_6g_xing_policy: %s", sta->lwm_6g_xing_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     hwm_2g_policy: %s", sta->hwm_2g_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     force_kick_policy: %s", sta->force_kick_policy == NULL ? "(nil)" : "(set)");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:     client: %s", sta->client == NULL ? "(nil)" : "");
        if (sta->client != NULL) {
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        ptr: %p", sta->client);
            osw_diag_pipe_writef(pipe, "ow: steer: bm:        bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->client->addr));
        }

        osw_diag_pipe_writef(pipe, "ow: steer: bm:     group:");
        osw_diag_pipe_writef(pipe, "ow: steer: bm:       id: %s", sta->group->id);
    }
    osw_diag_pipe_close(pipe);
}

static void
ow_steer_bm_policy_mediator_sched_stack_recalc_cb(struct ow_steer_policy *policy,
                                                  void *priv)
{
    ASSERT(policy != NULL, "");
    ASSERT(priv != NULL, "");

    struct ow_steer_bm_sta *sta = priv;
    struct ow_steer_policy_stack *policy_stack = ow_steer_sta_get_policy_stack(sta->steer_sta);

    ow_steer_policy_stack_schedule_recalc(policy_stack);
}

static bool
ow_steer_bm_policy_mediator_trigger_executor_cb(struct ow_steer_policy *policy,
                                                void *priv)
{
    ASSERT(policy != NULL, "");
    ASSERT(priv != NULL, "");

    struct ow_steer_bm_sta *sta = priv;

    if (sta->active_policy != NULL) {
        const struct ow_steer_policy *important = ow_steer_policy_get_more_important(policy, sta->active_policy);
        if (policy != important) {
            LOGD("ow: steer: bm: sta: "OSW_HWADDR_FMT" policy: %s issued trigger executor but was supressed because policy: %s is active",
                 OSW_HWADDR_ARG(&sta->addr), ow_steer_policy_get_name(policy), ow_steer_policy_get_name(sta->active_policy));
            return false;
        }
    }

    sta->active_policy = policy;
    if (sta->active_policy == ow_steer_bm_policy_hwm_2g_get_base(sta->hwm_2g_policy)) {
        ow_steer_bm_sta_kick_state_steering_trig(sta);
        ow_steer_bm_sta_execute_hwm_kick(sta);
    }
    else if (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_2g_xing_policy)) {
        ow_steer_bm_sta_kick_state_sticky_trig(sta);
        /* TODO */
    }
    else if (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_5g_xing_policy)) {
        ow_steer_bm_sta_kick_state_sticky_trig(sta);
        /* TODO */
    }
    else if (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_6g_xing_policy)) {
        ow_steer_bm_sta_kick_state_sticky_trig(sta);
        /* TODO */
    }
    else if (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->bottom_lwm_2g_xing_policy)) {
        ow_steer_bm_sta_kick_state_sticky_trig(sta);
        /* TODO */
    }
    else if (sta->active_policy == ow_steer_policy_force_kick_get_base(sta->force_kick_policy)) {
        ow_steer_bm_sta_kick_state_force_trig(sta);
        ow_steer_bm_sta_execute_force_kick(sta);
    }
    else {
        LOGD("ow: steer: bm: sta: addr "OSW_HWADDR_FMT" policy: %s unexpectedly triggerd executor, ignoring",
             OSW_HWADDR_ARG(&sta->addr), ow_steer_policy_get_name(sta->active_policy));
        return false;
    }

    return true;
}

static void
ow_steer_bm_policy_mediator_dismiss_executor_cb(struct ow_steer_policy *policy,
                                                void *priv)
{
    ASSERT(policy != NULL, "");
    ASSERT(priv != NULL, "");

    struct ow_steer_bm_sta *sta = priv;

    if (sta->active_policy != policy)
        return;

    LOGD("ow: steer: bm: sta: addr: "OSW_HWADDR_FMT" active policy name: %s dismissing execution", OSW_HWADDR_ARG(&sta->addr),
         ow_steer_policy_get_name(sta->active_policy));

    ow_steer_sta_schedule_executor_call(sta->steer_sta);

    sta->active_policy = policy;
    if ((sta->active_policy == ow_steer_bm_policy_hwm_2g_get_base(sta->hwm_2g_policy)) ||
        (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_2g_xing_policy)) ||
        (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_5g_xing_policy)) ||
        (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->lwm_6g_xing_policy)) ||
        (sta->active_policy == ow_steer_policy_snr_xing_get_base(sta->bottom_lwm_2g_xing_policy)) ||
        (sta->active_policy == ow_steer_policy_force_kick_get_base(sta->force_kick_policy))) {
        ow_steer_bm_sta_kick_state_reset(sta);
    }

    sta->active_policy = NULL;
}

static void
ow_steer_bm_policy_mediator_notify_backoff_cb(struct ow_steer_policy *policy,
                                              void *priv,
                                              const bool enabled,
                                              const unsigned int period)
{
    ASSERT(policy != NULL, "");
    ASSERT(priv != NULL, "");

    struct ow_steer_bm_sta *sta = priv;
    const char *policy_name = ow_steer_policy_get_name(policy);

    struct ow_steer_policy *base = ow_steer_policy_pre_assoc_get_base(sta->pre_assoc_2g_policy);
    if (base != policy) return;

    const struct osw_hwaddr *bssid = ow_steer_policy_pre_assoc_get_bssid(sta->pre_assoc_2g_policy);
    if (bssid == NULL) return;

    const struct osw_state_vif_info *vif_info = osw_state_vif_lookup_by_mac_addr(bssid);
    if (WARN_ON(vif_info == NULL)) return;

    LOGD("ow: steer: bm: policy %s notified about backoff,"
         " vif_name: %s"
         " mac_addr: "OSW_HWADDR_FMT
         " enabled: "OSW_BOOL_FMT
         " period: %u",
         policy_name,
         vif_info->vif_name,
         OSW_HWADDR_ARG(&sta->addr),
         OSW_BOOL_ARG(enabled),
         period);

    ow_steer_bm_stats_set_client_backoff(&sta->addr,
                                     vif_info->vif_name,
                                     enabled,
                                     period);
}

static void
ow_steer_bm_policy_mediator_notify_steering_attempt_cb(struct ow_steer_policy *policy,
                                                       const char *vif_name,
                                                       void *priv)
{
    ASSERT(policy != NULL, "");
    ASSERT(priv != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct ow_steer_bm_sta *sta = priv;
    const char *policy_name = ow_steer_policy_get_name(policy);

    LOGD("ow: steer: bm: policy %s notified about band steering attempt,"
         " vif_name: %s"
         " mac_addr: "OSW_HWADDR_FMT,
         policy_name,
         vif_name,
         OSW_HWADDR_ARG(&sta->addr));

    ow_steer_bm_stats_set_client_band_steering_attempt(&sta->addr,
                                                       vif_name);
}

static void
ow_steer_bm_executor_action_mediator_sched_recall_cb(struct ow_steer_executor_action *action,
                                                     void *mediator_priv)
{
    ASSERT(action != NULL, "");
    ASSERT(mediator_priv != NULL, "");

    struct ow_steer_bm_sta *sta = mediator_priv;
    ow_steer_sta_schedule_executor_call(sta->steer_sta);
}

static void
ow_steer_bm_executor_action_mediator_going_busy_cb(struct ow_steer_executor_action *action,
                                                   void *mediator_priv)
{
    ASSERT(action != NULL, "");
    ASSERT(mediator_priv != NULL, "");

    struct ow_steer_bm_sta *sta = mediator_priv;
    const char *action_name = ow_steer_executor_action_get_name(action);

    LOGD("ow: steer: bm: action %s going busy,"
         " mac_addr: "OSW_HWADDR_FMT,
         action_name,
         OSW_HWADDR_ARG(&sta->addr));
}

static void
ow_steer_bm_executor_action_mediator_data_sent_cb(struct ow_steer_executor_action *action,
                                                  void *mediator_priv)
{
    ASSERT(action != NULL, "");
    ASSERT(mediator_priv != NULL, "");

    struct ow_steer_bm_sta *sta = mediator_priv;
    const char *action_name = ow_steer_executor_action_get_name(action);

    LOGD("ow: steer: bm: action %s data sent,"
         " mac_addr: "OSW_HWADDR_FMT,
         action_name,
         OSW_HWADDR_ARG(&sta->addr));

    if (strcmp(action_name, "btm") == 0) {
        LOGD("ow: steer: bm: btm action data sent, update kick state monitor");

        const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(&sta->addr);
        if (WARN_ON(sta_info == NULL)) return;
        const struct osw_state_vif_info *vif_info = sta_info->vif;
        if (WARN_ON(vif_info == NULL)) return;
        ow_steer_bm_sta_kick_state_send_btm_event(sta,
                                                  vif_info->vif_name);
    }
}

static void
ow_steer_bm_executor_action_mediator_going_idle_cb(struct ow_steer_executor_action *action,
                                                   void *mediator_priv)
{
    ASSERT(action != NULL, "");
    ASSERT(mediator_priv != NULL, "");

    struct ow_steer_bm_sta *sta = mediator_priv;
    const char *action_name = ow_steer_executor_action_get_name(action);

    LOGD("ow: steer: bm: action %s going idle,"
         " mac_addr: "OSW_HWADDR_FMT,
         action_name,
         OSW_HWADDR_ARG(&sta->addr));

    if (strcmp(action_name, "btm") == 0) {
        LOGD("ow: steer: bm: btm action going idle, update kick state monitor");
        ow_steer_bm_sta_kick_state_reset(sta);
    }
}

const char*
ow_steer_bm_client_cs_state_to_cstr(enum ow_steer_bm_client_cs_state cs_state)
{
    switch (cs_state) {
        case OW_STEER_BM_CS_STATE_UNKNOWN:
            break;
        case OW_STEER_BM_CS_STATE_INIT:
            return "";
        case OW_STEER_BM_CS_STATE_NONE:
            return "none";
        case OW_STEER_BM_CS_STATE_STEERING:
            return "steering";
        case OW_STEER_BM_CS_STATE_EXPIRED:
            return "expired";
        case OW_STEER_BM_CS_STATE_FAILED:
            return "failed";
    }

    return NULL;
}

struct ow_steer_bm_group*
ow_steer_bm_get_group(const char *id)
{
    ASSERT(id != NULL, "");

    struct ow_steer_bm_group *group = ds_tree_find(&g_group_tree, id);
    if (group != NULL) {
        if (group->removed) {
            group->removed = false;
            ow_steer_bm_observer_register(&group->observer);
        }
        return group;
    }

    group = CALLOC(1, sizeof(*group));
    group->id = STRDUP(id);
    ds_tree_init(&group->vif_tree, (ds_key_cmp_t*) osw_ifname_cmp, struct ow_steer_bm_vif, group_node);
    ds_tree_init(&group->sta_tree, (ds_key_cmp_t*) osw_hwaddr_cmp, struct ow_steer_bm_sta, group_node);
    ds_tree_init(&group->bss_tree, (ds_key_cmp_t*) osw_hwaddr_cmp, struct ow_steer_bm_bss, group_node);
    ds_tree_init(&group->pending_vifs, (ds_key_cmp_t*)osw_ifname_cmp, struct ow_steer_bm_vif_pending, group_node);
    group->observer.vif_up_fn = ow_steer_bm_group_vif_toggled_cb;
    group->observer.vif_down_fn = ow_steer_bm_group_vif_toggled_cb;
    group->observer.neighbor_up_fn = ow_steer_bm_group_neighbor_toggled_cb;
    group->observer.neighbor_down_fn = ow_steer_bm_group_neighbor_toggled_cb;
    group->observer.client_added_fn = ow_steer_bm_group_client_added_cb;
    group->observer.client_removed_fn = ow_steer_bm_group_client_removed_cb;
    ds_tree_insert(&g_group_tree, group, group->id);

    LOGD("ow: steer: bm: group id: %s added", group->id);

    ow_steer_bm_observer_register(&group->observer);
    OW_STEER_BM_SCHEDULE_WORK;

    return group;
}

void
ow_steer_bm_group_unset(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");

    if (group->removed == true)
        return;

    LOGD("ow: steer: bm: group id: %s removing", group->id);

    group->removed = true;
    ow_steer_bm_observer_unregister(&group->observer);
    ow_steer_bm_group_free_pending_vifs(group);

    struct ow_steer_bm_vif *vif;
    ds_tree_foreach(&group->vif_tree, vif)
        ow_steer_bm_vif_unset(vif);

    /* This is a special case. The OW_STEER_BM_SCHEDULE_WORK
     * which uses a deferred execution of ow_steer_bm_work()
     * can't be used here. The reason is that if a steering
     * group is re-injected (unset+set) with overlapping
     * ifnames then it would experience collision in the
     * global structures that are keyed by vif_name.
     *
     * Running the recalc here makes sure to remove all
     * vifs _first_ before processing subsequent
     * ow_steer_bm_group_set().
     */
    ow_steer_bm_work();
}

void
ow_steer_bm_group_reset(struct ow_steer_bm_group *group)
{
    ASSERT(group != NULL, "");
    LOGN("ow: steer: bm: group: TODO %s", __FUNCTION__);
}

static void
ow_steer_bm_defer_vif_down_set(struct ow_steer_bm_vif *vif,
                               bool state)
{
    if (state) {
        WARN_ON(vif->shutting_down == true);
        vif->shutting_down = true;
        LOGI("ow: steer: bm: vif: %s: shutting down period started", vif->vif_name.buf);
    }
    else {
        WARN_ON(vif->shutting_down == false);
        vif->shutting_down = false;
        LOGI("ow: steer: bm: vif: %s: shutting down period stopped", vif->vif_name.buf);
    }
    OW_STEER_BM_SCHEDULE_WORK;
}

static void
ow_steer_bm_defer_vif_down_started_cb(void *fn_priv)
{
    ow_steer_bm_defer_vif_down_set(fn_priv, true);
}

static void
ow_steer_bm_defer_vif_down_stopped_cb(void *fn_priv)
{
    ow_steer_bm_defer_vif_down_set(fn_priv, false);
}

static void
ow_steer_bm_vif_init_defer_vif_down(struct ow_steer_bm_vif *vif,
                                    int grace_period_seconds)
{
    osw_defer_vif_down_t *defer_mod = OSW_MODULE_LOAD(osw_defer_vif_down);
    vif->defer_vif_down_rule = osw_defer_vif_down_rule(defer_mod,
                                                       vif->vif_name.buf,
                                                       grace_period_seconds);
    vif->defer_vif_down_obs = osw_defer_vif_down_observer(defer_mod,
                                                          vif->vif_name.buf,
                                                          ow_steer_bm_defer_vif_down_started_cb,
                                                          ow_steer_bm_defer_vif_down_stopped_cb,
                                                          vif);
}

struct ow_steer_bm_vif*
ow_steer_bm_group_get_vif(struct ow_steer_bm_group *group,
                          const char *vif_name)
{
    ASSERT(group != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct osw_ifname tmp_vif_name;
    STRSCPY(tmp_vif_name.buf, vif_name);

    struct ow_steer_bm_vif *vif = ds_tree_find(&g_vif_tree, &tmp_vif_name);
    if (vif != NULL) {
        if (vif->group != group) {
            struct ow_steer_bm_vif_pending *pending = ds_tree_find(&group->pending_vifs, &tmp_vif_name);
            if (pending == NULL) {
                LOGD("ow: steer: bm: vif vif_name: %s overlapping addition between group: %s vs %s",
                        vif->vif_name.buf,
                        vif->group->id,
                        group->id);
                struct ow_steer_bm_vif_pending *pending = CALLOC(1, sizeof(*pending));
                memcpy(&pending->vif_name, &tmp_vif_name, sizeof(pending->vif_name));
                ds_tree_insert(&group->pending_vifs, pending, &pending->vif_name);
                pending->group = group;
            }
            return NULL;
        }
        ASSERT(vif->group == group, "");
        vif->removed = false;
        return vif;
    }

    vif = CALLOC(1, sizeof(*vif));
    memcpy(&vif->vif_name, &tmp_vif_name, sizeof(vif->vif_name));
    vif->group = group;
    ds_tree_insert(&group->vif_tree, vif, &vif->vif_name);
    ds_tree_insert(&g_vif_tree, vif, &vif->vif_name);

    /* FIXME: This could be opt-in and configurable. Given
     * what this does, enable it by default for now with a
     * a rule of thumb grace period.
     */
    ow_steer_bm_vif_init_defer_vif_down(vif, 10);

    LOGD("ow: steer: bm: vif vif_name: %s added in group id: %s", vif->vif_name.buf, vif->group->id);

    vif->state_obs.name = "ow_steer_bm_group";
    vif->state_obs.vif_added_fn = ow_steer_bm_state_obs_vif_added_cb;
    vif->state_obs.vif_changed_fn = ow_steer_bm_state_obs_vif_changed_cb;
    vif->state_obs.vif_removed_fn = ow_steer_bm_state_obs_vif_removed_cb;
    osw_state_register_observer(&vif->state_obs);

    struct ow_steer_bm_observer *observer;
    ds_dlist_foreach(&g_observer_list, observer)
        if (observer->vif_added_fn != NULL)
            observer->vif_added_fn(observer, vif);

    OW_STEER_BM_SCHEDULE_WORK;

    return vif;
}

void
ow_steer_bm_vif_unset(struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");

    if (vif->removed == true)
        return;

    vif->removed = true;
    if (vif->bss != NULL)
        ow_steer_bm_bss_unset(vif->bss);

    OW_STEER_BM_SCHEDULE_WORK;
}

void
ow_steer_bm_vif_reset(struct ow_steer_bm_vif *vif)
{
    ASSERT(vif != NULL, "");
    LOGN("ow: steer: bm: vif: TODO %s", __FUNCTION__);
}

struct ow_steer_bm_neighbor*
ow_steer_bm_get_neighbor(const uint8_t *bssid)
{
    ASSERT(bssid != NULL, "");

    struct osw_hwaddr tmp_bssid;
    memcpy(&tmp_bssid.octet, bssid, sizeof(tmp_bssid.octet));

    struct ow_steer_bm_neighbor *neighbor = ds_tree_find(&g_neighbor_tree, &tmp_bssid);
    if (neighbor != NULL) {
        neighbor->removed = false;
        return neighbor;
    }

    neighbor = CALLOC(1, sizeof(*neighbor));
    memcpy(&neighbor->bssid, &tmp_bssid, sizeof(neighbor->bssid));
    neighbor->observer.vif_added_fn = ow_steer_bm_neighbor_vif_added_cb;
    neighbor->observer.vif_changed_channel_fn = ow_steer_bm_neighbor_vif_changed_channel_cb;
    neighbor->observer.vif_removed_fn = ow_steer_bm_neighbor_vif_removed_cb;
    ds_tree_insert(&g_neighbor_tree, neighbor, &neighbor->bssid);

    LOGD("ow: steer: bm: neighbor bssid: "OSW_HWADDR_FMT" added", OSW_HWADDR_ARG(&neighbor->bssid));

    ow_steer_bm_observer_register(&neighbor->observer);
    OW_STEER_BM_SCHEDULE_WORK;

    return neighbor;
}

void
ow_steer_bm_neighbor_unset(struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");

    if (neighbor->removed == true)
        return;

    neighbor->removed = true;
    if (neighbor->bss != NULL)
        ow_steer_bm_bss_unset(neighbor->bss);

    LOGD("%s unset", OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor));

    OW_STEER_BM_SCHEDULE_WORK;
}

void
ow_steer_bm_neighbor_reset(struct ow_steer_bm_neighbor *neighbor)
{
    ASSERT(neighbor != NULL, "");

    LOGD("%s reset TODO", OW_STEER_BM_NEIGHBOR_LOG_PREFIX(neighbor));
}

void
ow_steer_bm_neighbor_set_vif_name(struct ow_steer_bm_neighbor *neighbor,
                                  const char *vif_name)
{
    ASSERT(neighbor != NULL, "");

    struct osw_ifname tmp_vif_name;
    memset(&tmp_vif_name, 0, sizeof(tmp_vif_name));
    STRSCPY(tmp_vif_name.buf, vif_name);

    if (ow_steer_bm_mem_attr_cmp(neighbor->vif_name.next, &tmp_vif_name) == true)
        return;

    neighbor->vif_name.valid = false;
    FREE(neighbor->vif_name.next);
    neighbor->vif_name.next = NULL;
    if (vif_name != NULL) {
        neighbor->vif_name.next = CALLOC(1, sizeof(*neighbor->vif_name.next));
        STRSCPY(neighbor->vif_name.next->buf, vif_name);
    }

    OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_IFNAME(neighbor, vif_name);

    OW_STEER_BM_SCHEDULE_WORK;
}

void
ow_steer_bm_neighbor_set_channel_number(struct ow_steer_bm_neighbor *neighbor,
                                        const uint8_t *channel_number)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(neighbor, channel_number);
    OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_NUM(neighbor, channel_number, PRIu8);
}

void
ow_steer_bm_neighbor_set_ht_mode(struct ow_steer_bm_neighbor *neighbor,
                                 const enum ow_steer_bm_neighbor_ht_mode *ht_mode)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(neighbor, ht_mode);
    OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_ENUM(neighbor, ht_mode, ow_steer_bm_neighbor_ht_mode_to_str);
}

void
ow_steer_bm_neighbor_set_op_class(struct ow_steer_bm_neighbor *neighbor,
                                  const uint8_t *op_class)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(neighbor, op_class);
    OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_NUM(neighbor, op_class, PRIu8);
}

void
ow_steer_bm_neighbor_set_priority(struct ow_steer_bm_neighbor *neighbor,
                                  const unsigned int *priority)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(neighbor, priority);
    OW_STEER_BM_NEIGHBOR_ATTR_PRINT_CHANGE_NUM(neighbor, priority, "u");
}

const struct osw_hwaddr *
ow_steer_bm_neighbor_get_bssid(struct ow_steer_bm_neighbor *neighbor)
{
    return &neighbor->bssid;
}

const struct ow_steer_bm_bss *
ow_steer_bm_neighbor_get_bss(struct ow_steer_bm_neighbor *neighbor)
{
    return neighbor->bss;
}

struct ow_steer_bm_client*
ow_steer_bm_get_client(const uint8_t *addr)
{
    ASSERT(addr != NULL, "");

    struct osw_hwaddr tmp_addr;
    memcpy(&tmp_addr.octet, addr, sizeof(tmp_addr.octet));

    struct ow_steer_bm_client *client = ds_tree_find(&g_client_tree, &tmp_addr);
    if (client != NULL) {
        client->removed = false;
        return client;
    }

    client = CALLOC(1, sizeof(*client));
    memcpy(&client->addr, &tmp_addr, sizeof(client->addr));
    ds_dlist_init(&client->sta_list, struct ow_steer_bm_sta, client_node);
    ds_tree_init(&client->stats_tree, (ds_key_cmp_t*) osw_ifname_cmp, struct ow_steer_bm_vif_stats, node);
    ds_tree_insert(&g_client_tree, client, &client->addr);

    LOGD("ow: steer: bm: client addr: "OSW_HWADDR_FMT" added", OSW_HWADDR_ARG(&client->addr));

    OW_STEER_BM_SCHEDULE_WORK;

    struct ow_steer_bm_observer *observer;
    ds_dlist_foreach(&g_observer_list, observer)
        if (observer->client_added_fn != NULL)
            observer->client_added_fn(observer, client);

    return client;
}

void
ow_steer_bm_client_unset(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    LOGD("%s unset TODO", OW_STEER_BM_CLIENT_LOG_PREFIX(client));
}

void
ow_steer_bm_client_reset(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    LOGD("%s reset TODO", OW_STEER_BM_CLIENT_LOG_PREFIX(client));
}

void
ow_steer_bm_client_set_hwm(struct ow_steer_bm_client *client,
                           const unsigned int *hwm)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, hwm);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, hwm, "u");
}

void
ow_steer_bm_client_set_lwm(struct ow_steer_bm_client *client,
                           const unsigned int *lwm)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, lwm);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, lwm, "u");
}

void
ow_steer_bm_client_set_bottom_lwm(struct ow_steer_bm_client *client,
                                  const unsigned int *bottom_lwm)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, bottom_lwm);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, bottom_lwm, "u");
}

void
ow_steer_bm_client_set_pref_5g(struct ow_steer_bm_client *client,
                               const enum ow_steer_bm_client_pref_5g *pref_5g)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, pref_5g);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, pref_5g, ow_steer_bm_client_pref_5g_to_str);
}

void
ow_steer_bm_client_set_kick_type(struct ow_steer_bm_client *client,
                                  const enum ow_steer_bm_client_kick_type *kick_type)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, kick_type);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, kick_type, ow_steer_bm_client_kick_type_to_str);
}

void
ow_steer_bm_client_set_pre_assoc_auth_block(struct ow_steer_bm_client *client,
                                            const bool *pre_assoc_auth_block)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, pre_assoc_auth_block);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_BOOL(client, pre_assoc_auth_block);
}

void
ow_steer_bm_client_set_kick_upon_idle(struct ow_steer_bm_client *client,
                                      const bool *kick_upon_idle)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, kick_upon_idle);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_BOOL(client, kick_upon_idle);
}

void
ow_steer_bm_client_set_send_rrm_after_assoc(struct ow_steer_bm_client *client,
                                            const bool *send_rrm_after_assoc)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, send_rrm_after_assoc);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_BOOL(client, send_rrm_after_assoc);
}

void
ow_steer_bm_client_set_backoff_secs(struct ow_steer_bm_client *client,
                                    const unsigned int *backoff_secs)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, backoff_secs);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, backoff_secs, "u");
}

void
ow_steer_bm_client_set_backoff_exp_base(struct ow_steer_bm_client *client,
                                        const unsigned int *backoff_exp_base)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, backoff_exp_base);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, backoff_exp_base, "u");
}

void
ow_steer_bm_client_set_max_rejects(struct ow_steer_bm_client *client,
                                   const unsigned int *max_rejects)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, max_rejects);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, max_rejects, "u");
}

void
ow_steer_bm_client_set_rejects_tmout_secs(struct ow_steer_bm_client *client,
                                          const unsigned int *rejects_tmout_secs)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, rejects_tmout_secs);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, rejects_tmout_secs, "u");
}

void
ow_steer_bm_client_set_force_kick(struct ow_steer_bm_client *client,
                                  const enum ow_steer_bm_client_force_kick *force_kick)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, force_kick);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, force_kick, ow_steer_bm_client_force_kick_to_str);
}

void
ow_steer_bm_client_set_sc_kick_type(struct ow_steer_bm_client *client,
                                    const enum ow_steer_bm_client_sc_kick_type *sc_kick_type)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, sc_kick_type);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, sc_kick_type, ow_steer_bm_client_sc_kick_type_to_str);
}

void
ow_steer_bm_client_set_sticky_kick_type(struct ow_steer_bm_client *client,
                                        const enum ow_steer_bm_client_sticky_kick_type *sticky_kick_type)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, sticky_kick_type);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, sticky_kick_type, ow_steer_bm_client_sticky_kick_type_to_str);
}

void
ow_steer_bm_client_set_neighbor_list_filter_by_beacon_report(struct ow_steer_bm_client *client,
                                                             const bool *neighbor_list_filter_by_beacon_report)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, neighbor_list_filter_by_beacon_report);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_BOOL(client, neighbor_list_filter_by_beacon_report);
}

void
ow_steer_bm_client_set_pref_5g_pre_assoc_block_timeout_msecs(struct ow_steer_bm_client *client,
                                                             const unsigned int *pref_5g_pre_assoc_block_timeout_msecs)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, pref_5g_pre_assoc_block_timeout_msecs);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_NUM(client, pref_5g_pre_assoc_block_timeout_msecs, "u");
}

void
ow_steer_bm_client_set_cs_state_mutate_cb(struct ow_steer_bm_client *client,
                                          ow_steer_bm_client_set_cs_state_mutate_fn_t *cs_state_mutate_fn)
{
    ASSERT(client != NULL, "");
    client->cs_state_mutate_fn = cs_state_mutate_fn;
}

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_sc_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->sc_btm_params != NULL)
        goto end;

    client->sc_btm_params = CALLOC(1, sizeof(*client->sc_btm_params));
    client->sc_btm_params->name = "sc_btm_params";
    memcpy(&client->sc_btm_params->sta_addr, &client->addr, sizeof(client->sc_btm_params->sta_addr));

    LOGD("%s added", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->sc_btm_params));

    OW_STEER_BM_SCHEDULE_WORK;

end:
    return client->sc_btm_params;
}

void
ow_steer_bm_client_unset_sc_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->sc_btm_params == NULL)
        return;

    LOGD("%s unset", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->sc_btm_params));

    ow_steer_bm_btm_params_free(client->sc_btm_params);
    client->sc_btm_params = NULL;
    OW_STEER_BM_SCHEDULE_WORK;
}

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_steering_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->steering_btm_params != NULL)
        goto end;

    client->steering_btm_params = CALLOC(1, sizeof(*client->steering_btm_params));
    client->steering_btm_params->name = "steering_btm_params";
    memcpy(&client->steering_btm_params->sta_addr, &client->addr, sizeof(client->steering_btm_params->sta_addr));

    LOGD("%s added", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->steering_btm_params));

    OW_STEER_BM_SCHEDULE_WORK;

end:
    return client->steering_btm_params;
}

void
ow_steer_bm_client_unset_steering_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->steering_btm_params == NULL)
        return;

    LOGD("%s unset", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->steering_btm_params));

    ow_steer_bm_btm_params_free(client->steering_btm_params);
    client->steering_btm_params = NULL;
    OW_STEER_BM_SCHEDULE_WORK;
}

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_sticky_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->sticky_btm_params != NULL)
        goto end;

    client->sticky_btm_params = CALLOC(1, sizeof(*client->sticky_btm_params));
    client->sticky_btm_params->name = "sticky_btm_params";
    memcpy(&client->sticky_btm_params->sta_addr, &client->addr, sizeof(client->sticky_btm_params->sta_addr));

    LOGD("%s added", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->sticky_btm_params));

    OW_STEER_BM_SCHEDULE_WORK;

end:
    return client->sticky_btm_params;
}

void
ow_steer_bm_client_unset_sticky_btm_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->sticky_btm_params == NULL)
        return;

    LOGD("%s unset", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(client->sticky_btm_params));

    ow_steer_bm_btm_params_free(client->sticky_btm_params);
    client->sticky_btm_params = NULL;
    OW_STEER_BM_SCHEDULE_WORK;
}

void
ow_steer_bm_btm_params_reset(struct ow_steer_bm_btm_params *btm_params)
{
    ASSERT(btm_params != NULL, "");

    LOGN("%s reset TODO", OW_STEER_BM_BTM_PARAMS_LOG_PREFIX(btm_params));
}

void
ow_steer_bm_btm_params_set_bssid(struct ow_steer_bm_btm_params *btm_params,
                                 const struct osw_hwaddr *bssid)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(btm_params, bssid);
    OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_HWADDR(btm_params, bssid);
}

void
ow_steer_bm_btm_params_set_disassoc_imminent(struct ow_steer_bm_btm_params *btm_params,
                                             const bool *disassoc_imminent)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(btm_params, disassoc_imminent);
    OW_STEER_BM_BTM_PARAMS_ATTR_PRINT_CHANGE_DISASSOC_IMMINENT(btm_params, disassoc_imminent);
}

void
ow_steer_bm_sigusr1_dump(void)
{
    ow_steer_bm_sigusr1_dump_neighbors();
    ow_steer_bm_sigusr1_dump_vifs();
    ow_steer_bm_sigusr1_dump_clients();
    ow_steer_bm_sigusr1_dump_groups();
    ow_steer_bm_sigusr1_dump_bsses();
    ow_steer_bm_sigusr1_dump_stas();
}

OSW_MODULE(ow_steer_bm)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_bss_map);
    OSW_MODULE_LOAD(osw_rrm_meas);
    OSW_MODULE_LOAD(ow_steer);

    ow_steer_bm_init();

    return NULL;
}

void
ow_steer_bm_client_set_cs_mode(struct ow_steer_bm_client *client,
                               const enum ow_steer_bm_client_cs_mode *cs_mode)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(client, cs_mode);
    OW_STEER_BM_CLIENT_ATTR_PRINT_CHANGE_ENUM(client, cs_mode, ow_steer_bm_client_cs_mode_to_str);
}

struct ow_steer_bm_cs_params*
ow_steer_bm_client_get_cs_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->cs_params != NULL)
        goto end;

    client->cs_params = CALLOC(1, sizeof(*client->cs_params));
    memcpy(&client->cs_params->sta_addr, &client->addr, sizeof(client->cs_params->sta_addr));

    LOGD("%s added", OW_STEER_BM_CS_PARAMS_LOG_PREFIX(client->cs_params));

    OW_STEER_BM_SCHEDULE_WORK;

end:
    return client->cs_params;
}

void
ow_steer_bm_client_unset_cs_params(struct ow_steer_bm_client *client)
{
    ASSERT(client != NULL, "");

    if (client->cs_params == NULL)
        return;

    LOGD("%s unset", OW_STEER_BM_CS_PARAMS_LOG_PREFIX(client->cs_params));

    ow_steer_bm_cs_params_free(client->cs_params);
    client->cs_params = NULL;
    OW_STEER_BM_SCHEDULE_WORK;
}

void
ow_steer_bm_cs_params_set_band(struct ow_steer_bm_cs_params *cs_params,
                               const enum ow_steer_bm_cs_params_band *band)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(cs_params, band);
    OW_STEER_BM_CS_PARAMS_ATTR_PRINT_CHANGE_ENUM(cs_params, band, ow_steer_bm_cs_params_band_to_str);
}

void
ow_steer_bm_cs_params_set_enforce_period(struct ow_steer_bm_cs_params *cs_params,
                                         const unsigned int *enforce_period_secs)
{
    OW_STEER_BM_MEM_ATTR_SET_BODY(cs_params, enforce_period_secs);
    OW_STEER_BM_CS_PARAMS_ATTR_PRINT_CHANGE_NUM(cs_params, enforce_period_secs, "u");
}

#include "ow_steer_bm_ut.c"
