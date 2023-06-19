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

#include <memutil.h>
#include <log.h>
#include <util.h>
#include <const.h>
#include <osw_conf.h>
#include <osw_drv_common.h>
#include <osw_drv.h>
#include <osw_time.h>
#include <osw_module.h>
#include "osw_drv_i.h"

static inline bool
osw_mux_drv_is_ready(struct osw_drv *drv)
{
    return drv->initialized == true && drv->unregistered == false;
}

bool
osw_mux_request_config(struct osw_drv_conf *conf)
{
    struct osw_drv *drv;
    bool requested = false;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        struct osw_drv_conf *tmp = CALLOC(1, sizeof(*tmp));
        size_t i;

        for (i = 0; i < conf->n_phy_list; i++) {
            struct osw_drv_phy_config *phy = &conf->phy_list[i];
            if (phy->phy_name == NULL)
                continue;
            if (ds_tree_find(&drv->phy_tree, phy->phy_name) == NULL)
                continue;

            tmp->n_phy_list++;
            size_t s = tmp->n_phy_list * sizeof(*tmp->phy_list);
            tmp->phy_list = REALLOC(tmp->phy_list, s);
            memcpy(&tmp->phy_list[tmp->n_phy_list - 1], phy, sizeof(*phy));
            memset(phy, 0, sizeof(*phy));
            /* The memset() is important. It makes osw_drv_conf_free()
             * at return to free() only the bits that were not given
             * to any of the drivers.
             */
        }

        if (osw_drv_conf_changed(tmp) == false) {
            osw_drv_conf_free(tmp);
            continue;
        }

        osw_drv_set_chan_sync(drv, tmp);

        assert(drv->ops->request_config_fn != NULL);
        drv->ops->request_config_fn(drv, tmp);
        /* tmp is now owned by the driver. The driver
         * is expected to free it.
         */

        requested = true;
    }

    osw_drv_conf_free(conf);
    return requested;
}

static struct osw_drv_phy *
osw_mux_lookup_phy_by_name(const char *phy_name)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, phy_name);
        if (phy != NULL)
            return phy;
    }

    return NULL;
}

bool
osw_mux_request_sta_deauth(const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *mac_addr,
                           int dot11_reason_code)
{
    struct osw_drv_phy *phy = osw_mux_lookup_phy_by_name(phy_name);
    if (phy == NULL) return false;
    if (phy->drv->ops->request_sta_deauth_fn == NULL) return false;
    phy->drv->ops->request_sta_deauth_fn(phy->drv, phy_name, vif_name,
                                         mac_addr, dot11_reason_code);
    return true;
}

bool
osw_mux_request_sta_delete(const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *mac_addr)
{
    struct osw_drv_phy *phy = osw_mux_lookup_phy_by_name(phy_name);
    if (phy == NULL) return false;
    if (phy->drv->ops->request_sta_delete_fn == NULL) return false;
    phy->drv->ops->request_sta_delete_fn(phy->drv, phy_name, vif_name, mac_addr);
    return true;
}

bool
osw_mux_frame_tx_schedule(const char *phy_name,
                          const char *vif_name,
                          struct osw_drv_frame_tx_desc *desc)
{
    assert(phy_name != NULL);
    assert(desc != NULL);

    struct osw_drv_phy *phy = NULL;
    struct osw_drv_vif *vif = NULL;

    if (desc->state != OSW_DRV_FRAME_TX_STATE_UNUSED)
        return false;

    phy = osw_mux_lookup_phy_by_name(phy_name);
    if (phy == NULL)
        return false;

    if (phy->drv->ops->push_frame_tx_fn == NULL)
        return false;

    if (vif_name != NULL)
        vif = ds_tree_find(&phy->vif_tree, vif_name);

    if (vif_name != NULL && vif == NULL)
        return false;

    STRSCPY(desc->phy_name.buf, phy_name);
    if (vif_name != NULL) STRSCPY_WARN(desc->vif_name.buf, vif_name);
    desc->state = OSW_DRV_FRAME_TX_STATE_PENDING;
    desc->list = &phy->drv->frame_tx_list;
    desc->drv = phy->drv;
    LOGD(LOG_PREFIX_TX_DESC(desc, "pending"));
    ds_dlist_insert_tail(desc->list, desc);

    osw_drv_work_all_schedule();

    return true;
}

void
osw_mux_request_stats(unsigned int stats_mask)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if (osw_mux_drv_is_ready(drv) == true)
            if (drv->ops->request_stats_fn != NULL)
                drv->ops->request_stats_fn(drv, stats_mask);
}

bool
osw_mux_request_scan(const char *phy_name,
                     const char *vif_name,
                     const struct osw_drv_scan_params *params)
{
    struct osw_drv_phy *phy = osw_mux_lookup_phy_by_name(phy_name);
    if (phy == NULL) return false;

    struct ds_tree *vif_tree = &phy->vif_tree;
    struct osw_drv_vif *vif = ds_tree_find(vif_tree, vif_name);
    if (vif == NULL) return false;
    if (vif->scan_started) return false;
    if (vif->cur_state.exists == false) return false;

    struct osw_drv *drv = phy->drv;
    if (drv->ops == NULL) return false;
    if (drv->ops->request_scan_fn == NULL) return false;

    vif->scan_started = true;
    drv->ops->request_scan_fn(drv, phy_name, vif_name, params);

    return true;
}

void
osw_mux_poll(void)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        osw_drv_invalidate(drv);
    }
}

OSW_MODULE(osw_mux)
{
    return NULL;
}
