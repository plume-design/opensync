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

#include <stdlib.h>
#include <log.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_module.h>

/* Purpose:
 *
 * This module provides logic that mutates the
 * osw_conf/osw_confsync so that if there's an active
 * OSW_VIF_STA link then any OSW_VIF_AP channel
 * configurations will be overriden with the root AP
 * operational channel.
 *
 * Rationale:
 *
 * This is currently desired (and implied in the design of
 * the entire system). If desired AP channels are different
 * than where the STA link currently resides on, then its
 * better to stick to the STA's channel. Otherwise, moving
 * away will sever the connecting to STA's root AP.
 * Consequently this will break internet connectivity and
 * cloud connection. Extender will be essentially orphaned
 * and will enter recovery procedures.
 *
 * Observable effect:
 *
 * The module will take effect whenever underlying data
 * model (eg. ovsdb) doesn't get updated with a new channel
 * while the sta link is on another channel that what the
 * data model wants.
 *
 * Another case is when CSA is in progress. As soon as CSA
 * intention gets processed by the driver, and somehow data
 * model gets updated fast enough, then data model would be
 * pointing to a future state of the STA interface (that is
 * still on old channel, waiting for CSA countdown to
 * finish).
 *
 * Future:
 *
 * Channel stickiness should be an explicitly configured
 * hint per PHY (this still assumes single-channel radios
 * and 1xSTA + NxAP). Possible options would be:
 * inherit_from_sta and inherit_from_ap.
 */

static void
find_vsta_cb(const struct osw_state_vif_info *info,
            void *priv)
{
    const struct osw_drv_vif_state *state = info->drv_state;
    const struct osw_drv_vif_state_sta *vsta = &state->u.sta;
    const struct osw_state_vif_info **ret = priv;

    if (state->vif_type != OSW_VIF_STA) return;
    if (state->enabled == false) return;
    if (vsta->link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return;

    WARN_ON(*ret != NULL);
    *ret = info;
}

static void
ow_sta_channel_override_mutate_cb(struct osw_conf_mutator *mutator,
                                  struct ds_tree *phy_tree)
{
    struct osw_conf_phy *phy;

    ds_tree_foreach(phy_tree, phy) {
        const struct osw_state_vif_info *vsta = NULL;
        const char *phy_name = phy->phy_name;

        osw_state_vif_get_list(find_vsta_cb, phy_name, &vsta);
        if (vsta == NULL) continue;

        struct osw_conf_vif *vif;
        ds_tree_foreach(&phy->vif_tree, vif) {
            struct osw_conf_vif_ap *vap = &vif->u.ap;
            struct osw_channel *ap_chan = &vap->channel;
            const char *vif_name = vif->vif_name;
            const char *vsta_name = vsta->vif_name;
            const struct osw_channel *vsta_chan = &vsta->drv_state->u.sta.link.channel;
            const size_t size = sizeof(*ap_chan);
            const bool same_chan = (memcmp(ap_chan, vsta_chan, size) == 0);

            if (vif->vif_type != OSW_VIF_AP) continue;
            if (same_chan == true) continue;

            LOGI("ow: %s/%s: inheriting channel from %s: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
                 phy_name,
                 vif_name,
                 vsta_name,
                 OSW_CHANNEL_ARG(ap_chan),
                 OSW_CHANNEL_ARG(vsta_chan));

            *ap_chan = *vsta_chan;
        }
    }
}

OSW_MODULE(ow_sta_channel_override)
{
    OSW_MODULE_LOAD(osw_conf);
    OSW_MODULE_LOAD(osw_state);
    static struct osw_conf_mutator mut = {
        .name = __FILE__,
        .type = OSW_CONF_TAIL,
        .mutate_fn = ow_sta_channel_override_mutate_cb,
    };
    osw_conf_register_mutator(&mut);
    return NULL;
}
