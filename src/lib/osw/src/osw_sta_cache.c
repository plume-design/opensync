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

#include <time.h>
#include <memutil.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <ds_tree.h>
#include <ds_dlist.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_ut.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_sta_cache.h>
#include "osw_sta_i.h"
#include "osw_sta_link_i.h"

#define OSW_STA_GC_PERIOD_SEC 60.
#define OSW_STA_GC_EXPIRATION_SEC OSW_STA_GC_PERIOD_SEC

struct osw_sta_cache {
    struct osw_state_observer state_obs;
    struct ds_dlist observer_list;
    struct ds_tree sta_tree;
    ev_signal sigusr1;
};

static struct osw_sta_link*
osw_sta_link_create(const struct osw_state_vif_info *vif)
{
    ASSERT(vif != NULL, "");
    ASSERT(vif->vif_name != NULL, "");

    struct osw_sta_link *link = CALLOC(1, sizeof(*link));
    STRSCPY_WARN(link->vif_name.buf, vif->vif_name);
    link->vif = vif;
    return link;
}

static void
osw_sta_link_free(struct osw_sta_link *link)
{
    ASSERT(link != NULL, "");

    FREE(link->assoc_req_ies);
    FREE(link);
}

static bool
osw_sta_is_alive(struct osw_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct osw_sta_link *link;
    const uint64_t now_nsec = osw_time_mono_clk();

    ds_tree_foreach(&sta->link_tree, link) {
        if (link->connected == true)
            return true;

        if (link->last_connect_tstamp_nsec > 0 && OSW_TIME_SEC(now_nsec - link->last_connect_tstamp_nsec) <= OSW_STA_GC_EXPIRATION_SEC)
           return true;

        if (link->last_probe_req_tstamp_nsec > 0 && OSW_TIME_SEC(now_nsec - link->last_probe_req_tstamp_nsec) <= OSW_STA_GC_EXPIRATION_SEC)
           return true;
    }

    return false;
}

static void
osw_sta_free(struct osw_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct osw_sta_link *link;
    struct osw_sta_link *tmp_link;
    struct osw_sta_observer *obs;
    struct osw_sta_observer *tmp_obs;

    ds_tree_foreach_safe(&sta->link_tree, link, tmp_link) {
        ds_tree_remove(&sta->link_tree, link);
        osw_sta_link_free(link);
    }
    ds_dlist_foreach_safe(&sta->observer_list, obs, tmp_obs) {
        ds_dlist_remove(&sta->observer_list, obs);
    }
    osw_timer_disarm(&sta->gc);
    FREE(sta);
}

static void
osw_sta_gc_cb(struct osw_timer *timer)
{
    struct osw_sta_cache_observer *obs;
    struct osw_sta *sta = container_of(timer, struct osw_sta, gc);

    if (osw_sta_is_alive(sta) == true)
        return;

    ds_dlist_foreach(&sta->cache->observer_list, obs)
        if (obs->vanished_fn != NULL)
            obs->vanished_fn(obs, sta);

    LOGD("osw: sta: "OSW_HWADDR_FMT": gc absent", OSW_HWADDR_ARG(&sta->mac_addr));
    ds_tree_remove(&sta->cache->sta_tree, sta);
    osw_sta_free(sta);
}

static struct osw_sta*
osw_sta_create(struct osw_sta_cache *cache,
               const struct osw_hwaddr *mac_addr)
{
    ASSERT(mac_addr != NULL, "");

    struct osw_sta *sta = CALLOC(1, sizeof(*sta));

    memcpy(&sta->mac_addr, mac_addr, sizeof(*mac_addr));
    sta->cache = cache;
    ds_tree_init(&sta->link_tree, ds_str_cmp, struct osw_sta_link, node);
    ds_dlist_init(&sta->observer_list, struct osw_sta_observer, node);
    osw_timer_init(&sta->gc, osw_sta_gc_cb);
    osw_timer_arm_at_nsec(&sta->gc, osw_time_mono_clk() + OSW_TIME_SEC(OSW_STA_GC_PERIOD_SEC));

    return sta;
}

static struct osw_sta*
osw_sta_cache_get_sta(struct osw_sta_cache *cache,
                      const struct osw_hwaddr *mac_addr)
{
    ASSERT(mac_addr != NULL, "");

    struct osw_sta_cache_observer *obs;
    struct osw_sta *sta;

    sta = ds_tree_find(&cache->sta_tree, mac_addr);
    if (sta != NULL)
        return sta;

    sta = osw_sta_create(cache, mac_addr);
    ds_tree_insert(&cache->sta_tree, sta, &sta->mac_addr);

    ds_dlist_foreach(&cache->observer_list, obs)
        if (obs->appeared_fn != NULL)
            obs->appeared_fn(obs, sta);

    return sta;
}

static struct osw_sta_link*
osw_sta_get_link(struct osw_sta *sta,
                 const struct osw_state_vif_info *vif)
{
    ASSERT(sta != NULL, "");
    ASSERT(vif != NULL, "");

    struct osw_sta_link *link = ds_tree_find(&sta->link_tree, vif->vif_name);
    if (link != NULL)
        return link;

    link = osw_sta_link_create(vif);
    ds_tree_insert(&sta->link_tree, link, link->vif_name.buf);

    return link;
}

static void
osw_sta_cache_sta_connected_cb(struct osw_state_observer *self,
                               const struct osw_state_sta_info *sta_info)
{
    ASSERT(self != NULL, "");
    ASSERT(sta_info != NULL, "");

    struct osw_sta_observer *obs;
    struct osw_sta_cache *cache = container_of(self, struct osw_sta_cache, state_obs);
    struct osw_sta* sta = osw_sta_cache_get_sta(cache, sta_info->mac_addr);
    struct osw_sta_link *link = osw_sta_get_link(sta, sta_info->vif);

    link->connected = true;
    link->last_connect_tstamp_nsec = OSW_TIME_SEC(sta_info->connected_at);
    FREE(link->assoc_req_ies);
    if (link->assoc_req_ies != NULL && link->assoc_req_ies_len > 0) {
        link->assoc_req_ies = MEMNDUP(sta_info->assoc_req_ies, sta_info->assoc_req_ies_len);
        link->assoc_req_ies_len = sta_info->assoc_req_ies_len;
    }

    ds_dlist_foreach(&sta->observer_list, obs)
        if (obs->connected_fn != NULL)
            obs->connected_fn(obs, sta, link);
}

static void
osw_sta_cache_sta_disconnected_cb(struct osw_state_observer *self,
                                  const struct osw_state_sta_info *sta_info)
{
    ASSERT(self != NULL, "");
    ASSERT(sta_info != NULL, "");

    struct osw_sta_observer *obs;
    struct osw_sta_cache *cache = container_of(self, struct osw_sta_cache, state_obs);
    struct osw_sta* sta = osw_sta_cache_get_sta(cache, sta_info->mac_addr);
    struct osw_sta_link *link = osw_sta_get_link(sta, sta_info->vif);

    link->connected = false;

    ds_dlist_foreach(&sta->observer_list, obs)
        if (obs->disconnected_fn != NULL)
            obs->disconnected_fn(obs, sta, link);
}

static void
osw_sta_cache_vif_removed_cb(struct osw_state_observer *self,
                             const struct osw_state_vif_info *vif)
{
    ASSERT(self != NULL, "");
    ASSERT(vif != NULL, "");

    struct osw_sta* sta;
    struct osw_sta_cache *cache = container_of(self, struct osw_sta_cache, state_obs);

    ds_tree_foreach(&cache->sta_tree, sta) {
        struct osw_sta_link *link = ds_tree_find(&sta->link_tree, vif->vif_name);
        if (link == NULL)
            continue;

        ds_tree_remove(&sta->link_tree, link);
        osw_sta_link_free(link);
    }
}

static void
osw_sta_cache_vif_changed_cb(struct osw_state_observer *self,
                             const struct osw_state_vif_info *vif)
{
    ASSERT(self != NULL, "");
    ASSERT(vif != NULL, "");

    struct osw_sta* sta;
    struct osw_sta_cache *cache = container_of(self, struct osw_sta_cache, state_obs);

    ds_tree_foreach(&cache->sta_tree, sta) {
        struct osw_sta_link *link = ds_tree_find(&sta->link_tree, vif->vif_name);
        if (link == NULL)
            continue;

        link->vif = vif;
    }
}

static void
osw_sta_cache_vif_probe_req_cb(struct osw_state_observer *self,
                               const struct osw_state_vif_info *vif,
                               const struct osw_drv_report_vif_probe_req *probe_req)
{
    ASSERT(self != NULL, "");
    ASSERT(vif != NULL, "");
    ASSERT(probe_req != NULL, "");

    struct osw_sta_observer *obs;
    struct osw_sta_cache *cache = container_of(self, struct osw_sta_cache, state_obs);
    struct osw_sta* sta = osw_sta_cache_get_sta(cache, &probe_req->sta_addr);
    struct osw_sta_link *link = osw_sta_get_link(sta, vif);

    link->last_probe_req_tstamp_nsec = osw_time_mono_clk();

    ds_dlist_foreach(&sta->observer_list, obs)
        if (obs->probe_req_fn != NULL)
            obs->probe_req_fn(obs, sta, link, probe_req);
}

static void
osw_sta_cache_dump(struct osw_sta_cache *cache)
{
    ASSERT(cache != NULL, "");

    struct osw_sta *sta;

    LOGI("osw: sta_cache: ");
    LOGI("osw: sta_cache: sta_tree:");

    ds_tree_foreach(&cache->sta_tree, sta) {
        struct osw_sta_link *link;

        LOGI("osw: sta_cache:   sta: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->mac_addr));
        LOGI("osw: sta_cache:     link_tree:");

        ds_tree_foreach(&sta->link_tree, link) {
            char last_connect_tstamp_buf[64] = { 0 };
            char last_probe_req_tstamp_buf[64] = { 0 };
            const time_t last_connect_tstamp = OSW_TIME_TO_TIME_T(link->last_connect_tstamp_nsec);
            const time_t last_probe_req_tstamp = OSW_TIME_TO_TIME_T(link->last_probe_req_tstamp_nsec);
            struct tm buf;

            if (link->last_connect_tstamp_nsec != 0)
                strftime(last_connect_tstamp_buf, sizeof(last_connect_tstamp_buf), "%F %T", localtime_r(&last_connect_tstamp, &buf));
            else
                STRSCPY(last_connect_tstamp_buf, "(nil)");

            if (link->last_probe_req_tstamp_nsec != 0)
                strftime(last_probe_req_tstamp_buf, sizeof(last_probe_req_tstamp_buf), "%F %T", localtime_r(&last_probe_req_tstamp, &buf));
            else
                STRSCPY(last_probe_req_tstamp_buf, "(nil)");

            LOGI("osw: sta_cache:       link: vif: %s", link->vif_name.buf);
            LOGI("osw: sta_cache:         connected: %s", link->connected == true ? "true" : "false");
            LOGI("osw: sta_cache:         assoc_req_ies: (%s)", link->assoc_req_ies != NULL ? "present" : "nil");
            LOGI("osw: sta_cache:         assoc_req_ies_len: %zu", link->assoc_req_ies_len);
            LOGI("osw: sta_cache:         last_connect_tstamp: %s", last_connect_tstamp_buf);
            LOGI("osw: sta_cache:         last_probe_req_tstamp: %s", last_probe_req_tstamp_buf);
        }
    }
}

static void
osw_sta_cache_sigusr1_cb(EV_P_ ev_signal *arg,
                         int events)
{
    struct osw_sta_cache *sta_cache = container_of(arg, struct osw_sta_cache, sigusr1);
    osw_sta_cache_dump(sta_cache);
}

static struct osw_sta_cache g_sta_cache = {
    .state_obs = {
        .name = "osw_sta_cache",
        .vif_removed_fn = osw_sta_cache_vif_removed_cb,
        .vif_changed_fn = osw_sta_cache_vif_changed_cb,
        .vif_probe_req_fn = osw_sta_cache_vif_probe_req_cb,
        .sta_connected_fn = osw_sta_cache_sta_connected_cb,
        .sta_disconnected_fn = osw_sta_cache_sta_disconnected_cb,
    },
    .observer_list = DS_DLIST_INIT(struct osw_sta_cache_observer, node),
    .sta_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_sta, node),
};

static void
osw_sta_cache_init_priv(struct osw_sta_cache *cache)
{
    osw_state_register_observer(&cache->state_obs);
    ev_signal_init(&cache->sigusr1, osw_sta_cache_sigusr1_cb, SIGUSR1);
    ev_signal_start(EV_DEFAULT_ &cache->sigusr1);
    ev_unref(EV_DEFAULT);
}

static void
osw_sta_cache_init(void)
{
    osw_sta_cache_init_priv(&g_sta_cache);
}

void
osw_sta_cache_register_observer(struct osw_sta_cache_observer *observer)
{
    ASSERT(observer != NULL, "");

    struct osw_sta* sta;
    struct osw_sta_cache *cache = &g_sta_cache;

    ds_dlist_insert_tail(&cache->observer_list, observer);
    if (observer->appeared_fn == NULL)
        return;

    ds_tree_foreach(&cache->sta_tree, sta)
        observer->appeared_fn(observer, sta);
}

void
osw_sta_cache_unregister_observer(struct osw_sta_cache_observer *observer)
{
    ASSERT(observer != NULL, "");
    struct osw_sta_cache *cache = &g_sta_cache;
    ds_dlist_remove(&cache->observer_list, observer);
}

struct osw_sta*
osw_sta_cache_lookup_sta(const struct osw_hwaddr *sta_addr)
{
    ASSERT(sta_addr != NULL, "");
    struct osw_sta_cache *cache = &g_sta_cache;
    return ds_tree_find(&cache->sta_tree, sta_addr);
}

void
osw_sta_register_observer(struct osw_sta *sta,
                          struct osw_sta_observer *observer)
{
    ASSERT(sta != NULL, "");
    ASSERT(observer != NULL, "");

    struct osw_sta_link *link;

    ds_dlist_insert_tail(&sta->observer_list, observer);
    if (observer->connected_fn == NULL)
        return;

    ds_tree_foreach(&sta->link_tree, link)
        if (link->connected == true)
            observer->connected_fn(observer, sta, link);
}

void
osw_sta_unregister_observer(struct osw_sta *sta,
                            struct osw_sta_observer *observer)
{
    ASSERT(sta != NULL, "");
    ASSERT(observer != NULL, "");

    ds_dlist_remove(&sta->observer_list, observer);
}

const struct osw_hwaddr*
osw_sta_get_mac_addr(const struct osw_sta *sta)
{
    ASSERT(sta != NULL, "");
    return &sta->mac_addr;
}

const struct osw_state_vif_info*
osw_sta_link_get_vif_info(const struct osw_sta_link *link)
{
    ASSERT(link != NULL, "");
    return link->vif;
}

struct osw_sta_cache_ut_lifecycle_sta_cnt {
    unsigned int appeared_cnt;
    unsigned int vanished_cnt;
    unsigned int connected_cnt;
    unsigned int disconnected_cnt;
    unsigned int probe_req_cnt;
};

struct osw_sta_cache_ut_lifecycle_ctx {
    const struct osw_hwaddr sta_addr_a;
    const struct osw_hwaddr sta_addr_b;

    struct osw_drv_vif_state drv_vif_state_0;
    struct osw_state_vif_info vif_0;
    struct osw_drv_vif_state drv_vif_state_1;
    struct osw_state_vif_info vif_1;

    struct osw_state_sta_info sta_info_a;
    struct osw_state_sta_info sta_info_b;

    struct osw_sta_cache_observer sta_cache_observer;
    struct osw_sta_observer sta_a_observer;
    struct osw_sta_observer sta_b_observer;

    struct osw_drv_report_vif_probe_req sta_b_probe_req;

    struct osw_sta_cache_ut_lifecycle_sta_cnt sta_a_cnt;
    struct osw_sta_cache_ut_lifecycle_sta_cnt sta_b_cnt;
};

static void
osw_sta_cache_ut_lifecycle_sta_appeared_cb(struct osw_sta_cache_observer *self,
                                           struct osw_sta *sta)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_cache_observer);

    if (osw_hwaddr_cmp(osw_sta_get_mac_addr(sta), &ctx->sta_addr_a) == 0) {
        ctx->sta_a_cnt.appeared_cnt++;
        osw_sta_register_observer(sta, &ctx->sta_a_observer);
    }
    else if (osw_hwaddr_cmp(osw_sta_get_mac_addr(sta), &ctx->sta_addr_b) == 0) {
        ctx->sta_b_cnt.appeared_cnt++;
        osw_sta_register_observer(sta, &ctx->sta_b_observer);
    }
    else {
        assert(false);
    }
}

static void
osw_sta_cache_ut_lifecycle_sta_vanished_cb(struct osw_sta_cache_observer *self,
                                           struct osw_sta *sta)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_cache_observer);

    assert(osw_sta_get_mac_addr(sta) != NULL);
    
    if (osw_hwaddr_cmp(osw_sta_get_mac_addr(sta), &ctx->sta_addr_a) == 0) {
        ctx->sta_a_cnt.vanished_cnt++;
        osw_sta_register_observer(sta, &ctx->sta_a_observer);
    }
    else if (osw_hwaddr_cmp(osw_sta_get_mac_addr(sta), &ctx->sta_addr_b) == 0) {
        ctx->sta_b_cnt.vanished_cnt++;
        osw_sta_register_observer(sta, &ctx->sta_b_observer);
    }
    else {
        assert(false);
    }
}

static void
osw_sta_cache_ut_lifecycle_sta_a_connected_cb(struct osw_sta_observer *self,
                                              struct osw_sta *sta,
                                              const struct osw_sta_link *link)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_a_observer);
    ctx->sta_a_cnt.connected_cnt++;
}

static void
osw_sta_cache_ut_lifecycle_sta_a_disconnected_cb(struct osw_sta_observer *self,
                                                 struct osw_sta *sta,
                                                 const struct osw_sta_link *link)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_a_observer);
    ctx->sta_a_cnt.disconnected_cnt++;
}

static void
osw_sta_cache_ut_lifecycle_sta_a_probe_req_cb(struct osw_sta_observer *self,
                                              struct osw_sta *sta,
                                              const struct osw_sta_link *link,
                                              const struct osw_drv_report_vif_probe_req *probe_req)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_b_observer);
    ctx->sta_a_cnt.probe_req_cnt++;
}

static void
osw_sta_cache_ut_lifecycle_sta_b_connected_cb(struct osw_sta_observer *self,
                                              struct osw_sta *sta,
                                              const struct osw_sta_link *link)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_b_observer);
    ctx->sta_b_cnt.connected_cnt++;
}

static void
osw_sta_cache_ut_lifecycle_sta_b_disconnected_cb(struct osw_sta_observer *self,
                                                 struct osw_sta *sta,
                                                 const struct osw_sta_link *link)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_b_observer);
    ctx->sta_b_cnt.disconnected_cnt++;
}

static void
osw_sta_cache_ut_lifecycle_sta_b_probe_req_cb(struct osw_sta_observer *self,
                                              struct osw_sta *sta,
                                              const struct osw_sta_link *link,
                                              const struct osw_drv_report_vif_probe_req *probe_req)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = container_of(self, struct osw_sta_cache_ut_lifecycle_ctx, sta_b_observer);
    ctx->sta_b_cnt.probe_req_cnt++;
}

static struct osw_sta_cache_ut_lifecycle_ctx*
osw_sta_cache_ut_lifecycle_ctx_get(void)
{
    static struct osw_sta_cache_ut_lifecycle_ctx ctx = {
        .sta_addr_a = { .octet = { 0xAA, }, },
        .sta_addr_b = { .octet = { 0xBB, }, },

        .drv_vif_state_0 = {
            .mac_addr = { .octet = { 0xCC, }, },
        },
        .vif_0 = {
            .vif_name = "vif_0",
            .drv_state = &ctx.drv_vif_state_0,
        },
        .drv_vif_state_1 = {
            .mac_addr = { .octet = { 0xDD, }, },
        },
        .vif_1 = {
            .vif_name = "vif_1",
            .drv_state = &ctx.drv_vif_state_1,
        },

        .sta_info_a = {
            .mac_addr = &ctx.sta_addr_a,
            .vif = &ctx.vif_0,
        },
        .sta_info_b = {
            .mac_addr = &ctx.sta_addr_b,
            .vif = &ctx.vif_1,
        },

        .sta_b_probe_req = {
            .sta_addr = { .octet = { 0xBB, }, },
        },

        .sta_cache_observer = {
            .name = "osw_sta_cache_ut",
            .appeared_fn = osw_sta_cache_ut_lifecycle_sta_appeared_cb,
            .vanished_fn = osw_sta_cache_ut_lifecycle_sta_vanished_cb,
        },
        .sta_a_observer = {
            .name = "osw_sta_cache_ut_sta_a",
            .connected_fn = osw_sta_cache_ut_lifecycle_sta_a_connected_cb,
            .disconnected_fn = osw_sta_cache_ut_lifecycle_sta_a_disconnected_cb,
            .probe_req_fn = osw_sta_cache_ut_lifecycle_sta_a_probe_req_cb,
        },
        .sta_b_observer = {
            .name = "osw_sta_cache_ut_sta_b",
            .connected_fn = osw_sta_cache_ut_lifecycle_sta_b_connected_cb,
            .disconnected_fn = osw_sta_cache_ut_lifecycle_sta_b_disconnected_cb,
            .probe_req_fn = osw_sta_cache_ut_lifecycle_sta_b_probe_req_cb,
        },
    };

    return &ctx;
}

OSW_UT(osw_sta_cache_ut_lifecycle)
{
    struct osw_sta_cache_ut_lifecycle_ctx *ctx = osw_sta_cache_ut_lifecycle_ctx_get();

    osw_ut_time_init();
    osw_sta_cache_init();

    /* STA A connects before registering sta_cache observer */
    g_sta_cache.state_obs.sta_connected_fn(&g_sta_cache.state_obs, &ctx->sta_info_a);

    osw_sta_cache_register_observer(&ctx->sta_cache_observer);
    assert(ctx->sta_a_cnt.appeared_cnt == 1);
    assert(ctx->sta_a_cnt.vanished_cnt == 0);
    assert(ctx->sta_a_cnt.connected_cnt == 1);
    assert(ctx->sta_a_cnt.disconnected_cnt == 0);
    assert(ctx->sta_a_cnt.probe_req_cnt == 0);

    /* STA B probes (wildcard) */
    g_sta_cache.state_obs.vif_probe_req_fn(&g_sta_cache.state_obs, &ctx->vif_0, &ctx->sta_b_probe_req);
    g_sta_cache.state_obs.vif_probe_req_fn(&g_sta_cache.state_obs, &ctx->vif_1, &ctx->sta_b_probe_req);
    assert(ctx->sta_b_cnt.appeared_cnt == 1);
    assert(ctx->sta_b_cnt.vanished_cnt == 0);
    assert(ctx->sta_b_cnt.connected_cnt == 0);
    assert(ctx->sta_b_cnt.disconnected_cnt == 0);
    assert(ctx->sta_b_cnt.probe_req_cnt == 2);

    /* STA B connects */
    g_sta_cache.state_obs.sta_connected_fn(&g_sta_cache.state_obs, &ctx->sta_info_b);
    assert(ctx->sta_b_cnt.appeared_cnt == 1);
    assert(ctx->sta_b_cnt.vanished_cnt == 0);
    assert(ctx->sta_b_cnt.connected_cnt == 1);
    assert(ctx->sta_b_cnt.disconnected_cnt == 0);
    assert(ctx->sta_b_cnt.probe_req_cnt == 2);

    /* Both STAs disconnect */
    g_sta_cache.state_obs.sta_disconnected_fn(&g_sta_cache.state_obs, &ctx->sta_info_a);
    assert(ctx->sta_a_cnt.appeared_cnt == 1);
    assert(ctx->sta_a_cnt.vanished_cnt == 0);
    assert(ctx->sta_a_cnt.connected_cnt == 1);
    assert(ctx->sta_a_cnt.disconnected_cnt == 1);
    assert(ctx->sta_a_cnt.probe_req_cnt == 0);

    g_sta_cache.state_obs.sta_disconnected_fn(&g_sta_cache.state_obs, &ctx->sta_info_b);
    assert(ctx->sta_b_cnt.appeared_cnt == 1);
    assert(ctx->sta_b_cnt.vanished_cnt == 0);
    assert(ctx->sta_b_cnt.connected_cnt == 1);
    assert(ctx->sta_b_cnt.disconnected_cnt == 1);
    assert(ctx->sta_b_cnt.probe_req_cnt == 2);

    /* Wait until both STAs will vanish */
    osw_ut_time_advance(OSW_TIME_SEC(60));

    assert(ctx->sta_a_cnt.appeared_cnt == 1);
    assert(ctx->sta_a_cnt.vanished_cnt == 1);
    assert(ctx->sta_a_cnt.connected_cnt == 1);
    assert(ctx->sta_a_cnt.disconnected_cnt == 1);
    assert(ctx->sta_a_cnt.probe_req_cnt == 0);

    assert(ctx->sta_b_cnt.appeared_cnt == 1);
    assert(ctx->sta_b_cnt.vanished_cnt == 1);
    assert(ctx->sta_b_cnt.connected_cnt == 1);
    assert(ctx->sta_b_cnt.disconnected_cnt == 1);
    assert(ctx->sta_b_cnt.probe_req_cnt == 2);
}

OSW_MODULE(osw_sta_cache)
{
    OSW_MODULE_LOAD(osw_state);
    osw_sta_cache_init();
    return NULL;
}
