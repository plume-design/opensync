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
#include <const.h>
#include <log.h>
#include <util.h>
#include <osw_conf.h>
#include <osw_types.h>
#include <osw_module.h>
#include <osw_drv.h>
#include <osw_state.h>
#include <osw_etc.h>

/**
 * Rationale
 *
 * This serves as mitigation / workaround against buggy
 * client devices that can't deal well with Multi BSSID.
 *
 * According to some reports clients are less reliable in
 * connecting to a 6GHz AP if the transmitting interfaces is
 * the backhaul or onboard (hidden) network. 6GHz is
 * required to use Multi BSSID by the 802.11 spec.
 *
 *
 * What it does
 *
 * It applies a mutation somewhere at the end of osw_conf
 * mutation chain making sure that the configuration is
 * pointing to a non-hidden AP VIF if Multi BSSID is
 * detected to be operational.
 *
 *
 * When does it typically work
 *
 * This is expected to be having effects only on 6GHz PHYs
 * and the actual impact (where it'll override anything
 * actively) is during onboarding / initial AP setup, or
 * when fronthauls are reconfigured.
 *
 * The former will happen because the order in which APs
 * tend to be brought up in Opensync starts off with hidden
 * ones.
 *
 * The latter happens when the network administrator / user
 * reconfigured some parameters like WPA2/WPA3, Captive
 * Portal, etc that impact on the arrangement of non-hidden
 * APs.
 *
 * This module is expected to have no changes during most
 * osw_conf mutation rounds.
 *
 *
 * Can it be disabled?
 *
 * If the OW_MBSS_PREFER_NONHIDDEN_DISABLE environmental
 * variable is set (to any value), this module will never
 * mutate any configurations.
 *
 *
 * Can it be dry-run for testing?
 *
 * If the OW_MBSS_PREFER_NONHIDDEN_DRYRUN environmental
 * variable is set (to any value), this module will perform
 * it's calculations, but will never modify the resulting
 * configuration in mutation chain. This can be used to see
 * if the logic is sound step-by-step.
 */

struct ow_mbss_prefer_nonhidden {
    bool enabled;
    bool dryrun;
    struct osw_conf_mutator mut;
};

#define OW_MBSS_PREFER_NONHIDDEN_ENABLED_DEFAULT true
#define OW_MBSS_PREFER_NONHIDDEN_DRYRUN_DEFAULT false
#define LOG_PREFIX(fmt, ...) "ow: mbss_prefer_nonhidden: " fmt, ## __VA_ARGS__
#define LOG_PREFIX_PHY(phy_name, fmt, ...) LOG_PREFIX("%s: " fmt, phy_name, ## __VA_ARGS__)
#define mut_to_m(x) container_of(x, struct ow_mbss_prefer_nonhidden, mut)

static struct osw_conf_vif *
ow_mbss_prefer_nonhidden_first_ap_in_group(struct ds_tree *vif_tree,
                                           bool is_ssid_hidden,
                                           int group_id)
{
    struct osw_conf_vif *vif;
    ds_tree_foreach(vif_tree, vif) {
        const struct osw_state_vif_info *info = osw_state_vif_lookup(vif->phy->phy_name, vif->vif_name);
        const struct osw_drv_vif_state *vif_state = info->drv_state;
        if (vif->enabled &&
            vif_state->vif_type == OSW_VIF_AP &&
            vif_state->u.ap.mbss_group == group_id &&
            vif->vif_type == OSW_VIF_AP &&
            vif->u.ap.ssid_hidden == is_ssid_hidden) {

            return vif;
        }
    }
    return NULL;
}

static enum osw_mbss_vif_ap_mode
ow_mbss_prefer_nonhidden_get_mbss_mode(struct ds_tree *vif_tree,
                                       struct osw_conf_vif *vif,
                                       const struct osw_drv_vif_state_ap *ap_state)
{
    int group_id = ap_state->mbss_group;
    if (vif->vif_type != OSW_VIF_AP || vif->enabled == false) return OSW_MBSS_NONE;

    struct osw_conf_vif *first_non_hidden = ow_mbss_prefer_nonhidden_first_ap_in_group(vif_tree, false, group_id);
    struct osw_conf_vif *first_hidden = ow_mbss_prefer_nonhidden_first_ap_in_group(vif_tree, true, group_id);
    struct osw_conf_vif *tx_vif = first_non_hidden ?: first_hidden;
    if (tx_vif == NULL) return OSW_MBSS_NONE;

    const bool tx_vif_is_equal = (strcmp(vif->vif_name, tx_vif->vif_name) == 0);
    if (tx_vif_is_equal) return OSW_MBSS_TX_VAP;

    return OSW_MBSS_NON_TX_VAP;
}

static void
ow_mbss_prefer_nonhidden_mutate_cb(struct osw_conf_mutator *mut,
                                   struct ds_tree *phy_tree)
{
    struct ow_mbss_prefer_nonhidden *m = mut_to_m(mut);
    const bool is_disabled = (m->enabled == false);
    if (is_disabled) return;

    struct osw_conf_phy *phy;
    ds_tree_foreach(phy_tree, phy) {
        struct ds_tree *vif_tree = &phy->vif_tree;
        struct osw_conf_vif *vif;
        ds_tree_foreach(vif_tree, vif) {
            const struct osw_state_vif_info *info = osw_state_vif_lookup(phy->phy_name, vif->vif_name);
            const struct osw_drv_vif_state *vif_state = info->drv_state;
            if (vif_state->vif_type != OSW_VIF_AP) continue;
            const struct osw_drv_vif_state_ap *ap_state = &vif_state->u.ap;
            const enum osw_mbss_vif_ap_mode new_mbss_mode = ow_mbss_prefer_nonhidden_get_mbss_mode(vif_tree, vif, ap_state);
            if (ap_state->mbss_mode == OSW_MBSS_NONE) continue;
            vif->u.ap.mbss_mode = new_mbss_mode;
        }
    }
}

static void
ow_mbss_prefer_nonhidden_init(struct ow_mbss_prefer_nonhidden *m)
{
    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .mutate_fn = ow_mbss_prefer_nonhidden_mutate_cb,
        .type = OSW_CONF_TAIL,
    };
    m->enabled = OW_MBSS_PREFER_NONHIDDEN_ENABLED_DEFAULT;
    m->dryrun = OW_MBSS_PREFER_NONHIDDEN_DRYRUN_DEFAULT;
    m->mut = mut;

    if (osw_etc_get("OW_MBSS_PREFER_NONHIDDEN_DISABLE")) {
        m->enabled = false;
    }

    if (osw_etc_get("OW_MBSS_PREFER_NONHIDDEN_DRYRUN")) {
        m->dryrun = true;
    }

    LOGI(LOG_PREFIX("enabled: %d", m->enabled));
    LOGI(LOG_PREFIX("dryrun: %d", m->dryrun));
}

static void
ow_mbss_prefer_nonhidden_attach(struct ow_mbss_prefer_nonhidden *m)
{
    OSW_MODULE_LOAD(osw_conf);
    osw_conf_register_mutator(&m->mut);
}

OSW_MODULE(ow_mbss_prefer_nonhidden)
{
    static struct ow_mbss_prefer_nonhidden m;
    ow_mbss_prefer_nonhidden_init(&m);
    ow_mbss_prefer_nonhidden_attach(&m);
    return &m;
}
