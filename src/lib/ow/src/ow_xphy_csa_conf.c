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
#include <const.h>
#include <util.h>
#include <os.h>
#include <memutil.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <osw_types.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_module.h>
#include <osw_ut.h>

/**
 * @brief Cross-PHY CSA support
 *
 * This is intended to handle cases where one radio's STA
 * interface receives a CSA intent with a target channel
 * that isn't part of the receiving radio's supported
 * channel list. Instead, if another radio supports that
 * channel it should inherit the STA interface configuration
 * and continue (uplink) operation on that other radio.
 *
 * This uses the osw_conf module to mutate the configuration
 * at the end, after base configs, like. ow_conf is already
 * done providing the baseline.
 *
 * The expectation is that the configuration entity will
 * eventually update it's data model and ow_conf will
 * get reconfigured with uplink to the new radio.
 */

struct ow_xphy_csa_conf {
    struct osw_state_observer obs;
    struct osw_conf_mutator mut;

    struct osw_ifname phy_name;
    struct osw_channel channel;
    bool active;
};

static void
ow_xphy_csa_conf_set(struct ow_xphy_csa_conf *csa,
                     const char *phy_name,
                     const struct osw_channel *c)
{
    csa->active = true;
    csa->channel = *c;
    STRSCPY_WARN(csa->phy_name.buf, phy_name);
}

static void
ow_xphy_csa_conf_state_cb(struct osw_state_observer *self,
                          const struct osw_state_vif_info *vif,
                          const struct osw_state_phy_info *to_phy,
                          const struct osw_channel *c)
{
    struct ow_xphy_csa_conf *csa = container_of(self, struct ow_xphy_csa_conf, obs);
    LOGI("csa: moving from %s to %s for "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
         vif->phy->phy_name,
         to_phy->phy_name,
         OSW_CHANNEL_ARG(&vif->drv_state->u.sta.link.channel),
         OSW_CHANNEL_ARG(c));
    ow_xphy_csa_conf_set(csa, to_phy->phy_name, c);
    osw_conf_invalidate(&csa->mut);
}

static void
ow_xphy_csa_conf_apply_vsta(struct osw_conf_vif *from,
                           struct osw_conf_vif *to)
{
    from->enabled = false;
    to->enabled = true;

    struct osw_conf_net *net;
    while ((net = ds_dlist_remove_head(&to->u.sta.net_list)) != NULL) {
        LOGT("csa: applying: %s: removing "OSW_SSID_FMT,
             to->vif_name,
             OSW_SSID_ARG(&net->ssid));
        FREE(net);
    }

    ds_dlist_foreach(&from->u.sta.net_list, net) {
        struct osw_conf_net *n = MEMNDUP(net, sizeof(*net));
        LOGT("csa: applying %s: adding "OSW_SSID_FMT,
             to->vif_name,
             OSW_SSID_ARG(&n->ssid));
        memset(&n->node, 0, sizeof(n->node));
        ds_dlist_insert_tail(&to->u.sta.net_list, n);
    }
}

static void
ow_xphy_csa_conf_apply_channel(struct ow_xphy_csa_conf *csa,
                              struct osw_conf_phy *phy)
{
    struct osw_conf_vif *vif;
    ds_tree_foreach(&phy->vif_tree, vif) {
        if (vif->vif_type != OSW_VIF_AP) continue;
        LOGT("csa: applying %s: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             vif->vif_name,
             OSW_CHANNEL_ARG(&vif->u.ap.channel),
             OSW_CHANNEL_ARG(&csa->channel));
        vif->u.ap.channel = csa->channel;
    }
}

static void
ow_xphy_csa_conf_disarm__(struct ow_xphy_csa_conf *csa)
{
    if (csa->active == false) return;
    LOGI("csa: disarming override for %s to "OSW_CHANNEL_FMT,
         csa->phy_name.buf,
         OSW_CHANNEL_ARG(&csa->channel));
    csa->active = false;
    osw_conf_invalidate(&csa->mut);
}

enum ow_xphy_csa_conf_result {
    OW_XPHY_CSA_CONF_INACTIVE,
    OW_XPHY_CSA_CONF_MULTI_VSTA,
    OW_XPHY_CSA_CONF_NO_CSA_VSTA,
    OW_XPHY_CSA_CONF_NO_CUR_VSTA,
    OW_XPHY_CSA_CONF_DISARM,
    OW_XPHY_CSA_CONF_APPLIED,
};

static enum ow_xphy_csa_conf_result
ow_xphy_csa_conf_apply(struct ow_xphy_csa_conf *csa,
                       struct ds_tree *phy_tree)
{
    if (csa->active == false) return OW_XPHY_CSA_CONF_INACTIVE;

    LOGD("csa: applying override for %s to "OSW_CHANNEL_FMT,
         csa->phy_name.buf,
         OSW_CHANNEL_ARG(&csa->channel));

    struct osw_conf_phy *phy;
    struct osw_conf_vif *vif;
    struct osw_conf_vif *cur_vsta = NULL;
    struct osw_conf_vif *csa_vsta = NULL;
    struct osw_conf_phy *csa_phy = NULL;
    size_t n_vsta_enabled = 0;

    ds_tree_foreach(phy_tree, phy) {
        const bool is_csa_phy = (strcmp(phy->phy_name, csa->phy_name.buf) == 0);
        ds_tree_foreach(&phy->vif_tree, vif) {
            if (vif->vif_type != OSW_VIF_STA) continue;
            if (vif->enabled == true) n_vsta_enabled++;
            if (vif->enabled == true && is_csa_phy == false) cur_vsta = vif;
            if (is_csa_phy == true) csa_vsta = vif;
        }
        if (is_csa_phy == true) csa_phy = phy;
    }

    if (n_vsta_enabled > 1) {
        /* In theory this could happen during onboarding. In
         * that case better do nothing. If link is lost the
         * system will backoff and use another STA
         * configuration. If there are no other STA link
         * configurations anymore, it'll eventually go
         * through to apply_vsta() below on next try.
         */
        LOGI("csa: unable to handle multi-vsta configurations, ignoring for now");
        return OW_XPHY_CSA_CONF_MULTI_VSTA;
    }

    if (csa_vsta == NULL) {
        LOGI("csa: unable to override: %s has no sta vif", csa->phy_name.buf);
        return OW_XPHY_CSA_CONF_NO_CSA_VSTA;
    }

    if (csa_vsta->enabled == true) {
        LOGI("csa: already on target phy, stopping");
        return OW_XPHY_CSA_CONF_DISARM;
    }

    if (cur_vsta == NULL) {
        LOGI("csa: unable to override: there's no current sta link to inherit info from");
        return OW_XPHY_CSA_CONF_NO_CUR_VSTA;
    }

    ow_xphy_csa_conf_apply_vsta(cur_vsta, csa_vsta);
    ow_xphy_csa_conf_apply_channel(csa, csa_phy);
    return OW_XPHY_CSA_CONF_APPLIED;
}

static void
ow_xphy_csa_conf_mutate_cb(struct osw_conf_mutator *self,
                           struct ds_tree *phy_tree)
{
    struct ow_xphy_csa_conf *csa = container_of(self, struct ow_xphy_csa_conf, mut);
    switch (ow_xphy_csa_conf_apply(csa, phy_tree)) {
        case OW_XPHY_CSA_CONF_INACTIVE:
        case OW_XPHY_CSA_CONF_MULTI_VSTA:
        case OW_XPHY_CSA_CONF_NO_CSA_VSTA:
        case OW_XPHY_CSA_CONF_NO_CUR_VSTA:
        case OW_XPHY_CSA_CONF_APPLIED:
            break;
        case OW_XPHY_CSA_CONF_DISARM:
            ow_xphy_csa_conf_disarm__(csa);
            break;
    }
}

void
ow_xphy_csa_conf_disarm(struct ow_xphy_csa_conf *m)
{
    ow_xphy_csa_conf_disarm__(m);
}

static void
mod_init(struct ow_xphy_csa_conf *m)
{
    const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_csa_to_phy_fn = ow_xphy_csa_conf_state_cb,
    };
    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .mutate_fn = ow_xphy_csa_conf_mutate_cb,
        .type = OSW_CONF_TAIL,
    };
    m->obs = obs;
    m->mut = mut;
}

static void
mod_attach(struct ow_xphy_csa_conf *m)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_conf);
    osw_state_register_observer(&m->obs);
    osw_conf_register_mutator(&m->mut);
}

OSW_MODULE(ow_xphy_csa_conf)
{
    static struct ow_xphy_csa_conf m;
    mod_init(&m);
    mod_attach(&m);
    return &m;
}

OSW_UT(ow_xphy_csa_conf_ut)
{
    struct ds_tree phy_tree;
    struct osw_conf_net n1 = {
        .ssid = { .buf = "foo", .len = 3 },
    };
    struct osw_conf_net n2 = {
        .ssid = { .buf = "foobar", .len = 6 },
    };
    struct osw_conf_phy p1 = {
        .phy_name = "phy1",
    };
    struct osw_conf_phy p2 = {
        .phy_name = "phy2",
    };
    struct osw_conf_vif vsta1 = {
        .vif_name = "vsta1",
        .vif_type = OSW_VIF_STA,
        .enabled = true,
    };
    struct osw_conf_vif vsta2 = {
        .vif_name = "vsta2",
        .vif_type = OSW_VIF_STA,
        .enabled = false,
    };
    struct osw_conf_vif vap1_1 = {
        .vif_name = "vap1_1",
        .vif_type = OSW_VIF_AP,
        .enabled = true,
        .u = { .ap = { .channel = { .control_freq_mhz = 2412 } } },
    };
    struct osw_conf_vif vap1_2 = {
        .vif_name = "vap1_2",
        .vif_type = OSW_VIF_AP,
        .enabled = true,
        .u = { .ap = { .channel = { .control_freq_mhz = 2412 } } },
    };
    struct osw_conf_vif vap2_1 = {
        .vif_name = "vap2_1",
        .vif_type = OSW_VIF_AP,
        .enabled = true,
        .u = { .ap = { .channel = { .control_freq_mhz = 5180 } } },
    };
    struct osw_conf_vif vap2_2 = {
        .vif_name = "vap2_2",
        .vif_type = OSW_VIF_AP,
        .enabled = true,
        .u = { .ap = { .channel = { .control_freq_mhz = 5180 } } },
    };
    struct osw_channel c1 = {
        .control_freq_mhz = 5200,
    };
    struct osw_channel c2 = {
        .control_freq_mhz = 2417,
    };

    ds_tree_init(&phy_tree, ds_str_cmp, struct osw_conf_phy, conf_node);
    ds_tree_init(&p1.vif_tree, ds_str_cmp, struct osw_conf_vif, phy_node);
    ds_tree_init(&p2.vif_tree, ds_str_cmp, struct osw_conf_vif, phy_node);
    ds_dlist_init(&vsta1.u.sta.net_list, struct osw_conf_net, node);
    ds_dlist_init(&vsta2.u.sta.net_list, struct osw_conf_net, node);

    ds_tree_insert(&phy_tree, &p1, p1.phy_name);
    ds_tree_insert(&phy_tree, &p2, p2.phy_name);
    ds_tree_insert(&p1.vif_tree, &vsta1, vsta1.vif_name);
    ds_tree_insert(&p1.vif_tree, &vap1_1, vap1_1.vif_name);
    ds_tree_insert(&p1.vif_tree, &vap1_2, vap1_2.vif_name);
    ds_tree_insert(&p2.vif_tree, &vsta2, vsta2.vif_name);
    ds_tree_insert(&p2.vif_tree, &vap2_1, vap2_1.vif_name);
    ds_tree_insert(&p2.vif_tree, &vap2_2, vap2_2.vif_name);
    ds_dlist_insert_tail(&vsta1.u.sta.net_list, MEMNDUP(&n1, sizeof(n1)));
    ds_dlist_insert_tail(&vsta1.u.sta.net_list, MEMNDUP(&n2, sizeof(n2)));

    struct ow_xphy_csa_conf csa;
    MEMZERO(csa);

    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_INACTIVE);

    ow_xphy_csa_conf_set(&csa, "phy2", &c1);
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_APPLIED);
    OSW_UT_EVAL(ds_dlist_head(&vsta2.u.sta.net_list) != NULL);
    struct osw_conf_net *n3 = ds_dlist_head(&vsta2.u.sta.net_list);
    struct osw_conf_net *n4 = ds_dlist_tail(&vsta2.u.sta.net_list);
    OSW_UT_EVAL(memcmp(&n1.ssid, &n3->ssid, sizeof(n1.ssid)) == 0);
    OSW_UT_EVAL(memcmp(&n2.ssid, &n4->ssid, sizeof(n2.ssid)) == 0);
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_DISARM);
    OSW_UT_EVAL(vsta1.enabled == false);
    OSW_UT_EVAL(vsta2.enabled == true);
    OSW_UT_EVAL(vap2_1.u.ap.channel.control_freq_mhz == c1.control_freq_mhz);

    vsta1.enabled = true;
    vsta2.enabled = true;
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_MULTI_VSTA);

    ow_xphy_csa_conf_set(&csa, "phy1", &c2);
    vsta1.enabled = false;
    vsta2.enabled = false;
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_NO_CUR_VSTA);

    vsta1.vif_type = OSW_VIF_AP;
    vsta2.enabled = true;
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_NO_CSA_VSTA);

    vsta1.vif_type = OSW_VIF_STA;
    OSW_UT_EVAL(ow_xphy_csa_conf_apply(&csa, &phy_tree) == OW_XPHY_CSA_CONF_APPLIED);
    struct osw_conf_net *n5 = ds_dlist_head(&vsta1.u.sta.net_list);
    struct osw_conf_net *n6 = ds_dlist_tail(&vsta1.u.sta.net_list);
    OSW_UT_EVAL(memcmp(&n1.ssid, &n5->ssid, sizeof(n1.ssid)) == 0);
    OSW_UT_EVAL(memcmp(&n2.ssid, &n6->ssid, sizeof(n2.ssid)) == 0);
}
