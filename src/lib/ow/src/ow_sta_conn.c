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

#include <string.h>
#include <zlib.h>

#include <os.h>
#include <log.h>
#include <const.h>
#include <util.h>
#include <memutil.h>
#include <ds_tree.h>
#include <ds_dlist.h>
#include <osw_state.h>
#include <osw_module.h>
#include <osw_ut.h>

#include "ow_sta_conn.h"

#define LOG_PREFIX(fmt, ...)               "ow: sta_conn: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_VIF(vif_name, fmt, ...) LOG_PREFIX("%s: " fmt, vif_name, ##__VA_ARGS__)

struct ow_sta_conn
{
    struct osw_state_observer state_obs;
    struct ds_tree vifs;
    struct ds_dlist observers;
};

struct ow_sta_conn_vif
{
    struct ds_tree_node node;
    char *vif_name;
    bool failed;
    struct osw_drv_vif_sta_conn_failure failure;
    uint32_t networks_hash;
    bool networks_hash_valid;
};

struct ow_sta_conn_observer
{
    struct ds_dlist_node node;
    struct ow_sta_conn *m;
    ow_sta_conn_changed_fn_t *fn;
    void *priv;
};

static void ow_sta_conn_notify_changed(struct ow_sta_conn *m, const char *vif_name)
{
    struct ow_sta_conn_observer *o;
    ds_dlist_foreach (&m->observers, o)
    {
        if (o->fn != NULL) o->fn(o->priv, vif_name);
    }
}

/* IEEE 802.11 Status Code 1: UNSPECIFIED_FAILURE */
#define OW_STA_CONN_STATUS_UNSPECIFIED 1

/* How conclusive a failure cause is. Higher rank means more conclusive.
 *
 * A failed connection attempt produces a cascade of reports where
 * later events are only consequences of the root cause.
 *
 * E.g.: SSID-TEMP-DISABLED (WRONG_KEY) then NETWORK-NOT-FOUND once
 * wpa_supplicant puts the BSSID on its ignore list. Or wrong SAE
 * key: AUTH-REJECT (auth_type=3), followed by ASSOC-REJECT (status_code=1).
 * Or NETWORK-NOT-FOUND followed by ASSOC-REJECT (status_code=1). Etc.
 *
 * Less conclusive causes must not mask the more conclusive ones.
 * Equal rank: last one wins.
 */
static int ow_sta_conn_failure_rank(const struct osw_drv_vif_sta_conn_failure *f)
{
    switch (f->kind)
    {
        case OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY:
            return 5;
        case OSW_DRV_VIF_STA_CONN_FAILURE_AUTH_REJECT:
        case OSW_DRV_VIF_STA_CONN_FAILURE_ASSOC_REJECT:
            return (f->code > OW_STA_CONN_STATUS_UNSPECIFIED) ? 4 : 2;
        case OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND:
            return 3;
        case OSW_DRV_VIF_STA_CONN_FAILURE_GENERAL_ERR:
            return 2;
        case OSW_DRV_VIF_STA_CONN_FAILURE_DISCONNECTED:
            return 1;
    }
    return 0;
}

static uint32_t ow_sta_conn_networks_hash(const struct osw_drv_vif_sta_network *n)
{
    /* CRC-32 over the network list identity. A different identity
     * means a new connection attempt window. */
    uint32_t h = crc32(0L, Z_NULL, 0);
    for (; n != NULL; n = n->next)
    {
        h = crc32(h, (const Bytef *)&n->ssid.len, sizeof(n->ssid.len));
        h = crc32(h, (const Bytef *)n->ssid.buf, n->ssid.len);
        h = crc32(h, (const Bytef *)&n->bssid, sizeof(n->bssid));
        h = crc32(h, (const Bytef *)n->psk.str, strnlen(n->psk.str, sizeof(n->psk.str)));
    }
    return h;
}

static struct ow_sta_conn_vif *ow_sta_conn_vif_lookup(struct ow_sta_conn *m, const char *vif_name)
{
    return ds_tree_find(&m->vifs, vif_name);
}

static struct ow_sta_conn_vif *ow_sta_conn_vif_get_or_alloc(struct ow_sta_conn *m, const char *vif_name)
{
    struct ow_sta_conn_vif *vif = ow_sta_conn_vif_lookup(m, vif_name);
    if (vif != NULL) return vif;

    vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    return vif;
}

static void ow_sta_conn_vif_drop(struct ow_sta_conn *m, struct ow_sta_conn_vif *vif)
{
    if (vif == NULL) return;

    ds_tree_remove(&m->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static void ow_sta_conn_vif_clear_failure(struct ow_sta_conn *m, struct ow_sta_conn_vif *vif, const char *why)
{
    if (vif->failed == false) return;

    LOGI(LOG_PREFIX_VIF(
            vif->vif_name,
            "failure: %s: cleared: %s",
            osw_drv_vif_sta_conn_failure_kind_to_cstr(vif->failure.kind),
            why));
    vif->failed = false;
    MEMZERO(vif->failure);
    ow_sta_conn_notify_changed(m, vif->vif_name);
}

static void ow_sta_conn_vif_sta_conn_failure_cb(
        struct osw_state_observer *obs,
        const struct osw_state_vif_info *info,
        const struct osw_drv_vif_sta_conn_failure *failure)
{
    struct ow_sta_conn *m = container_of(obs, struct ow_sta_conn, state_obs);
    if (info->drv_state->vif_type != OSW_VIF_STA) return;

    struct ow_sta_conn_vif *vif = ow_sta_conn_vif_get_or_alloc(m, info->vif_name);

    if (vif->failed)
    {
        const int old_rank = ow_sta_conn_failure_rank(&vif->failure);
        const int new_rank = ow_sta_conn_failure_rank(failure);
        if (new_rank < old_rank) return;
    }

    const bool same = vif->failed && (memcmp(&vif->failure, failure, sizeof(*failure)) == 0);
    vif->failed = true;
    memcpy(&vif->failure, failure, sizeof(vif->failure));
    if (same) return;

    LOGI(LOG_PREFIX_VIF(
            vif->vif_name,
            "failure: %s code=%u%s%s%s",
            osw_drv_vif_sta_conn_failure_kind_to_cstr(failure->kind),
            failure->code,
            failure->local ? " local" : "",
            failure->detail[0] != '\0' ? " " : "",
            failure->detail));
    ow_sta_conn_notify_changed(m, vif->vif_name);
}

static void ow_sta_conn_vif_changed_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *info)
{
    struct ow_sta_conn *m = container_of(obs, struct ow_sta_conn, state_obs);

    if (info->drv_state->vif_type != OSW_VIF_STA)
    {
        /* Drop the vif entry if it changed its type from STA to something else at runtime */
        ow_sta_conn_vif_drop(m, ow_sta_conn_vif_lookup(m, info->vif_name));
        return;
    }

    struct ow_sta_conn_vif *vif = ow_sta_conn_vif_get_or_alloc(m, info->vif_name);
    const struct osw_drv_vif_state_sta *vsta = &info->drv_state->u.sta;

    if (vsta->link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED)
    {
        ow_sta_conn_vif_clear_failure(m, vif, "connected");
    }

    /* An empty list alone is skipped on purpose: it may appear
     * transiently while the supplicant is being restarted and does
     * not mean a new attempt window.
     */
    if (vsta->network != NULL)
    {
        const uint32_t hash = ow_sta_conn_networks_hash(vsta->network);

        /* Check if the networks have changed, if yes, clear the failure.
         * We don't want to clear the failure (and notify observers) at first
         * invocation, as at that moment the failure is already cleared. */
        const bool changed = vif->networks_hash_valid && (vif->networks_hash != hash);
        if (changed)
        {
            ow_sta_conn_vif_clear_failure(m, vif, "network changed");
        }
        vif->networks_hash = hash;
        vif->networks_hash_valid = true;
    }
    else if (info->drv_state->status == OSW_VIF_DISABLED)
    {
        /* No configured networks and the vif is down: a deliberate,
         * config-induced disable, ending the attempt window. */
        ow_sta_conn_vif_clear_failure(m, vif, "disabled");
        vif->networks_hash_valid = false;
    }
}

static void ow_sta_conn_vif_removed_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *info)
{
    struct ow_sta_conn *m = container_of(obs, struct ow_sta_conn, state_obs);
    struct ow_sta_conn_vif *vif = ow_sta_conn_vif_lookup(m, info->vif_name);

    ow_sta_conn_vif_drop(m, vif);
}

const struct osw_drv_vif_sta_conn_failure *ow_sta_conn_get_failure(ow_sta_conn_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (vif_name == NULL) return NULL;

    struct ow_sta_conn_vif *vif = ow_sta_conn_vif_lookup(m, vif_name);
    if (vif == NULL) return NULL;
    if (vif->failed == false) return NULL;
    return &vif->failure;
}

ow_sta_conn_observer_t *ow_sta_conn_observer_alloc(ow_sta_conn_t *m, ow_sta_conn_changed_fn_t *fn, void *priv)
{
    if (m == NULL) return NULL;

    struct ow_sta_conn_observer *o = CALLOC(1, sizeof(*o));
    o->m = m;
    o->fn = fn;
    o->priv = priv;
    ds_dlist_insert_tail(&m->observers, o);
    return o;
}

void ow_sta_conn_observer_drop(ow_sta_conn_observer_t *o)
{
    if (o == NULL) return;
    if (WARN_ON(o->m == NULL)) return;

    ds_dlist_remove(&o->m->observers, o);
    o->m = NULL;
    FREE(o);
}

static void ow_sta_conn_init(struct ow_sta_conn *m)
{
    ds_tree_init(&m->vifs, ds_str_cmp, struct ow_sta_conn_vif, node);
    ds_dlist_init(&m->observers, struct ow_sta_conn_observer, node);
    m->state_obs.name = "ow_sta_conn";
    m->state_obs.vif_added_fn = ow_sta_conn_vif_changed_cb;
    m->state_obs.vif_changed_fn = ow_sta_conn_vif_changed_cb;
    m->state_obs.vif_removed_fn = ow_sta_conn_vif_removed_cb;
    m->state_obs.vif_sta_conn_failure_fn = ow_sta_conn_vif_sta_conn_failure_cb;
}

OSW_MODULE(ow_sta_conn)
{
    OSW_MODULE_LOAD(osw_state);
    static struct ow_sta_conn m;
    ow_sta_conn_init(&m);
    osw_state_register_observer(&m.state_obs);
    return &m;
}

OSW_UT(ow_sta_conn_failure_ranking_ut)
{
    struct ow_sta_conn m;
    ow_sta_conn_init(&m);

    struct osw_drv_vif_state dstate = {.vif_type = OSW_VIF_STA};
    struct osw_state_vif_info info = {.vif_name = "sta1", .drv_state = &dstate};
    struct osw_drv_vif_sta_conn_failure f;
    const struct osw_drv_vif_sta_conn_failure *got;

    /* nothing latched */
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") == NULL);

    /* disconnected latches with its code */
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_DISCONNECTED;
    f.code = 3;
    f.local = true;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    got = ow_sta_conn_get_failure(&m, "sta1");
    OSW_UT_EVAL(got != NULL);
    OSW_UT_EVAL(got->kind == OSW_DRV_VIF_STA_CONN_FAILURE_DISCONNECTED);
    OSW_UT_EVAL(got->code == 3);
    OSW_UT_EVAL(got->local == true);

    /* wrong_key overrides disconnected */
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    got = ow_sta_conn_get_failure(&m, "sta1");
    OSW_UT_EVAL(got != NULL);
    OSW_UT_EVAL(got->kind == OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY);

    /* the derivative assoc_reject:1 must not mask wrong_key */
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_ASSOC_REJECT;
    f.code = 1;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    got = ow_sta_conn_get_failure(&m, "sta1");
    OSW_UT_EVAL(got != NULL);
    OSW_UT_EVAL(got->kind == OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY);

    /* ssid_not_found (BSSID on ignore list) must not mask wrong_key */
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    got = ow_sta_conn_get_failure(&m, "sta1");
    OSW_UT_EVAL(got != NULL);
    OSW_UT_EVAL(got->kind == OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY);

    /* connected clears */
    dstate.u.sta.link.status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") == NULL);

    /* assoc_reject:1 should not mask ssid_not_found */
    dstate.u.sta.link.status = OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_DISCONNECTED;
    f.code = 3;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_ASSOC_REJECT;
    f.code = 1;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    got = ow_sta_conn_get_failure(&m, "sta1");
    OSW_UT_EVAL(got != NULL);
    OSW_UT_EVAL(got->kind == OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND);
}

OSW_UT(ow_sta_conn_network_change_ut)
{
    struct ow_sta_conn m;
    ow_sta_conn_init(&m);

    struct osw_drv_vif_state dstate = {.vif_type = OSW_VIF_STA};
    struct osw_state_vif_info info = {.vif_name = "sta1", .drv_state = &dstate};
    struct osw_drv_vif_sta_conn_failure f;
    struct osw_drv_vif_sta_network net;

    MEMZERO(net);
    STRSCPY(net.ssid.buf, "net-a");
    net.ssid.len = strlen("net-a");
    dstate.u.sta.network = &net;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);

    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_WRONG_KEY;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") != NULL);

    /* same network re-applied: retry loop, failure must stick */
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") != NULL);

    /* transiently empty network list: failure must stick */
    dstate.u.sta.network = NULL;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") != NULL);
    dstate.u.sta.network = &net;

    /* different psk: new attempt window, failure cleared */
    STRSCPY(net.psk.str, "new-psk-123");
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") == NULL);

    MEMZERO(f);
    f.kind = OSW_DRV_VIF_STA_CONN_FAILURE_SSID_NOT_FOUND;
    ow_sta_conn_vif_sta_conn_failure_cb(&m.state_obs, &info, &f);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") != NULL);

    /* socket flap: no networks but vif not disabled, failure sticks */
    dstate.u.sta.network = NULL;
    dstate.status = OSW_VIF_BROKEN;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") != NULL);

    /* config-induced disable: no networks and vif down, failure cleared */
    dstate.status = OSW_VIF_DISABLED;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") == NULL);

    /* re-enable with the same network: still no stale failure */
    dstate.u.sta.network = &net;
    dstate.status = OSW_VIF_ENABLED;
    ow_sta_conn_vif_changed_cb(&m.state_obs, &info);
    OSW_UT_EVAL(ow_sta_conn_get_failure(&m, "sta1") == NULL);
}
