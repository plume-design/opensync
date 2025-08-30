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

#include <fcntl.h>
#include <memutil.h>
#include <module.h>
#include <os.h>
#include <os_time.h>
#include <osw_time.h>
#include <ev.h>
#include <const.h>
#include <osw_timer.h>
#include <osw_drv_common.h>
#include <osw_drv.h>
#include <osw_util.h>
#include "osw_drv_i.h"
#include "osw_state_i.h"
#include "osw_stats_i.h"

/* TODO
 * - use macros to automate value/string/list comparisons for "is changed", along with dumping to logs/traces
 * - add counters: settle/nosettle counts, work rounds, watchdog counts, request retry counts: will make testing teasier
 * - add global timestamp of when system last settled, for debugging purposes
 * -
 */

struct ds_tree g_osw_drv_tree = DS_TREE_INIT(ds_void_cmp, struct osw_drv, node);
static ev_async g_osw_drv_work_all_async;
static ev_timer g_osw_drv_work_watchdog;
static bool g_osw_drv_work_done;
static bool g_osw_drv_settled;

#define OSW_DRV_WORK_ALL_WATCHDOG_SECONDS 60.0
#define OSW_DRV_CHAN_SYNC_SECONDS 5.0
#define OSW_DRV_RESYNC_SECONDS 3.0
#define OSW_DRV_CAC_SYNC_SECONDS 3.0
#define OSW_DRV_NOL_SYNC_SECONDS 30.0
#define OSW_DRV_TX_TIMEOUT_SECONDS 10.0
#define OSW_DRV_STA_IES_TIMEOUT_SEC 5
#define OSW_DRV_VIF_RECENT_CHANNEL_TIMEOUT_SEC 10

#define osw_log_drv_register_ops(ops) \
    LOGI("osw: drv: registering ops: name=%s", ops->name)
#define osw_log_drv_unregister_ops(ops) \
    LOGI("osw: drv: unregistering ops: name=%s", ops->name)
#define osw_log_drv_retry_phy_request(phy) \
    LOGI("osw: drv: retrying phy state request: drv=%s phy=%s", \
         phy->drv->ops->name, phy->phy_name);
#define osw_log_drv_retry_vif_request(vif) \
    LOGI("osw: drv: retrying vif state request: drv=%s phy=%s vif=%s", \
         vif->phy->drv->ops->name, vif->phy->phy_name, vif->vif_name);
#define osw_log_drv_retry_sta_request(sta) \
    LOGI("osw: drv: retrying sta state request: drv=%s phy=%s vif=%s sta=" OSW_HWADDR_FMT, \
         sta->vif->phy->drv->ops->name, sta->vif->phy->phy_name, sta->vif->vif_name, OSW_HWADDR_ARG(&(sta)->mac_addr));
#define osw_log_drv_watchdog() \
    LOGW("osw: drv: watchdog triggered: system has not settled for %.2f seconds", \
         OSW_DRV_WORK_ALL_WATCHDOG_SECONDS);

#define ARRDUP(src, dst, memb_ptr, memb_len) \
    do { \
        (dst)->memb_ptr = NULL; \
        (dst)->memb_len = 0; \
        if ((src)->memb_ptr != NULL && (src)->memb_len > 0) { \
            size_t n = sizeof(*(dst)->memb_ptr) * (src)->memb_len; \
            (dst)->memb_ptr = MALLOC(n); \
            (dst)->memb_len = (src)->memb_len; \
            memcpy((dst)->memb_ptr, (src)->memb_ptr, n); \
        } \
    } while (0)

static void
osw_drv_buf_free(struct osw_drv_buf *buf)
{
    FREE(buf->data);
    buf->data = NULL;
    buf->len = 0;
}

static void
osw_drv_buf_set(struct osw_drv_buf *buf, const void *data, size_t len)
{
    osw_drv_buf_free(buf);
    if (data == NULL) return;
    if (len == 0) return;
    buf->data = MEMNDUP(data, len);
    buf->len = len;
}

static void
osw_drv_buf_copy(struct osw_drv_buf *dst, const struct osw_drv_buf *src)
{
    osw_drv_buf_set(dst, src->data, src->len);
}

static bool
osw_drv_buf_is_same(const struct osw_drv_buf *a,
                    const struct osw_drv_buf *b)
{
    const void *o = a->data;
    const void *n = b->data;

    if (o == NULL && n == NULL) return true;
    if (o == NULL && n != NULL) return false;
    if (o != NULL && n == NULL) return false;
    assert(o != NULL && n != NULL);
    if (a->len != b->len) return false;
    return memcmp(o, n, a->len) == 0 ? true : false;
}

static void
osw_drv_assoc_ie_cache_fill_path(struct osw_drv_sta *sta,
                                 void *buf,
                                 size_t size)
{
    snprintf(buf, size, "/tmp/osw_drv_assoc_ie_cache_%s_%s_"OSW_HWADDR_FMT,
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr));
}

static void
osw_drv_assoc_ie_cache_load(struct osw_drv_sta *sta)
{
    const bool new_ies_present = (sta->new_ies.data != NULL)
                              && (sta->new_ies.len > 0);
    if (new_ies_present) return;

    char path[4096];
    osw_drv_assoc_ie_cache_fill_path(sta, path, sizeof(path));
    LOGT("osw: drv: %s/%s/"OSW_HWADDR_FMT": ie cache: loading: %s",
         sta->vif->phy->phy_name,
         sta->vif->vif_name,
         OSW_HWADDR_ARG(&sta->mac_addr),
         path);

    const int fd = open(path, O_RDONLY);
    if (fd < 0) return; /* perhaps doesn't exist, that's fine */
    char buf[4096];
    const ssize_t rv = read(fd, buf, sizeof(buf));
    close(fd);
    if (rv > 0) {
        LOGT("osw: drv: %s/%s/"OSW_HWADDR_FMT": ie cache: loaded",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr));

        FREE(sta->new_ies.data);
        const size_t len = (size_t)rv; /* rv>0, so it'll fit */
        sta->new_ies.data = MEMNDUP(buf, len);
        sta->new_ies.len = len;
    }
}

static void
osw_drv_assoc_ie_cache_store(struct osw_drv_sta *sta)
{
    const bool new_ies_missing = (sta->cur_ies.data == NULL)
                              || (sta->cur_ies.len == 0);
    if (new_ies_missing) return;

    char path[4096];
    osw_drv_assoc_ie_cache_fill_path(sta, path, sizeof(path));
    LOGT("osw: drv: %s/%s/"OSW_HWADDR_FMT": ie cache: storing: %s",
         sta->vif->phy->phy_name,
         sta->vif->vif_name,
         OSW_HWADDR_ARG(&sta->mac_addr),
         path);

    const int fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (WARN_ON(fd < 0)) return;
    const ssize_t rv = write(fd, sta->cur_ies.data, sta->cur_ies.len);
    close(fd);
    if (WARN_ON(rv <= 0)) return;
    const size_t len = (size_t)rv; /* rv>0, so it'll fit */
    WARN_ON(len != sta->cur_ies.len); /* truncated, oops; unlikely */

    LOGT("osw: drv: %s/%s/"OSW_HWADDR_FMT": ie cache: stored",
         sta->vif->phy->phy_name,
         sta->vif->vif_name,
         OSW_HWADDR_ARG(&sta->mac_addr));
}

static const char *
osw_drv_vif_link_status_to_str(const enum osw_drv_vif_state_sta_link_status s)
{
    switch (s) {
        case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN: return "unknown";
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED: return "connected";
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING: return "connecting";
        case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED: return "disconnected";
    }
    return "unknown";
}

static void
osw_drv_frame_tx_desc_report_result(struct osw_drv_frame_tx_desc *desc,
                                    enum osw_drv_frame_tx_desc_state state)
{
    if (desc->result_fn == NULL) return;

    switch (state) {
        case OSW_DRV_FRAME_TX_STATE_UNUSED:
            break;
        case OSW_DRV_FRAME_TX_STATE_PENDING:
            desc->result_fn(desc, OSW_FRAME_TX_RESULT_DROPPED, desc->caller_priv);
            break;
        case OSW_DRV_FRAME_TX_STATE_PUSHED:
            desc->result_fn(desc, OSW_FRAME_TX_RESULT_DROPPED, desc->caller_priv);
            break;
        case OSW_DRV_FRAME_TX_STATE_SUBMITTED:
            desc->result_fn(desc, OSW_FRAME_TX_RESULT_SUBMITTED, desc->caller_priv);
            break;
        case OSW_DRV_FRAME_TX_STATE_FAILED:
            desc->result_fn(desc, OSW_FRAME_TX_RESULT_FAILED, desc->caller_priv);
            break;
    }
}

static void
osw_drv_frame_tx_desc_reset(struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);

    LOGD(LOG_PREFIX_TX_DESC(desc, "resetting"));

    const enum osw_drv_frame_tx_desc_state state = desc->state;

    osw_timer_disarm(&desc->expiry);
    desc->drv = NULL;
    memset(&desc->phy_name, 0, sizeof(desc->phy_name));
    memset(&desc->vif_name, 0, sizeof(desc->vif_name));
    if (desc->frame_len > 0) {
        FREE(desc->frame);
        desc->frame = NULL;
    }
    desc->frame_len = 0;
    desc->state = OSW_DRV_FRAME_TX_STATE_UNUSED;
    if (desc->list != NULL) {
        ds_dlist_remove(desc->list, desc);
        desc->list = NULL;
    }

    osw_drv_frame_tx_desc_report_result(desc, state);
}

static void
osw_drv_set_phy_list_valid(struct osw_drv *drv, bool valid)
{
    if (drv->phy_list_valid == valid) return;

    drv->phy_list_valid = valid;
    g_osw_drv_work_done = true;
    osw_drv_work_all_schedule();
}

static void
osw_drv_phy_set_vif_list_valid(struct osw_drv_phy *phy, bool valid)
{
    if (phy->vif_list_valid == valid) return;

    phy->vif_list_valid = valid;
    g_osw_drv_work_done = true;
    osw_drv_work_all_schedule();
}

static void
osw_drv_vif_set_sta_list_valid(struct osw_drv_vif *vif, bool valid)
{
    if (vif->sta_list_valid == valid) return;

    vif->sta_list_valid = valid;
    g_osw_drv_work_done = true;
    osw_drv_work_all_schedule();
}

static void
osw_drv_obj_set_state(struct osw_drv_obj *obj, enum osw_drv_obj_state state)
{
    if (obj->state == OSW_DRV_OBJ_REQUESTED && state == OSW_DRV_OBJ_INVALID) {
        state = OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED;
    }

    if (obj->state == OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED && state == OSW_DRV_OBJ_VALID) {
        state = OSW_DRV_OBJ_INVALID;
    }

    if (obj->state == state) return;

    obj->state = state;
    g_osw_drv_work_done = true;
    osw_drv_work_all_schedule();
}

static bool
osw_drv_sta_is_settled(struct osw_drv_sta *sta)
{
    return sta->obj.state == OSW_DRV_OBJ_PROCESSED;
}

static int
osw_drv_sta_addr_cmp(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(struct osw_hwaddr));
}

static void
osw_drv_sta_ies_timeout_cb(struct osw_timer *t)
{
    osw_drv_work_all_schedule();
}

static void
osw_drv_sta_ies_hold(struct osw_drv_sta *sta)
{
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OSW_DRV_STA_IES_TIMEOUT_SEC);
    osw_timer_arm_at_nsec(&sta->ies_timeout, at);
}

static struct osw_drv_sta *
osw_drv_sta_alloc(struct osw_drv_vif *vif, const struct osw_hwaddr *mac_addr)
{
    struct osw_drv_sta *sta = CALLOC(1, sizeof(*sta));

    sta->mac_addr = *mac_addr;
    sta->vif = vif;
    osw_timer_init(&sta->ies_timeout, osw_drv_sta_ies_timeout_cb);
    ds_tree_insert(&vif->sta_tree, sta, &sta->mac_addr);
    return sta;
}

static void
osw_drv_sta_free(struct osw_drv_sta *sta)
{
    assert(osw_timer_is_armed(&sta->ies_timeout) == false);
    g_osw_drv_work_done = true;
    osw_drv_buf_free(&sta->cur_ies);
    osw_drv_buf_free(&sta->new_ies);
    ds_tree_remove(&sta->vif->sta_tree, sta);
    FREE(sta);
}

static struct osw_drv_sta *
osw_drv_sta_get(struct osw_drv_vif *vif, const struct osw_hwaddr *mac_addr)
{
    return ds_tree_find(&vif->sta_tree, mac_addr) ?: osw_drv_sta_alloc(vif, mac_addr);
}

static void
osw_drv_sta_assign_state(struct osw_drv_sta_state *dst,
                         const struct osw_drv_sta_state *src)
{
    const struct osw_drv_sta_state zero = {0};

    if (src == NULL)
        src = &zero;

    *dst = *src;
}

static void
osw_drv_sta_set_state(struct osw_drv_sta *sta,
                      const struct osw_drv_sta_state *state)
{
    osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_VALID);
    osw_drv_sta_assign_state(&sta->new_state, state);
}

static bool
osw_drv_sta_request_state_vsta_override(struct osw_drv_sta *sta)
{
    struct osw_drv_vif *vif = sta->vif;
    if (vif->vsta_root_ap == NULL) return false;

    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;
    const bool match = (osw_hwaddr_cmp(vif->vsta_root_ap, &sta->mac_addr) == 0);
    const struct osw_drv_sta_state state = {
        .connected = match,
        .mld_addr = *(match ? &vif->cur_state.u.sta.link.mld_addr : osw_hwaddr_zero()),
    };

    LOGT("osw: drv: %s/%s/%s/"OSW_HWADDR_FMT": match=%d",
         sta->vif->phy->drv->ops->name ?: "",
         sta->vif->phy->phy_name,
         sta->vif->vif_name,
         OSW_HWADDR_ARG(&sta->mac_addr),
         match);

    osw_drv_report_sta_state(drv, phy->phy_name, vif->vif_name, &sta->mac_addr, &state);
    return true;
}

static void
osw_drv_sta_request_state(struct osw_drv_sta *sta)
{
    struct osw_drv_vif *vif = sta->vif;
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;

    if (drv->unregistered == true) {
        struct osw_drv_sta_state state = {0};
        osw_drv_sta_set_state(sta, &state);
        return;
    }

    assert(sta != NULL);
    assert(drv->ops->request_sta_state_fn != NULL);

    osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_REQUESTED);
    if (osw_drv_sta_request_state_vsta_override(sta) == true) return;
    drv->ops->request_sta_state_fn(drv, phy->phy_name, vif->vif_name, &sta->mac_addr);
}

static bool
osw_drv_sta_state_is_changed(const struct osw_drv_sta *sta)
{
    bool changed = false;
    const bool changed_connected = sta->cur_state.connected != sta->new_state.connected;
    const bool changed_key_id = sta->cur_state.key_id != sta->new_state.key_id;
    const bool changed_ies = (osw_drv_buf_is_same(&sta->cur_ies, &sta->new_ies) == false);
    const bool changed_pmf = sta->cur_state.pmf != sta->new_state.pmf;
    const bool changed_akm = sta->cur_state.akm != sta->new_state.akm;
    const bool changed_pairwise_cipher = sta->cur_state.pairwise_cipher != sta->new_state.pairwise_cipher;
    const bool changed_mld_addr = (osw_hwaddr_is_equal(&sta->cur_state.mld_addr, &sta->new_state.mld_addr) == false);

    if (changed_key_id) {
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": key_id: %d -> %d",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             sta->cur_state.key_id,
             sta->new_state.key_id);
    }

    if (changed_pmf) {
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": pmf: %d -> %d",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             sta->cur_state.pmf,
             sta->new_state.pmf);
    }

    if (changed_akm) {
        const char *from = osw_akm_into_cstr(sta->cur_state.akm) ?: "";
        const char *to = osw_akm_into_cstr(sta->new_state.akm) ?: "";
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": akm: %s -> %s",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             from,
             to);
    }

    if (changed_pairwise_cipher) {
        const char *from = osw_cipher_into_cstr(sta->cur_state.pairwise_cipher) ?: "";
        const char *to = osw_cipher_into_cstr(sta->new_state.pairwise_cipher) ?: "";
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": pairwise_cipher: %s -> %s",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             from,
             to);
    }

    if (changed_mld_addr) {
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": mld_addr: " OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             OSW_HWADDR_ARG(&sta->cur_state.mld_addr),
             OSW_HWADDR_ARG(&sta->new_state.mld_addr));
    }

    if (changed_ies) {
        LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": information elements (ies) changed (no payload)",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr));
    }

    changed |= changed_connected;
    changed |= changed_key_id;
    changed |= changed_ies;
    changed |= changed_pmf;
    changed |= changed_akm;
    changed |= changed_pairwise_cipher;
    changed |= changed_mld_addr;

    return changed;
}

static void
osw_drv_sta_process_state(struct osw_drv_sta *sta)
{
    bool added = sta->cur_state.connected == false
              && sta->new_state.connected == true;
    bool removed = sta->cur_state.connected == true
                && sta->new_state.connected == false;
    bool changed = sta->cur_state.connected == true
                && sta->new_state.connected == true
                && osw_drv_sta_state_is_changed(sta) == true;
    const bool changed_ies = (osw_drv_buf_is_same(&sta->cur_ies, &sta->new_ies) == false);
    const time_t now = time_monotonic();

    if (sta->cur_state.connected == true &&
        sta->new_state.connected == true &&
        (sta->cur_state.connected_duration_seconds >
         sta->new_state.connected_duration_seconds)) {
        sta->pub.connected_at = 0;
        added = true;
        removed = true;
    }

    if (added == true) {
        if (sta->vif->cur_state.exists == false) {
            osw_drv_phy_set_vif_list_valid(sta->vif->phy, false);
            return;
        }
    }

    if (removed == true) {
        osw_drv_vif_set_sta_list_valid(sta->vif, false);
    }

    if (sta->new_state.connected == false &&
        osw_timer_is_armed(&sta->ies_timeout)) {
        return;
    }

    osw_drv_assoc_ie_cache_load(sta);
    osw_drv_buf_copy(&sta->cur_ies, &sta->new_ies);
    osw_drv_assoc_ie_cache_store(sta);
    osw_drv_sta_assign_state(&sta->cur_state, &sta->new_state);
    osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_PROCESSED);

    sta->pub.mac_addr = &sta->mac_addr;
    sta->pub.vif = &sta->vif->pub;
    sta->pub.drv_state = &sta->cur_state;
    sta->pub.assoc_req_ies = sta->cur_ies.data;
    sta->pub.assoc_req_ies_len = sta->cur_ies.len;

    if (sta->cur_state.connected_duration_seconds != 0 || sta->pub.connected_at == 0) {
        sta->pub.connected_at = now;
        sta->pub.connected_at -= sta->cur_state.connected_duration_seconds;
    }

    const int in_network_sec = now - sta->pub.connected_at;

    if (removed == true) OSW_STATE_NOTIFY(sta_disconnected_fn, &sta->pub);
    if (added == true) OSW_STATE_NOTIFY(sta_connected_fn, &sta->pub);
    if (changed == true) OSW_STATE_NOTIFY(sta_changed_fn, &sta->pub);

    if (removed == true) {
        LOGN("osw: drv: %s/%s/"OSW_HWADDR_FMT": disconnected (after %d seconds)",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             in_network_sec);
    }

    if (added == true) {
        LOGN("osw: drv: %s/%s/"OSW_HWADDR_FMT": connected keyid=%d pmf=%d akm=%s pairwise_cipher=%s mld="OSW_HWADDR_FMT" (since %d seconds)",
             sta->vif->phy->phy_name,
             sta->vif->vif_name,
             OSW_HWADDR_ARG(&sta->mac_addr),
             sta->cur_state.key_id,
             sta->cur_state.pmf,
             osw_akm_into_cstr(sta->cur_state.akm) ?: "",
             osw_cipher_into_cstr(sta->cur_state.pairwise_cipher) ?: "",
             OSW_HWADDR_ARG(&sta->cur_state.mld_addr),
             in_network_sec);
    }

    if (added == true || changed == true) {
        if (changed_ies == true) {
            LOGI("osw: drv: %s/%s/"OSW_HWADDR_FMT": assoc ies available, len = %zu",
                 sta->vif->phy->phy_name,
                 sta->vif->vif_name,
                 OSW_HWADDR_ARG(&sta->mac_addr),
                 sta->pub.assoc_req_ies_len);
        }
    }
}

static void
osw_drv_sta_work(struct osw_drv_sta *sta)
{
    switch (sta->obj.state) {
        case OSW_DRV_OBJ_INVALID:
            osw_drv_sta_request_state(sta);
            break;
        case OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED:
        case OSW_DRV_OBJ_REQUESTED:
            /* Waiting for driver to call osw_drv_report_sta_state().
             * That'll move the state to VALID.
             */
            break;
        case OSW_DRV_OBJ_VALID:
            osw_drv_sta_process_state(sta);
            break;
        case OSW_DRV_OBJ_PROCESSED:
            if (sta->cur_state.connected == false) {
                osw_drv_sta_free(sta);
                return;
            }
            break;
    }
}

static void
osw_drv_sta_enumerate_cb(const struct osw_hwaddr *mac_addr, void *data)
{
    struct osw_drv_sta *sta = osw_drv_sta_get(data, mac_addr);
    sta->obj.exists = true;
}

static void
osw_drv_sta_enumerate(struct osw_drv_vif *vif)
{
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;
    struct osw_drv_sta *sta;
    const char *drv_name = drv->ops->name ?: "";
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;

    ds_tree_foreach(&vif->sta_tree, sta) {
        sta->obj.existed = sta->obj.exists;
        sta->obj.exists = false;
    }

    if (drv->unregistered == false) {
        drv->ops->get_sta_list_fn(drv, phy_name, vif_name, osw_drv_sta_enumerate_cb, vif);
        if (vif->vsta_root_ap != NULL) {
            LOGT("osw: drv %s/%s/%s: adding extra sta "OSW_HWADDR_FMT,
                 drv_name,
                 phy_name,
                 vif_name,
                 OSW_HWADDR_ARG(vif->vsta_root_ap));
            osw_drv_sta_enumerate_cb(vif->vsta_root_ap, vif);
        }
    }

    ds_tree_foreach(&vif->sta_tree, sta) {
        if (sta->obj.existed != sta->obj.exists) {
            osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_INVALID);
        }
    }
}

static bool
osw_drv_vif_stas_are_settled(struct osw_drv_vif *vif)
{
    struct osw_drv_sta *sta;

    ds_tree_foreach(&vif->sta_tree, sta)
        if (osw_drv_sta_is_settled(sta) == false)
            return false;

    return true;
}

static bool
osw_drv_vif_is_settled(struct osw_drv_vif *vif)
{
    if (osw_drv_vif_stas_are_settled(vif) == false)
        return false;

    return vif->obj.state == OSW_DRV_OBJ_PROCESSED;
}

static void
osw_drv_vif_assert_unique(const char *vif_name)
{
    struct osw_drv_phy *phy;
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        ds_tree_foreach(&drv->phy_tree, phy) {
            assert(ds_tree_find(&phy->vif_tree, vif_name) == NULL);
        }
    }
}

static void
osw_drv_vif_resync_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_vif *vif = container_of(arg, struct osw_drv_vif, resync);
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;

    LOGD("osw: drv: %s/%s: re-syncing state",
         phy->phy_name, vif->vif_name);

    osw_drv_report_vif_changed(drv, phy->phy_name, vif->vif_name);
}

static void
osw_drv_set_resync(struct osw_drv_vif *vif)
{
    struct osw_drv_phy *phy = vif->phy;

    LOGD("osw: drv: %s/%s: scheduling state re-sync",
         phy->phy_name, vif->vif_name);

    ev_timer_stop(EV_DEFAULT_ &vif->resync);
    ev_timer_set(&vif->resync, OSW_DRV_RESYNC_SECONDS, 0);
    ev_timer_start(EV_DEFAULT_ &vif->resync);
}

static void
osw_drv_vif_chan_sync_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_vif *vif = container_of(arg, struct osw_drv_vif, chan_sync);
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;

    LOGD("osw: drv: %s/%s: syncing channel state",
         phy->phy_name, vif->vif_name);

    osw_drv_report_vif_changed(drv, phy->phy_name, vif->vif_name);
    osw_drv_report_phy_changed(drv, phy->phy_name);
}

void
osw_drv_set_chan_sync(struct osw_drv *drv, const struct osw_drv_conf *conf)
{
    /* FIXME: Drivers should advertise capability to skip
     * this to avoid needless work if they really can report
     * proper channel state updates on VIFs.
     */
    size_t i;
    for (i = 0; i < conf->n_phy_list; i++) {
        const struct osw_drv_phy_config *cphy = &conf->phy_list[i];
        struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, cphy->phy_name);
        if (WARN_ON(phy == NULL)) continue;

        size_t j;
        for (j = 0; j < cphy->vif_list.count; j++) {
            const struct osw_drv_vif_config *cvif = &cphy->vif_list.list[j];
            struct osw_drv_vif *vif = ds_tree_find(&phy->vif_tree, cvif->vif_name);
            if (WARN_ON(vif == NULL)) continue;
            if (cvif->vif_type != OSW_VIF_AP) continue;
            if (cvif->u.ap.channel_changed == false) continue;

            LOGD("osw: drv: %s/%s: scheduling channel state sync",
                 phy->phy_name, vif->vif_name);

            ev_timer_stop(EV_DEFAULT_ &vif->chan_sync);
            ev_timer_set(&vif->chan_sync, OSW_DRV_CHAN_SYNC_SECONDS, 0);
            ev_timer_start(EV_DEFAULT_ &vif->chan_sync);
        }
    }
}

static void
osw_drv_vif_recent_channel_timeout_cb(struct osw_timer *t)
{
    /* nop */
}

static struct osw_drv_vif *
osw_drv_vif_alloc(struct osw_drv_phy *phy, const char *vif_name)
{
    struct osw_drv_vif *vif = CALLOC(1, sizeof(*vif));

    osw_drv_vif_assert_unique(vif_name);
    vif->vif_name = STRDUP(vif_name);
    vif->phy = phy;
    osw_timer_init(&vif->recent_channel_timeout, osw_drv_vif_recent_channel_timeout_cb);
    ev_timer_init(&vif->chan_sync, osw_drv_vif_chan_sync_cb, 0, 0);
    ev_timer_init(&vif->resync, osw_drv_vif_resync_cb, 0, 0);
    ds_tree_init(&vif->sta_tree, osw_drv_sta_addr_cmp, struct osw_drv_sta, node);
    ds_tree_insert(&phy->vif_tree, vif, vif->vif_name);
    return vif;
}

static void
osw_drv_vif_free(struct osw_drv_vif *vif)
{
    g_osw_drv_work_done = true;
    osw_timer_disarm(&vif->recent_channel_timeout);
    ev_timer_stop(EV_DEFAULT_ &vif->chan_sync);
    ev_timer_stop(EV_DEFAULT_ &vif->resync);
    ds_tree_remove(&vif->phy->vif_tree, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static struct osw_drv_vif *
osw_drv_vif_get(struct osw_drv_phy *phy, const char *vif_name)
{
    return ds_tree_find(&phy->vif_tree, vif_name) ?: osw_drv_vif_alloc(phy, vif_name);
}

static void
osw_drv_vif_assign_state(struct osw_drv_vif_state *dst,
                         const struct osw_drv_vif_state *src)
{
    const struct osw_drv_vif_state zero = {0};
    const struct osw_drv_vif_state_ap *ap_src = &src->u.ap;
    const struct osw_drv_vif_state_ap_vlan *ap_vlan_src = &src->u.ap_vlan;
    const struct osw_drv_vif_state_sta *sta_src = &src->u.sta;
    struct osw_drv_vif_state_ap *ap_dst = &dst->u.ap;
    struct osw_drv_vif_state_ap_vlan *ap_vlan_dst = &dst->u.ap_vlan;
    struct osw_drv_vif_state_sta *sta_dst = &dst->u.sta;

    if (src == NULL)
       src = &zero;

    switch (dst->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            FREE(ap_dst->acl.list);
            FREE(ap_dst->psk_list.list);
            osw_radius_list_free(&ap_dst->radius_list);
            osw_radius_list_free(&ap_dst->acct_list);
            FREE(ap_dst->neigh_list.list);
            FREE(ap_dst->neigh_ft_list.list);
            osw_passpoint_free_internal(&ap_dst->passpoint);
            break;
        case OSW_VIF_AP_VLAN:
            FREE(ap_vlan_dst->sta_addrs.list);
            break;
        case OSW_VIF_STA:
            while (sta_dst->network != NULL) {
                struct osw_drv_vif_sta_network *n = sta_dst->network->next;
                FREE(sta_dst->network);
                sta_dst->network = n;
            }
            break;
    }

    *dst = *src;

    switch (dst->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            ARRDUP(ap_src, ap_dst, acl.list, acl.count);
            ARRDUP(ap_src, ap_dst, psk_list.list, psk_list.count);
            osw_radius_list_copy(&ap_src->radius_list, &ap_dst->radius_list);
            osw_radius_list_copy(&ap_src->acct_list, &ap_dst->acct_list);
            ARRDUP(ap_src, ap_dst, neigh_list.list, neigh_list.count);
            ARRDUP(ap_src, ap_dst, neigh_ft_list.list, neigh_ft_list.count);
            osw_passpoint_copy(&ap_src->passpoint, &ap_dst->passpoint);
            break;
        case OSW_VIF_AP_VLAN:
            ARRDUP(ap_vlan_src, ap_vlan_dst, sta_addrs.list, sta_addrs.count);
            break;
        case OSW_VIF_STA:
            {
                struct osw_drv_vif_sta_network **p = &sta_dst->network;
                struct osw_drv_vif_sta_network *i;
                for (i = sta_src->network; i != NULL; i = i->next) {
                    *p = CALLOC(1, sizeof(**p));
                    memcpy(*p, i, sizeof(*i));
                    p = &(*p)->next;
                    *p = NULL;
                }
            }
            break;
    }

}

static void
osw_drv_vif_set_state(struct osw_drv_vif *vif,
                      const struct osw_drv_vif_state *state)
{
    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_VALID);
    osw_drv_vif_assign_state(&vif->new_state, state);
}

static void
osw_drv_vif_request_state(struct osw_drv_vif *vif)
{
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;

    if (drv->unregistered == true) {
        struct osw_drv_vif_state state = {0};
        osw_drv_vif_set_state(vif, &state);
        return;
    }

    assert(vif != NULL);
    assert(drv->unregistered == false);
    assert(drv->ops->request_vif_state_fn != NULL);

    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_REQUESTED);
    drv->ops->request_vif_state_fn(drv, phy->phy_name, vif->vif_name);
}

static bool
osw_drv_vif_has_ap_channel_changed(const struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state *o = &vif->cur_state;
    const struct osw_drv_vif_state *n = &vif->new_state;
    const size_t size = sizeof(n->u.ap.channel);
    const bool was_ap = (o->vif_type == OSW_VIF_AP);
    const bool still_is_ap = (n->vif_type == OSW_VIF_AP);
    const bool channel_changed = (memcmp(&o->u.ap.channel, &n->u.ap.channel, size) != 0);
    return (was_ap && still_is_ap && channel_changed);
}

static bool
osw_drv_vsta_has_channel_changed(const struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state *o = &vif->cur_state;
    const struct osw_drv_vif_state *n = &vif->new_state;
    const size_t size = sizeof(n->u.sta.link.channel);
    const bool was_sta = (o->vif_type == OSW_VIF_STA);
    const bool still_is_sta = (n->vif_type == OSW_VIF_STA);
    const bool was_connected = (o->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    const bool is_connected = (n->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
    const bool channel_changed = (memcmp(&o->u.sta.link.channel, &n->u.sta.link.channel, size) != 0);
    return (was_sta && still_is_sta && was_connected && is_connected && channel_changed);
}

static void
osw_drv_vif_set_recent_channel(struct osw_drv_vif *vif,
                               const struct osw_channel *c)
{
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OSW_DRV_VIF_RECENT_CHANNEL_TIMEOUT_SEC);
    LOGD("osw: drv: %s/%s/%s: recent_channel: "OSW_CHANNEL_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_CHANNEL_ARG(c));
    vif->recent_channel = *c;
    osw_timer_arm_at_nsec(&vif->recent_channel_timeout, at);
}

static void
osw_drv_debug_passpoint_list(const struct osw_drv_vif *vif,
                             char **const o, const size_t o_len,
                             char **const n, const size_t n_len, char name[])
{
    size_t i, len;
    if (osw_passpoint_str_list_is_equal(o, n, o_len, n_len)) return;

    len = (o_len > n_len)
          ? o_len
          : n_len;

    for (i = 0; i < len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: %s[%zu]: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             name, i,
             (o_len > i) ? o[i] : "",
             (n_len > i) ? n[i] : "");
    }
}

static void
osw_drv_debug_passpoint_list_int(const struct osw_drv_vif *vif,
                                 const int *o, const size_t o_len,
                                 const int *n, const size_t n_len, char name[])
{
    size_t i, len;
    if (o_len == n_len && memcmp(o, n, sizeof(*o) * o_len) == 0) return;

    len = (o_len > n_len)
          ? o_len
          : n_len;

    for (i = 0; i < len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: %s[%zu]: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             name, i,
             (o_len > i) ? o[i] : 0,
             (n_len > i) ? n[i] : 0);
    }
}

static void
osw_drv_vif_dump_neigh_ft(const struct osw_drv_vif *vif)
{
    const struct osw_neigh_ft_list *o = &vif->cur_state.u.ap.neigh_ft_list;
    size_t i;

    for (i = 0; i < o->count; i++) {
        const struct osw_neigh_ft *oi = &o->list[i];
        LOGI("osw: drv: %s/%s/%s: neigh_ft: "OSW_NEIGH_FT_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_NEIGH_FT_ARG(oi));
    }
}

static void
osw_drv_vif_changed_neigh_ft(const struct osw_drv_vif *vif)
{
    const struct osw_neigh_ft_list *o = &vif->cur_state.u.ap.neigh_ft_list;
    const struct osw_neigh_ft_list *n = &vif->new_state.u.ap.neigh_ft_list;
    size_t i;

    for (i = 0; i < o->count; i++) {
        const struct osw_neigh_ft *oi = &o->list[i];
        const struct osw_neigh_ft *ni = osw_neigh_ft_list_lookup(n, &oi->bssid);
        const bool removed = (ni == NULL);
        if (removed) {
            LOGI("osw: drv: %s/%s/%s: neigh_ft: "OSW_NEIGH_FT_FMT": removed",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_NEIGH_FT_ARG(oi));
        }
    }

    for (i = 0; i < n->count; i++) {
        const struct osw_neigh_ft *ni = &n->list[i];
        const struct osw_neigh_ft *oi = osw_neigh_ft_list_lookup(o, &ni->bssid);
        const bool added = (oi == NULL);
        const bool changed = (oi != NULL)
                          && (osw_neigh_ft_cmp(oi, ni) != 0);
        if (added) {
            LOGI("osw: drv: %s/%s/%s: neigh_ft: "OSW_NEIGH_FT_FMT": added",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_NEIGH_FT_ARG(ni));
        }
        if (changed) {
            LOGI("osw: drv: %s/%s/%s: neigh_ft: "OSW_NEIGH_FT_FMT" -> "OSW_NEIGH_FT_FMT,
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_NEIGH_FT_ARG(oi),
                 OSW_NEIGH_FT_ARG(ni));
        }
    }
}

static bool
osw_drv_vif_state_is_changed_ap(const struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state_ap *o = &vif->cur_state.u.ap;
    const struct osw_drv_vif_state_ap *n = &vif->new_state.u.ap;

    bool changed = false;
    const bool changed_bridge = strcmp(o->bridge_if_name.buf, n->bridge_if_name.buf) != 0;
    const bool changed_nas_id = strcmp(o->nas_identifier.buf, n->nas_identifier.buf) != 0;
    const bool changed_ft_encr_key = strcmp(o->ft_encr_key.buf, n->ft_encr_key.buf) != 0;
    const bool changed_isolated = o->isolated != n->isolated;
    const bool changed_ssid_hidden = o->ssid_hidden != n->ssid_hidden;
    const bool changed_mcast2ucast = o->mcast2ucast != n->mcast2ucast;
    const bool changed_beacon_interval_tu = o->beacon_interval_tu != n->beacon_interval_tu;
    const bool changed_channel = memcmp(&o->channel, &n->channel, sizeof(o->channel));
    const bool changed_mode = memcmp(&o->mode, &n->mode, sizeof(o->mode));
    const bool changed_wpa = memcmp(&o->wpa, &n->wpa, sizeof(o->wpa));
    const bool changed_ssid = memcmp(&o->ssid, &n->ssid, sizeof(o->ssid));
    const bool changed_acl_policy = o->acl_policy != n->acl_policy;
    const bool changed_wps_pbc = o->wps_pbc != n->wps_pbc;
    const bool changed_multi_ap = memcmp(&o->multi_ap, &n->multi_ap, sizeof(o->multi_ap));
    const bool changed_mld_addr = (osw_hwaddr_is_equal(&o->mld.addr, &n->mld.addr) == false);
    const bool changed_mld_if_name = (strcmp(o->mld.if_name.buf, n->mld.if_name.buf) != 0);
    const bool changed_mbss_mode = o->mbss_mode != n->mbss_mode;
    const bool changed_mbss_group = o->mbss_group != n->mbss_group;
    const bool changed_radius = (osw_radius_list_is_equal(&o->radius_list, &n->radius_list) == false);
    const bool changed_accounting = (osw_radius_list_is_equal(&o->acct_list, &n->acct_list) == false);
    const bool changed_passpoint = (osw_passpoint_is_equal(&o->passpoint, &n->passpoint) == false);
    const bool changed_ft_over_ds = o->ft_over_ds != n->ft_over_ds;
    const bool changed_ft_pmk_r1_push = o->ft_pmk_r1_push != n->ft_pmk_r1_push;
    const bool changed_ft_psk_generate_local = o->ft_psk_generate_local != n->ft_psk_generate_local;
    const bool changed_ft_pmk_r0_key_lifetime_sec = o->ft_pmk_r0_key_lifetime_sec != n->ft_pmk_r0_key_lifetime_sec;
    const bool changed_ft_pmk_r1_max_key_lifetime_sec = o->ft_pmk_r1_max_key_lifetime_sec != n->ft_pmk_r1_max_key_lifetime_sec;
    const bool changed_ft_mobility_domain = o->ft_mobility_domain != n->ft_mobility_domain;
    const bool changed_neigh_ft = osw_neigh_ft_list_cmp(&o->neigh_ft_list, &n->neigh_ft_list);
    const bool changed_mbo = o->mbo != n->mbo;
    const bool changed_oce = o->oce != n->oce;
    const bool changed_oce_min_rssi_enable = o->oce_min_rssi_enable != n->oce_min_rssi_enable;
    const bool changed_oce_min_rssi_dbm = o->oce_min_rssi_dbm != n->oce_min_rssi_dbm;
    const bool changed_oce_retry_delay_sec = o->oce_retry_delay_sec != n->oce_retry_delay_sec;
    const bool changed_max_sta = o->max_sta != n->max_sta;

    bool changed_acl = false;
    bool changed_psk = false;
    bool changed_neigh = false;
    bool changed_wps_cred_list = false;

    {
        size_t i;
        size_t j;
        size_t a = 0;
        size_t b = 0;

        for (i = 0; i < o->acl.count; i++)
            a++;
        for (i = 0; i < n->acl.count; i++)
            b++;

        if (a == b) {
            for (i = 0; i < o->acl.count; i++) {
                for (j = 0; j < n->acl.count; j++) {
                    const struct osw_hwaddr *x = &o->acl.list[i];
                    const struct osw_hwaddr *y = &n->acl.list[j];
                    if (memcmp(x, y, sizeof(*x)) == 0)
                        break;
                }
                if (j == n->acl.count) {
                    changed_acl = true;
                    break;
                }
            }
        }
        else {
            changed_acl = true;
        }
    }

    {
        size_t i;
        size_t j;
        size_t a = 0;
        size_t b = 0;

        for (i = 0; i < o->psk_list.count; i++)
            a++;
        for (i = 0; i < n->psk_list.count; i++)
            b++;

        if (a == b) {
            for (i = 0; i < o->psk_list.count; i++) {
                for (j = 0; j < n->psk_list.count; j++) {
                    const struct osw_ap_psk *x = &o->psk_list.list[i];
                    const struct osw_ap_psk *y = &n->psk_list.list[j];
                    if (osw_ap_psk_is_same(x, y))
                        break;
                }
                if (j == n->psk_list.count) {
                    changed_psk = true;
                    break;
                }
            }
        }
        else {
            changed_psk = true;
        }
    }

    {
        size_t i;
        size_t j;

        if (o->neigh_list.count == n->neigh_list.count) {
            for (i = 0; i < o->neigh_list.count; i++) {
                for (j = 0; j < n->neigh_list.count; j++) {
                    const struct osw_neigh *x = &o->neigh_list.list[i];
                    const struct osw_neigh *y = &n->neigh_list.list[j];
                    const bool is_same = (memcmp(x, y, sizeof(*x)) == 0);
                    if (is_same)
                        break;
                }
                if (j == n->neigh_list.count) {
                    changed_neigh = true;
                    break;
                }
            }
        }
        else {
            changed_neigh = true;
        }
    }

    {
        size_t i;
        size_t j;

        if (o->wps_cred_list.count == n->wps_cred_list.count) {
            for (i = 0; i < o->wps_cred_list.count; i++) {
                for (j = 0; j < n->wps_cred_list.count; j++) {
                    const struct osw_wps_cred *x = &o->wps_cred_list.list[i];
                    const struct osw_wps_cred *y = &n->wps_cred_list.list[j];
                    const bool is_same = (memcmp(x, y, sizeof(*x)) == 0);
                    if (is_same)
                        break;
                }
                if (j == n->wps_cred_list.count) {
                    changed_wps_cred_list = true;
                    break;
                }
            }
        }
        else {
            changed_wps_cred_list = true;
        }
    }

    changed |= changed_bridge;
    changed |= changed_nas_id;
    changed |= changed_ft_encr_key;
    changed |= changed_isolated;
    changed |= changed_ssid_hidden;
    changed |= changed_mcast2ucast;
    changed |= changed_beacon_interval_tu;
    changed |= changed_channel;
    changed |= changed_mode;
    changed |= changed_wpa;
    changed |= changed_ssid;
    changed |= changed_acl_policy;
    changed |= changed_wps_pbc;
    changed |= changed_multi_ap;
    changed |= changed_mld_addr;
    changed |= changed_mld_if_name;
    changed |= changed_mbss_mode;
    changed |= changed_mbss_group;
    changed |= changed_acl;
    changed |= changed_psk;
    changed |= changed_neigh;
    changed |= changed_neigh_ft;
    changed |= changed_wps_cred_list;
    changed |= changed_radius;
    changed |= changed_accounting;
    changed |= changed_passpoint;
    changed |= changed_ft_over_ds;
    changed |= changed_ft_pmk_r1_push;
    changed |= changed_ft_psk_generate_local;
    changed |= changed_ft_pmk_r0_key_lifetime_sec;
    changed |= changed_ft_pmk_r1_max_key_lifetime_sec;
    changed |= changed_ft_mobility_domain;
    changed |= changed_mbo;
    changed |= changed_oce;
    changed |= changed_oce_min_rssi_enable;
    changed |= changed_oce_min_rssi_dbm;
    changed |= changed_oce_retry_delay_sec;
    changed |= changed_max_sta;

    if (changed_bridge) {
        const int max = ARRAY_SIZE(o->bridge_if_name.buf);
        LOGI("osw: drv: %s/%s/%s: bridge: '%.*s' -> '%.*s'",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             max, o->bridge_if_name.buf,
             max, n->bridge_if_name.buf);
    }

    if (changed_nas_id) {
        const int max = ARRAY_SIZE(o->nas_identifier.buf);
        LOGI("osw: drv: %s/%s/%s: nas_identifier: '%.*s' -> '%.*s'",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             max, o->nas_identifier.buf,
             max, n->nas_identifier.buf);
    }

    if (changed_ft_encr_key) {
        LOGI("osw: drv: %s/%s/%s: ft_encr_key: "OSW_FT_ENCR_KEY_FMT" -> "OSW_FT_ENCR_KEY_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_FT_ENCR_KEY_ARG(&o->ft_encr_key),
             OSW_FT_ENCR_KEY_ARG(&n->ft_encr_key));
    }

    if (changed_mbo) {
        LOGI("osw: drv: %s/%s/%s: mbo: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->mbo,
             n->mbo);
    }

    if (changed_oce) {
        LOGI("osw: drv: %s/%s/%s: oce: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->oce,
             n->oce);
    }

    if (changed_oce_min_rssi_dbm) {
        LOGI("osw: drv: %s/%s/%s: oce_min_rssi_dbm: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->oce_min_rssi_dbm,
             n->oce_min_rssi_dbm);
    }

    if (changed_oce_min_rssi_enable) {
        LOGI("osw: drv: %s/%s/%s: oce_min_rssi_enable: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->oce_min_rssi_enable,
             n->oce_min_rssi_enable);
    }

    if (changed_oce_retry_delay_sec) {
        LOGI("osw: drv: %s/%s/%s: oce_retry_delay_sec: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->oce_retry_delay_sec,
             n->oce_retry_delay_sec);
    }

    if (changed_max_sta) {
        LOGI("osw: drv: %s/%s/%s: max_sta: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->max_sta,
             n->max_sta);
    }

    if (changed_isolated) {
        LOGI("osw: drv: %s/%s/%s: isolated: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->isolated,
             n->isolated);
    }

    if (changed_ssid_hidden) {
        LOGI("osw: drv: %s/%s/%s: ssid_hidden: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ssid_hidden,
             n->ssid_hidden);
    }

    if (changed_mcast2ucast) {
        LOGI("osw: drv: %s/%s/%s: mcast2ucast: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->mcast2ucast,
             n->mcast2ucast);
    }

    if (changed_beacon_interval_tu) {
        LOGI("osw: drv: %s/%s/%s: beacon_interval_tu: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->beacon_interval_tu,
             n->beacon_interval_tu);
    }

    if (changed_acl_policy) {
        const char *from = osw_acl_policy_to_str(o->acl_policy);
        const char *to = osw_acl_policy_to_str(n->acl_policy);
        LOGI("osw: drv: %s/%s/%s: acl_policy: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_wps_pbc) {
        LOGI("osw: drv: %s/%s/%s: wps_pbc: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->wps_pbc,
             n->wps_pbc);
    }

    if (changed_ssid == true) {
        LOGI("osw: drv: %s/%s/%s: ssid: "OSW_SSID_FMT" -> "OSW_SSID_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_SSID_ARG(&o->ssid),
             OSW_SSID_ARG(&n->ssid));
    }

    if (changed_channel == true) {
        LOGI("osw: drv: %s/%s/%s: channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_CHANNEL_ARG(&o->channel),
             OSW_CHANNEL_ARG(&n->channel));
    }

    if (changed_wpa) {
        char from[128];
        char to[128];
        osw_wpa_to_str(from, sizeof(from), &o->wpa);
        osw_wpa_to_str(to, sizeof(to), &n->wpa);
        LOGI("osw: drv: %s/%s/%s: wpa: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_mode) {
        char from[128];
        char to[128];
        osw_ap_mode_to_str(from, sizeof(from), &o->mode);
        osw_ap_mode_to_str(to, sizeof(to), &n->mode);
        LOGI("osw: drv: %s/%s/%s: mode: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_psk) {
        char from[1024];
        char to[1024];
        osw_ap_psk_list_to_str(from, sizeof(from), &o->psk_list);
        osw_ap_psk_list_to_str(to, sizeof(to), &n->psk_list);
        LOGI("osw: drv: %s/%s/%s: psk: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_acl) {
        size_t i;

        for (i = 0; i < o->acl.count; i++) {
            const struct osw_hwaddr *mac = &o->acl.list[i];
            const bool found = osw_hwaddr_list_contains(n->acl.list,
                                                        n->acl.count,
                                                        mac);
            const bool entry_disappeared = (found == false);
            if (entry_disappeared) {
                LOGI("osw: drv: %s/%s/%s: acl: removing: "OSW_HWADDR_FMT,
                        vif->phy->drv->ops->name,
                        vif->phy->phy_name,
                        vif->vif_name,
                        OSW_HWADDR_ARG(mac));
            }
        }

        for (i = 0; i < n->acl.count; i++) {
            const struct osw_hwaddr *mac = &n->acl.list[i];
            const bool found = osw_hwaddr_list_contains(o->acl.list,
                                                        o->acl.count,
                                                        mac);
            const bool entry_appeared = (found == false);
            if (entry_appeared) {
                LOGI("osw: drv: %s/%s/%s: acl: adding: "OSW_HWADDR_FMT,
                        vif->phy->drv->ops->name,
                        vif->phy->phy_name,
                        vif->vif_name,
                        OSW_HWADDR_ARG(mac));
            }
        }
    }

    if (changed_neigh) {
        char from[1024];
        char to[1024];
        osw_neigh_list_to_str(from, sizeof(from), &o->neigh_list);
        osw_neigh_list_to_str(to, sizeof(to), &n->neigh_list);
        LOGI("osw: drv: %s/%s/%s: neigh: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_neigh_ft) {
        osw_drv_vif_changed_neigh_ft(vif);
    }

    if (changed_wps_cred_list) {
        char from[1024];
        char to[1024];
        osw_wps_cred_list_to_str(from, sizeof(from), &o->wps_cred_list);
        osw_wps_cred_list_to_str(to, sizeof(to), &n->wps_cred_list);
        LOGI("osw: drv: %s/%s/%s: wps_cred_list: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_multi_ap) {
        char *from = osw_multi_ap_into_str(&o->multi_ap);
        char *to = osw_multi_ap_into_str(&n->multi_ap);
        LOGI("osw: drv: %s/%s/%s: multi_ap: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
        FREE(from);
        FREE(to);
    }

    if (changed_mld_addr) {
        LOGI("osw: drv: %s/%s/%s: mld.addr: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_HWADDR_ARG(&o->mld.addr),
             OSW_HWADDR_ARG(&n->mld.addr));
    }

    if (changed_mld_if_name) {
        LOGI("osw: drv: %s/%s/%s: mld.if_name: "OSW_IFNAME_FMT" -> "OSW_IFNAME_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_IFNAME_ARG(&o->mld.if_name),
             OSW_IFNAME_ARG(&n->mld.if_name));
    }

    if (changed_mbss_mode) {
        const char *from = osw_mbss_vif_ap_mode_to_str(o->mbss_mode);
        const char *to = osw_mbss_vif_ap_mode_to_str(n->mbss_mode);
        LOGI("osw: drv: %s/%s/%s: mbss_mode: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_mbss_group) {
        LOGI("osw: drv: %s/%s/%s: mbss_group: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->mbss_group,
             n->mbss_group);
    }

    if (changed_passpoint) {
        if (o->passpoint.hs20_enabled != n->passpoint.hs20_enabled)
            LOGI("osw: drv: %s/%s/%s: passpoint: hs20_enabled: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.hs20_enabled,
                 n->passpoint.hs20_enabled);
        if (o->passpoint.adv_wan_status != n->passpoint.adv_wan_status)
            LOGI("osw: drv: %s/%s/%s: passpoint: adv_wan_status: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.adv_wan_status,
                 n->passpoint.adv_wan_status);
        if (o->passpoint.adv_wan_symmetric != n->passpoint.adv_wan_symmetric)
            LOGI("osw: drv: %s/%s/%s: passpoint: adv_wan_symmetric: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.adv_wan_symmetric,
                 n->passpoint.adv_wan_symmetric);
        if (o->passpoint.adv_wan_at_capacity != n->passpoint.adv_wan_at_capacity)
            LOGI("osw: drv: %s/%s/%s: passpoint: adv_wan_at_capacity: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.adv_wan_at_capacity,
                 n->passpoint.adv_wan_at_capacity);
        if (o->passpoint.osen != n->passpoint.osen)
            LOGI("osw: drv: %s/%s/%s: passpoint: osen: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.osen,
                 n->passpoint.osen);
        if (o->passpoint.asra != n->passpoint.asra)
            LOGI("osw: drv: %s/%s/%s: passpoint: asra: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.asra,
                 n->passpoint.asra);
        if (o->passpoint.ant != n->passpoint.ant)
            LOGI("osw: drv: %s/%s/%s: passpoint: ant: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.ant,
                 n->passpoint.ant);
        if (o->passpoint.venue_group != n->passpoint.venue_group)
            LOGI("osw: drv: %s/%s/%s: passpoint: venue_group: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.venue_group,
                 n->passpoint.venue_group);
        if (o->passpoint.venue_type != n->passpoint.venue_type)
            LOGI("osw: drv: %s/%s/%s: passpoint: venue_type: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.venue_type,
                 n->passpoint.venue_type);
        if (o->passpoint.anqp_domain_id != n->passpoint.anqp_domain_id)
            LOGI("osw: drv: %s/%s/%s: passpoint: anqp_domain_id: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.anqp_domain_id,
                 n->passpoint.anqp_domain_id);
        if (o->passpoint.pps_mo_id != n->passpoint.pps_mo_id)
            LOGI("osw: drv: %s/%s/%s: passpoint: pps_mo_id: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.pps_mo_id,
                 n->passpoint.pps_mo_id);
        if (o->passpoint.t_c_timestamp != n->passpoint.t_c_timestamp)
            LOGI("osw: drv: %s/%s/%s: passpoint: t_c_timestamp: %d -> %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.t_c_timestamp,
                 n->passpoint.t_c_timestamp);
        if (STRSCMP(o->passpoint.t_c_filename, n->passpoint.t_c_filename) != 0)
            LOGI("osw: drv: %s/%s/%s: passpoint: t_c_filename: %s -> %s",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.t_c_filename,
                 n->passpoint.t_c_filename);
        if (STRSCMP(o->passpoint.anqp_elem, n->passpoint.anqp_elem) != 0)
            LOGI("osw: drv: %s/%s/%s: passpoint: anqp_elem: %s -> %s",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.anqp_elem,
                 n->passpoint.anqp_elem);
        if (!osw_hwaddr_is_equal(&o->passpoint.hessid, &n->passpoint.hessid))
            LOGI("osw: drv: %s/%s/%s: passpoint: hessid: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT"",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_HWADDR_ARG(&o->passpoint.hessid),
                 OSW_HWADDR_ARG(&n->passpoint.hessid));
        if (osw_ssid_cmp(&o->passpoint.osu_ssid, &n->passpoint.osu_ssid) != 0)
            LOGI("osw: drv: %s/%s/%s: passpoint: osu_ssid: %s -> %s",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 o->passpoint.osu_ssid.buf,
                 n->passpoint.osu_ssid.buf);

        osw_drv_debug_passpoint_list(vif, o->passpoint.domain_list, o->passpoint.domain_list_len,
                                     n->passpoint.domain_list, n->passpoint.domain_list_len,
                                     "domain_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.nairealm_list, o->passpoint.nairealm_list_len,
                                     n->passpoint.nairealm_list, n->passpoint.nairealm_list_len,
                                     "nairealm_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.roamc_list, o->passpoint.roamc_list_len,
                                     n->passpoint.roamc_list, n->passpoint.roamc_list_len,
                                     "roamc_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.oper_fname_list, o->passpoint.oper_fname_list_len,
                                     n->passpoint.oper_fname_list, n->passpoint.oper_fname_list_len,
                                     "oper_fname_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.venue_name_list, o->passpoint.venue_name_list_len,
                                     n->passpoint.venue_name_list, n->passpoint.venue_name_list_len,
                                     "venue_name_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.venue_url_list, o->passpoint.venue_url_list_len,
                                     n->passpoint.venue_url_list, n->passpoint.venue_url_list_len,
                                     "venue_url_list");
        osw_drv_debug_passpoint_list(vif, o->passpoint.list_3gpp_list, o->passpoint.list_3gpp_list_len,
                                     n->passpoint.list_3gpp_list, n->passpoint.list_3gpp_list_len,
                                     "list_3gpp_list");
        osw_drv_debug_passpoint_list_int(vif, o->passpoint.net_auth_type_list, o->passpoint.net_auth_type_list_len,
                                         n->passpoint.net_auth_type_list, n->passpoint.net_auth_type_list_len,
                                         "net_auth_type_list");
    }

    if (changed_ft_over_ds) {
        LOGI("osw: drv: %s/%s/%s: ft_over_ds: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_over_ds,
             n->ft_over_ds);
    }

    if (changed_ft_pmk_r0_key_lifetime_sec) {
        LOGI("osw: drv: %s/%s/%s: ft_pmk_r0_key_lifetime_sec: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_pmk_r0_key_lifetime_sec,
             n->ft_pmk_r0_key_lifetime_sec);
    }

    if (changed_ft_pmk_r1_max_key_lifetime_sec) {
        LOGI("osw: drv: %s/%s/%s: ft_pmk_r1_max_key_lifetime_sec: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_pmk_r1_max_key_lifetime_sec,
             n->ft_pmk_r1_max_key_lifetime_sec);
    }

    if (changed_ft_pmk_r1_push) {
        LOGI("osw: drv: %s/%s/%s: ft_pmk_r1_push: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_pmk_r1_push,
             n->ft_pmk_r1_push);
    }

    if (changed_ft_psk_generate_local) {
        LOGI("osw: drv: %s/%s/%s: ft_psk_generate_local: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_psk_generate_local,
             n->ft_psk_generate_local);
    }

    if (changed_ft_mobility_domain) {
        LOGI("osw: drv: %s/%s/%s: ft_mobility_domain: %d -> %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             o->ft_mobility_domain,
             n->ft_mobility_domain);
    }

    if (changed_radius) {
        char from[512];
        char to[512];
        osw_radius_list_to_str(from, sizeof(from), &o->radius_list);
        osw_radius_list_to_str(to, sizeof(to), &n->radius_list);
        LOGI("osw: drv: %s/%s: radius_list: %s -> %s",
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_accounting) {
        char from[512];
        char to[512];
        osw_radius_list_to_str(from, sizeof(from), &o->acct_list);
        osw_radius_list_to_str(to, sizeof(to), &n->acct_list);
        LOGI("osw: drv: %s/%s: acct_list: %s -> %s",
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    return changed;
}

static bool
osw_drv_vif_state_is_changed_ap_vlan(const struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state_ap_vlan *o = &vif->cur_state.u.ap_vlan;
    const struct osw_drv_vif_state_ap_vlan *n = &vif->new_state.u.ap_vlan;

    const bool changed_sta_addrs = (osw_hwaddr_list_is_equal(&o->sta_addrs,
                                                             &n->sta_addrs) == false);
    const bool changed = false
                       | changed_sta_addrs;

    if (changed_sta_addrs) {
        char from[1024];
        char to[1024];
        osw_hwaddr_list_to_str(from, sizeof(from), &o->sta_addrs);
        osw_hwaddr_list_to_str(to, sizeof(to), &n->sta_addrs);
        LOGI("osw: drv: %s/%s/%s: sta_addrs: %s -> %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    return changed;
}

static int
osw_drv_vif_sta_network_cmp(const struct osw_drv_vif_sta_network *a,
                            const struct osw_drv_vif_sta_network *b)
{
    int r;

    r = strncmp(a->bridge_if_name.buf,
                b->bridge_if_name.buf,
                sizeof(a->bridge_if_name.buf));
    if (r != 0) return r;

    r = osw_hwaddr_cmp(&a->bssid, &b->bssid);
    if (r != 0) return r;

    r = a->ssid.len - b->ssid.len;
    if (r != 0) return r;

    r = memcmp(a->ssid.buf, b->ssid.buf, a->ssid.len);
    if (r != 0) return r;

    r = strncmp(a->psk.str, b->psk.str, sizeof(a->psk.str));
    if (r != 0) return r;

    r = memcmp(&a->wpa, &b->wpa, sizeof(a->wpa));
    if (r != 0) return r;

    r = a->multi_ap < b->multi_ap ? -1
      : a->multi_ap > b->multi_ap ? 1
      : 0;
    if (r != 0) return r;

    r = a->priority < b->priority ? -1
      : a->priority > b->priority ? 1
      : 0;
    if (r != 0) return r;

    return 0;

}
static bool
osw_drv_vif_state_is_changed_sta_networks(const struct osw_drv_vif *vif)
{
    struct osw_drv_vif_sta_network *o = vif->cur_state.u.sta.network;
    struct osw_drv_vif_sta_network *n = vif->new_state.u.sta.network;
    struct osw_drv_vif_sta_network *i;
    bool changed = false;

    for (i = o; i != NULL; i = i->next) {
        struct osw_drv_vif_sta_network *j;

        for (j = n; j != NULL; j = j->next) {
            if (osw_drv_vif_sta_network_cmp(i, j) == 0)
                break;
        }

        const bool found = (j != NULL);
        if (found == false) {
            const size_t psk_len = strnlen(i->psk.str, sizeof(i->psk.str));
            char wpa_str[64];
            osw_wpa_to_str(wpa_str, sizeof(wpa_str), &i->wpa);
            LOGI("osw: drv: %s/%s: network: removed: "OSW_SSID_FMT"/"OSW_HWADDR_FMT"/len=%zu/%s%s/p=%d",
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_SSID_ARG(&i->ssid),
                 OSW_HWADDR_ARG(&i->bssid),
                 psk_len,
                 wpa_str,
                 i->multi_ap ? " map" : "",
                 i->priority);
            changed = true;
        }
    }

    for (i = n; i != NULL; i = i->next) {
        struct osw_drv_vif_sta_network *j;

        for (j = o; j != NULL; j = j->next) {
            if (osw_drv_vif_sta_network_cmp(i, j) == 0)
                break;
        }

        const bool found = (j != NULL);
        if (found == false) {
            const size_t psk_len = strnlen(i->psk.str, sizeof(i->psk.str));
            char wpa_str[64];
            osw_wpa_to_str(wpa_str, sizeof(wpa_str), &i->wpa);
            LOGI("osw: drv: %s/%s: network: added: "OSW_SSID_FMT"/"OSW_HWADDR_FMT"/len=%zu/%s%s/p=%d",
                 vif->phy->phy_name,
                 vif->vif_name,
                 OSW_SSID_ARG(&i->ssid),
                 OSW_HWADDR_ARG(&i->bssid),
                 psk_len,
                 wpa_str,
                 i->multi_ap ? " map" : "",
                 i->priority);
            changed = true;
        }
    }

    return changed;
}

static bool
osw_drv_vif_state_is_changed_sta(const struct osw_drv_vif *vif)
{
    const char *vif_name = vif->vif_name;
    const char *phy_name = vif->phy->phy_name;

    const struct osw_drv_vif_state_sta *o = &vif->cur_state.u.sta;
    const struct osw_drv_vif_state_sta *n = &vif->new_state.u.sta;

    const bool changed_mld_addr = (osw_hwaddr_is_equal(&o->mld.addr, &n->mld.addr) == false);
    const bool changed_mld_if_name = (strcmp(o->mld.if_name.buf, n->mld.if_name.buf) != 0);
    const bool changed_status = o->link.status != n->link.status;
    const bool changed_ssid = osw_ssid_cmp(&o->link.ssid, &n->link.ssid);
    const bool changed_bssid = osw_hwaddr_cmp(&o->link.bssid, &n->link.bssid);
    const bool changed_psk = strcmp(o->link.psk.str, n->link.psk.str) != 0;
    const bool changed_wpa = memcmp(&o->link.wpa, &n->link.wpa, sizeof(n->link.wpa));
    const bool changed_link_mld_addr = (osw_hwaddr_is_equal(&o->link.mld_addr, &n->link.mld_addr) == false);
    const bool changed_channel = memcmp(&o->link.channel, &n->link.channel, sizeof(n->link.channel));
    const bool changed_networks = osw_drv_vif_state_is_changed_sta_networks(vif);

    if (changed_status == true) {
        const char *from = osw_drv_vif_link_status_to_str(o->link.status);
        const char *to = osw_drv_vif_link_status_to_str(n->link.status);
        LOGN("osw: drv: %s/%s: link %s -> %s", phy_name, vif_name, from, to);
    }

    if (changed_ssid == true) {
        const struct osw_ssid *from = &o->link.ssid;
        const struct osw_ssid *to = &n->link.ssid;
        LOGI("osw: drv: %s/%s: ssid "OSW_SSID_FMT" -> "OSW_SSID_FMT,
             phy_name, vif_name,
             OSW_SSID_ARG(from), OSW_SSID_ARG(to));
    }

    if (changed_bssid == true) {
        const struct osw_hwaddr *from = &o->link.bssid;
        const struct osw_hwaddr *to = &n->link.bssid;
        LOGI("osw: drv: %s/%s: bssid "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             phy_name, vif_name,
             OSW_HWADDR_ARG(from), OSW_HWADDR_ARG(to));
    }

    if (changed_wpa == true) {
        char from[64];
        char to[64];
        osw_wpa_to_str(from, sizeof(from), &o->link.wpa);
        osw_wpa_to_str(to, sizeof(to), &n->link.wpa);
        LOGI("osw: drv: %s/%s: link: wpa: %s -> %s",
             vif->phy->phy_name,
             vif->vif_name,
             from,
             to);
    }

    if (changed_psk == true) {
        LOGI("osw: drv: %s/%s: sta: link: psk: len=%zu -> %zu",
             vif->phy->phy_name,
             vif->vif_name,
             strnlen(o->link.psk.str, sizeof(o->link.psk.str)),
             strnlen(n->link.psk.str, sizeof(n->link.psk.str)));
    }

    if (changed_channel == true) {
        LOGI("osw: drv: %s/%s: link: channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_CHANNEL_ARG(&o->link.channel),
             OSW_CHANNEL_ARG(&n->link.channel));
    }

    if (changed_link_mld_addr) {
        LOGI("osw: drv: %s/%s/%s: link: mld_addr: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_HWADDR_ARG(&o->link.mld_addr),
             OSW_HWADDR_ARG(&n->link.mld_addr));
    }

    if (changed_mld_addr) {
        LOGI("osw: drv: %s/%s/%s: mld.addr: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_HWADDR_ARG(&o->mld.addr),
             OSW_HWADDR_ARG(&n->mld.addr));
    }

    if (changed_mld_if_name) {
        LOGI("osw: drv: %s/%s/%s: mld.if_name: "OSW_IFNAME_FMT" -> "OSW_IFNAME_FMT,
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             OSW_IFNAME_ARG(&o->mld.if_name),
             OSW_IFNAME_ARG(&n->mld.if_name));
    }

    const bool changed = false
                       | changed_mld_addr
                       | changed_mld_if_name
                       | changed_networks
                       | changed_status
                       | changed_ssid
                       | changed_bssid
                       | changed_psk
                       | changed_channel
                       | changed_wpa;

    return changed;
}

static bool
osw_drv_vif_state_is_changed(const struct osw_drv_vif *vif)
{
    bool changed = false;
    const bool changed_status = vif->cur_state.status != vif->new_state.status;
    const bool changed_vif_type = vif->cur_state.vif_type != vif->new_state.vif_type;
    const bool changed_tx_power_dbm = vif->cur_state.tx_power_dbm != vif->new_state.tx_power_dbm;
    const bool changed_mac_addr = memcmp(&vif->cur_state.mac_addr,
                                         &vif->new_state.mac_addr,
                                         sizeof(vif->new_state.mac_addr));

    changed |= changed_status;
    changed |= changed_vif_type;
    changed |= changed_tx_power_dbm;
    changed |= changed_mac_addr;

    if (changed_status) {
        LOGI("osw: drv: %s/%s: status: %s -> %s",
             vif->phy->phy_name,
             vif->vif_name,
             osw_vif_status_into_cstr(vif->cur_state.status),
             osw_vif_status_into_cstr(vif->new_state.status));
    }

    if (changed_tx_power_dbm) {
        LOGI("osw: drv: %s/%s: tx_power_dbm: %d -> %d",
             vif->phy->phy_name,
             vif->vif_name,
             vif->cur_state.tx_power_dbm,
             vif->new_state.tx_power_dbm);
    }

    if (changed_vif_type == false) {
        switch (vif->new_state.vif_type) {
            case OSW_VIF_UNDEFINED:
                break;
            case OSW_VIF_AP:
                changed |= osw_drv_vif_state_is_changed_ap(vif);
                break;
            case OSW_VIF_AP_VLAN:
                changed |= osw_drv_vif_state_is_changed_ap_vlan(vif);
                break;
            case OSW_VIF_STA:
                changed |= osw_drv_vif_state_is_changed_sta(vif);
                break;
        }
    }

    return changed;
}

static void
osw_drv_vif_process_state_vsta(struct osw_drv_vif *vif)
{
    const bool was_vsta = (vif->cur_state.exists == false ||
                           vif->cur_state.vif_type == OSW_VIF_STA);
    const bool is_vsta = (vif->new_state.vif_type == OSW_VIF_STA);
    const struct osw_drv_vif_state_sta *o = was_vsta ? &vif->cur_state.u.sta : NULL;
    const struct osw_drv_vif_state_sta *n = is_vsta ? &vif->new_state.u.sta : NULL;
    const bool was_connected = o != NULL && o->link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
    const bool is_connected = n != NULL && n->link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
    const struct osw_hwaddr *vsta_root_ap = is_connected == true
                                          ? &vif->new_state.u.sta.link.bssid
                                          : NULL;
    struct osw_drv_phy *phy = vif->phy;
    struct osw_drv *drv = phy->drv;
    const char *drv_name = drv->ops->name ?: "";
    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;
    const struct osw_hwaddr zero = {0};

    if (was_connected == is_connected) return;
    if (vif->vsta_root_ap == vsta_root_ap) return;

    LOGD("osw: drv: %s/%s/%s: setting vsta_root_ap from "OSW_HWADDR_FMT" to "OSW_HWADDR_FMT,
         drv_name,
         phy_name,
         vif_name,
         OSW_HWADDR_ARG(vif->vsta_root_ap ?: &zero),
         OSW_HWADDR_ARG(vsta_root_ap ?: &zero));

    vif->vsta_root_ap = vsta_root_ap;
    osw_drv_vif_set_sta_list_valid(vif, false);
    if (vsta_root_ap != NULL) {
        osw_drv_report_sta_changed(drv, phy_name, vif_name, vsta_root_ap);
    }
}

static void
osw_drv_vif_dump_passpoint(struct osw_drv_vif *vif)
{
    size_t i;
    const struct osw_passpoint *passpoint = &vif->cur_state.u.ap.passpoint;
    LOGI("osw: drv: %s/%s/%s: passpoint: enabled: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->hs20_enabled);

    LOGI("osw: drv: %s/%s/%s: passpoint: hessid: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&passpoint->hessid));

    LOGI("osw: drv: %s/%s/%s: passpoint: osu_ssid: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->osu_ssid.buf);

    LOGI("osw: drv: %s/%s/%s: passpoint: adv_wan: %d/%d/%d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->adv_wan_status ? 1 : 0,
         passpoint->adv_wan_symmetric ? 1 : 0,
         passpoint->adv_wan_at_capacity ? 1 : 0);

    LOGI("osw: drv: %s/%s/%s: passpoint: osen: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->osen ? 1 : 0);

    LOGI("osw: drv: %s/%s/%s: passpoint: asra: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->asra ? 1 : 0);

    LOGI("osw: drv: %s/%s/%s: passpoint: ant: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->ant);

    LOGI("osw: drv: %s/%s/%s: passpoint: venue_group: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->venue_group);

    LOGI("osw: drv: %s/%s/%s: passpoint: venue_type: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->venue_type);

    LOGI("osw: drv: %s/%s/%s: passpoint: anqp_domain_id: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->anqp_domain_id);

    LOGI("osw: drv: %s/%s/%s: passpoint: t_c_filename: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->t_c_filename);

    LOGI("osw: drv: %s/%s/%s: passpoint: t_c_timestamp: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->t_c_timestamp);

    for (i = 0; i < passpoint->domain_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: domain_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->domain_list[i]);
    }

    for (i = 0; i < passpoint->nairealm_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: nairealm_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->nairealm_list[i]);
    }

    for (i = 0; i < passpoint->roamc_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: roamc_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->roamc_list[i]);
    }

    for (i = 0; i < passpoint->oper_fname_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: oper_fname_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->oper_fname_list[i]);
    }

    for (i = 0; i < passpoint->venue_name_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: venue_name_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->venue_name_list[i]);
    }

    for (i = 0; i < passpoint->venue_url_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: venue_url_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->venue_url_list[i]);
    }

    for (i = 0; i < passpoint->list_3gpp_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: list_3gpp_list[%zu]: %s",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->list_3gpp_list[i]);
    }

    for (i = 0; i < passpoint->net_auth_type_list_len; i++) {
        LOGI("osw: drv: %s/%s/%s: passpoint: net_auth_type_list[%zu]: %d",
             vif->phy->drv->ops->name,
             vif->phy->phy_name,
             vif->vif_name,
             i, passpoint->net_auth_type_list[i]);
    }

    LOGI("osw: drv: %s/%s/%s: passpoint: anqp_elem: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         passpoint->anqp_elem);
}

static void
osw_drv_vif_dump_ap(struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state_ap *ap = &vif->cur_state.u.ap;
    char buf[4096];

    LOGI("osw: drv: %s/%s/%s: ap: ssid: "OSW_SSID_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_SSID_ARG(&ap->ssid));

    LOGI("osw: drv: %s/%s/%s: ap: channel: "OSW_CHANNEL_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_CHANNEL_ARG(&ap->channel));

    osw_neigh_list_to_str(buf, sizeof(buf), &ap->neigh_list);
    LOGI("osw: drv: %s/%s/%s: ap: neigh: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         buf);

    osw_ap_mode_to_str(buf, sizeof(buf), &ap->mode);
    LOGI("osw: drv: %s/%s/%s: ap: mode: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         buf);

    char *multi_ap_str = osw_multi_ap_into_str(&ap->multi_ap);
    LOGI("osw: drv: %s/%s/%s: ap: multi_ap: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         multi_ap_str);
    FREE(multi_ap_str);

    LOGI("osw: drv: %s/%s/%s: ap: mld.addr: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&ap->mld.addr));

    LOGI("osw: drv: %s/%s/%s: ap: mld.if_name: "OSW_IFNAME_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_IFNAME_ARG(&ap->mld.if_name));

    const char *mbss_mode_str = osw_mbss_vif_ap_mode_to_str(ap->mbss_mode);
    LOGI("osw: drv: %s/%s/%s: ap: mbss_mode: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         mbss_mode_str);

    LOGI("osw: drv: %s/%s/%s: ap: ft_over_ds: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_over_ds);

    LOGI("osw: drv: %s/%s/%s: ap: ft_pmk_r1_push: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_pmk_r1_push);

    LOGI("osw: drv: %s/%s/%s: ap: ft_psk_generate_local: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_psk_generate_local);

    LOGI("osw: drv: %s/%s/%s: ap: ft_pmk_r0_key_lifetime_sec: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_pmk_r0_key_lifetime_sec);

    LOGI("osw: drv: %s/%s/%s: ap: ft_pmk_r1_max_key_lifetime_sec: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_pmk_r1_max_key_lifetime_sec);

    LOGI("osw: drv: %s/%s/%s: ap: ft_encr_key: "OSW_FT_ENCR_KEY_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_FT_ENCR_KEY_ARG(&ap->ft_encr_key));

    LOGI("osw: drv: %s/%s/%s: ap: ft_mobility_domain: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->ft_mobility_domain);

    LOGI("osw: drv: %s/%s/%s: ap: mbo: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->mbo);


    LOGI("osw: drv: %s/%s/%s: ap: oce: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->oce);

    LOGI("osw: drv: %s/%s/%s: ap: oce_min_rssi_dbm: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->oce_min_rssi_dbm);

    LOGI("osw: drv: %s/%s/%s: ap: oce_min_rssi_enable: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->oce_min_rssi_enable);

    LOGI("osw: drv: %s/%s/%s: ap: oce_retry_delay_sec: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->oce_retry_delay_sec);


    LOGI("osw: drv: %s/%s/%s: ap: max_sta: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         ap->max_sta);

    osw_drv_vif_dump_neigh_ft(vif);
    osw_drv_vif_dump_passpoint(vif);
}

static void
osw_drv_vif_dump_ap_vlan(struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state_ap_vlan *ap_vlan = &vif->cur_state.u.ap_vlan;
    char sta_addrs_str[1024];

    osw_hwaddr_list_to_str(sta_addrs_str,
                           sizeof(sta_addrs_str),
                           &ap_vlan->sta_addrs);

    LOGI("osw: drv: %s/%s/%s: ap_vlan: sta_addrs: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         sta_addrs_str);
}

static void
osw_drv_vif_dump_sta(struct osw_drv_vif *vif)
{
    const struct osw_drv_vif_state_sta *sta = &vif->cur_state.u.sta;
    const struct osw_drv_vif_state_sta_link *link = &vif->cur_state.u.sta.link;

    LOGI("osw: drv: %s/%s/%s: sta: link: status: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         osw_drv_vif_link_status_to_str(link->status));

    LOGI("osw: drv: %s/%s/%s: sta: link: bssid: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&link->bssid));

    LOGI("osw: drv: %s/%s/%s: sta: link: ssid: "OSW_SSID_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_SSID_ARG(&link->ssid));

    LOGI("osw: drv: %s/%s/%s: sta: link: channel: "OSW_CHANNEL_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_CHANNEL_ARG(&link->channel));

    LOGI("osw: drv: %s/%s/%s: sta: link: psk: len=%zu",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         strnlen(link->psk.str, sizeof(link->psk.str)));

    char wpa_str[64];
    osw_wpa_to_str(wpa_str, sizeof(wpa_str), &link->wpa);
    LOGI("osw: drv: %s/%s/%s: sta: link: wpa: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         wpa_str);

    LOGI("osw: drv: %s/%s/%s: sta: link: bridge_if_name: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         link->bridge_if_name.buf);

    LOGI("osw: drv: %s/%s/%s: sta: link: multi_ap: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         link->multi_ap ? "yes" : "no");

    LOGI("osw: drv: %s/%s/%s: sta: link: mld_addr: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&sta->link.mld_addr));

    LOGI("osw: drv: %s/%s/%s: sta: mld.addr: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&sta->mld.addr));

    LOGI("osw: drv: %s/%s/%s: sta: mld.if_name: "OSW_IFNAME_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_IFNAME_ARG(&sta->mld.if_name));

    if (sta->network != NULL) {
        struct osw_drv_vif_sta_network *sta_net = sta->network;
        while (sta_net != NULL) {
            osw_wpa_to_str(wpa_str, sizeof(wpa_str), &sta_net->wpa);
            LOGI("osw: drv: %s/%s/%s: sta: net: bridge_if_name: %s bssid: "OSW_HWADDR_FMT" ssid: "OSW_SSID_FMT" psk: len=%zu wpa: %s multi_ap: %d priority: %d",
                 vif->phy->drv->ops->name,
                 vif->phy->phy_name,
                 vif->vif_name,
                 sta_net->bridge_if_name.buf,
                 OSW_HWADDR_ARG(&sta_net->bssid),
                 OSW_SSID_ARG(&sta_net->ssid),
                 strnlen(sta_net->psk.str, sizeof(sta_net->psk.str)),
                 wpa_str,
                 sta_net->multi_ap,
                 sta_net->priority);
            sta_net = sta_net->next;
        }
    }
}

static void
osw_drv_vif_dump(struct osw_drv_vif *vif)
{
    LOGI("osw: drv: %s/%s/%s: status: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         osw_vif_status_into_cstr(vif->cur_state.status));

    LOGI("osw: drv: %s/%s/%s: type: %s",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         osw_vif_type_to_str(vif->cur_state.vif_type));

    LOGI("osw: drv: %s/%s/%s: tx_power_dbm: %d",
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         vif->cur_state.tx_power_dbm);

    LOGI("osw: drv: %s/%s/%s: mac_addr: "OSW_HWADDR_FMT,
         vif->phy->drv->ops->name,
         vif->phy->phy_name,
         vif->vif_name,
         OSW_HWADDR_ARG(&vif->cur_state.mac_addr));

    switch (vif->cur_state.vif_type) {
        case OSW_VIF_UNDEFINED: break;
        case OSW_VIF_AP: osw_drv_vif_dump_ap(vif); break;
        case OSW_VIF_AP_VLAN: osw_drv_vif_dump_ap_vlan(vif); break;
        case OSW_VIF_STA: osw_drv_vif_dump_sta(vif); break;
    }
}

static void
osw_drv_vif_process_state(struct osw_drv_vif *vif)
{
    const bool added = vif->cur_state.exists == false
                    && vif->new_state.exists == true;
    const bool removed = vif->cur_state.exists == true
                      && vif->new_state.exists == false;
    const bool changed = vif->cur_state.exists == true
                      && vif->new_state.exists == true
                      && osw_drv_vif_state_is_changed(vif) == true;

    osw_drv_vif_process_state_vsta(vif);

    if (removed == true) {
        if (osw_drv_vif_stas_are_settled(vif) == false) {
            return;
        }

        if (ds_tree_is_empty(&vif->sta_tree) == false) {
            osw_drv_vif_set_sta_list_valid(vif, false);
            return;
        }
    }

    if (added == true) {
        if (vif->phy->cur_state.exists == false) {
            osw_drv_set_phy_list_valid(vif->phy->drv, false);
            return;
        }
    }

    const bool sta_vif_link_changed = vif->new_state.vif_type == OSW_VIF_STA
                                  && vif->cur_state.vif_type == OSW_VIF_STA
                                  ? vif->new_state.u.sta.link.status != vif->cur_state.u.sta.link.status
                                  : true;

    const bool ap_channel_changed = osw_drv_vif_has_ap_channel_changed(vif);
    const bool vsta_channel_changed = osw_drv_vsta_has_channel_changed(vif);
    const bool notify_channel_changed = ((vsta_channel_changed ||
                                          ap_channel_changed) &&
                                          vif->new_state.status == OSW_VIF_ENABLED);
    struct osw_channel old_channel = {0};
    struct osw_channel new_channel = {0};

    if (sta_vif_link_changed) {
        osw_drv_vif_set_sta_list_valid(vif, false);
        struct osw_drv_sta *sta;
        ds_tree_foreach(&vif->sta_tree, sta) {
            osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_INVALID);
        }
    }

    if (ap_channel_changed) {
        osw_drv_vif_set_recent_channel(vif, &vif->cur_state.u.ap.channel);
        new_channel = vif->new_state.u.ap.channel;
        old_channel = vif->cur_state.u.ap.channel;
    }

    if (vsta_channel_changed) {
        osw_drv_vif_set_recent_channel(vif, &vif->new_state.u.sta.link.channel);
        new_channel = vif->new_state.u.sta.link.channel;
        old_channel = vif->cur_state.u.sta.link.channel;
    }

    osw_drv_vif_assign_state(&vif->cur_state, &vif->new_state);
    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_PROCESSED);

    vif->pub.vif_name = vif->vif_name;
    vif->pub.phy = &vif->phy->pub;
    vif->pub.drv_state = &vif->cur_state;

    if (added == true) osw_drv_vif_dump(vif);
    if (added == true) OSW_STATE_NOTIFY(vif_added_fn, &vif->pub);
    if (changed == true) OSW_STATE_NOTIFY(vif_changed_fn, &vif->pub);
    if (changed == true) osw_drv_set_resync(vif);
    if (notify_channel_changed == true) OSW_STATE_NOTIFY(vif_channel_changed_fn, &vif->pub, &new_channel, &old_channel);
    if (removed == true) OSW_STATE_NOTIFY(vif_removed_fn, &vif->pub);
    if (vif->radar_detected == true) OSW_STATE_NOTIFY(vif_radar_detected_fn, &vif->pub, &vif->radar_channel);

    vif->radar_detected = false;
}

static void
osw_drv_vif_scan_done(struct osw_drv_vif *vif,
                      enum osw_drv_scan_complete_reason reason)
{
    if (vif == NULL) return;
    if (vif->scan_started == false) return;
    vif->scan_started = false;
    OSW_STATE_NOTIFY(vif_scan_completed_fn, &vif->pub, reason);
}

static void
osw_drv_vif_work(struct osw_drv_vif *vif)
{
    struct osw_drv_sta *sta;
    struct osw_drv_sta *tmp;

    switch (vif->obj.state) {
        case OSW_DRV_OBJ_INVALID:
            osw_drv_vif_request_state(vif);
            break;
        case OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED:
        case OSW_DRV_OBJ_REQUESTED:
            /* Waiting for driver to call osw_drv_report_vif_state().
             * That'll move the state to VALID.
             */
            break;
        case OSW_DRV_OBJ_VALID:
            osw_drv_vif_process_state(vif);
            break;
        case OSW_DRV_OBJ_PROCESSED:
            if (vif->cur_state.exists == false) {
                osw_drv_vif_scan_done(vif, OSW_DRV_SCAN_FAILED);
                osw_drv_vif_free(vif);
                return;
            }
            break;
    }

    if (vif->sta_list_valid == false) {
        osw_drv_sta_enumerate(vif);
        osw_drv_vif_set_sta_list_valid(vif, true);
    }

    ds_tree_foreach_safe(&vif->sta_tree, sta, tmp)
        osw_drv_sta_work(sta);
}

static void
osw_drv_vif_enumerate_cb(const char *vif_name, void *data)
{
    struct osw_drv_vif *vif = osw_drv_vif_get(data, vif_name);
    if (vif == NULL) return;
    vif->obj.exists = true;
}

static void
osw_drv_vif_enumerate(struct osw_drv_phy *phy)
{
    struct osw_drv *drv = phy->drv;
    const char *phy_name = phy->phy_name;
    struct osw_drv_vif *vif;

    ds_tree_foreach(&phy->vif_tree, vif) {
        vif->obj.existed = vif->obj.exists;
        vif->obj.exists = false;
    }

    if (drv->unregistered == false) {
        drv->ops->get_vif_list_fn(drv, phy_name, osw_drv_vif_enumerate_cb, phy);
    }

    ds_tree_foreach(&phy->vif_tree, vif) {
        if (vif->obj.exists != vif->obj.existed) {
            osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_INVALID);
        }
    }
}

const struct osw_channel *
osw_drv_vif_get_channel(const struct osw_drv_vif_state *state)
{
    const struct osw_drv_vif_state_ap *vap = &state->u.ap;
    const struct osw_drv_vif_state_sta *vsta = &state->u.sta;
    const enum osw_drv_vif_state_sta_link_status status = vsta->link.status;

    if (state->status != OSW_VIF_ENABLED) return NULL;

    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP_VLAN:
            /* This makes no sense, really. Even if,
             * this would need to look at the AP vif
             * associated with it.
             */
            WARN_ON(1);
            break;
        case OSW_VIF_AP:
            return &vap->channel;
        case OSW_VIF_STA:
            if (status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) break;
            return &vsta->link.channel;
    }

    return NULL;
}

static struct osw_drv_vif *
osw_drv_vif_lookup_by_name(const char *phy_name, const char *vif_name)
{
    struct osw_drv *drv;
    ds_tree_foreach(&g_osw_drv_tree, drv) {
        struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, phy_name);
        if (phy == NULL) continue;

        struct osw_drv_vif *vif = ds_tree_find(&phy->vif_tree, vif_name);
        if (vif == NULL) continue;

        return vif;
    }

    return NULL;
}

static void
osw_drv_phy_assert_unique(const char *phy_name)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        assert(ds_tree_find(&drv->phy_tree, phy_name) == NULL);
    }
}

static void
osw_drv_phy_sync_cb(struct osw_drv_phy *phy,
                    const char *name)
{
    struct osw_drv *drv = phy->drv;
    const char *drv_name = drv->ops->name ?: "";
    const char *phy_name = phy->phy_name;

    LOGD("osw: drv: %s/%s: forced state invalidation due to %s",
         drv_name,
         phy_name,
         name);

    osw_drv_report_phy_changed(drv, phy_name);
}

static void
osw_drv_phy_cac_sync_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_phy *phy = arg->data;
    osw_drv_phy_sync_cb(phy, "cac_in_progress");
}

static void
osw_drv_phy_nol_sync_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_phy *phy = arg->data;
    osw_drv_phy_sync_cb(phy, "nol");
}

static struct osw_drv_phy *
osw_drv_phy_alloc(struct osw_drv *drv, const char *phy_name)
{
    struct osw_drv_phy *phy = CALLOC(1, sizeof(*phy));

    osw_drv_phy_assert_unique(phy_name);
    phy->phy_name = STRDUP(phy_name);
    phy->drv = drv;
    ev_timer_init(&phy->cac_sync, osw_drv_phy_cac_sync_cb, 0, 0);
    ev_timer_init(&phy->nol_sync, osw_drv_phy_nol_sync_cb, 0, 0);
    phy->cac_sync.data = phy;
    phy->nol_sync.data = phy;
    ds_tree_init(&phy->vif_tree, ds_str_cmp, struct osw_drv_vif, node);
    ds_tree_insert(&drv->phy_tree, phy, phy->phy_name);
    return phy;
}

static struct osw_drv_phy *
osw_drv_phy_get(struct osw_drv *drv, const char *phy_name)
{
    return ds_tree_find(&drv->phy_tree, phy_name) ?: osw_drv_phy_alloc(drv, phy_name);
}

static void
osw_drv_phy_free(struct osw_drv_phy *phy)
{
    g_osw_drv_work_done = true;
    ev_timer_stop(EV_DEFAULT_ &phy->cac_sync);
    ev_timer_stop(EV_DEFAULT_ &phy->nol_sync);
    ds_tree_remove(&phy->drv->phy_tree, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

static bool
osw_drv_phy_vifs_are_settled(struct osw_drv_phy *phy)
{
    struct osw_drv_vif *vif;

    ds_tree_foreach(&phy->vif_tree, vif)
        if (osw_drv_vif_is_settled(vif) == false)
            return false;

    return true;
}

static bool
osw_drv_phy_is_settled(struct osw_drv_phy *phy)
{
    if (osw_drv_phy_vifs_are_settled(phy) == false)
        return false;

    if (phy->obj.state == OSW_DRV_OBJ_PROCESSED
    &&  phy->cur_state.exists == false)
        return false;

    return phy->obj.state == OSW_DRV_OBJ_PROCESSED;
}

static void
osw_drv_phy_assign_state(struct osw_drv_phy_state *dst,
                         const struct osw_drv_phy_state *src)
{
    const struct osw_drv_phy_state zero = {0};

    if (src == NULL)
        src = &zero;

    FREE(dst->channel_states);
    *dst = *src;
    ARRDUP(src, dst, channel_states, n_channel_states);
}

static void
osw_drv_phy_set_state(struct osw_drv_phy *phy,
                      const struct osw_drv_phy_state *state)
{
    osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_VALID);
    osw_drv_phy_assign_state(&phy->new_state, state);
}

static void
osw_drv_phy_request_state(struct osw_drv_phy *phy)
{
    struct osw_drv *drv = phy->drv;

    if (drv->unregistered == true) {
        struct osw_drv_phy_state state = {0};
        osw_drv_phy_set_state(phy, &state);
        return;
    }

    assert(phy != NULL);
    assert(drv->unregistered == false);
    assert(drv->ops->request_phy_state_fn != NULL);

    osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_REQUESTED);
    drv->ops->request_phy_state_fn(drv, phy->phy_name);
}

static bool
osw_channel_overlaps_with_freq(const struct osw_channel *c, const int freq)
{
    if (c == NULL) return false;
    if (c->control_freq_mhz == 0) return false;
    int segs[16];
    const size_t n_segs = osw_channel_20mhz_segments(c, segs, ARRAY_SIZE(segs));
    if (n_segs == 0) return false;

    size_t i;
    for (i = 0; i < n_segs; i++) {
        if (segs[i] == freq) return true;
    }

    return false;
}

static void
osw_drv_phy_mark_vif_radar(struct osw_drv_phy *phy,
                           const struct osw_channel *c)
{
    struct ds_tree *vifs = &phy->vif_tree;
    struct osw_drv_vif *vif;
    const int freq = c->control_freq_mhz;

    ds_tree_foreach(vifs, vif) {
        const struct osw_channel *c = NULL;
        switch (vif->cur_state.vif_type) {
            case OSW_VIF_UNDEFINED:
                break;
            case OSW_VIF_AP_VLAN:
                break;
            case OSW_VIF_AP:
                c = &vif->cur_state.u.ap.channel;
                break;
            case OSW_VIF_STA:
                c = vif->cur_state.u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED
                  ? &vif->cur_state.u.sta.link.channel
                  : NULL;
                break;
        }

        const bool is_on_freq = osw_channel_overlaps_with_freq(c, freq);
        if (is_on_freq) {
            vif->radar_detected = true;
            vif->radar_channel = *c;
            osw_drv_report_vif_changed(phy->drv, phy->phy_name, vif->vif_name);
            return;
        }

        const bool was_on_freq = osw_timer_is_armed(&vif->recent_channel_timeout)
                               ? osw_channel_overlaps_with_freq(&vif->recent_channel, freq)
                               : false;
        if (was_on_freq) {
            vif->radar_detected = true;
            vif->radar_channel = vif->recent_channel;
            osw_drv_report_vif_changed(phy->drv, phy->phy_name, vif->vif_name);
            return;
        }
    }
}

static const struct osw_channel_state *
osw_drv_channel_state_lookup(const struct osw_channel_state *arr,
                             size_t arr_len,
                             const struct osw_channel *c)
{
    const size_t size = sizeof(*c);
    size_t i;
    for (i = 0; i < arr_len; i++) {
        const struct osw_channel_state *cs = &arr[i];
        const bool matching = (memcmp(&cs->channel, c, size) == 0);
        if (matching) {
            return cs;
        }
    }
    return NULL;
}

static void
osw_drv_phy_mark_radar(struct osw_drv_phy *phy)
{
    const struct osw_drv_phy_state *o = &phy->cur_state;
    const struct osw_drv_phy_state *n = &phy->new_state;

    size_t i;
    for (i = 0; i < o->n_channel_states; i++) {
        const struct osw_channel_state *os = &o->channel_states[i];
        const struct osw_channel *c = &os->channel;
        const struct osw_channel_state *ns = osw_drv_channel_state_lookup(n->channel_states,
                                                                          n->n_channel_states,
                                                                          c);
        if (ns == NULL) continue;

        const bool was_not_nol = (os->dfs_state != OSW_CHANNEL_DFS_NOL);
        const bool now_is_nol = (ns->dfs_state == OSW_CHANNEL_DFS_NOL);
        const bool radar_happened = (was_not_nol && now_is_nol);
        if (radar_happened) {
            osw_drv_phy_mark_vif_radar(phy, &ns->channel);
        }
    }
}

static bool
osw_drv_phy_state_is_channels_changed(const struct osw_drv_phy *phy)
{
    const struct osw_drv_phy_state *o = &phy->cur_state;
    const struct osw_drv_phy_state *n = &phy->new_state;
    bool changed = false;
    bool radar_detected = false;
    const size_t size = sizeof(struct osw_channel);

    size_t i;
    for (i = 0; i < o->n_channel_states; i++) {
        const struct osw_channel_state *os = &o->channel_states[i];
        const struct osw_channel_state *ns = NULL;
        size_t j;
        for (j = 0; j < n->n_channel_states; j++) {
            ns = &n->channel_states[j];
            if (memcmp(os, ns, size) == 0) break;
        }
        const bool found = ns != NULL && (j < n->n_channel_states);
        if (found == true) {
            const bool dfs_changed = (os->dfs_state != ns->dfs_state);
            const bool nol_changed = (os->dfs_nol_remaining_seconds != ns->dfs_nol_remaining_seconds);
            const bool cs_changed = dfs_changed || nol_changed;
            changed |= cs_changed;
            if (cs_changed == true) {
                LOGI("osw: drv: %s: channel "OSW_CHANNEL_FMT": "OSW_CHAN_STATE_FMT" -> "OSW_CHAN_STATE_FMT,
                     phy->phy_name,
                     OSW_CHANNEL_ARG(&ns->channel),
                     OSW_CHAN_STATE_ARG(os),
                     OSW_CHAN_STATE_ARG(ns));

                if (dfs_changed && ns->dfs_state == OSW_CHANNEL_DFS_NOL) {
                    radar_detected = true;
                }
            }
        }
        else {
            LOGI("osw: drv: %s: channel "OSW_CHANNEL_FMT": "OSW_CHAN_STATE_FMT" -> ()",
                 phy->phy_name,
                 OSW_CHANNEL_ARG(&os->channel),
                 OSW_CHAN_STATE_ARG(os));
            changed = true;
        }
    }

    for (i = 0; i < n->n_channel_states; i++) {
        const struct osw_channel_state *ns = &n->channel_states[i];
        const struct osw_channel_state *os = NULL;
        size_t j;
        for (j = 0; j < o->n_channel_states; j++) {
            os = &o->channel_states[j];
            if (memcmp(os, ns, size) == 0) break;
        }
        const bool found = os != NULL && (j < o->n_channel_states);
        if (found == false) {
            LOGI("osw: drv: %s: channel "OSW_CHANNEL_FMT": () -> "OSW_CHAN_STATE_FMT,
                 phy->phy_name,
                 OSW_CHANNEL_ARG(&ns->channel),
                 OSW_CHAN_STATE_ARG(ns));
            changed = true;
        }
    }

    if (radar_detected) {
        struct osw_drv *drv = phy->drv;
        const char *drv_name = drv->ops->name ?: "";
        const char *phy_name = phy->phy_name;

        LOGN("osw: drv: %s/%s: radar detected",
             drv_name,
             phy_name);
    }

    return changed;
}

static bool
osw_drv_phy_state_is_changed(const struct osw_drv_phy *phy)
{
    bool changed = false;
    const bool changed_enabled = phy->cur_state.enabled != phy->new_state.enabled;
    const bool changed_tx_chainmask = phy->cur_state.tx_chainmask != phy->new_state.tx_chainmask;
    const bool changed_radar = phy->cur_state.radar != phy->new_state.radar;
    const bool changed_channels = osw_drv_phy_state_is_channels_changed(phy);
    const bool changed_radar_next_channel = !osw_channel_is_equal(&(phy->cur_state.radar_next_channel),
                                                                  &(phy->new_state.radar_next_channel));

    changed |= changed_enabled;
    changed |= changed_tx_chainmask;
    changed |= changed_radar;
    changed |= changed_channels;
    changed |= changed_radar_next_channel;

    if (changed_enabled) {
        LOGI("osw: drv: %s/%s: enabled: %d -> %d",
             phy->drv->ops->name,
             phy->phy_name,
             phy->cur_state.enabled,
             phy->new_state.enabled);
    }

    if (changed_tx_chainmask) {
        LOGI("osw: drv: %s/%s: tx_chainmask: 0x%04x -> 0x%04x",
             phy->drv->ops->name,
             phy->phy_name,
             phy->cur_state.tx_chainmask,
             phy->new_state.tx_chainmask);
    }

    if (changed_radar_next_channel) {
        LOGI("osw: drv: %s/%s: radar_next_channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             phy->drv->ops->name,
             phy->phy_name,
             OSW_CHANNEL_ARG(&(phy->cur_state.radar_next_channel)),
             OSW_CHANNEL_ARG(&(phy->new_state.radar_next_channel)));
    }

    if (changed_radar) {
        const char *from = osw_radar_to_str(phy->cur_state.radar);
        const char *to = osw_radar_to_str(phy->new_state.radar);
        LOGI("osw: drv: %s/%s: radar: %s -> %s",
             phy->drv->ops->name,
             phy->phy_name,
             from,
             to);
    }

    return changed;
}

static void
osw_drv_phy_dump_channels(struct osw_drv_phy *phy)
{
    size_t i;
    for (i = 0; i < phy->cur_state.n_channel_states; i++) {
        const struct osw_channel_state *cs = &phy->cur_state.channel_states[i];
        LOGI("osw: drv: %s/%s: channel "OSW_CHANNEL_FMT": "OSW_CHAN_STATE_FMT,
             phy->drv->ops->name,
             phy->phy_name,
             OSW_CHANNEL_ARG(&cs->channel),
             OSW_CHAN_STATE_ARG(cs));
    }
}

static void
osw_drv_phy_dump(struct osw_drv_phy *phy)
{
    LOGI("osw: drv: %s/%s: enabled: %d",
         phy->drv->ops->name,
         phy->phy_name,
         phy->cur_state.enabled);

    LOGI("osw: drv: %s/%s: tx_chainmask: 0x%04x",
         phy->drv->ops->name,
         phy->phy_name,
         phy->cur_state.tx_chainmask);

    LOGI("osw: drv: %s/%s: radar_next_channel: "OSW_CHANNEL_FMT,
         phy->drv->ops->name,
         phy->phy_name,
         OSW_CHANNEL_ARG(&phy->cur_state.radar_next_channel));

    LOGI("osw: drv: %s/%s: radar: %s",
         phy->drv->ops->name,
         phy->phy_name,
         osw_radar_to_str(phy->cur_state.radar));

    LOGI("osw: drv: %s/%s: mac_addr: "OSW_HWADDR_FMT,
         phy->drv->ops->name,
         phy->phy_name,
         OSW_HWADDR_ARG(&phy->cur_state.mac_addr));

    LOGI("osw: drv: %s/%s: regulatory: "OSW_REG_DOMAIN_FMT,
         phy->drv->ops->name,
         phy->phy_name,
         OSW_REG_DOMAIN_ARG(&phy->cur_state.reg_domain));

    osw_drv_phy_dump_channels(phy);
}

static const char *
osw_drv_phy_tx_chain_to_str(enum osw_drv_phy_tx_chain tx_chain)
{
    switch (tx_chain) {
        case OSW_DRV_PHY_TX_CHAIN_UNSPEC:
            return "unspec";
        case OSW_DRV_PHY_TX_CHAIN_SUPPORTED:
            return "supported";
        case OSW_DRV_PHY_TX_CHAIN_NOT_SUPPORTED:
            return "not supported";
    }
    return "";
}

static bool
osw_drv_phy_process_capab_tx_chain(struct osw_drv_phy *phy)
{
    const bool supported = (phy->new_state.tx_chainmask != 0);
    const enum osw_drv_phy_tx_chain tx_chain = supported
                                             ? OSW_DRV_PHY_TX_CHAIN_SUPPORTED
                                             : OSW_DRV_PHY_TX_CHAIN_NOT_SUPPORTED;
    const char *drv_name = phy->drv->ops->name;
    const char *phy_name = phy->phy_name;
    const bool changed = (phy->capab.tx_chain != tx_chain);
    const bool permissible = (phy->capab.tx_chain == OSW_DRV_PHY_TX_CHAIN_UNSPEC);
    const bool not_permissible = (permissible == false);
    if (changed) {
        const char *from = osw_drv_phy_tx_chain_to_str(phy->capab.tx_chain);
        const char *to = osw_drv_phy_tx_chain_to_str(tx_chain);
        if (permissible) {
            if (supported == false) {
                LOGI("osw: drv: %s/%s: capab: tx_chain: reported 0x0 tx_chainmask, inferring lack of support",
                     drv_name,
                     phy_name);
            }
            LOGI("osw: drv: %s/%s: capab: tx_chain: %s -> %s",
                 drv_name,
                 phy_name,
                 from,
                 to);
            phy->capab.tx_chain = tx_chain;
        }
        /* FIXME: This might need to be relaxed or reworked
         * later if tx_chain support can be explicitly
         * advertised by the driver. It could then be
         * cross-checked with state reports to warn about
         * different unexpected scenarios.
         */
        WARN_ON(not_permissible);
    }
    return changed;
}

static bool
osw_drv_phy_process_capab(struct osw_drv_phy *phy)
{
    bool changed = false;
    changed |= osw_drv_phy_process_capab_tx_chain(phy);
    return changed;
}

static bool
osw_drv_phy_process_changed(struct osw_drv_phy *phy)
{
    bool changed = false;

    if (phy->new_state.exists == true) {
        changed |= osw_drv_phy_process_capab(phy);
    }

    const bool exists_and_stays = phy->cur_state.exists == true
                               && phy->new_state.exists == true;
    if (exists_and_stays) {
        /* FIXME: This has print side-effects so be careful
         * with moving this someplace else, especially as an
         * expression.
         */
        changed |= osw_drv_phy_state_is_changed(phy);
    }

    return changed;
}

static bool
osw_drv_phy_has_any_channel_state_in(struct osw_drv_phy *phy,
                                     enum osw_channel_state_dfs dfs)
{
    const struct osw_channel_state *states = phy->cur_state.channel_states;
    const size_t n =  phy->cur_state.n_channel_states;
    size_t i;
    for (i = 0; i < n; i++) {
        const struct osw_channel_state *cs = &states[i];
        if (cs->dfs_state == dfs)
            return true;
    }
    return false;
}

static void
osw_drv_phy_sync_update(struct osw_drv_phy *phy,
                        const char *name,
                        const enum osw_channel_state_dfs state,
                        ev_timer *timer,
                        float delay_seconds)
{
    const bool found = osw_drv_phy_has_any_channel_state_in(phy, state);
    const bool was_armed = !!ev_is_active(timer);
    const char *action = (was_armed && !found) ? "stopping" :
                         (!was_armed && found) ? "starting" :
                         NULL;

    if (action != NULL) {
        struct osw_drv *drv = phy->drv;
        const char *drv_name = drv->ops->name ?: "";
        const char *phy_name = phy->phy_name;
        LOGI("osw: drv: %s/%s: %s %s sync timer",
              drv_name,
              phy_name,
              action,
              name);
    }

    ev_timer_stop(EV_DEFAULT_ timer);
    if (!found) return;
    ev_timer_set(timer, delay_seconds, delay_seconds);
    ev_timer_start(EV_DEFAULT_ timer);
}

static void
osw_drv_phy_cac_sync_update(struct osw_drv_phy *phy)
{
    osw_drv_phy_sync_update(phy,
                            "cac",
                            OSW_CHANNEL_DFS_CAC_IN_PROGRESS,
                            &phy->cac_sync,
                            OSW_DRV_CAC_SYNC_SECONDS);
}

static void
osw_drv_phy_nol_sync_update(struct osw_drv_phy *phy)
{
    osw_drv_phy_sync_update(phy,
                            "nol",
                            OSW_CHANNEL_DFS_NOL,
                            &phy->nol_sync,
                            OSW_DRV_NOL_SYNC_SECONDS);
}

static void
osw_drv_phy_process_state(struct osw_drv_phy *phy)
{
    const bool added = phy->cur_state.exists == false
                    && phy->new_state.exists == true;
    const bool removed = phy->cur_state.exists == true
                      && phy->new_state.exists == false;

    if (removed == true) {
        if (osw_drv_phy_vifs_are_settled(phy) == false) {
            return;
        }

        if (ds_tree_is_empty(&phy->vif_tree) == false) {
            osw_drv_phy_set_vif_list_valid(phy, false);
            return;
        }
    }

    const bool changed = osw_drv_phy_process_changed(phy);

    osw_drv_phy_mark_radar(phy);

    osw_drv_phy_assign_state(&phy->cur_state, &phy->new_state);
    osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_PROCESSED);

    phy->pub.phy_name = phy->phy_name;
    phy->pub.drv = phy->drv;
    phy->pub.drv_state = &phy->cur_state;
    phy->pub.drv_capab = &phy->capab;

    osw_drv_phy_cac_sync_update(phy);
    osw_drv_phy_nol_sync_update(phy);

    if (added == true) osw_drv_phy_dump(phy);
    if (added == true) OSW_STATE_NOTIFY(phy_added_fn, &phy->pub);
    if (changed == true) OSW_STATE_NOTIFY(phy_changed_fn, &phy->pub);
    if (removed == true) OSW_STATE_NOTIFY(phy_removed_fn, &phy->pub);
}

static void
osw_drv_phy_work(struct osw_drv_phy *phy)
{
    struct osw_drv_vif *vif;
    struct osw_drv_vif *tmp;

    switch (phy->obj.state) {
        case OSW_DRV_OBJ_INVALID:
            osw_drv_phy_request_state(phy);
            break;
        case OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED:
        case OSW_DRV_OBJ_REQUESTED:
            /* Waiting for driver to call osw_drv_report_phy_state().
             * That'll move the state to VALID.
             */
            break;
        case OSW_DRV_OBJ_VALID:
            osw_drv_phy_process_state(phy);
            break;
        case OSW_DRV_OBJ_PROCESSED:
            if (phy->cur_state.exists == false) {
                osw_drv_phy_free(phy);
                return;
            }
            break;
    }

    if (phy->vif_list_valid == false) {
        osw_drv_vif_enumerate(phy);
        osw_drv_phy_set_vif_list_valid(phy, true);
    }

    ds_tree_foreach_safe(&phy->vif_tree, vif, tmp)
        osw_drv_vif_work(vif);
}

static bool
osw_drv_frame_tx_desc_is_orphaned(struct osw_drv *drv,
                                  struct osw_drv_frame_tx_desc *desc)
{
    struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, desc->phy_name.buf);
    if (phy == NULL) return true;
    if (phy->cur_state.exists == false) return true;

    const char *vif_name = strlen(desc->vif_name.buf) > 0 ? desc->vif_name.buf : NULL;
    if (vif_name == NULL) return false; /* can't tell */

    struct osw_drv_vif *vif = vif = ds_tree_find(&phy->vif_tree, vif_name);
    if (vif == NULL) return true;
    if (vif->cur_state.exists == false) return true;

    return false;
}

static void
osw_drv_frame_tx_drop_orphaned_frames(struct osw_drv *drv)
{
    assert(drv != NULL);

    struct osw_drv_frame_tx_desc *desc;
    struct osw_drv_frame_tx_desc *tmp;

    ds_dlist_foreach_safe(&drv->frame_tx_list, desc, tmp) {
        const bool is_orphaned = osw_drv_frame_tx_desc_is_orphaned(drv, desc);
        if (is_orphaned) {
            LOGD(LOG_PREFIX_TX_DESC(desc, "orphaning"));
            osw_drv_frame_tx_desc_reset(desc);
        }
    }
}

static void
osw_drv_frame_tx_process_frames(struct osw_drv *drv)
{
    assert(drv != NULL);

    while (ds_dlist_is_empty(&drv->frame_tx_list) == false) {
        struct osw_drv_frame_tx_desc *desc = ds_dlist_head(&drv->frame_tx_list);
        const char *vif_name = NULL;
        const uint64_t timeout_at = osw_time_mono_clk() + OSW_TIME_SEC(OSW_DRV_TX_TIMEOUT_SECONDS);

        switch (desc->state) {
            case OSW_DRV_FRAME_TX_STATE_UNUSED:
                LOGD("osw: drv: unused frame tx on pending list, resetting frame");
                break;
            case OSW_DRV_FRAME_TX_STATE_PENDING:
                vif_name = strlen(desc->vif_name.buf) > 0 ? desc->vif_name.buf : NULL;
                assert(drv->ops->push_frame_tx_fn != NULL);
                desc->state = OSW_DRV_FRAME_TX_STATE_PUSHED;
                osw_timer_arm_at_nsec(&desc->expiry, timeout_at);
                LOGT("osw: drv: pushing frame");
                drv->ops->push_frame_tx_fn(drv, desc->phy_name.buf, vif_name, desc);
                if (desc->state == OSW_DRV_FRAME_TX_STATE_PUSHED)
                    return; /* jump out of while() */
                else
                    break;
            case OSW_DRV_FRAME_TX_STATE_PUSHED:
                if (osw_timer_is_armed(&desc->expiry)) {
                    LOGD("osw: drv: frame already pushed to driver, waiting for submission confirmation");
                    return; /* jump out of while() */
                }
                LOGW("osw: drv: frame pushed to driver,"
                     " but timed out waiting for submission/failure report;"
                     " resetting frame");
                break;
            case OSW_DRV_FRAME_TX_STATE_SUBMITTED:
            case OSW_DRV_FRAME_TX_STATE_FAILED:
                LOGD("osw: drv: submitted/failed frame tx at the beginning of pending list, resetting frame");
                osw_timer_disarm(&desc->expiry);
                break;
        }

        /* Reaching here means the descriptor is to be
         * tossed - regardless if it was gracefully handled
         * by the driver, or not.
         */
        osw_drv_frame_tx_desc_reset(desc);
    }
}

static void
osw_drv_frame_tx_work(struct osw_drv *drv)
{
    assert(drv != NULL);

    osw_drv_frame_tx_drop_orphaned_frames(drv);
    osw_drv_frame_tx_process_frames(drv);
}

static void
osw_drv_frame_tx_expiry_cb(struct osw_timer *timer)
{
    struct osw_drv_frame_tx_desc *desc = container_of(timer, struct osw_drv_frame_tx_desc, expiry);
    struct osw_drv *drv = desc->drv;

    osw_drv_frame_tx_work(drv);
}

static void
osw_drv_phy_enumerate_cb(const char *phy_name, void *data)
{
    struct osw_drv_phy *phy = osw_drv_phy_get(data, phy_name);
    phy->obj.exists = true;
}

static void
osw_drv_phy_enumerate(struct osw_drv *drv)
{
    struct osw_drv_phy *phy;

    ds_tree_foreach(&drv->phy_tree, phy) {
        phy->obj.existed = phy->obj.exists;
        phy->obj.exists = false;
    }

    if (drv->unregistered == false) {
        drv->ops->get_phy_list_fn(drv, osw_drv_phy_enumerate_cb, drv);
    }

    ds_tree_foreach(&drv->phy_tree, phy) {
        if (phy->obj.exists != phy->obj.existed) {
            osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_INVALID);
        }
    }
}

static void
osw_drv_free(struct osw_drv *drv)
{
    struct osw_drv_frame_tx_desc *desc;
    struct osw_drv_frame_tx_desc *tmp;

    ds_dlist_foreach_safe(&drv->frame_tx_list, desc, tmp) {
        if (desc->result_fn != NULL)
            desc->result_fn(desc, OSW_FRAME_TX_RESULT_DROPPED, desc->caller_priv);

        ds_dlist_remove(&drv->frame_tx_list, desc);
    }

    OSW_STATE_NOTIFY_REVERSE(drv_removed_fn, drv);
    g_osw_drv_work_done = true;
    ds_tree_remove(&g_osw_drv_tree, drv);
    FREE(drv);
}

static bool
osw_drv_is_settled(struct osw_drv *drv)
{
    struct osw_drv_phy *phy;

    if (drv->phy_list_valid == false)
        return false;

    if (drv->unregistered == true && ds_tree_is_empty(&drv->phy_tree) == true)
        return false;

    ds_tree_foreach(&drv->phy_tree, phy)
        if (osw_drv_phy_is_settled(phy) == false)
            return false;

    return true;
}

bool
osw_drv_work_is_settled(void)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        if (osw_drv_is_settled(drv) == false)
            return false;

    return true;
}

void
osw_drv_work(struct osw_drv *drv)
{
    struct osw_drv_phy *phy;
    struct osw_drv_phy *tmp;

    if (drv->initialized == false) {
        if (drv->unregistered == true)
            goto free;
        drv->ops->init_fn(drv);
        drv->initialized = true;
        g_osw_drv_work_done = true;
    }

    if (drv->phy_list_valid == false) {
        osw_drv_phy_enumerate(drv);
        osw_drv_set_phy_list_valid(drv, true);
    }

    ds_tree_foreach_safe(&drv->phy_tree, phy, tmp)
        osw_drv_phy_work(phy);

    /* FIXME: This should use a specialized
     * osw_state_ helper to check if it's ready to
     * be freed.
     */
free:
    if (drv->unregistered == true && ds_tree_is_empty(&drv->phy_tree) == true)
        osw_drv_free(drv);
}

static void
osw_drv_work_dump_debug(void); // FIXME
void
osw_drv_work_all(void)
{
    g_osw_drv_work_done = false;

    struct osw_drv *drv;
    struct osw_drv *tmp;
    const bool was_settled = g_osw_drv_settled;

    ds_tree_foreach_safe(&g_osw_drv_tree, drv, tmp)
        osw_drv_work(drv);

    const bool is_settled = osw_drv_work_is_settled();

    if (is_settled == false) {
        /* Work processing is designed to work in passes. A
         * single pass isn't guaranteed to be able to
         * process everything because sometimes processing
         * dependency is upwards or downwards in the object
         * hierarchy.
         *
         * If there was no work done, and structures haven't
         * settled it may mean a couple of things:
         *  - driver hasn't generated some events (yet),
         *    hopefully will do that soon. Eg. vif removal
         *    will need sta removals to happen too first.
         *  - driver has been requested to report a
         *    phy/vif/sta state. This has undefined run time.
         */
        if (g_osw_drv_work_done == true)
            osw_drv_work_all_schedule();
        else if (ev_is_active(&g_osw_drv_work_watchdog) == false)
            ev_timer_again(EV_DEFAULT_ &g_osw_drv_work_watchdog);
    } else {
        ev_timer_stop(EV_DEFAULT_ &g_osw_drv_work_watchdog);

        ds_tree_foreach(&g_osw_drv_tree, drv)
            osw_drv_frame_tx_work(drv);
    }

    if (was_settled == false && is_settled == true) OSW_STATE_NOTIFY(idle_fn);
    if (was_settled == true && is_settled == false) OSW_STATE_NOTIFY(busy_fn);
    g_osw_drv_settled = is_settled;
}

bool
osw_drv_poll(void)
{
    int n = 64;
    while (n-- > 0) {
        osw_drv_work_all();
        if (osw_drv_work_is_settled()) return true;
    }
    return false;
}

void
osw_drv_work_all_cb(EV_P_ ev_async *arg, int events)
{
    osw_drv_work_all();
}

void
osw_drv_work_all_schedule(void)
{
    ev_async_send(EV_DEFAULT_ &g_osw_drv_work_all_async);
}

static void
osw_drv_work_all_retry_requests(void)
{
    struct ds_tree *drv_tree = &g_osw_drv_tree;
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    ds_tree_foreach(drv_tree, drv) {
        ds_tree_foreach(&drv->phy_tree, phy) {
            if (phy->obj.state == OSW_DRV_OBJ_REQUESTED) {
                osw_log_drv_retry_phy_request(phy);
                osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_INVALID);
            }

            ds_tree_foreach(&phy->vif_tree, vif) {
                if (vif->obj.state == OSW_DRV_OBJ_REQUESTED) {
                    osw_log_drv_retry_vif_request(vif);
                    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_INVALID);
                }

                ds_tree_foreach(&vif->sta_tree, sta) {
                    if (sta->obj.state == OSW_DRV_OBJ_REQUESTED) {
                        osw_log_drv_retry_sta_request(sta);
                        osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_INVALID);
                    }
                }
            }
        }
    }
}

static void
osw_drv_work_dump_debug(void)
{
    struct osw_drv *drv;
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;
    struct osw_drv_sta *sta;

    ds_tree_foreach(&g_osw_drv_tree, drv) {
        LOGI("osw: drv: settled debug: drv=%s list=%d unreg=%d empty=%d settled=%d",
             drv->ops->name, drv->phy_list_valid, drv->unregistered, ds_tree_is_empty(&drv->phy_tree), osw_drv_is_settled(drv));
        ds_tree_foreach(&drv->phy_tree, phy) {
            LOGI("osw: drv: settled debug: phy=%s list=%d empty=%d state=%d settled=%d",
                 phy->phy_name, phy->vif_list_valid, ds_tree_is_empty(&phy->vif_tree), phy->obj.state, osw_drv_phy_is_settled(phy));
            ds_tree_foreach(&phy->vif_tree, vif) {
                LOGI("osw: drv: settled debug: vif=%s list=%d empty=%d state=%d settled=%d",
                     vif->vif_name, vif->sta_list_valid, ds_tree_is_empty(&vif->sta_tree), vif->obj.state, osw_drv_vif_is_settled(vif));
                ds_tree_foreach(&vif->sta_tree, sta) {
                    LOGI("osw: drv: settled debug: sta=" OSW_HWADDR_FMT " state=%d settled=%d",
                         OSW_HWADDR_ARG(&sta->mac_addr), sta->obj.state, osw_drv_sta_is_settled(sta));
                }
            }
        }
    }
}

static void
osw_drv_work_all_watchdog_cb(EV_P_ ev_timer *arg, int events)
{
    osw_log_drv_watchdog();
    osw_drv_work_dump_debug();
    osw_drv_work_all_retry_requests();
    osw_drv_work_all_schedule();
}

static struct osw_drv_phy *
osw_drv_phy_from_report(struct osw_drv *drv,
                       const char *phy_name)
{
    if (WARN_ON(drv == NULL)) return NULL;
    if (drv->unregistered == true) return NULL;
    if (WARN_ON(phy_name == NULL)) return NULL;

    struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, phy_name);
    if (phy == NULL) {
        osw_drv_set_phy_list_valid(drv, false);
        phy = osw_drv_phy_get(drv, phy_name);
    }

    return phy;
}

static struct osw_drv_vif *
osw_drv_vif_from_report(struct osw_drv *drv,
                        const char *phy_name,
                        const char *vif_name)
{
    if (WARN_ON(drv == NULL)) return NULL;
    if (WARN_ON(vif_name == NULL)) return NULL;

    struct osw_drv_phy *phy = osw_drv_phy_from_report(drv, phy_name);
    if (WARN_ON(phy == NULL)) return NULL;

    struct osw_drv_vif *vif = ds_tree_find(&phy->vif_tree, vif_name);
    if (vif == NULL) {
        osw_drv_phy_set_vif_list_valid(phy, false);
        vif = osw_drv_vif_get(phy, vif_name);
    }

    return vif;
}

static struct osw_drv_sta *
osw_drv_sta_from_report(struct osw_drv *drv,
                        const char *phy_name,
                        const char *vif_name,
                        const struct osw_hwaddr *sta_addr)
{
    if (WARN_ON(drv == NULL)) return NULL;
    if (WARN_ON(sta_addr == NULL)) return NULL;

    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (WARN_ON(vif == NULL)) return NULL;

    struct osw_drv_sta *sta = ds_tree_find(&vif->sta_tree, sta_addr);
    if (sta == NULL) {
        osw_drv_vif_set_sta_list_valid(vif, false);
        sta = osw_drv_sta_get(vif, sta_addr);
    }

    return sta;
}

static void
osw_drv_report_frame_tx_state(struct osw_drv *drv,
                              enum osw_drv_frame_tx_desc_state state)
{
    assert(drv != NULL);
    assert(state == OSW_DRV_FRAME_TX_STATE_SUBMITTED || state == OSW_DRV_FRAME_TX_STATE_FAILED);

    struct osw_drv_frame_tx_desc *desc = NULL;

    desc = ds_dlist_head(&drv->frame_tx_list);
    if (desc == NULL) {
        LOGD("osw: drv: drv reported state for nonexistent frame tx");
        return;
    }

    if (desc->state != OSW_DRV_FRAME_TX_STATE_PUSHED) {
        LOGD("osw: drv: drv reported state for non-'pending' frame tx");
        return;
    }

    desc->state = state;
    osw_drv_work_all_schedule();
}

void
osw_drv_report_phy_state(struct osw_drv *drv,
                         const char *phy_name,
                         const struct osw_drv_phy_state *state)
{
    struct osw_drv_phy *phy = osw_drv_phy_from_report(drv, phy_name);
    if (WARN_ON(phy == NULL)) return;
    if (phy->obj.state != OSW_DRV_OBJ_REQUESTED &&
        phy->obj.state != OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED) return;

    osw_drv_phy_set_state(phy, state->exists ? state : NULL);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_phy_changed(struct osw_drv *drv,
                           const char *phy_name)
{
    struct osw_drv_phy *phy = osw_drv_phy_from_report(drv, phy_name);
    if (WARN_ON(phy == NULL)) return;

    osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_INVALID);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_vif_state(struct osw_drv *drv,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_drv_vif_state *state)
{
    if (WARN_ON(state == NULL)) return;

    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (WARN_ON(vif == NULL)) return;
    if (vif->obj.state != OSW_DRV_OBJ_REQUESTED &&
        vif->obj.state != OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED) return;

    osw_drv_vif_set_state(vif, state->exists ? state : NULL);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_vif_changed(struct osw_drv *drv,
                           const char *phy_name,
                           const char *vif_name)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (WARN_ON(vif == NULL)) return;

    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_INVALID);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_vif_channel_change_started(struct osw_drv *drv,
                                          const char *phy_name,
                                          const char *vif_name,
                                          const struct osw_channel *target_channel)
{
    if (WARN_ON(target_channel == NULL)) return;

    struct osw_drv_phy *phy = osw_drv_phy_from_report(drv, phy_name);
    if (WARN_ON(phy == NULL)) return;

    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (WARN_ON(vif == NULL)) return;

    osw_drv_obj_set_state(&phy->obj, OSW_DRV_OBJ_INVALID);
    osw_drv_obj_set_state(&vif->obj, OSW_DRV_OBJ_INVALID);
    osw_drv_work_all_schedule();

    if (target_channel != NULL)
        vif->csa_channel = *target_channel;
}

static bool
osw_drv_phy_supports_channel(const struct osw_drv_phy *phy,
                             const struct osw_channel *c)
{
    const struct osw_channel_state *arr = phy->cur_state.channel_states;
    const size_t n = phy->cur_state.n_channel_states;
    size_t i;
    for (i = 0; i < n; i++) {
        const struct osw_channel *c2 = &arr[i].channel;
        /* FIXME: This should verify if the width is also
         * doable by checking adjacent channels. *
         */
        if (c2->control_freq_mhz == c->control_freq_mhz)
            return true;
    }
    return false;
}

static struct osw_drv_phy *
osw_drv_phy_lookup_for_channel(struct ds_tree *drvs,
                               const struct osw_channel *c)
{
    struct osw_drv *drv;
    ds_tree_foreach(drvs, drv) {
        struct osw_drv_phy *phy;
        ds_tree_foreach(&drv->phy_tree, phy) {
            if (osw_drv_phy_supports_channel(phy, c) == true) {
                return phy;
            }
        }
    }
    return NULL;
}

static void
osw_drv_report_vif_channel_change_advertised_xphy__(struct ds_tree *drvs,
                                                    struct osw_drv *drv,
                                                    const char *phy_name,
                                                    const char *vif_name,
                                                    const struct osw_channel *channel,
                                                    struct osw_drv_phy **phy,
                                                    struct osw_drv_vif **vif)
{
    *phy = NULL;
    *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (*vif == NULL) return;

    osw_drv_obj_set_state(&vif[0]->obj, OSW_DRV_OBJ_INVALID);

    if (vif[0]->cur_state.vif_type != OSW_VIF_STA) return;
    if (channel == NULL) return;

    *phy = osw_drv_phy_lookup_for_channel(drvs, channel);
}

static void
osw_drv_report_vif_channel_change_advertised_xphy(struct osw_drv *drv,
                                                  const char *phy_name,
                                                  const char *vif_name,
                                                  const struct osw_channel *channel)
{
    struct ds_tree *drvs = &g_osw_drv_tree;
    struct osw_drv_phy *phy_reported = osw_drv_phy_from_report(drv, phy_name);
    struct osw_drv_phy *phy;
    struct osw_drv_vif *vif;

    if (WARN_ON(channel == NULL)) return;

    osw_drv_report_vif_channel_change_advertised_xphy__(drvs, drv, phy_name, vif_name, channel, &phy, &vif);
    if (phy == NULL) {
        LOGI("osw: drv: %s/%s/%s: csa: "OSW_CHANNEL_FMT": cannot switch; no phy is capable",
                drv->ops->name,
                phy_name,
                vif_name,
                OSW_CHANNEL_ARG(channel));
        return;
    }
    if (phy_reported == phy) {
        LOGD("osw: drv: %s/%s/%s: csa: "OSW_CHANNEL_FMT": not a cross-phy switch",
             drv->ops->name,
             phy_name,
             vif_name,
             OSW_CHANNEL_ARG(channel));
        return;
    }

    if (vif->cur_state.exists == false) return;
    if (phy->cur_state.exists == false) return;
    if (vif->cur_state.vif_type != OSW_VIF_STA) return;

    OSW_STATE_NOTIFY(vif_csa_to_phy_fn, &vif->pub, &phy->pub, channel);
}

static void
osw_drv_report_vif_channel_change_advertised_onphy(struct osw_drv *drv,
                                                   const char *phy_name,
                                                   const char *vif_name,
                                                   const struct osw_channel *channel)
{
    struct ds_tree *drvs = &g_osw_drv_tree;
    const struct osw_drv_phy *phy_reported = osw_drv_phy_from_report(drv, phy_name);

    if (WARN_ON(channel == NULL)) return;
    const struct osw_drv_phy *phy = osw_drv_phy_lookup_for_channel(drvs, channel);
    if (phy == NULL) {
        LOGI("osw: drv: %s/%s/%s: csa: "OSW_CHANNEL_FMT": cannot switch; no phy is capable",
                drv->ops->name,
                phy_name,
                vif_name,
                OSW_CHANNEL_ARG(channel));
        return;
    }
    if (phy_reported != phy) {
        LOGD("osw: drv: %s/%s/%s: csa: "OSW_CHANNEL_FMT": not a on-phy switch",
             drv->ops->name,
             phy_name,
             vif_name,
             OSW_CHANNEL_ARG(channel));
        return;
    }

    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (vif == NULL) return;
    if (vif->cur_state.exists == false) return;
    if (vif->cur_state.vif_type != OSW_VIF_STA) return;
    OSW_STATE_NOTIFY(vif_csa_rx_fn, &vif->pub, channel);
}

void
osw_drv_report_vif_channel_change_advertised(struct osw_drv *drv,
                                             const char *phy_name,
                                             const char *vif_name,
                                             const struct osw_channel *channel)
{
    osw_drv_report_vif_channel_change_advertised_onphy(drv, phy_name, vif_name, channel);
    osw_drv_report_vif_channel_change_advertised_xphy(drv, phy_name, vif_name, channel);
}

void
osw_drv_report_vif_wps_success(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (vif == NULL) return;
    if (vif->cur_state.exists == false) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
    OSW_STATE_NOTIFY(wps_success_fn, &vif->pub);
}

void
osw_drv_report_vif_wps_overlap(struct osw_drv *drv,
                               const char *phy_name,
                               const char *vif_name)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (vif == NULL) return;
    if (vif->cur_state.exists == false) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
    OSW_STATE_NOTIFY(wps_overlap_fn, &vif->pub);
}

void
osw_drv_report_vif_wps_pbc_timeout(struct osw_drv *drv,
                                   const char *phy_name,
                                   const char *vif_name)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (vif == NULL) return;
    if (vif->cur_state.exists == false) return;

    osw_drv_report_vif_changed(drv, phy_name, vif_name);
    OSW_STATE_NOTIFY(wps_pbc_timeout_fn, &vif->pub);
}

void
osw_drv_report_sta_state(struct osw_drv *drv,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_hwaddr *mac_addr,
                         const struct osw_drv_sta_state *state)
{
    struct osw_drv_sta *sta = osw_drv_sta_from_report(drv, phy_name, vif_name, mac_addr);
    if (WARN_ON(sta == NULL)) return;
    if (sta->obj.state != OSW_DRV_OBJ_REQUESTED &&
        sta->obj.state != OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED) return;

    osw_drv_sta_set_state(sta, state->connected ? state : NULL);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_sta_changed(struct osw_drv *drv,
                           const char *phy_name,
                           const char *vif_name,
                           const struct osw_hwaddr *mac_addr)
{
    struct osw_drv_sta *sta = osw_drv_sta_from_report(drv, phy_name, vif_name, mac_addr);
    if (WARN_ON(sta == NULL)) return;

    osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_INVALID);
    osw_drv_work_all_schedule();
}

void
osw_drv_report_vif_probe_req(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_drv_report_vif_probe_req *probe_req)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (WARN_ON(vif == NULL)) return;
    if (vif->cur_state.exists == false) return;

    OSW_STATE_NOTIFY(vif_probe_req_fn, &vif->pub, probe_req);
}

static void
osw_drv_process_vif_frame_rx_internal_probe_req(struct osw_drv *drv,
                                                const char *phy_name,
                                                const char *vif_name,
                                                const struct osw_drv_vif_frame_rx *rx,
                                                const struct osw_drv_dot11_frame_header *hdr,
                                                const void *ies,
                                                const size_t ies_len)
{
    const struct osw_hwaddr *bssid = osw_hwaddr_from_cptr_unchecked(hdr->bssid);
    const struct osw_hwaddr *sta_addr = osw_hwaddr_from_cptr_unchecked(hdr->sa);
    struct osw_drv_phy *phy = ds_tree_find(&drv->phy_tree, phy_name);
    if (phy == NULL) return;
    if (phy->cur_state.exists == false) return;

    size_t ssid_len;
    const uint8_t ssid_eid = 0;
    const void *ssid_buf = osw_ie_find(ies, ies_len, ssid_eid, &ssid_len);
    const unsigned int snr = rx->snr;

    struct osw_drv_vif *vif;
    ds_tree_foreach(&phy->vif_tree, vif) {
        if (vif->cur_state.exists == false) continue;
        if (osw_hwaddr_is_to_addr(bssid, &vif->cur_state.mac_addr)) {
            const char *vif_name = vif->vif_name;
            struct osw_ssid ssid;
            const bool ssid_missing = osw_ssid_from_cbuf(&ssid, ssid_buf, ssid_len);
            if (ssid_missing && ies_len == 0) {
                if (vif->cur_state.vif_type == OSW_VIF_AP) {
                    ssid = vif->cur_state.u.ap.ssid;
                }
            }
            const struct osw_drv_report_vif_probe_req probe_req = {
                .sta_addr = *sta_addr,
                .snr = snr,
                .ssid = ssid,
            };
            osw_drv_report_vif_probe_req(drv, phy_name, vif_name, &probe_req);
        }
    }
}

static void
osw_drv_process_vif_frame_rx_internal(struct osw_drv *drv,
                                      const char *phy_name,
                                      const char *vif_name,
                                      const struct osw_drv_vif_frame_rx *rx)
{
    const void *data = rx->data;
    const size_t len = rx->len;
    size_t rem;

    if (drv == NULL) return;
    if (phy_name == NULL) return;
    if (vif_name == NULL) return;

    const struct osw_drv_dot11_frame_header *hdr = ieee80211_frame_into_header(data, len, rem);
    if (hdr == NULL) {
        return;
    }

    const void *ies = hdr + 1;
    const size_t ies_len = rem;

    const uint16_t fc = le16toh(hdr->frame_control);
    const uint16_t subtype = (fc & DOT11_FRAME_CTRL_SUBTYPE_MASK);
    switch (subtype) {
        case DOT11_FRAME_CTRL_SUBTYPE_PROBE_REQ:
            osw_drv_process_vif_frame_rx_internal_probe_req(drv, phy_name, vif_name, rx, hdr, ies, ies_len);
            break;
        case DOT11_FRAME_CTRL_SUBTYPE_ASSOC_REQ: {
            const struct osw_drv_dot11_frame_assoc_req *assoc_req = ieee80211_frame_into_assoc_req(data, len, rem);
            WARN_ON(assoc_req == NULL);
            if (assoc_req) {
                const struct osw_hwaddr *sta_addr = osw_hwaddr_from_cptr_unchecked(hdr->sa);
                const void *ies = assoc_req->variable;
                osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, sta_addr, ies, rem);
            }
            break;
        }
        case DOT11_FRAME_CTRL_SUBTYPE_REASSOC_REQ: {
            const struct osw_drv_dot11_frame_reassoc_req *reassoc_req = ieee80211_frame_into_reassoc_req(data, len, rem);
            WARN_ON(reassoc_req == NULL);
            if (reassoc_req) {
                const struct osw_hwaddr *sta_addr = osw_hwaddr_from_cptr_unchecked(hdr->sa);
                const void *ies = reassoc_req->variable;
                osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, sta_addr, ies, rem);
            }
            break;
        }
    }
}

void
osw_drv_report_vif_frame_rx(struct osw_drv *drv,
                            const char *phy_name,
                            const char *vif_name,
                            const struct osw_drv_vif_frame_rx *rx)
{
    assert(rx != NULL);

    const void *data = rx->data;
    const size_t len = rx->len;

    if (data == NULL) return;
    if (len == 0) return;

    osw_drv_process_vif_frame_rx_internal(drv, phy_name, vif_name, rx);

    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    if (vif == NULL || vif->cur_state.exists == false) {
        return;
    }

    OSW_STATE_NOTIFY(vif_frame_rx_fn, &vif->pub, data, len);
}

void
osw_drv_report_stats(struct osw_drv *drv,
                     const struct osw_tlv *tlv)
{
    osw_stats_put(tlv);
}

void
osw_drv_report_stats_reset(enum osw_stats_id id)
{
    osw_stats_reset_last(id);
}

void
osw_drv_register_ops(const struct osw_drv_ops *ops)
{
    struct osw_drv *drv;

    osw_log_drv_register_ops(ops);

    assert(ops != NULL);
    assert(ops->init_fn != NULL);
    assert(ops->get_phy_list_fn != NULL);
    assert(ops->get_vif_list_fn != NULL);
    assert(ops->get_sta_list_fn != NULL);
    assert(ops->request_sta_state_fn != NULL);
    assert(ops->request_vif_state_fn != NULL);
    assert(ops->request_phy_state_fn != NULL);
    assert(ds_tree_find(&g_osw_drv_tree, ops) == NULL);

    drv = CALLOC(1, sizeof(*drv));
    drv->ops = ops;
    ds_tree_init(&drv->phy_tree, ds_str_cmp, struct osw_drv_phy, node);
    ds_dlist_init(&drv->frame_tx_list, struct osw_drv_frame_tx_desc, node);
    ds_tree_insert(&g_osw_drv_tree, drv, ops);
    OSW_STATE_NOTIFY(drv_added_fn, drv);
    osw_drv_work_all_schedule();
}

void
osw_drv_unregister_ops(const struct osw_drv_ops *ops)
{
    struct osw_drv *drv = ds_tree_find(&g_osw_drv_tree, ops);

    osw_log_drv_unregister_ops(ops);

    osw_drv_work_all_schedule();

    if (drv == NULL) return;
    if (drv->unregistered == true) return;

    drv->unregistered = true;
    drv->phy_list_valid = false;
}

void
osw_drv_set_priv(struct osw_drv *drv, void *priv)
{
    drv->priv = priv;
}

void *
osw_drv_get_priv(struct osw_drv *drv)
{
    return drv->priv;
}

const struct osw_drv_ops *
osw_drv_get_ops(struct osw_drv *drv)
{
    return drv->ops;
}

bool
osw_drv_conf_changed(const struct osw_drv_conf *drv_conf)
{
    size_t i;
    for (i = 0; i < drv_conf->n_phy_list; i++) {
        const struct osw_drv_phy_config *phy = &drv_conf->phy_list[i];

        if (drv_conf->phy_list[i].changed)
            return true;

        size_t j;
        for (j = 0; j < phy->vif_list.count; j++)
            if (phy->vif_list.list[j].changed)
                return true;
    }
    return false;
}

static void
osw_drv_conf_free_vif_sta(struct osw_drv_vif_config_sta *sta)
{
    while (sta->network != NULL) {
        struct osw_drv_vif_sta_network *next = sta->network->next;
        FREE(sta->network);
        sta->network = next;
    }
}
void
osw_drv_conf_free(struct osw_drv_conf *conf)
{
    size_t i;
    size_t j;

    if (conf == NULL)
        return;

    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &conf->phy_list[i];

        for (j = 0; j < phy->vif_list.count; j++) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[j];

            FREE(vif->vif_name);
            switch (vif->vif_type) {
                case OSW_VIF_UNDEFINED:
                    break;
                case OSW_VIF_AP:
                    FREE(vif->u.ap.psk_list.list);
                    FREE(vif->u.ap.acl.list);
                    FREE(vif->u.ap.acl_add.list);
                    FREE(vif->u.ap.acl_del.list);
                    FREE(vif->u.ap.neigh_list.list);
                    FREE(vif->u.ap.neigh_add_list.list);
                    FREE(vif->u.ap.neigh_mod_list.list);
                    FREE(vif->u.ap.neigh_del_list.list);
                    FREE(vif->u.ap.neigh_ft_list.list);
                    FREE(vif->u.ap.wps_cred_list.list);
                    osw_radius_list_free(&vif->u.ap.radius_list);
                    osw_radius_list_free(&vif->u.ap.acct_list);
                    osw_passpoint_free_internal(&vif->u.ap.passpoint);
                    break;
                case OSW_VIF_AP_VLAN:
                    break;
                case OSW_VIF_STA:
                    osw_drv_conf_free_vif_sta(&vif->u.sta);
                    break;
            }
        }

        FREE(phy->phy_name);
        FREE(phy->vif_list.list);
    }

    FREE(conf->phy_list);
    FREE(conf);
    g_osw_drv_work_done = true;
}

struct osw_drv_vif_config *
osw_drv_conf_lookup_vif(struct osw_drv_conf *conf,
                        const char *phy_name,
                        const char *vif_name)
{
    if (conf == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    const bool wildcard = (phy_name == NULL);
    size_t i;
    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &conf->phy_list[i];
        const bool phy_name_matches = wildcard || (strcmp(phy_name, phy->phy_name) == 0);
        const bool phy_name_does_not_match = !phy_name_matches;
        if (phy_name_does_not_match) continue;

        size_t j;
        for (j = 0; j < phy->vif_list.count; j++) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[j];
            const bool vif_name_matches = (strcmp(vif_name, vif->vif_name) == 0);
            const bool vif_name_does_not_match = !vif_name_matches;
            if (vif_name_does_not_match) continue;

            return vif;
        }
    }

    return NULL;
}

static void
osw_drv_init(void)
{
    static bool initialized;

    if (initialized == true) return;

    ev_async_init(&g_osw_drv_work_all_async, osw_drv_work_all_cb);
    ev_async_start(EV_DEFAULT_ &g_osw_drv_work_all_async);
    ev_unref(EV_DEFAULT);

    ev_timer_init(&g_osw_drv_work_watchdog,
                  osw_drv_work_all_watchdog_cb,
                  OSW_DRV_WORK_ALL_WATCHDOG_SECONDS,
                  OSW_DRV_WORK_ALL_WATCHDOG_SECONDS);

    initialized = true;
    osw_drv_work_all_schedule();
}

void
osw_drv_unregister_all(void)
{
    struct osw_drv *drv;

    ds_tree_foreach(&g_osw_drv_tree, drv)
        osw_drv_unregister_ops(drv->ops);
}

void
osw_drv_report_frame_tx_state_submitted(struct osw_drv *drv)
{
    assert(drv != NULL);
    osw_drv_report_frame_tx_state(drv, OSW_DRV_FRAME_TX_STATE_SUBMITTED);
}

void
osw_drv_report_frame_tx_state_failed(struct osw_drv *drv)
{
    assert(drv != NULL);
    osw_drv_report_frame_tx_state(drv, OSW_DRV_FRAME_TX_STATE_FAILED);
}

const char*
osw_frame_tx_result_to_cstr(enum osw_frame_tx_result result)
{
    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            return "submitted";
        case OSW_FRAME_TX_RESULT_FAILED:
            return "failed";
        case OSW_FRAME_TX_RESULT_DROPPED:
            return "dropped";
    }

    return NULL;
}

const uint8_t*
osw_drv_frame_tx_desc_get_frame(const struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    return desc->frame;
}

size_t
osw_drv_frame_tx_desc_get_frame_len(const struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    return desc->frame_len;
}

bool
osw_drv_frame_tx_desc_has_channel(const struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    const bool has_channel = (desc->channel.control_freq_mhz != 0);
    return has_channel;
}

const struct osw_channel *
osw_drv_frame_tx_desc_get_channel(const struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    if (desc->channel.control_freq_mhz != 0)
        return &desc->channel;

    const char *phy_name = desc->phy_name.buf;
    const char *vif_name = desc->vif_name.buf;
    const struct osw_drv_vif *vif = osw_drv_vif_lookup_by_name(phy_name, vif_name);
    if (vif == NULL) return NULL;

    return osw_drv_vif_get_channel(&vif->cur_state);
}

struct osw_drv_frame_tx_desc*
osw_drv_frame_tx_desc_new(osw_drv_frame_tx_result_fn_t *result_fn,
                          void *caller_priv)
{
    struct osw_drv_frame_tx_desc *desc = CALLOC(1, sizeof(*desc));

    osw_timer_init(&desc->expiry, osw_drv_frame_tx_expiry_cb);
    desc->result_fn = result_fn;
    desc->caller_priv = caller_priv;

    return desc;
}

void
osw_drv_frame_tx_desc_free(struct osw_drv_frame_tx_desc *desc)
{
    if (desc == NULL)
        return;
    if (desc->freeing)
        return;

    desc->freeing = true;
    osw_drv_frame_tx_desc_reset(desc);
    FREE(desc);
}

void
osw_drv_frame_tx_desc_cancel(struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    osw_drv_frame_tx_desc_reset(desc);
}

bool
osw_drv_frame_tx_desc_is_scheduled(const struct osw_drv_frame_tx_desc *desc)
{
    assert(desc != NULL);
    return desc->list != NULL;
}

void
osw_drv_frame_tx_desc_set_channel(struct osw_drv_frame_tx_desc *desc,
                                  const struct osw_channel *channel)
{
    assert(desc != NULL);
    memset(&desc->channel, 0, sizeof(desc->channel));
    if (channel != NULL) desc->channel = *channel;
}

void
osw_drv_frame_tx_desc_set_frame(struct osw_drv_frame_tx_desc *desc,
                                const uint8_t *data,
                                size_t data_len)
{
    assert(desc != NULL);
    assert(data != NULL);
    assert(data_len > 0);

    FREE(desc->frame);
    desc->frame = MEMNDUP(data, data_len);
    desc->frame_len = data_len;
}

void
osw_drv_report_sta_assoc_ies(struct osw_drv *drv,
                             const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *sta_addr,
                             const void *ies,
                             const size_t ies_len)
{
    struct osw_drv_sta *sta = osw_drv_sta_from_report(drv, phy_name, vif_name, sta_addr);
    const struct osw_hwaddr zero = {0};
    char ies_hex[4096];

    if (drv == NULL) return;
    if (phy_name == NULL) return;
    if (vif_name == NULL) return;

    MEMZERO(ies_hex);
    bin2hex(ies, ies_len, ies_hex, sizeof(ies_hex));

    LOGT("osw: drv: %s/%s/%s/"OSW_HWADDR_FMT": report assoc ies: %s (len=%zu)",
         drv->ops->name,
         phy_name ?: "",
         vif_name ?: "",
         OSW_HWADDR_ARG(sta_addr ?: &zero),
         ies_hex,
         ies_len);

    if (ies == NULL) return;
    if (ies_len == 0) return;

    osw_drv_buf_set(&sta->new_ies, ies, ies_len);
    if (osw_drv_buf_is_same(&sta->cur_ies, &sta->new_ies) == true) return;

    /* Make sure the osw_drv_sta entry doesn't get cleared
     * up for a little bit. If there's assoc ies report then
     * it is likely it'll be followed up by a connection
     * report.
     */
    const bool ies_before_connect = (sta->cur_state.connected == false);
    if (ies_before_connect) {
        osw_drv_sta_ies_hold(sta);
    }

    switch (sta->obj.state) {
        case OSW_DRV_OBJ_INVALID:
            break;
        case OSW_DRV_OBJ_REQUESTED_AND_INVALIDATED:
        case OSW_DRV_OBJ_REQUESTED:
            break;
        case OSW_DRV_OBJ_VALID:
            break;
        case OSW_DRV_OBJ_PROCESSED:
            /* There's no need to invalidate the entire sta
             * state if it's already in a processed state.
             * This only needs to go through the process
             * logic to figure out if IEs changed and report
             * that back up.
             */
            osw_drv_obj_set_state(&sta->obj, OSW_DRV_OBJ_VALID);
            osw_drv_work_all_schedule();
            break;
    }
}

void
osw_drv_invalidate(struct osw_drv *drv)
{
    if (WARN_ON(drv == NULL)) return;
    if (drv->initialized == false) return;
    if (drv->unregistered == true) return;

    struct osw_drv_phy *phy;

    osw_drv_set_phy_list_valid(drv, false);

    ds_tree_foreach(&drv->phy_tree, phy) {
        const char *phy_name = phy->phy_name;
        struct osw_drv_vif *vif;

        osw_drv_report_phy_changed(drv, phy_name);
        osw_drv_phy_set_vif_list_valid(phy, false);
        ds_tree_foreach(&phy->vif_tree, vif) {
            const char *vif_name = vif->vif_name;
            osw_drv_report_vif_changed(drv, phy_name, vif_name);

            osw_drv_vif_set_sta_list_valid(vif, false);
            struct osw_drv_sta *sta;
            ds_tree_foreach(&vif->sta_tree, sta) {
                const struct osw_hwaddr *sta_addr = &sta->mac_addr;
                osw_drv_report_sta_changed(drv, phy_name, vif_name, sta_addr);
            }
        }
    }
}

void
osw_drv_report_scan_completed(struct osw_drv *drv,
                              const char *phy_name,
                              const char *vif_name,
                              enum osw_drv_scan_complete_reason reason)
{
    struct osw_drv_vif *vif = osw_drv_vif_from_report(drv, phy_name, vif_name);
    osw_drv_vif_scan_done(vif, reason);
}

const char *
osw_drv_scan_reason_to_str(enum osw_drv_scan_complete_reason reason)
{
    switch (reason) {
        case OSW_DRV_SCAN_DONE: return "done";
        case OSW_DRV_SCAN_FAILED: return "failed";
        case OSW_DRV_SCAN_ABORTED: return "aborted";
        case OSW_DRV_SCAN_TIMED_OUT: return "timed out";
    }
    return NULL;
}

void
osw_drv_report_overrun(struct osw_drv *drv)
{
    const char *drv_name = drv ? (drv->ops->name ?: "") : "";
    LOGI("osw: drv: %s: overrun reported", drv_name);
    return osw_drv_invalidate(drv);
}

void
osw_drv_phy_state_report_free(struct osw_drv_phy_state *state)
{
    if (state == NULL) return;

    FREE(state->channel_states);

    memset(state, 0, sizeof(*state));
}

void
osw_drv_vif_state_report_free(struct osw_drv_vif_state *state)
{
    struct osw_drv_vif_sta_network *net;

    if (state == NULL) return;

    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            FREE(state->u.ap.acl.list);
            FREE(state->u.ap.psk_list.list);
            osw_radius_list_free(&state->u.ap.radius_list);
            osw_radius_list_free(&state->u.ap.acct_list);
            FREE(state->u.ap.neigh_list.list);
            FREE(state->u.ap.neigh_ft_list.list);
            osw_passpoint_free_internal(&state->u.ap.passpoint);
            break;
        case OSW_VIF_AP_VLAN:
            FREE(state->u.ap_vlan.sta_addrs.list);
            break;
        case OSW_VIF_STA:
            while ((net = state->u.sta.network) != NULL) {
                state->u.sta.network = net->next;
                FREE(net);
            }
            break;
    }

    memset(state, 0, sizeof(*state));
}

void
osw_drv_sta_state_report_free(struct osw_drv_sta_state *state)
{
    if (state == NULL) return;

    memset(state, 0, sizeof(*state));
}

const char *osw_drv_vif_state_sta_link_status_to_cstr(enum osw_drv_vif_state_sta_link_status s)
{
    switch (s) {
        case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN: return "unknown";
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED: return "connected";
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING: return "connecting";
        case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED: return "disconnected";
    }
    WARN_ON(1);
    return "unexpected";
}

const struct osw_drv_vif_sta_network *
osw_drv_vif_sta_network_get(const struct osw_drv_vif_state_sta *vsta)
{
    if (vsta == NULL) return NULL;
    if (vsta->link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return NULL;

    const struct osw_drv_vif_state_sta_link *link = &vsta->link;
    const struct osw_drv_vif_sta_network *best_fit = NULL;
    const struct osw_drv_vif_sta_network *net;

    /* This is simplified, and should be improved. This
     * covers the common case now only. Perhaps the osw drv
     * impl should indicate which network is currently in
     * use.
     */
    for (net = vsta->network; net != NULL; net = net->next) {
        if (osw_ssid_cmp(&net->ssid, &link->ssid) == 0) {
            if (best_fit == NULL) {
                /* Doesn't matter if bssid is zero at this
                 * point. Just take the first SSID-matching
                 * as best fit first.
                 */
                best_fit = net;
            }
            else if (osw_hwaddr_is_zero(&net->bssid)) {
                /* If the currently inspected network has no
                 * bssid filter, then it is no better than
                 * any current best_fit. Only non-zero bssid
                 * could be considered better.
                 */
                continue;
            }
            else if (osw_hwaddr_is_zero(&best_fit->bssid)) {
                /* If the currently inspected network has a
                 * bssid filter, it makes only sense to use
                 * it best fit.
                 */
                best_fit = net;
            }
            else {
                /* Otherwise.. there are 2 networks that are
                 * already SSID *and* BSSID matched.
                 */
                WARN_ON(1);
            }
        }
    }

    return best_fit;
}

const struct osw_drv_mld_state *
osw_drv_vif_state_get_mld_state(const struct osw_drv_vif_state *state)
{
    if (state == NULL) return NULL;
    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            return &state->u.ap.mld;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            return &state->u.sta.mld;
    }
    return NULL;
}

const char *
osw_drv_mld_state_get_name(const struct osw_drv_mld_state *mld_state)
{
    if (mld_state == NULL) return NULL;
    if (osw_hwaddr_is_zero(&mld_state->addr)) return NULL;
    if (strlen(mld_state->if_name.buf) == 0) return NULL;
    return mld_state->if_name.buf;
}

OSW_MODULE(osw_drv)
{
    osw_drv_init();
    return NULL;
}

#include "osw_drv_ut.c"
