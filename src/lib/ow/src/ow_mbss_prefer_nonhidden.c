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
 * It applies a mutation somwhere at the end of osw_conf
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
ow_mbss_prefer_nonhidden_find_first_nonhidden(struct osw_conf_phy *phy)
{
    struct osw_conf_vif *vif;
    ds_tree_foreach(&phy->vif_tree, vif) {
        if (vif->enabled == false) {
            continue;
        }

        switch (vif->vif_type) {
            case OSW_VIF_AP:
                if (vif->u.ap.ssid_hidden == false) {
                    return vif;
                }
                break;
            case OSW_VIF_UNDEFINED:
            case OSW_VIF_AP_VLAN:
            case OSW_VIF_STA:
                break;
        }
    }
    return NULL;
}

static struct osw_conf_vif *
ow_mbss_prefer_nonhidden_find_any_enabled(struct osw_conf_phy *phy)
{
    struct osw_conf_vif *vif;
    ds_tree_foreach(&phy->vif_tree, vif) {
        switch (vif->vif_type) {
            case OSW_VIF_AP:
                if (vif->enabled) {
                    return vif;
                }
                break;
            case OSW_VIF_UNDEFINED:
            case OSW_VIF_AP_VLAN:
            case OSW_VIF_STA:
                break;
        }
    }
    return NULL;
}

static struct osw_conf_vif *
ow_mbss_prefer_nonhidden_find_suitable_vif(struct osw_conf_phy *phy)
{
    return ow_mbss_prefer_nonhidden_find_first_nonhidden(phy)
        ?: ow_mbss_prefer_nonhidden_find_any_enabled(phy);
}

static void
ow_mbss_prefer_nonhidden_fix_phy(struct ow_mbss_prefer_nonhidden *m,
                                 struct osw_conf_phy *phy)
{
    struct osw_ifname *tx_vif_name = &phy->mbss_tx_vif_name;
    const bool mbss_is_active = (osw_ifname_is_valid(tx_vif_name));
    const bool mbss_is_inactive = !mbss_is_active;
    if (mbss_is_inactive) return;

    struct osw_conf_vif *tx_vif = ds_tree_find(&phy->vif_tree, tx_vif_name->buf);
    const bool tx_vif_not_found = (tx_vif == NULL);
    const bool tx_vif_is_disabled = (tx_vif != NULL)
                                 && (tx_vif->enabled == false);
    const bool tx_vif_is_ap_hidden = (tx_vif != NULL)
                                  && (tx_vif->vif_type == OSW_VIF_AP)
                                  && (tx_vif->u.ap.ssid_hidden == true);
    const bool tx_vif_is_not_ap = (tx_vif != NULL)
                               && (tx_vif->vif_type != OSW_VIF_AP);
    const bool needs_fixing = tx_vif_not_found
                           || tx_vif_is_disabled
                           || tx_vif_is_ap_hidden
                           || tx_vif_is_not_ap;
    const bool does_not_need_fixing = !needs_fixing;
    if (does_not_need_fixing) return;

    struct osw_conf_vif *new_tx_vif = ow_mbss_prefer_nonhidden_find_suitable_vif(phy);
    if (new_tx_vif == NULL) return;
    if (new_tx_vif == tx_vif) return;

    const char *phy_name = phy->phy_name;
    LOGI(LOG_PREFIX_PHY(phy_name, "overriding: "OSW_IFNAME_FMT" -> %s",
                        OSW_IFNAME_ARG(tx_vif_name),
                        new_tx_vif->vif_name));

    if (m->dryrun) return;
    STRSCPY_WARN(tx_vif_name->buf, new_tx_vif->vif_name);
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
        ow_mbss_prefer_nonhidden_fix_phy(m, phy);
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

    if (getenv("OW_MBSS_PREFER_NONHIDDEN_DISABLE")) {
        m->enabled = false;
    }

    if (getenv("OW_MBSS_PREFER_NONHIDDEN_DRYRUN")) {
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
