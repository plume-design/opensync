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

#ifndef OSW_STATE_H_INCLUDED
#define OSW_STATE_H_INCLUDED

#include <osw_drv_common.h>
#include <osw_drv.h>
#include <ds_dlist.h>

struct osw_state_observer;

struct osw_state_phy_info {
    const char *phy_name;
    struct osw_drv *drv;
    const struct osw_drv_phy_capab *drv_capab;
    const struct osw_drv_phy_state *drv_state;
};

struct osw_state_vif_info {
    const char *vif_name;
    const struct osw_state_phy_info *phy;
    const struct osw_drv_vif_state *drv_state;
};

struct osw_state_sta_info {
    const struct osw_hwaddr *mac_addr;
    const struct osw_state_vif_info *vif;
    const struct osw_drv_sta_state *drv_state;
    time_t connected_at;
    const void *assoc_req_ies;
    size_t assoc_req_ies_len;
};

struct osw_state_sta_auth_fail_arg {
    unsigned int snr;
};

struct osw_state_sta_action_frame_arg {
    const void *action_frame;
    size_t action_frame_len;
};

typedef void
osw_state_report_phy_fn_t(const struct osw_state_phy_info *info,
                          void *priv);

typedef void
osw_state_report_vif_fn_t(const struct osw_state_vif_info *info,
                          void *priv);

typedef void
osw_state_report_sta_fn_t(const struct osw_state_sta_info *info,
                          void *priv);

typedef void
osw_state_idle_fn_t(struct osw_state_observer *self);

typedef void
osw_state_busy_fn_t(struct osw_state_observer *self);

typedef void
osw_state_drv_added_fn_t(struct osw_state_observer *self,
                         struct osw_drv *drv);

typedef void
osw_state_drv_removed_fn_t(struct osw_state_observer *self,
                           struct osw_drv *drv);

typedef void
osw_state_phy_added_fn_t(struct osw_state_observer *self,
                         const struct osw_state_phy_info *phy);

typedef void
osw_state_phy_removed_fn_t(struct osw_state_observer *self,
                           const struct osw_state_phy_info *phy);

typedef void
osw_state_phy_changed_fn_t(struct osw_state_observer *self,
                           const struct osw_state_phy_info *phy);

typedef void
osw_state_vif_added_fn_t(struct osw_state_observer *self,
                         const struct osw_state_vif_info *vif);

typedef void
osw_state_vif_removed_fn_t(struct osw_state_observer *self,
                           const struct osw_state_vif_info *vif);

typedef void
osw_state_vif_changed_fn_t(struct osw_state_observer *self,
                           const struct osw_state_vif_info *vif);

typedef void
osw_state_vif_channel_changed_fn_t(struct osw_state_observer *self,
                                   const struct osw_state_vif_info *vif,
                                   const struct osw_channel *new_channel,
                                   const struct osw_channel *old_channel);

typedef void
osw_state_vif_probe_req_fn_t(struct osw_state_observer *self,
                             const struct osw_state_vif_info *vif,
                             const struct osw_drv_report_vif_probe_req *probe_req);

typedef void
osw_state_vif_csa_rx_fn_t(struct osw_state_observer *self,
                          const struct osw_state_vif_info *vif,
                          const struct osw_channel *channel);

typedef void
osw_state_vif_frame_rx_fn_t(struct osw_state_observer *self,
                            const struct osw_state_vif_info *vif,
                            const uint8_t *data,
                            size_t len);

typedef void
osw_state_vif_csa_to_phy_fn_t(struct osw_state_observer *self,
                              const struct osw_state_vif_info *vif,
                              const struct osw_state_phy_info *to_phy,
                              const struct osw_channel *channel);

typedef void
osw_state_vif_scan_completed_fn_t(struct osw_state_observer *self,
                                  const struct osw_state_vif_info *vif,
                                  const enum osw_drv_scan_complete_reason reason);

typedef void
osw_state_vif_radar_detected_fn_t(struct osw_state_observer *self,
                                  const struct osw_state_vif_info *vif,
                                  const struct osw_channel *channel);

typedef void
osw_state_sta_connected_fn_t(struct osw_state_observer *self,
                             const struct osw_state_sta_info *sta);

typedef void
osw_state_sta_disconnected_fn_t(struct osw_state_observer *self,
                                const struct osw_state_sta_info *sta);

typedef void
osw_state_sta_changed_fn_t(struct osw_state_observer *self,
                           const struct osw_state_sta_info *sta);

typedef void
osw_state_wps_success_fn_t(struct osw_state_observer *self,
                           const struct osw_state_vif_info *vif);

typedef void
osw_state_wps_overlap_fn_t(struct osw_state_observer *self,
                           const struct osw_state_vif_info *vif);

typedef void
osw_state_wps_pbc_timeout_fn_t(struct osw_state_observer *self,
                               const struct osw_state_vif_info *vif);

struct osw_state_observer {
    struct ds_dlist_node node;
    const char *name;
    osw_state_idle_fn_t *idle_fn;
    osw_state_busy_fn_t *busy_fn;
    osw_state_drv_added_fn_t *drv_added_fn;
    osw_state_drv_removed_fn_t *drv_removed_fn;
    osw_state_phy_added_fn_t *phy_added_fn;
    osw_state_phy_removed_fn_t *phy_removed_fn;
    osw_state_phy_changed_fn_t *phy_changed_fn;
    osw_state_vif_added_fn_t *vif_added_fn;
    osw_state_vif_removed_fn_t *vif_removed_fn;
    osw_state_vif_changed_fn_t *vif_changed_fn;
    osw_state_vif_channel_changed_fn_t *vif_channel_changed_fn;
    osw_state_vif_probe_req_fn_t *vif_probe_req_fn;
    osw_state_vif_csa_rx_fn_t *vif_csa_rx_fn;
    osw_state_vif_frame_rx_fn_t *vif_frame_rx_fn;
    osw_state_vif_csa_to_phy_fn_t *vif_csa_to_phy_fn;
    osw_state_vif_scan_completed_fn_t *vif_scan_completed_fn;
    osw_state_vif_radar_detected_fn_t *vif_radar_detected_fn;
    osw_state_sta_connected_fn_t *sta_connected_fn;
    osw_state_sta_disconnected_fn_t *sta_disconnected_fn;
    osw_state_sta_changed_fn_t *sta_changed_fn;
    osw_state_wps_success_fn_t *wps_success_fn;
    osw_state_wps_overlap_fn_t *wps_overlap_fn;
    osw_state_wps_pbc_timeout_fn_t *wps_pbc_timeout_fn;
};

void
osw_state_register_observer(struct osw_state_observer *observer);

void
osw_state_unregister_observer(struct osw_state_observer *observer);

const struct osw_state_phy_info *
osw_state_phy_lookup(const char *phy_name);

const struct osw_state_vif_info *
osw_state_vif_lookup(const char *phy_name,
                     const char *vif_name);

const struct osw_state_vif_info *
osw_state_vif_lookup_by_mac_addr(const struct osw_hwaddr *mac_addr);

const struct osw_state_vif_info *
osw_state_vif_lookup_by_vif_name(const char *vif_name);

const struct osw_state_sta_info *
osw_state_sta_lookup(const char *phy_name,
                     const char *vif_name,
                     const struct osw_hwaddr *mac_addr);

const struct osw_state_sta_info *
osw_state_sta_lookup_newest(const struct osw_hwaddr *mac_addr);

void
osw_state_phy_get_list(osw_state_report_phy_fn_t fn,
                       void *priv);

void
osw_state_vif_get_list(osw_state_report_vif_fn_t fn,
                       const char *phy_name,
                       void *priv);

void
osw_state_sta_get_list(osw_state_report_sta_fn_t fn,
                       const char *phy_name,
                       const char *vif_name,
                       void *priv);

int
osw_state_get_max_2g_chan_phy(const struct osw_drv_phy_state *phy);

int
osw_state_get_max_2g_chan_phy_name(const char *phy_name);


#define OSW_STATE_DEFINE(obs) \
    static void obs##module_start(void *data) { osw_state_register_observer(&obj); } \
    static void obs##module_stop(void *data) { osw_state_unregister_observer(&obj); } \
    MODULE(obs##module, obs##module_start, obs##module_stop)

#endif /* OSW_STATE_H_INCLUDED */
