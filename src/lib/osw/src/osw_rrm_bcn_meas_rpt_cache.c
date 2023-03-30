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

#include <stdint.h>
#include <ds_tree.h>
#include <log.h>
#include <util.h>
#include <const.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_timer.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_util.h>
#include <osw_module.h>
#include <osw_rrm.h>
#include <osw_rrm_bcn_meas_rpt_cache.h>

#define OSW_RRM_BCN_MEAS_RPT_GC_PERIOD_SEC 30
#define OSW_RRM_BCN_MEAS_RPT_EXPIRE_PERIOD_SEC 10

struct osw_rrm_bcn_meas_rpt_sta {
    struct osw_hwaddr sta_addr;
    struct osw_rrm_bcn_meas_rpt_cache *cache;
    struct ds_tree neigh_tree;
    struct osw_timer gc_timer;

    ds_tree_node_t node;
};

struct osw_rrm_bcn_meas_rpt_neigh {
    struct osw_hwaddr bssid;
    struct osw_rrm_bcn_meas_rpt rpt;
    uint64_t last_seen_at_nsec;

    ds_tree_node_t node;
};

struct osw_rrm_bcn_meas_rpt_cache {
    struct ds_tree sta_tree;
    struct osw_timer gc_timer;
    struct osw_rrm_rpt_observer rrm_rpt_observer;
    struct ds_dlist observer_list;
};

static struct osw_rrm_bcn_meas_rpt_neigh*
osw_rrm_bcn_meas_rpt_neigh_new(struct osw_rrm_bcn_meas_rpt_sta *sta,
                               const struct osw_hwaddr *bssid)
{
    ASSERT(sta != NULL, "");
    ASSERT(bssid != NULL, "");

    struct osw_rrm_bcn_meas_rpt_neigh *rpt_neigh = CALLOC(1, sizeof(*rpt_neigh));
    memcpy(&rpt_neigh->bssid, bssid, sizeof(rpt_neigh->bssid));

    ds_tree_insert(&sta->neigh_tree, rpt_neigh, &rpt_neigh->bssid);

    return rpt_neigh;
}

static bool
osw_rrm_bcn_meas_rpt_neigh_is_fresh(const struct osw_rrm_bcn_meas_rpt_neigh *neigh)
{
    ASSERT(neigh != NULL, "");
    const uint64_t now_nsec = osw_time_mono_clk();
    ASSERT(now_nsec >= neigh->last_seen_at_nsec, "");
    const uint64_t age_nsec = now_nsec - neigh->last_seen_at_nsec;
    const bool fresh = (age_nsec < OSW_TIME_SEC(OSW_RRM_BCN_MEAS_RPT_EXPIRE_PERIOD_SEC));
    return fresh;
}

static void
osw_rrm_bcn_meas_rpt_sta_free(struct osw_rrm_bcn_meas_rpt_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct osw_rrm_bcn_meas_rpt_neigh *neigh = NULL;
    struct osw_rrm_bcn_meas_rpt_neigh *tmp_neigh = NULL;
    ds_tree_foreach_safe(&sta->neigh_tree, neigh, tmp_neigh) {
        ds_tree_remove(&sta->neigh_tree, neigh);
        FREE(neigh);
    }
    osw_timer_disarm(&sta->gc_timer);
    FREE(sta);
}

static void
osw_rrm_bcn_meas_rpt_sta_gc_timer_cb(struct osw_timer *timer)
{
    struct osw_rrm_bcn_meas_rpt_sta *sta = container_of(timer, struct osw_rrm_bcn_meas_rpt_sta, gc_timer);
    LOGT("osw: rrm_bcn_meas_rpt_cache: sta_addr: "OSW_HWADDR_FMT" periodic gc call", OSW_HWADDR_ARG(&sta->sta_addr));

    struct osw_rrm_bcn_meas_rpt_neigh *neigh = NULL;
    struct osw_rrm_bcn_meas_rpt_neigh *tmp_neigh = NULL;
    ds_tree_foreach_safe(&sta->neigh_tree, neigh, tmp_neigh) {
        const bool fresh = osw_rrm_bcn_meas_rpt_neigh_is_fresh(neigh);
        if (fresh == true)
            continue;

        LOGD("osw: rrm_bcn_meas_rpt_cache: sta_addr: "OSW_HWADDR_FMT" bssid: "OSW_HWADDR_FMT" drop expired rpt",
             OSW_HWADDR_ARG(&sta->sta_addr), OSW_HWADDR_ARG(&neigh->bssid));

        ds_tree_remove(&sta->neigh_tree, neigh);
        FREE(neigh);
    }

    if (ds_tree_is_empty(&sta->neigh_tree) == true) {
        LOGD("osw: rrm_bcn_meas_rpt_cache: sta_addr: "OSW_HWADDR_FMT" sta holds no reports, drop", OSW_HWADDR_ARG(&sta->sta_addr));

        struct osw_rrm_bcn_meas_rpt_cache *cache = sta->cache;
        ds_tree_remove(&cache->sta_tree, sta);
        osw_rrm_bcn_meas_rpt_sta_free(sta);
    }
    else {
        const uint64_t next_at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OSW_RRM_BCN_MEAS_RPT_GC_PERIOD_SEC);
        osw_timer_arm_at_nsec(timer, next_at_nsec);
    }
}

static struct osw_rrm_bcn_meas_rpt_sta*
osw_rrm_bcn_meas_rpt_sta_new(struct osw_rrm_bcn_meas_rpt_cache *cache,
                             const struct osw_hwaddr *sta_addr)
{
    ASSERT(cache != NULL, "");
    ASSERT(sta_addr != NULL, "");

    struct osw_rrm_bcn_meas_rpt_sta *sta = CALLOC(1, sizeof(*sta));
    memcpy(&sta->sta_addr, sta_addr, sizeof(sta->sta_addr));
    sta->cache = cache;
    ds_tree_init(&sta->neigh_tree, (ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_rrm_bcn_meas_rpt_neigh, node);
    osw_timer_init(&sta->gc_timer, osw_rrm_bcn_meas_rpt_sta_gc_timer_cb);

    const uint64_t next_at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OSW_RRM_BCN_MEAS_RPT_GC_PERIOD_SEC);
    osw_timer_arm_at_nsec(&sta->gc_timer, next_at_nsec);

    ds_tree_insert(&cache->sta_tree, sta, &sta->sta_addr);

    return sta;
}

static void
osw_rrm_bcn_rpt_cb(struct osw_rrm_rpt_observer *observer,
                   const struct osw_drv_dot11_frame_header *frame_header,
                   const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep,
                   const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed,
                   const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon)
{
    ASSERT(observer != NULL, "");
    ASSERT(frame_header != NULL, "");
    ASSERT(rrm_meas_rep != NULL, "");

    if (meas_rpt_ie_beacon == NULL)
        return;

    struct osw_rrm_bcn_meas_rpt_cache *cache = container_of(observer, struct osw_rrm_bcn_meas_rpt_cache, rrm_rpt_observer);

    struct osw_hwaddr sta_addr;
    memcpy(sta_addr.octet, &frame_header->sa, sizeof(sta_addr.octet));
    struct osw_rrm_bcn_meas_rpt_sta *sta = ds_tree_find(&cache->sta_tree, & sta_addr);
    if (sta == NULL)
        sta = osw_rrm_bcn_meas_rpt_sta_new(cache, &sta_addr);

    struct osw_hwaddr bssid;
    memcpy(&bssid.octet, &meas_rpt_ie_beacon->bssid, sizeof(bssid.octet));
    struct osw_rrm_bcn_meas_rpt_neigh *rpt_neigh = ds_tree_find(&sta->neigh_tree, &bssid);
    if (rpt_neigh == NULL)
        rpt_neigh = osw_rrm_bcn_meas_rpt_neigh_new(sta, &bssid);

    rpt_neigh->rpt.op_class = meas_rpt_ie_beacon->op_class;
    rpt_neigh->rpt.channel = meas_rpt_ie_beacon->channel;
    rpt_neigh->rpt.rcpi = meas_rpt_ie_beacon->rcpi;
    rpt_neigh->last_seen_at_nsec = osw_time_mono_clk();

    LOGD("osw: rrm_bcn_meas_rpt_cache: sta_addr: "OSW_HWADDR_FMT" bssid: "OSW_HWADDR_FMT" upsert rpt",
         OSW_HWADDR_ARG(&sta->sta_addr), OSW_HWADDR_ARG(&rpt_neigh->bssid));

    struct osw_rrm_bcn_meas_rpt_cache_observer *cache_observer = NULL;
    ds_dlist_foreach(&cache->observer_list, cache_observer)
        if (cache_observer->update_cb != NULL)
            cache_observer->update_cb(cache_observer, &sta->sta_addr, &rpt_neigh->bssid, &rpt_neigh->rpt);

}

static void
osw_rrm_bcn_meas_rpt_cache_init(struct osw_rrm_bcn_meas_rpt_cache *cache)
{
    ASSERT(cache != NULL, "");

    memset(cache, 0, sizeof(*cache));
    ds_tree_init(&cache->sta_tree, (ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_rrm_bcn_meas_rpt_sta, node);
    cache->rrm_rpt_observer.bcn_meas_fn = osw_rrm_bcn_rpt_cb;
    ds_dlist_init(&cache->observer_list, struct osw_rrm_bcn_meas_rpt_cache_observer, node);
}

const struct osw_rrm_bcn_meas_rpt*
osw_rrm_bcn_meas_rpt_cache_lookup(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                  const struct osw_hwaddr *sta_addr,
                                  const struct osw_hwaddr *bssid)
{
    ASSERT(cache != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(bssid != NULL, "");

    struct osw_rrm_bcn_meas_rpt_sta *sta = ds_tree_find(&cache->sta_tree, sta_addr);
    if (sta == NULL)
        return NULL;

    struct osw_rrm_bcn_meas_rpt_neigh *neigh = ds_tree_find(&sta->neigh_tree, bssid);
    if (neigh == NULL)
        return NULL;

    const bool fresh = osw_rrm_bcn_meas_rpt_neigh_is_fresh(neigh);
    if (fresh == false)
        return NULL;

    return &neigh->rpt;
}

OSW_MODULE(osw_rrm_bcn_meas_rpt_cache)
{
    struct osw_rrm *rrm = OSW_MODULE_LOAD(osw_rrm);
    static struct osw_rrm_bcn_meas_rpt_cache cache;
    osw_rrm_bcn_meas_rpt_cache_init(&cache);
    osw_rrm_register_rpt_observer(rrm, &cache.rrm_rpt_observer);
    return &cache;
}

void
osw_rrm_bcn_meas_rpt_cache_register_observer(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                             struct osw_rrm_bcn_meas_rpt_cache_observer *observer)
{
    ASSERT(cache != NULL, "");
    ASSERT(observer != NULL, "");

    ds_dlist_insert_tail(&cache->observer_list, observer);

    if (observer->update_cb == NULL)
        return;

    struct osw_rrm_bcn_meas_rpt_sta *sta = NULL;
    ds_tree_foreach(&cache->sta_tree, sta) {
        struct osw_rrm_bcn_meas_rpt_neigh *neigh = NULL;
        ds_tree_foreach(&sta->neigh_tree, neigh) {
            const bool fresh = osw_rrm_bcn_meas_rpt_neigh_is_fresh(neigh);
            if (fresh == false)
                continue;

            observer->update_cb(observer, &sta->sta_addr, &neigh->bssid, &neigh->rpt);
        }
    }
}

void
osw_rrm_bcn_meas_rpt_cache_unregister_observer(struct osw_rrm_bcn_meas_rpt_cache *cache,
                                               struct osw_rrm_bcn_meas_rpt_cache_observer *observer)
{
    ASSERT(cache != NULL, "");
    ASSERT(observer != NULL, "");

    ds_dlist_remove(&cache->observer_list, observer);
}

#include "osw_rrm_bcn_meas_rpt_cache_ut.c"
