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

#ifndef OPENSYNC_HAPD_H_INCLUDED
#define OPENSYNC_HAPD_H_INCLUDED

#include "schema.h"

struct hapd {
    char phy[IFNAMSIZ];
    char driver[32]; /* eg. nl80211, wext, ... */
    char pskspath[PATH_MAX];
    char confpath[PATH_MAX];
    char rxkhpath[PATH_MAX];
    char conf[4096];
    char psks[4096];
    char rxkh[4096];
    char country[3];
    int respect_multi_ap;
    int skip_probe_response;
    int ieee80211n;
    int ieee80211ac;
    int ieee80211ax;
    int ieee80211be;
    char htcaps[256];
    char vhtcaps[512];
    void (*sta_connected)(struct hapd *hapd, const char *mac, const char *keyid);
    void (*sta_disconnected)(struct hapd *hapd, const char *mac);
    void (*dfs_event_cac_start)(struct hapd *hapd, const char *event);
    void (*dfs_event_cac_completed)(struct hapd *hapd, const char *event);
    void (*dfs_event_radar_detected)(struct hapd *hapd, const char *event);
    void (*dfs_event_pre_cac_expired)(struct hapd *hapd, const char *event);
    void (*dfs_event_nop_finished)(struct hapd *hapd, const char *event);
    void (*ap_csa_finished)(struct hapd *hapd, const char *event);
    void (*ap_enabled)(struct hapd *hapd);
    void (*ap_disabled)(struct hapd *hapd);
    void (*wps_active)(struct hapd *hapd);
    void (*wps_success)(struct hapd *hapd);
    void (*wps_timeout)(struct hapd *hapd);
    void (*wps_disable)(struct hapd *hapd);
    void (*wpa_key_mismatch)(struct hapd *hapd, const char *mac);
    struct ctrl ctrl;
    bool legacy_controller;
    bool group_by_phy_name;
    bool use_driver_iface_addr;
    bool use_driver_rrb_lo;
    bool use_reload_rxkhs;
    bool use_rxkh_file;
};

struct hapd *hapd_lookup(const char *bss);
struct hapd *hapd_new(const char *phy, const char *bss);
void hapd_lookup_radius(struct hapd *hapd,
                        struct schema_RADIUS *radius_list,
                        int max_radius_num,
                        int *num_radius_list);
void hapd_lookup_nbors(struct hapd *hapd,
                       struct schema_Wifi_VIF_Neighbors *nbors_list,
                       int max_nbors_num,
                       int *num_nbors_list);
void hapd_destroy(struct hapd *hapd);
void hapd_release(struct hapd *hapd);
int hapd_conf_gen(struct hapd *hapd,
                  const struct schema_Wifi_Radio_Config *rconf,
                  const struct schema_Wifi_VIF_Config *vconf);
int hapd_conf_gen2(struct hapd *hapd,
                   const struct schema_Wifi_Radio_Config *rconf,
                   const struct schema_Wifi_VIF_Config *vconf,
                   const struct schema_Wifi_VIF_Neighbors *nbors,
                   const struct schema_RADIUS *radius_list,
                   const int num_nbors_list,
                   const int num_radius_list,
                   const char *bssid);
int hapd_conf_apply(struct hapd *hapd);
int hapd_bss_get(struct hapd *hapd,
                 struct schema_Wifi_VIF_State *vstate);
int hapd_sta_get(struct hapd *hapd,
                 const char *mac,
                 struct schema_Wifi_Associated_Clients *client);
int hapd_sta_deauth(struct hapd *hapd, const char *mac);
void hapd_sta_iter(struct hapd *hapd,
                   void (*cb)(struct hapd *hapd, const char *mac, void *data),
                   void *data);
int hapd_wps_activate(struct hapd *hapd);
int hapd_wps_cancel(struct hapd *hapd);
void hapd_dpp_set_target_akm(struct target_dpp_conf_network *conf);
int hapd_each(int (*iter)(struct ctrl *ctrl, void *ptr), void *ptr);

#endif /* OPENSYNC_WPAS_H_INCLUDED */
