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

#ifndef OSW_DRV_I_H_INCLUDED
#define OSW_DRV_I_H_INCLUDED

#include <ds_dlist.h>
#include <ds_tree.h>
#include <ev.h>
#include <osw_timer.h>
#include <osw_drv.h>
#include "osw_state_i.h"

enum osw_drv_obj_state {
    OSW_DRV_OBJ_INVALID,
    OSW_DRV_OBJ_REQUESTED,
    OSW_DRV_OBJ_VALID,
    OSW_DRV_OBJ_PROCESSED,
};

enum osw_drv_frame_tx_desc_state {
    OSW_DRV_FRAME_TX_STATE_UNUSED = 0,
    OSW_DRV_FRAME_TX_STATE_PENDING,
    OSW_DRV_FRAME_TX_STATE_SUBMITTED,
    OSW_DRV_FRAME_TX_STATE_FAILED,
};

struct osw_drv_obj {
    enum osw_drv_obj_state state;
    bool existed;
    bool exists;
};

struct osw_drv_buf {
    void *data;
    size_t len;
};

struct osw_drv {
    const struct osw_drv_ops *ops;
    void *priv;

    bool initialized;
    bool unregistered;

    struct ds_tree_node node;
    struct ds_tree phy_tree;
    struct ds_dlist frame_tx_list;
    ev_async work_async;
    bool phy_list_valid; // FIXME
};

struct osw_drv_phy {
    char *phy_name;
    struct osw_drv *drv;
    struct ds_tree_node node;
    struct ds_tree vif_tree;
    struct osw_state_phy_info pub; // FIXME
    struct osw_drv_obj obj;
    struct osw_drv_phy_state cur_state;
    struct osw_drv_phy_state new_state;
    struct osw_drv_phy_capab capab;
    bool vif_list_valid;
    ev_timer cac_sync;
    ev_timer nol_sync;
};

struct osw_drv_vif {
    char *vif_name;
    struct osw_drv_phy *phy;
    struct ds_tree_node node;
    struct ds_tree sta_tree;
    struct osw_state_vif_info pub; // FIXME
    struct osw_drv_obj obj;
    struct osw_drv_vif_state cur_state;
    struct osw_drv_vif_state new_state;
    struct osw_channel csa_channel;
    struct osw_ssid ssid;
    struct osw_hwaddr bssid;
    const struct osw_hwaddr *vsta_root_ap;
    bool connected;
    bool sta_list_valid;
    ev_timer chan_sync; /* used for CSA state invalidation */
};

struct osw_drv_sta {
    struct osw_hwaddr mac_addr;
    struct osw_drv_vif *vif;
    struct ds_tree_node node;
    struct osw_state_sta_info pub; // FIXME
    struct osw_drv_obj obj;
    struct osw_drv_sta_state cur_state;
    struct osw_drv_sta_state new_state;
    struct osw_drv_buf cur_ies;
    struct osw_drv_buf new_ies;
};

struct osw_drv_frame_tx_desc {
    struct osw_ifname phy_name;
    struct osw_ifname vif_name;
    osw_drv_frame_tx_result_fn_t *result_fn;
    uint8_t *frame;
    size_t frame_len;
    enum osw_drv_frame_tx_desc_state state;
    void *caller_priv;
    struct ds_dlist *list;
    struct osw_channel channel;

    struct ds_dlist_node node;
};

extern struct ds_tree g_osw_drv_tree;

void
osw_drv_work_all_schedule(void);

bool
osw_drv_work_is_settled(void);

void
osw_drv_unregister_all(void);

void
osw_drv_invalidate(struct osw_drv *drv);

void
osw_drv_set_chan_sync(struct osw_drv *drv, const struct osw_drv_conf *conf);

bool
osw_drv_conf_changed(const struct osw_drv_conf *drv_conf);

void
osw_drv_frame_tx_desc_free(struct osw_drv_frame_tx_desc *desc);

#endif /* OSW_DRV_I_H_INCLUDED */
