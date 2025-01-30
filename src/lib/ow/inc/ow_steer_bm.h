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

#ifndef OW_STEER_BM_H
#define OW_STEER_BM_H

#include <osw_state.h>
#include <osw_defer_vif_down.h>

enum ow_steer_bm_client_pref_5g {
    OW_STEER_BM_CLIENT_PREF_5G_NEVER,
    OW_STEER_BM_CLIENT_PREF_5G_ALWAYS,
    OW_STEER_BM_CLIENT_PREF_5G_HWM,
    OW_STEER_BM_CLIENT_PREF_5G_NON_DFS,
};

enum ow_steer_bm_client_kick_type {
    OW_STEER_BM_CLIENT_KICK_TYPE_DEAUTH,
    OW_STEER_BM_CLIENT_KICK_TYPE_BTM_DEAUTH,
};

enum ow_steer_bm_client_sc_kick_type {
    OW_STEER_BM_CLIENT_SC_KICK_TYPE_DEAUTH,
    OW_STEER_BM_CLIENT_SC_KICK_TYPE_BTM_DEAUTH,
};

enum ow_steer_bm_client_sticky_kick_type {
    OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_DEAUTH,
    OW_STEER_BM_CLIENT_STICKY_KICK_TYPE_BTM_DEAUTH,
};

enum ow_steer_bm_client_force_kick {
    OW_STEER_BM_CLIENT_FORCE_KICK_SPECULATIVE,
    OW_STEER_BM_CLIENT_FORCE_KICK_DIRECTED,
};

enum ow_steer_bm_neighbor_ht_mode {
    OW_STEER_BM_NEIGHBOR_HT20,
    OW_STEER_BM_NEIGHBOR_HT2040,
    OW_STEER_BM_NEIGHBOR_HT40,
    OW_STEER_BM_NEIGHBOR_HT40P, /* 40+ */
    OW_STEER_BM_NEIGHBOR_HT40M, /* 40- */
    OW_STEER_BM_NEIGHBOR_HT80,
    OW_STEER_BM_NEIGHBOR_HT160,
    OW_STEER_BM_NEIGHBOR_HT80P80, /* 80+80 */
    OW_STEER_BM_NEIGHBOR_HT320,
};

enum ow_steer_bm_client_cs_mode {
    OW_STEER_BM_CLIENT_CS_MODE_OFF,
    OW_STEER_BM_CLIENT_CS_MODE_HOME,
    OW_STEER_BM_CLIENT_CS_MODE_AWAY,
};

enum ow_steer_bm_cs_params_band {
    OW_STEER_BM_CLIENT_CS_PARAMS_BAND_2G,
    OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5G,
    OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GL,
    OW_STEER_BM_CLIENT_CS_PARAMS_BAND_5GU,
    OW_STEER_BM_CLIENT_CS_PARAMS_BAND_6G,
};

enum ow_steer_bm_client_cs_state {
    OW_STEER_BM_CS_STATE_UNKNOWN,
    OW_STEER_BM_CS_STATE_INIT,
    OW_STEER_BM_CS_STATE_NONE,
    OW_STEER_BM_CS_STATE_STEERING,
    OW_STEER_BM_CS_STATE_EXPIRED,
    OW_STEER_BM_CS_STATE_FAILED,
};

struct ow_steer_bm_group;
struct ow_steer_bm_vif;
struct ow_steer_bm_client;
struct ow_steer_bm_neighbor;
struct ow_steer_bm_btm_params;
struct ow_steer_bm_cs_params;
struct ow_steer_bm_observer;

typedef void
ow_steer_bm_vif_added_fn_t(struct ow_steer_bm_observer *observer,
                           struct ow_steer_bm_vif *vif);

typedef void
ow_steer_bm_vif_removed_fn_t(struct ow_steer_bm_observer *observer,
                             struct ow_steer_bm_vif *vif);

typedef void
ow_steer_bm_vif_up_fn_t(struct ow_steer_bm_observer *observer,
                        struct ow_steer_bm_vif *vif);

typedef void
ow_steer_bm_vif_down_fn_t(struct ow_steer_bm_observer *observer,
                          struct ow_steer_bm_vif *vif);

typedef void
ow_steer_bm_vif_changed_fn_t(struct ow_steer_bm_observer *observer,
                             struct ow_steer_bm_vif *vif);

typedef void
ow_steer_bm_vif_changed_channel_fn_t(struct ow_steer_bm_observer *observer,
                                     struct ow_steer_bm_vif *vif,
                                     const struct osw_channel *old_channel,
                                     const struct osw_channel *new_channel);

typedef void
ow_steer_bm_neighbor_up_fn_t(struct ow_steer_bm_observer *observer,
                             struct ow_steer_bm_neighbor *neighbor);

typedef void
ow_steer_bm_neighbor_down_fn_t(struct ow_steer_bm_observer *observer,
                               struct ow_steer_bm_neighbor *neighbor);

typedef void
ow_steer_bm_neighbor_changed_channel_fn_t(struct ow_steer_bm_observer *observer,
                                          struct ow_steer_bm_neighbor *neighbor,
                                          const struct osw_channel *old_channel,
                                          const struct osw_channel *new_channel);

typedef void
ow_steer_bm_client_added_fn_t(struct ow_steer_bm_observer *observer,
                              struct ow_steer_bm_client *client);

typedef void
ow_steer_bm_client_changed_fn_t(struct ow_steer_bm_observer *observer,
                                struct ow_steer_bm_client *client);

typedef void
ow_steer_bm_client_removed_fn_t(struct ow_steer_bm_observer *observer,
                                struct ow_steer_bm_client *client);

struct ow_steer_bm_observer {
    ow_steer_bm_vif_added_fn_t *vif_added_fn;
    ow_steer_bm_vif_removed_fn_t *vif_removed_fn;
    ow_steer_bm_vif_up_fn_t *vif_up_fn;
    ow_steer_bm_vif_down_fn_t *vif_down_fn;
    ow_steer_bm_vif_changed_fn_t *vif_changed_fn;
    ow_steer_bm_vif_changed_channel_fn_t *vif_changed_channel_fn;
    ow_steer_bm_neighbor_up_fn_t *neighbor_up_fn;
    ow_steer_bm_neighbor_down_fn_t *neighbor_down_fn;
    ow_steer_bm_neighbor_changed_channel_fn_t *neighbor_changed_channel_fn;
    ow_steer_bm_client_added_fn_t *client_added_fn;
    ow_steer_bm_client_changed_fn_t *client_changed_fn;
    ow_steer_bm_client_removed_fn_t *client_removed_fn;

    struct ds_dlist_node node;
};

struct ow_steer_bm_group {
    char *id;
    bool removed;

    struct ds_tree vif_tree;
    struct ds_tree sta_tree;
    struct ds_tree bss_tree;
    struct ow_steer_bm_observer observer;
    struct ds_tree pending_vifs;

    struct ds_tree_node node;
};

struct ow_steer_bm_bss {
    struct osw_hwaddr bssid;
    bool removed;

    struct ow_steer_bm_neighbor *neighbor;
    struct ow_steer_bm_vif *vif;

    struct osw_bss_entry *bss_entry;
    struct ow_steer_bm_group *group;
    struct ds_tree_node group_node;

    struct ds_dlist_node node;
};

struct ow_steer_bm_vif_untracked_client {
    struct ds_tree_node node;
    struct osw_hwaddr addr;
    struct ow_steer_bm_vif_stats *stats;
};

struct ow_steer_bm_vif {
    struct osw_ifname vif_name;
    bool removed;

    const struct osw_state_vif_info *vif_info;
    struct osw_state_observer state_obs;
    struct ow_steer_bm_bss *bss;
    struct ow_steer_bm_group *group;
    struct ds_tree_node group_node;
    struct ds_tree untracked_clients;

    struct osw_defer_vif_down_rule *defer_vif_down_rule;
    struct osw_defer_vif_down_observer *defer_vif_down_obs;
    bool shutting_down;

    struct ds_tree_node node;
};

struct ow_steer_bm_vif_pending {
    struct osw_ifname vif_name;
    struct ds_tree_node group_node;
    struct ow_steer_bm_group *group;
};

typedef void
ow_steer_bm_client_set_cs_state_mutate_fn_t(const struct osw_hwaddr *client_addr,
                                            const enum ow_steer_bm_client_cs_state cs_state);

const char*
ow_steer_bm_client_cs_state_to_cstr(enum ow_steer_bm_client_cs_state cs_state);

struct ow_steer_bm_group*
ow_steer_bm_get_group(const char *id);

void
ow_steer_bm_group_unset(struct ow_steer_bm_group *group);

void
ow_steer_bm_group_reset(struct ow_steer_bm_group *group);

struct ow_steer_bm_vif*
ow_steer_bm_group_get_vif(struct ow_steer_bm_group *group,
                          const char *vif_name);

void
ow_steer_bm_vif_unset(struct ow_steer_bm_vif *vif);

void
ow_steer_bm_vif_reset(struct ow_steer_bm_vif *vif);

struct ow_steer_bm_neighbor*
ow_steer_bm_get_neighbor(const uint8_t *bssid);

void
ow_steer_bm_neighbor_unset(struct ow_steer_bm_neighbor *neighbor);

void
ow_steer_bm_neighbor_reset(struct ow_steer_bm_neighbor *neighbor);

void
ow_steer_bm_neighbor_set_vif_name(struct ow_steer_bm_neighbor *neighbor,
                                  const char *vif_name);

void
ow_steer_bm_neighbor_set_channel_number(struct ow_steer_bm_neighbor *neighbor,
                                        const uint8_t *channel_number);

void
ow_steer_bm_neighbor_set_center_freq0_chan_number(struct ow_steer_bm_neighbor *neighbor,
                                        const uint8_t *channel_number);

void
ow_steer_bm_neighbor_set_ht_mode(struct ow_steer_bm_neighbor *neighbor,
                                 const enum ow_steer_bm_neighbor_ht_mode *ht_mode);

void
ow_steer_bm_neighbor_set_op_class(struct ow_steer_bm_neighbor *neighbor,
                                  const uint8_t *op_class);

void
ow_steer_bm_neighbor_set_priority(struct ow_steer_bm_neighbor *neighbor,
                                  const unsigned int *priority);

void
ow_steer_bm_neighbor_set_mld_addr(struct ow_steer_bm_neighbor *neighbor,
                                  const struct osw_hwaddr *mld_addr);

const struct osw_hwaddr *
ow_steer_bm_neighbor_get_bssid(struct ow_steer_bm_neighbor *neighbor);

const struct ow_steer_bm_bss *
ow_steer_bm_neighbor_get_bss(struct ow_steer_bm_neighbor *neighbor);

struct ow_steer_bm_client*
ow_steer_bm_get_client(const uint8_t *addr);

void
ow_steer_bm_client_unset(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_reset(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_set_hwm(struct ow_steer_bm_client *client,
                           const unsigned int *hwm);

void
ow_steer_bm_client_set_lwm(struct ow_steer_bm_client *client,
                           const unsigned int *lwm);

void
ow_steer_bm_client_set_bottom_lwm(struct ow_steer_bm_client *client,
                                  const unsigned int *bottom_lwm);

void
ow_steer_bm_client_set_pref_5g(struct ow_steer_bm_client *client,
                               const enum ow_steer_bm_client_pref_5g *pref_5g);

void
ow_steer_bm_client_set_kick_type(struct ow_steer_bm_client *client,
                                 const enum ow_steer_bm_client_kick_type *kick_type);

void
ow_steer_bm_client_set_kick_upon_idle(struct ow_steer_bm_client *client,
                                      const bool *kick_upon_idle);

void
ow_steer_bm_client_set_pre_assoc_auth_block(struct ow_steer_bm_client *client,
                                            const bool *pre_assoc_auth_block);

void
ow_steer_bm_client_set_send_rrm_after_assoc(struct ow_steer_bm_client *client,
                                            const bool *send_rrm_after_assoc);

void
ow_steer_bm_client_set_backoff_secs(struct ow_steer_bm_client *client,
                                    const unsigned int *backoff_secs);

void
ow_steer_bm_client_set_backoff_exp_base(struct ow_steer_bm_client *client,
                                        const unsigned int *backoff_exp_base);

void
ow_steer_bm_client_set_max_rejects(struct ow_steer_bm_client *client,
                                   const unsigned int *max_rejects);

void
ow_steer_bm_client_set_rejects_tmout_secs(struct ow_steer_bm_client *client,
                                          const unsigned int *rejects_tmout_secs);

void
ow_steer_bm_client_set_force_kick(struct ow_steer_bm_client *client,
                                  const enum ow_steer_bm_client_force_kick *force_kick);

void
ow_steer_bm_client_set_sc_kick_type(struct ow_steer_bm_client *client,
                                    const enum ow_steer_bm_client_sc_kick_type *sc_kick_type);

void
ow_steer_bm_client_set_sticky_kick_type(struct ow_steer_bm_client *client,
                                        const enum ow_steer_bm_client_sticky_kick_type *sticky_kick_type);

void
ow_steer_bm_client_set_neighbor_list_filter_by_beacon_report(struct ow_steer_bm_client *client,
                                                             const bool *neighbor_list_filter_by_beacon_report);

void
ow_steer_bm_client_set_pref_5g_pre_assoc_block_timeout_msecs(struct ow_steer_bm_client *client,
                                                             const unsigned int *pref_5g_pre_assoc_block_timeout_msecs);

void
ow_steer_bm_client_set_cs_state_mutate_cb(struct ow_steer_bm_client *client,
                                          ow_steer_bm_client_set_cs_state_mutate_fn_t *cs_state_mutate_fn);

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_sc_btm_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_unset_sc_btm_params(struct ow_steer_bm_client *client);

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_steering_btm_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_unset_steering_btm_params(struct ow_steer_bm_client *client);

struct ow_steer_bm_btm_params*
ow_steer_bm_client_get_sticky_btm_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_unset_sticky_btm_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_btm_params_reset(struct ow_steer_bm_btm_params *btm_params);

void
ow_steer_bm_btm_params_set_bssid(struct ow_steer_bm_btm_params *btm_params,
                                 const struct osw_hwaddr *bssid);

void
ow_steer_bm_btm_params_set_disassoc_imminent(struct ow_steer_bm_btm_params *btm_params,
                                             const bool *disassoc_imminent);

void
ow_steer_bm_client_set_cs_mode(struct ow_steer_bm_client *client,
                               const enum ow_steer_bm_client_cs_mode *cs_mode);

struct ow_steer_bm_cs_params*
ow_steer_bm_client_get_cs_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_client_unset_cs_params(struct ow_steer_bm_client *client);

void
ow_steer_bm_cs_params_set_band(struct ow_steer_bm_cs_params *cs_params,
                               const enum ow_steer_bm_cs_params_band *band);
void
ow_steer_bm_cs_params_set_enforce_period(struct ow_steer_bm_cs_params *cs_params,
                                         const unsigned int *enforce_period_secs);

void
ow_steer_bm_observer_register(struct ow_steer_bm_observer *observer);

void
ow_steer_bm_observer_unregister(struct ow_steer_bm_observer *observer);

#endif /* OW_STEER_BM_H */
