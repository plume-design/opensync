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

#include <osw_module.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_thread.h>
#include <osw_confsync.h>
#include <osw_ut.h>
#include <osw_drv_common.h>
#include <osw_drv_dummy.h>
#include <osw_bss_map.h>
#include <ow_steer_bm.h>
#include "ow_conf.h"
#include <module.h>
#include <memutil.h>
#include <ds_tree.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <os.h>
#include <ev.h>
#include <ovsdb.h>

#define OW_STEER_LOCAL_NEIGH_DEFAULT_BSSID_INFO 0x0000008f

#define LOG_PREFIX(fmt, ...) \
    "ow: steer: local_neigh: " fmt, ##__VA_ARGS__

#define LOG_VIF_PREFIX(vif_name, fmt, ...) \
    LOG_PREFIX(                            \
        "%s: " fmt,                        \
        vif_name,                          \
        ##__VA_ARGS__)

#define LOG_GROUP_PREFIX(group_id, fmt, ...) \
    LOG_PREFIX(                              \
        OVSDB_SHORT_UUID_FMT": " fmt,        \
        OVSDB_SHORT_UUID_ARG(group_id),      \
        ##__VA_ARGS__)

#define LOG_VIF_GROUP_PREFIX(vif_name, group_id, fmt, ...) \
    LOG_VIF_PREFIX(vif_name,                               \
        OVSDB_SHORT_UUID_FMT": " fmt,                      \
        OVSDB_SHORT_UUID_ARG(group_id),                    \
        ##__VA_ARGS__)


struct ow_steer_local_neigh_vif_neigh {
    struct osw_ifname vif_name;
    struct osw_neigh neigh;
    bool ft_enabled;
    int ft_mobility_domain;
    struct ds_tree_node node;
};

struct ow_steer_local_neigh_group {
    char *id;
    struct ds_tree vif_neigh_tree; /* ow_steer_local_neigh_vif_neigh */
    struct ds_tree_node node;
};

struct ow_steer_local_neigh {
    struct osw_conf_mutator conf_mutator;
    struct ow_steer_bm_observer bm_observer;
    struct ds_tree group_tree; /* ow_steer_local_neigh_group */
};

static struct ow_steer_local_neigh g_ow_steer_local_neigh;

static struct ow_steer_local_neigh_group *
ow_steer_local_neigh_lookup_group(const char *id)
{
    ASSERT(id != NULL, "");

    struct ow_steer_local_neigh_group *found_group = ds_tree_find(&g_ow_steer_local_neigh.group_tree, id);
    return found_group;
}

static struct ow_steer_local_neigh_group *
ow_steer_local_neigh_lookup_group_by_vif_name(const char *vif_name)
{
    ASSERT(vif_name != NULL, "");

    /* for every group */
    struct ow_steer_local_neigh_group *found_group = NULL;
    struct ow_steer_local_neigh_group *g;
    ds_tree_foreach(&g_ow_steer_local_neigh.group_tree, g) {

        /* find neighbor with matching vif_name */
        struct osw_ifname osw_vif_name;
        STRSCPY_WARN(osw_vif_name.buf, vif_name);
        struct ow_steer_local_neigh_vif_neigh *vif_neigh = ds_tree_find(&g->vif_neigh_tree, &osw_vif_name);

        /* neighbor found in group */
        if (vif_neigh != NULL) {
            found_group = g;
            break;
        }
    }
    return found_group;
}

static struct ow_steer_local_neigh_group *
ow_steer_local_neigh_get_group(const char *id)
{
    ASSERT(id != NULL, "");

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group(id);
    if (group != NULL)
        return group;

    group = CALLOC(1, sizeof(*group));
    group->id = STRDUP(id);
    ds_tree_init(&group->vif_neigh_tree, (ds_key_cmp_t*)osw_ifname_cmp, struct ow_steer_local_neigh_vif_neigh, node);
    ds_tree_insert(&g_ow_steer_local_neigh.group_tree, group, group->id);

    return group;
}

static bool
ow_steer_local_neigh_remove_group(const char *id)
{
    ASSERT(id != NULL, "");

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group(id);
    if (group == NULL)
        return false;

    struct ds_tree *vif_neigh_tree = &group->vif_neigh_tree;
    struct ow_steer_local_neigh_vif_neigh *n;
    while ((n = ds_tree_head(vif_neigh_tree)) != NULL) {
        ds_tree_remove(vif_neigh_tree, n);
        FREE(n);
    }

    ds_tree_remove(&g_ow_steer_local_neigh.group_tree, group);
    FREE(group->id);
    FREE(group);
    return true;
}

static struct ow_steer_local_neigh_vif_neigh *
ow_steer_local_neigh_lookup_neigh_in_group(const char *id,
                                           const char *vif_name)
{
    ASSERT(id != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group(id);
    if (group == NULL)
        return NULL;

    struct osw_ifname osw_vif_name;
    STRSCPY_WARN(osw_vif_name.buf, vif_name);
    struct ow_steer_local_neigh_vif_neigh *vif_neigh = ds_tree_find(&group->vif_neigh_tree, &osw_vif_name);
    if (vif_neigh == NULL)
        return NULL;
    return vif_neigh;
}

static struct ow_steer_local_neigh_vif_neigh *
ow_steer_local_neigh_get_neigh_in_group(const char *id,
                                        const char *vif_name)
{
    ASSERT(id != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct ow_steer_local_neigh_vif_neigh *neigh = ow_steer_local_neigh_lookup_neigh_in_group(id, vif_name);
    if (neigh != NULL)
        return neigh;

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group(id);
    if (group == NULL)
        return NULL;

    neigh = CALLOC(1, sizeof(*neigh));
    memset(neigh, 0, sizeof(*neigh));
    STRSCPY_WARN(neigh->vif_name.buf, vif_name);
    ds_tree_insert(&group->vif_neigh_tree,
                   neigh,
                   &neigh->vif_name);
    return neigh;
}

static bool
ow_steer_local_neigh_remove_neigh_in_group(const char *id,
                                           const char *vif_name)
{
    ASSERT(id != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct ow_steer_local_neigh_vif_neigh *neigh = ow_steer_local_neigh_lookup_neigh_in_group(id, vif_name);
    if (neigh == NULL)
        return false;

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group(id);
    if (group == NULL)
        return false;

    ds_tree_remove(&group->vif_neigh_tree, neigh);
    FREE(neigh);

    return true;
}

static void
ow_steer_local_neigh_conf_mutate_upsert_neigh(const struct osw_neigh *neigh,
                                              const bool ft_enabled,
                                              struct ds_tree *osw_neigh_tree)
{
    ASSERT(neigh != NULL, "");
    ASSERT(osw_neigh_tree != NULL, "");

    struct osw_conf_neigh *dest_neigh = ds_tree_find(osw_neigh_tree,
                                                     &neigh->bssid);
    if (dest_neigh != NULL)
        return;

    dest_neigh = CALLOC(1, sizeof(*dest_neigh));
    memcpy(&dest_neigh->neigh, neigh, sizeof(dest_neigh->neigh));
    dest_neigh->neigh.bssid_info &= ~0x400;
    dest_neigh->neigh.bssid_info |= ft_enabled ? 0x400 : 0;
    ds_tree_insert(osw_neigh_tree,
                   dest_neigh,
                   &dest_neigh->neigh.bssid);
}

static bool
ow_steer_local_neigh_is_neigh_complete(struct ow_steer_local_neigh_vif_neigh *local_neigh)
{
    ASSERT(local_neigh != NULL, "");

    struct osw_neigh *neigh = &local_neigh->neigh;

    struct osw_hwaddr bssid_zeros;
    MEMZERO(bssid_zeros);
    const bool bssid_empty = (memcmp(&neigh->bssid, &bssid_zeros, sizeof(neigh->bssid)) == 0);
    if (bssid_empty == true)
        return false;

    const bool bssid_info_empty = (neigh->bssid_info == 0);
    if (bssid_info_empty == true)
        return false;

    const bool op_class_empty = (neigh->op_class == 0);
    if (op_class_empty == true)
        return false;

    const bool channel_empty = (neigh->channel == 0);
    if (channel_empty == true)
        return false;
    return true;
}

static void
ow_steer_local_neigh_conf_mutate_vif(struct ow_steer_local_neigh *self,
                                     struct osw_conf_vif *osw_vif)
{
    ASSERT(self != NULL ,"");
    ASSERT(osw_vif != NULL ,"");

    const char *vif_name = osw_vif->vif_name;
    enum osw_vif_type type = osw_vif->vif_type;
    if (type != OSW_VIF_AP) {
        return;
    }

    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group_by_vif_name(vif_name);
    if (group == NULL) {
        return;
    }

    /* ensure all vifs in a group have filled in information */
    struct ow_steer_local_neigh_vif_neigh *neigh;
    ds_tree_foreach(&group->vif_neigh_tree, neigh) {
        const bool is_neigh_complete = ow_steer_local_neigh_is_neigh_complete(neigh);
        if (is_neigh_complete == false)
            return;
    }

    const bool self_ft_enabled = osw_wpa_is_ft(&osw_vif->u.ap.wpa);

    /* upsert all local vifs as neighbors */
    ds_tree_foreach(&group->vif_neigh_tree, neigh) {
        LOGD(LOG_VIF_PREFIX(vif_name, "bssid: "OSW_HWADDR_FMT": "
                                      "upserting neighbor: "
                                      "(bssid_info: %02x%02x%02x%02x, "
                                      "op_class: %u, "
                                      "channel: %u, "
                                      "phy_type: %u)",
                                      OSW_HWADDR_ARG(&neigh->neigh.bssid),
                                      (neigh->neigh.bssid_info & 0xff000000) >> 24,
                                      (neigh->neigh.bssid_info & 0x00ff0000) >> 16,
                                      (neigh->neigh.bssid_info & 0x0000ff00) >> 8,
                                      (neigh->neigh.bssid_info & 0x000000ff),
                                      neigh->neigh.op_class,
                                      neigh->neigh.channel,
                                      neigh->neigh.phy_type));

        const bool ft_enabled = self_ft_enabled
                             && neigh->ft_enabled
                             && (osw_vif->u.ap.ft_mobility_domain ==
                                 neigh->ft_mobility_domain);
        ow_steer_local_neigh_conf_mutate_upsert_neigh(&neigh->neigh,
                                                      ft_enabled,
                                                      &osw_vif->u.ap.neigh_tree);
    }
}

static void
ow_steer_local_neigh_conf_mutate_cb(struct osw_conf_mutator *mutator,
                                    struct ds_tree *phy_tree)
{
    ASSERT(mutator != NULL, "");
    ASSERT(phy_tree != NULL, "");

    struct ow_steer_local_neigh *self = container_of(mutator, struct ow_steer_local_neigh, conf_mutator);
    struct osw_conf_phy *osw_phy;
    ds_tree_foreach(phy_tree, osw_phy) {
        struct osw_conf_vif *osw_vif;
        ds_tree_foreach(&osw_phy->vif_tree, osw_vif) {
            ow_steer_local_neigh_conf_mutate_vif(self, osw_vif);
        }
    }
    LOGT(LOG_PREFIX("conf mutated"));
}

static void
ow_steer_local_neigh_bm_vif_added_cb(struct ow_steer_bm_observer *observer,
                                     struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");
    ASSERT(vif->group != NULL, "");

    struct ow_steer_bm_group *bm_group = vif->group;
    struct ow_steer_local_neigh_group *local_group = ow_steer_local_neigh_get_group(bm_group->id);
    WARN_ON(local_group == NULL);

    struct ow_steer_local_neigh_vif_neigh *local_neigh = ow_steer_local_neigh_get_neigh_in_group(local_group->id,
                                                                                                 vif->vif_name.buf);
    WARN_ON(local_neigh == NULL);
    osw_conf_invalidate(&g_ow_steer_local_neigh.conf_mutator);
}

static void
ow_steer_local_neigh_bm_vif_removed_cb(struct ow_steer_bm_observer *observer,
                                       struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");

    const char *vif_name = vif->vif_name.buf;
    struct ow_steer_local_neigh_group *group = ow_steer_local_neigh_lookup_group_by_vif_name(vif_name);
    if (group == NULL) {
        LOGW(LOG_VIF_PREFIX(vif_name, "group not found for vif,"));
        return;
    }

    const char *group_id = STRDUP(group->id);
    const bool neigh_removed = ow_steer_local_neigh_remove_neigh_in_group(group_id, vif_name);
    if (neigh_removed == true)
        osw_conf_invalidate(&g_ow_steer_local_neigh.conf_mutator);

    const bool group_is_empty = (ds_tree_len(&group->vif_neigh_tree) == 0);
    if (group_is_empty == true)
        ow_steer_local_neigh_remove_group(group_id);
    FREE(group_id);
}

static void
ow_steer_local_neigh_update_local_neigh_cache(const char *group_id,
                                              const char *vif_name,
                                              const struct osw_hwaddr *bssid,
                                              const struct osw_channel *channel,
                                              const bool ft_enabled,
                                              const int ft_mobility_domain,
                                              const uint8_t *op_class)
{
    ASSERT(group_id != NULL, "");
    ASSERT(vif_name != NULL, "");
    ASSERT(bssid != NULL, "");
    ASSERT(channel != NULL, "");
    ASSERT(op_class != NULL, "");

    struct ow_steer_local_neigh_vif_neigh *local_neigh = ow_steer_local_neigh_lookup_neigh_in_group(group_id, vif_name);
    if (local_neigh == NULL) {
        LOGW(LOG_VIF_GROUP_PREFIX(vif_name, group_id, "local neigh not found"));
        return;
    }

    const bool hwaddr_same = (osw_hwaddr_cmp(&local_neigh->neigh.bssid, bssid) == 0);
    const bool op_class_same = (local_neigh->neigh.op_class == *op_class);
    const bool ft_same = (local_neigh->ft_enabled == ft_enabled)
                      && (local_neigh->ft_mobility_domain == ft_mobility_domain);
    const bool channel_same = (local_neigh->neigh.channel == osw_freq_to_chan(channel->control_freq_mhz));
    if ((hwaddr_same == true) &&
        (op_class_same == true) &&
        (ft_same == true) &&
        (channel_same == true))
        return;

    memcpy(&local_neigh->neigh.bssid, bssid, sizeof(local_neigh->neigh.bssid));
    local_neigh->neigh.op_class = *op_class;
    local_neigh->neigh.channel = osw_freq_to_chan(channel->control_freq_mhz);
    local_neigh->neigh.bssid_info = OW_STEER_LOCAL_NEIGH_DEFAULT_BSSID_INFO; /* FIXME */
    local_neigh->neigh.phy_type = 0x00; /* FIXME */
    local_neigh->ft_enabled = ft_enabled;
    local_neigh->ft_mobility_domain = ft_mobility_domain;

    LOGI(LOG_VIF_PREFIX(vif_name, 
                    "bssid: "OSW_HWADDR_FMT": "
                    "bssid_info: %02x%02x%02x%02x: "
                    "ft_mobility_domain: %d: "
                    "op_class: %u: "
                    "channel: %u: "
                    "phy_type: %u: "
                    "updated local neighbor cache - invalidating osw_conf",
                    OSW_HWADDR_ARG(&local_neigh->neigh.bssid),
                    (local_neigh->neigh.bssid_info & 0xff000000) >> 24,
                    (local_neigh->neigh.bssid_info & 0x00ff0000) >> 16,
                    (local_neigh->neigh.bssid_info & 0x0000ff00) >> 8,
                    (local_neigh->neigh.bssid_info & 0x000000ff),
                    local_neigh->ft_enabled ? (int)local_neigh->ft_mobility_domain : -1,
                    local_neigh->neigh.op_class,
                    local_neigh->neigh.channel,
                    local_neigh->neigh.phy_type));

    osw_conf_invalidate(&g_ow_steer_local_neigh.conf_mutator);
}

static void
ow_steer_local_neigh_bm_vif_up_changed_cb(struct ow_steer_bm_observer *observer,
                                          struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");

    /* get channel and operating class*/
    const struct ow_steer_bm_bss *bss = vif->bss;

    /* no warning here, because at the moment of change bss might not be linked to vif (i.e. when building vif) */
    if (bss == NULL) return;

    const struct osw_hwaddr *bssid = &bss->bssid;
    const struct osw_channel *channel = osw_bss_get_channel(bssid);
    if (channel == NULL)
        return;
    const uint8_t *op_class = osw_bss_get_op_class(bssid);
    if (op_class == NULL)
        return;

    const int *ft_mobility_domain = osw_bss_get_ft_mobility_domain(bssid);

    /* update local cache */
    const struct ow_steer_bm_group *group = bss->group;
    if (WARN_ON(group == NULL)) return;
    const char *group_id = group->id;
    if (WARN_ON(group_id == NULL)) return;
    const char *vif_name = vif->vif_name.buf;
    ow_steer_local_neigh_update_local_neigh_cache(group_id,
                                                  vif_name,
                                                  bssid,
                                                  channel,
                                                  ft_mobility_domain ? true : false,
                                                  ft_mobility_domain ? *ft_mobility_domain : 0,
                                                  op_class);
}

static void
ow_steer_local_neigh_bm_vif_up_cb(struct ow_steer_bm_observer *observer,
                                  struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");
    ow_steer_local_neigh_bm_vif_up_changed_cb(observer, vif);
}

static void
ow_steer_local_neigh_bm_vif_changed_cb(struct ow_steer_bm_observer *observer,
                                       struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");
    ow_steer_local_neigh_bm_vif_up_changed_cb(observer, vif);
}

static void
ow_steer_local_neigh_bm_vif_down_cb(struct ow_steer_bm_observer *observer,
                                    struct ow_steer_bm_vif *vif)
{
    ASSERT(observer != NULL, "");
    ASSERT(vif != NULL, "");

    const char *vif_name = vif->vif_name.buf;
    const struct ow_steer_bm_bss *bss = vif->bss;
    if (WARN_ON(bss == NULL)) return;
    const struct ow_steer_bm_group *group = bss->group;
    if (WARN_ON(group == NULL)) return;
    const char *group_id = group->id;
    if (WARN_ON(group_id == NULL)) return;

    struct ow_steer_local_neigh_vif_neigh *local_neigh = ow_steer_local_neigh_lookup_neigh_in_group(group_id, vif_name);
    if (local_neigh == NULL) {
        LOGW(LOG_VIF_GROUP_PREFIX(vif_name, group_id, "local neigh not found"));
        return;
    }

    MEMZERO(local_neigh->neigh);
    osw_conf_invalidate(&g_ow_steer_local_neigh.conf_mutator);
}

static struct ow_steer_local_neigh g_ow_steer_local_neigh = {
    .conf_mutator = {
        .name = "ow_steer_local_neigh",
        .type = OSW_CONF_TAIL,
        .mutate_fn = ow_steer_local_neigh_conf_mutate_cb,
    },
    .bm_observer = {
        .vif_added_fn = ow_steer_local_neigh_bm_vif_added_cb,
        .vif_removed_fn = ow_steer_local_neigh_bm_vif_removed_cb,
        .vif_up_fn = ow_steer_local_neigh_bm_vif_up_cb,
        .vif_changed_fn = ow_steer_local_neigh_bm_vif_changed_cb,
        .vif_down_fn = ow_steer_local_neigh_bm_vif_down_cb,
    },
};

static void
ow_steer_local_neigh_init(struct ow_steer_local_neigh *self)
{
    ASSERT(self != NULL, "");

    static bool initialized;
    if (initialized == true) return;

    ds_tree_init(&self->group_tree, ds_str_cmp, struct ow_steer_local_neigh_group, node);
    osw_conf_register_mutator(&self->conf_mutator);
    ow_steer_bm_observer_register(&self->bm_observer);

    initialized = true;
}

OSW_MODULE(ow_steer_local_neigh)
{
    OSW_MODULE_LOAD(osw_conf);
    OSW_MODULE_LOAD(ow_steer_bm);
    ow_steer_local_neigh_init(&g_ow_steer_local_neigh);
    return NULL;
}

#include "ow_steer_local_neigh_ut.c"
