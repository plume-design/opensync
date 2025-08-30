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

#include <module.h>
#include <log.h>
#include <const.h>
#include <memutil.h>
#include <ds_tree.h>
#include <osw_ut.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_module.h>
#include "ow_conf.h"

/*
 * OneWifi ACL Kick
 *
 * ACL in drivers themselves don't (typically) kick stations
 * when ACL is changed. The usecase for that is, eg.
 * steering, where one first prepares the ACL into a desired
 * state and then initiates actions like sending Deauth or
 * Action frames to stations of interest.
 *
 * This modules makes sure that ACLs configured via ow_conf
 * are enforced at all times, ie. regardless of the ordering
 * of ACL mutation and stations connecting/disconnecting, a
 * client that is effectively banned through the ACL, will
 * be kicked if necessary.
 *
 * This could be improved by looking at osw_state as well.
 * Whatever ow_conf expresses to be configured on the ACL
 * doesn't necessarily mean it will be configured on an
 * interface because osw_conf can mutate the configuration.
 */

struct ow_acl_kick {
    struct ow_conf_observer conf_obs;
    struct osw_state_observer state_obs;
    struct ds_tree vif_tree;
    ev_timer work;
};

struct ow_acl_kick_vif {
    struct ds_tree_node node;
    char *vif_name;
};

static bool
ow_acl_kick_addr_is_allowed(const char *vif_name,
                            const struct osw_hwaddr *addr)
{
    const enum osw_acl_policy *p = ow_conf_vif_get_ap_acl_policy(vif_name);
    if (p == NULL)
        return true;

    const bool listed = ow_conf_vif_has_ap_acl(vif_name, addr);
    switch (*p) {
        case OSW_ACL_NONE: return true;
        case OSW_ACL_ALLOW_LIST: return (listed == true);
        case OSW_ACL_DENY_LIST: return (listed == false);
    }

    /* shouldn't reach here */
    assert(0);
    return true;
}

static void
ow_acl_kick_sta_police(const struct osw_state_sta_info *sta)
{
    const char *phy_name = sta->vif->phy->phy_name;
    const char *vif_name = sta->vif->vif_name;
    const struct osw_hwaddr *sta_addr = sta->mac_addr;
    const int reason = 1; /* unspecified */

    LOGD("ow: acl kick: %s/"OSW_HWADDR_FMT": policing",
         vif_name, OSW_HWADDR_ARG(sta_addr));

    if (ow_acl_kick_addr_is_allowed(vif_name, sta_addr) == true)
        return;

    LOGI("ow: acl kick: %s/"OSW_HWADDR_FMT": "
         "connected but not allowed, kicking",
         vif_name, OSW_HWADDR_ARG(sta_addr));

    const bool ok = osw_mux_request_sta_deauth(phy_name, vif_name, sta_addr, reason);
    const bool failed = !ok;
    WARN_ON(failed);
}

static void
ow_acl_kick_sta_police_sta_cb(const struct osw_state_sta_info *sta,
                              void *priv)
{
    ow_acl_kick_sta_police(sta);
}

static void
ow_acl_kick_vif_add(struct ow_acl_kick *oak, const char *vif_name)
{
    struct ow_acl_kick_vif *vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&oak->vif_tree, vif, vif->vif_name);
    ev_timer_stop(EV_DEFAULT_ &oak->work);
    ev_timer_set(&oak->work, 0, 0);
    ev_timer_start(EV_DEFAULT_ &oak->work);
}

static void
ow_acl_kick_vif_del(struct ow_acl_kick *oak, struct ow_acl_kick_vif *vif)
{
    ds_tree_remove(&oak->vif_tree, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static void
ow_acl_kick_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_acl_kick *oak = container_of(arg, struct ow_acl_kick, work);
    struct ow_acl_kick_vif *vif;
    struct ow_acl_kick_vif *tmp;
    const char *phy_name = NULL;

    ds_tree_foreach_safe(&oak->vif_tree, vif, tmp) {
        LOGD("ow: acl kick: %s: doing work", vif->vif_name);
        osw_state_sta_get_list(ow_acl_kick_sta_police_sta_cb,
                               phy_name, vif->vif_name, NULL);
        ow_acl_kick_vif_del(oak, vif);
    }
}

static void
ow_acl_kick_conf_vif_changed_cb(struct ow_conf_observer *obs,
                                const char *vif_name)
{
    struct ow_acl_kick *oak = container_of(obs, struct ow_acl_kick, conf_obs);
    struct ow_acl_kick_vif *vif = ds_tree_find(&oak->vif_tree, vif_name);
    if (vif != NULL) return;
    LOGD("ow: acl kick: %s: adding work", vif_name);
    ow_acl_kick_vif_add(oak, vif_name);
}

static void
ow_acl_kick_sta_connected_cb(struct osw_state_observer *obs,
                             const struct osw_state_sta_info *sta)
{
    ow_acl_kick_sta_police(sta);
}

static void
ow_acl_kick_sta_changed_cb(struct osw_state_observer *obs,
                           const struct osw_state_sta_info *sta)
{
    ow_acl_kick_sta_police(sta);
}

static struct ow_acl_kick g_ow_acl_kick = {
    .conf_obs = {
        .name = __FILE__,
        .vif_changed_fn = ow_acl_kick_conf_vif_changed_cb,
    },
    .state_obs = {
        .name = __FILE__,
        .sta_connected_fn = ow_acl_kick_sta_connected_cb,
        .sta_changed_fn = ow_acl_kick_sta_changed_cb,
    },
    .vif_tree = DS_TREE_INIT(ds_str_cmp, struct ow_acl_kick_vif, node),
};

static void
ow_acl_kick_init(struct ow_acl_kick *oak)
{
    LOGI("ow: acl kick: initializing");
    ev_timer_init(&oak->work, ow_acl_kick_work_cb, 0, 0);
    ow_conf_register_observer(&oak->conf_obs);
    osw_state_register_observer(&oak->state_obs);
}

OSW_UT(ow_acl_kick_ut)
{
    const char *vif_name = "vif1";
    const struct osw_hwaddr addr1 = { .octet = {0,1,2,3,4,5} };
    const struct osw_hwaddr addr2 = { .octet = {0,2,3,4,5,6} };
    const enum osw_acl_policy none = OSW_ACL_NONE;
    const enum osw_acl_policy allow = OSW_ACL_ALLOW_LIST;
    const enum osw_acl_policy deny = OSW_ACL_DENY_LIST;

    osw_module_load_name("ow_conf");

    ow_conf_vif_set_ap_acl_policy(vif_name, &deny);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_add_ap_acl(vif_name, &addr1);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr1) == false);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr2) == true);

    ow_conf_vif_set_ap_acl_policy(vif_name, &deny);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_add_ap_acl(vif_name, &addr2);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr1) == true);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr2) == false);

    ow_conf_vif_set_ap_acl_policy(vif_name, &none);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_add_ap_acl(vif_name, &addr2);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr1) == true);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr2) == true);

    ow_conf_vif_set_ap_acl_policy(vif_name, &allow);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_add_ap_acl(vif_name, &addr1);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr1) == true);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr2) == false);

    ow_conf_vif_set_ap_acl_policy(vif_name, &allow);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_add_ap_acl(vif_name, &addr2);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr1) == false);
    assert(ow_acl_kick_addr_is_allowed(vif_name, &addr2) == true);
}

OSW_MODULE(ow_acl_kick)
{
    OSW_MODULE_LOAD(ow_conf);
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_mux);
    ow_acl_kick_init(&g_ow_acl_kick);
    return NULL;
}
