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

#include <log.h>
#include <util.h>
#include <osw_module.h>
#include <osw_state.h>
#include <osw_timer.h>
#include <osw_time.h>
#include <osw_phy_chan_observer.h>

/*
 * PHY Channel Observer
 *
 * Purpose:
 *
 * This module can be used to observe and deliver the information about the
 * currently operating channel.
 *
 * Description:
 *
 * It's achieved by using osw_state_observer per PHY. When a PHY is set to be
 * observed, struct osw_phy_chan_observer is constructed. It catches the events
 * on all the PHYs, but reacts only to those on the observed PHY.
 * The operating channel of the PHY is an AND operation on all the VIFs
 * belonging to this PHY. When it's not unified, the reported channel is
 * osw_channel_none.
 */

struct osw_phy_chan_observer
{
    char *phy_name;
    osw_phy_chan_changed_fn_t *callback;
    void *priv;
    struct osw_timer debounce;
    struct osw_channel reported_channel;
    struct osw_state_observer state_obs;
    ds_tree_t vif_tree;
};

struct osw_phy_chan_observer_vif
{
    ds_tree_node_t node;
    char *vif_name;
    bool channel_is_valid;
    struct osw_channel operating_channel;
};

#define state_obs_to_obs(obs_) container_of(obs_, struct osw_phy_chan_observer, state_obs)

#define LOG_PREFIX(fmt, ...)               "osw: phy_chan_observer: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_PHY(phy, fmt, ...)      LOG_PREFIX("%s: " fmt, phy, ##__VA_ARGS__)
#define LOG_PREFIX_VIF(phy, vif, fmt, ...) LOG_PREFIX_PHY(phy, "%s: " fmt, vif, ##__VA_ARGS__)

static const struct osw_channel *osw_phy_chan_observer_calculate_channel(struct ds_tree *vif_tree)
{
    if (vif_tree == NULL) return osw_channel_none();
    struct osw_phy_chan_observer_vif *vif;
    const struct osw_channel *common_channel = NULL;
    ds_tree_foreach (vif_tree, vif)
    {
        if (!vif->channel_is_valid) continue;
        if (common_channel == NULL) common_channel = &vif->operating_channel;
        if (!osw_channel_is_equal(common_channel, &vif->operating_channel))
        {
            common_channel = osw_channel_none();
            break;
        }
    }
    return common_channel;
}

static void osw_phy_chan_observer_notify(struct osw_timer *debounce)
{
    struct osw_phy_chan_observer *obs = container_of(debounce, struct osw_phy_chan_observer, debounce);
    const struct osw_channel *current_channel = osw_phy_chan_observer_calculate_channel(&obs->vif_tree);

    if (!osw_channel_is_equal(&obs->reported_channel, current_channel))
    {
        obs->reported_channel = current_channel ? *current_channel : *osw_channel_none();
        if (obs->callback != NULL) obs->callback(obs->priv, current_channel);
    }
}

static void osw_phy_chan_observer_check_consistency(struct osw_state_observer *obs)
{
    if (WARN_ON(obs == NULL)) return;
    struct osw_phy_chan_observer *phy_obs = state_obs_to_obs(obs);
    struct ds_tree *vif_tree = &phy_obs->vif_tree;
    const struct osw_channel *phy_channel = osw_phy_chan_observer_calculate_channel(vif_tree);

    if (osw_channel_is_none(phy_channel) || phy_channel == NULL)
    {
        if (!osw_timer_is_armed(&phy_obs->debounce))
        {
            const uint64_t nsec = OSW_TIME_SEC(5);
            const uint64_t now = osw_time_mono_clk();
            osw_timer_arm_at_nsec(&phy_obs->debounce, now + nsec);
        }
    }
    else
    {
        osw_timer_disarm(&phy_obs->debounce);
        osw_phy_chan_observer_notify(&phy_obs->debounce);
    }
}

static void osw_phy_chan_observer_vif_set_channel(
        struct osw_phy_chan_observer_vif *vif,
        const struct osw_state_vif_info *vif_info)
{
    if (WARN_ON(vif_info == NULL)) return;
    if (WARN_ON(vif == NULL)) return;

    vif->channel_is_valid = (vif_info->drv_state->status == OSW_VIF_ENABLED);
    switch (vif_info->drv_state->vif_type)
    {
        case OSW_VIF_AP:
            vif->operating_channel = vif_info->drv_state->u.ap.channel;
            break;
        case OSW_VIF_STA:
            if (vif_info->drv_state->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED)
                vif->operating_channel = vif_info->drv_state->u.sta.link.channel;
            else
                vif->channel_is_valid = false;
            break;
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_UNDEFINED:
            vif->channel_is_valid = false;
            vif->operating_channel = *osw_channel_none();
            break;
    }
}

static void osw_phy_chan_observer_vif_added_cb(
        struct osw_state_observer *obs,
        const struct osw_state_vif_info *vif_info)
{
    if (WARN_ON(vif_info == NULL)) return;
    struct osw_phy_chan_observer *phy_obs = state_obs_to_obs(obs);
    int different_phy_names = strcmp(phy_obs->phy_name, vif_info->phy->phy_name) != 0;
    if (different_phy_names) return;

    const char *vif_name = vif_info->vif_name;
    struct ds_tree *vif_tree = &phy_obs->vif_tree;
    struct osw_phy_chan_observer_vif *already_inserted = ds_tree_find(vif_tree, vif_name);
    if (WARN_ON(already_inserted != NULL)) return;

    struct osw_phy_chan_observer_vif *vif;
    vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);

    osw_phy_chan_observer_vif_set_channel(vif, vif_info);
    ds_tree_insert(vif_tree, vif, vif->vif_name);
    osw_phy_chan_observer_check_consistency(obs);
}

static void osw_phy_chan_observer_vif_changed_cb(
        struct osw_state_observer *obs,
        const struct osw_state_vif_info *vif_info)
{
    if (WARN_ON(vif_info == NULL)) return;
    struct osw_phy_chan_observer *phy_obs = state_obs_to_obs(obs);
    int different_phy_names = strcmp(vif_info->phy->phy_name, phy_obs->phy_name) != 0;
    if (different_phy_names) return;

    struct ds_tree *vif_tree = &phy_obs->vif_tree;
    struct osw_phy_chan_observer_vif *vif;
    vif = ds_tree_find(vif_tree, vif_info->vif_name);

    if (WARN_ON(vif == NULL)) return;

    osw_phy_chan_observer_vif_set_channel(vif, vif_info);
    osw_phy_chan_observer_check_consistency(obs);
}

static void osw_phy_chan_observer_vif_removed_cb(
        struct osw_state_observer *obs,
        const struct osw_state_vif_info *vif_info)
{
    if (WARN_ON(vif_info == NULL)) return;
    struct osw_phy_chan_observer *phy_obs = state_obs_to_obs(obs);
    int different_phy_names = strcmp(vif_info->phy->phy_name, phy_obs->phy_name) != 0;
    if (different_phy_names) return;

    struct ds_tree *vif_tree = &phy_obs->vif_tree;
    struct osw_phy_chan_observer_vif *vif;
    vif = ds_tree_find(vif_tree, vif_info->vif_name);
    if (WARN_ON(vif == NULL)) return;

    FREE(vif->vif_name);
    ds_tree_remove(vif_tree, vif);
    FREE(vif);
    osw_phy_chan_observer_check_consistency(obs);
}

struct osw_phy_chan_observer *osw_phy_chan_observer_setup(
        const char *phy_name,
        osw_phy_chan_changed_fn_t *func,
        void *priv)
{
    struct osw_phy_chan_observer *obs;
    obs = CALLOC(1, sizeof(*obs));
    obs->phy_name = STRDUP(phy_name);
    obs->callback = func;
    obs->priv = priv;
    obs->reported_channel = *osw_channel_none();

    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .vif_added_fn = osw_phy_chan_observer_vif_added_cb,
        .vif_changed_fn = osw_phy_chan_observer_vif_changed_cb,
        .vif_removed_fn = osw_phy_chan_observer_vif_removed_cb,
    };
    obs->state_obs = state_obs;

    osw_timer_init(&obs->debounce, osw_phy_chan_observer_notify);
    ds_tree_init(&obs->vif_tree, ds_str_cmp, struct osw_phy_chan_observer_vif, node);
    osw_state_register_observer(&obs->state_obs);
    return obs;
}

void osw_phy_chan_observer_dismantle(struct osw_phy_chan_observer *obs)
{
    if (obs == NULL) return;
    FREE(obs->phy_name);
    osw_state_unregister_observer(&obs->state_obs);
    bool tree_not_empty = !ds_tree_is_empty(&obs->vif_tree);
    WARN_ON(tree_not_empty);
    FREE(obs);
}

OSW_MODULE(osw_phy_chan_observer)
{
    return NULL;
}
