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

#ifndef OSW_DRV_DUMMY_H_INCLUDED
#define OSW_DRV_DUMMY_H_INCLUDED

#include <ds_tree.h>
#include <ds_dlist.h>
#include <osw_drv_common.h>
#include <osw_drv.h>

struct osw_drv_dummy;

struct osw_drv_dummy_phy {
    struct ds_tree_node node;
    struct osw_ifname phy_name;
    struct osw_drv_phy_state state;
};

struct osw_drv_dummy_vif {
    struct ds_tree_node node;
    struct osw_ifname phy_name;
    struct osw_ifname vif_name;
    struct osw_drv_vif_state state;
};

struct osw_drv_dummy_sta {
    struct ds_dlist_node node;
    struct osw_ifname phy_name;
    struct osw_ifname vif_name;
    struct osw_hwaddr sta_addr;
    struct osw_drv_sta_state state;
    void *ies;
    size_t ies_len;
};


typedef void osw_drv_dummy_fini_phy_fn_t(struct osw_drv_dummy *dummy, struct osw_drv_phy_state *phy);
typedef void osw_drv_dummy_fini_vif_fn_t(struct osw_drv_dummy *dummy, struct osw_drv_vif_state *vif);
typedef void osw_drv_dummy_fini_sta_fn_t(struct osw_drv_dummy *dummy, struct osw_drv_sta_state *sta);
typedef void osw_drv_dummy_iter_sta_fn_t(struct osw_drv_dummy *dummy,
                                         const char *phy_name,
                                         const char *vif_name,
                                         const struct osw_hwaddr *sta_addr,
                                         void *fn_data);

struct osw_drv_dummy {
    char name[32];
    struct osw_drv_ops ops;
    struct osw_drv *drv;
    struct ds_tree phy_tree;
    struct ds_tree vif_tree;
    struct ds_dlist sta_list;
    osw_drv_dummy_fini_phy_fn_t *const fini_phy_fn;
    osw_drv_dummy_fini_vif_fn_t *const fini_vif_fn;
    osw_drv_dummy_fini_sta_fn_t *const fini_sta_fn;
    osw_drv_init_fn_t *const init_fn;
    osw_drv_request_config_fn_t *const request_config_fn;
    osw_drv_request_stats_fn_t *const request_stats_fn;
    osw_drv_request_sta_deauth_fn_t *request_sta_deauth_fn;
    osw_drv_push_frame_tx_fn_t *const push_frame_tx_fn;
};

void
osw_drv_dummy_init(struct osw_drv_dummy *dummy);

void
osw_drv_dummy_fini(struct osw_drv_dummy *dummy);

void
osw_drv_dummy_set_phy(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      struct osw_drv_phy_state *state);

void
osw_drv_dummy_set_vif(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      const char *vif_name,
                      struct osw_drv_vif_state *state);

void
osw_drv_dummy_set_sta(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      const char *vif_name,
                      const struct osw_hwaddr *sta_addr,
                      struct osw_drv_sta_state *state);

void
osw_drv_dummy_set_sta_ies(struct osw_drv_dummy *dummy,
                          const char *phy_name,
                          const char *vif_name,
                          const struct osw_hwaddr *sta_addr,
                          const void *ies,
                          size_t ies_len);

void
osw_drv_dummy_iter_sta(struct osw_drv_dummy *dummy,
                       osw_drv_dummy_iter_sta_fn_t *fn,
                       void *fn_data);


#endif /* OSW_DRV_DUMMY_H_INCLUDED */
