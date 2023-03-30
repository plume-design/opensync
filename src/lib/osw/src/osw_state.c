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

#include <ev.h>
#include <const.h>
#include <os.h>
#include <memutil.h>
#include <osw_state.h>
#include <osw_module.h>
#include "osw_state_i.h"
#include "osw_drv_i.h"

struct ds_dlist g_osw_state_observer_list = DS_DLIST_INIT(struct osw_state_observer, node);

#define osw_log_state_observer_register(o) \
    LOGD("osw: state: registering observer: name=%s", o->name)
#define osw_log_state_observer_unregister(o) \
    LOGD("osw: state: unregistering observer: name=%s", o->name)

static void
osw_state_observer_notify_add(struct osw_state_observer *observer)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        if (observer->drv_added_fn != NULL)
            observer->drv_added_fn(observer, drv);

        ds_tree_foreach(&drv->phy_tree, phy) {
            if (phy->cur_state.exists == false)
                continue;

            if (observer->phy_added_fn != NULL)
                observer->phy_added_fn(observer, &phy->pub);

            ds_tree_foreach(&phy->vif_tree, vif) {
                if (vif->cur_state.exists == false)
                    continue;

                if (observer->vif_added_fn != NULL)
                    observer->vif_added_fn(observer, &vif->pub);

                ds_tree_foreach(&vif->sta_tree, sta) {
                    if (sta->cur_state.connected == false)
                        continue;

                    if (observer->sta_connected_fn != NULL)
                        observer->sta_connected_fn(observer, &sta->pub);
                }
            }
        }
    }
}

static void
osw_state_observer_notify_remove(struct osw_state_observer *observer)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        ds_tree_foreach(&drv->phy_tree, phy) {
            if (phy->cur_state.exists == false)
                continue;

            ds_tree_foreach(&phy->vif_tree, vif) {
                if (vif->cur_state.exists == false)
                    continue;

                ds_tree_foreach(&vif->sta_tree, sta) {
                    if (sta->cur_state.connected == false)
                        continue;

                    if (observer->sta_disconnected_fn != NULL)
                        observer->sta_disconnected_fn(observer, &sta->pub);
                }
                if (observer->vif_removed_fn != NULL)
                    observer->vif_removed_fn(observer, &vif->pub);
            }
            if (observer->phy_removed_fn != NULL)
                observer->phy_removed_fn(observer, &phy->pub);
        }
        if (observer->drv_removed_fn != NULL)
            observer->drv_removed_fn(observer, drv);
    }
}

static void
osw_state_observer_notify_settled(struct osw_state_observer *observer)
{
    bool is_settled = osw_drv_work_is_settled();

    if (is_settled == true && observer->idle_fn != NULL)
        observer->idle_fn(observer);

    if (is_settled == false && observer->busy_fn != NULL)
        observer->busy_fn(observer);
}

void
osw_state_register_observer(struct osw_state_observer *observer)
{
    osw_log_state_observer_register(observer);
    ds_dlist_insert_tail(&g_osw_state_observer_list, observer);
    osw_state_observer_notify_add(observer);
    osw_state_observer_notify_settled(observer);
}

void
osw_state_unregister_observer(struct osw_state_observer *observer)
{
    osw_log_state_observer_unregister(observer);
    ds_dlist_remove(&g_osw_state_observer_list, observer);
    osw_state_observer_notify_remove(observer);
}

const struct osw_state_phy_info *
osw_state_phy_lookup(const char *phy_name)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if ((phy = ds_tree_find(&drv->phy_tree, phy_name)) != NULL)
            if (phy->cur_state.exists == true)
                return &phy->pub;

    return NULL;
}

const struct osw_state_vif_info *
osw_state_vif_lookup(const char *phy_name,
                     const char *vif_name)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if ((phy = ds_tree_find(&drv->phy_tree, phy_name)) != NULL)
            if (phy->cur_state.exists == true)
                if ((vif = ds_tree_find(&phy->vif_tree, vif_name)) != NULL)
                    if (vif->cur_state.exists == true)
                        return &vif->pub;

    return NULL;
}

const struct osw_state_vif_info *
osw_state_vif_lookup_by_mac_addr(const struct osw_hwaddr *mac_addr)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    /* TODO This may be optimized by introducing global (mac addr, vif) tree. */
    ds_tree_foreach(&g_osw_drv_tree, drv)
        ds_tree_foreach(&drv->phy_tree, phy)
            if (phy->cur_state.exists == true)
                ds_tree_foreach(&phy->vif_tree, vif)
                    if (vif->cur_state.exists == true)
                        if (osw_hwaddr_cmp(&vif->cur_state.mac_addr, mac_addr) == 0)
                            return &vif->pub;

    return NULL;
}

const struct osw_state_vif_info *
osw_state_vif_lookup_by_vif_name(const char *vif_name)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        ds_tree_foreach(&drv->phy_tree, phy)
            if (phy->cur_state.exists == true)
                if ((vif = ds_tree_find(&phy->vif_tree, vif_name)) != NULL)
                    if (vif->cur_state.exists == true)
                        return &vif->pub;

    return NULL;
}

const struct osw_state_sta_info *
osw_state_sta_lookup(const char *phy_name,
                     const char *vif_name,
                     const struct osw_hwaddr *mac_addr)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if ((phy = ds_tree_find(&drv->phy_tree, phy_name)) != NULL)
            if (phy->cur_state.exists == true)
                if ((vif = ds_tree_find(&phy->vif_tree, vif_name)) != NULL)
                    if (vif->cur_state.exists == true)
                        if ((sta = ds_tree_find(&vif->sta_tree, mac_addr)) != NULL)
                            if (sta->cur_state.connected == true)
                                return &sta->pub;

    return NULL;
}

const struct osw_state_sta_info *
osw_state_sta_lookup_newest(const struct osw_hwaddr *mac_addr)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;
    struct osw_drv_sta *newest = NULL;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        ds_tree_foreach(&drv->phy_tree, phy)
            if (phy->cur_state.exists == true)
                ds_tree_foreach(&phy->vif_tree, vif)
                    if (vif->cur_state.exists == true)
                        if ((sta = ds_tree_find(&vif->sta_tree, mac_addr)) != NULL)
                            if (sta->cur_state.connected == true)
                                if (newest == NULL || sta->pub.connected_at > newest->pub.connected_at)
                                    newest = sta;

    return newest != NULL ? &newest->pub : NULL;
}

void
osw_state_phy_get_list(osw_state_report_phy_fn_t fn,
                       void *priv)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        ds_tree_foreach(&drv->phy_tree, phy)
            if (phy->cur_state.exists == true)
                fn(&phy->pub, priv);
}

void
osw_state_vif_get_list(osw_state_report_vif_fn_t fn,
                       const char *phy_name,
                       void *priv)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if ((phy = ds_tree_find(&drv->phy_tree, phy_name)) != NULL)
            if (phy->cur_state.exists == true)
                ds_tree_foreach(&phy->vif_tree, vif)
                    if (vif->cur_state.exists == true)
                        fn(&vif->pub, priv);
}

static void
osw_state_sta_get_list_on_phy(osw_state_report_sta_fn_t fn,
                              struct osw_drv_phy *phy,
                              const char *vif_name,
                              void *priv)
{
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    if (phy->cur_state.exists == true)
        if ((vif = ds_tree_find(&phy->vif_tree, vif_name)) != NULL)
            if (vif->cur_state.exists == true)
                ds_tree_foreach(&vif->sta_tree, sta)
                    if (sta->cur_state.connected == true)
                        fn(&sta->pub, priv);
}

static void
osw_state_sta_get_list_on_drv(osw_state_report_sta_fn_t fn,
                              struct osw_drv *drv,
                              const char *vif_name,
                              void *priv)
{
    struct osw_drv_phy *phy;

    ds_tree_foreach(&drv->phy_tree, phy)
        osw_state_sta_get_list_on_phy(fn, phy, vif_name, priv);
}

void
osw_state_sta_get_list(osw_state_report_sta_fn_t fn,
                       const char *phy_name,
                       const char *vif_name,
                       void *priv)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        if (phy_name == NULL)
            osw_state_sta_get_list_on_drv(fn, drv, vif_name, priv);
        else if ((phy = ds_tree_find(&drv->phy_tree, phy_name)) != NULL)
            osw_state_sta_get_list_on_phy(fn, phy, vif_name, priv);
    }
}

int
osw_state_get_max_2g_chan_phy(const struct osw_drv_phy_state *phy)
{
    return osw_cs_get_max_2g_chan(phy ? phy->channel_states : NULL,
                                  phy ? phy->n_channel_states : 0);
}

int
osw_state_get_max_2g_chan_phy_name(const char *phy_name)
{
    const struct osw_state_phy_info *phy = osw_state_phy_lookup(phy_name);
    return osw_state_get_max_2g_chan_phy(phy ? phy->drv_state : NULL);
}

OSW_MODULE(osw_state)
{
    OSW_MODULE_LOAD(osw_drv);
    return NULL;
}
