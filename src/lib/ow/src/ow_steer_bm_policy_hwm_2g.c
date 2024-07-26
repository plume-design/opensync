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
#include <memutil.h>
#include <const.h>
#include <os.h>

#include <osw_types.h>
#include <osw_bss_map.h>

#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_snr_level.h"
#include "ow_steer_bm_policy_hwm_2g.h"

#define LOG_PREFIX(hwm, fmt, ...) \
    "ow: steer: bm: %s %s: " fmt, \
    ow_steer_policy_get_prefix((hwm)->policy->base), \
    ow_steer_policy_snr_level_state_to_cstr((hwm)->policy->state), \
    ## __VA_ARGS__

#define OW_STEER_BM_POLICY_HWM_ACTIVE_BYTES 2000

/* The high level idea is that all 2.4GHz
 * BSSes are considered "slow" and all others are
 * "fast". If a station SNR is above, or at, HWM, and it
 * is connected to one of the "slow" BSSes, then it is
 * considered for steering through policing and executor
 * action.
 */
struct ow_steer_bm_policy_hwm_2g {
    struct ow_steer_policy_snr_level *policy;
    struct ow_steer_bm_observer bm_obs;
    struct ow_steer_bm_group *group;
    struct osw_hwaddr_list from_bssids;
    struct osw_hwaddr_list to_bssids;
};

static void
ow_steer_bm_policy_hwm_2g_set_list(struct osw_hwaddr_list *list,
                                   const struct osw_hwaddr *bssid,
                                   const bool want_on_list)
{
    struct osw_hwaddr_list tmp;
    MEMZERO(tmp);

    size_t i;
    for (i = 0; i < list->count; i++) {
        const struct osw_hwaddr *addr = &list->list[i];
        /* skip the current one, it will be (re)added later
         * if its eligible, ie. up and running.
         */
        if (osw_hwaddr_is_equal(addr, bssid)) continue;
        osw_hwaddr_list_append(&tmp, addr);
    }

    if (want_on_list) {
        osw_hwaddr_list_append(&tmp, bssid);
    }

    FREE(list->list);
    memcpy(list, &tmp, sizeof(tmp));
}

static void
ow_steer_bm_policy_hwm_2g_set_bss(struct ow_steer_bm_policy_hwm_2g *hwm,
                                  const struct osw_hwaddr *bssid,
                                  const struct osw_channel *channel,
                                  const bool co_located)
{
    const enum osw_band band = osw_freq_to_band(channel ? channel->control_freq_mhz : 0);
    switch (band) {
        case OSW_BAND_2GHZ:
            ow_steer_bm_policy_hwm_2g_set_list(&hwm->to_bssids, bssid, false);
            ow_steer_bm_policy_hwm_2g_set_list(&hwm->from_bssids, bssid, true);
            break;
        case OSW_BAND_5GHZ:
        case OSW_BAND_6GHZ:
            if (co_located) {
                ow_steer_bm_policy_hwm_2g_set_list(&hwm->to_bssids, bssid, true);
                ow_steer_bm_policy_hwm_2g_set_list(&hwm->from_bssids, bssid, false);
            }
            else {
                /* This may seem confusing, but the intention is that once a
                 * 2.4GHz link reaches HWM the steering list needs to point only
                 * to co-located APs. If the co-located APs list is an empty set
                 * (as a result of other filters, like channel capability
                 * policy) then HWM policing will be withheld (in recalc_cb)
                 * because it makes no sense and the device locally does not
                 * hold the necessary info to perform an educated decision to
                 * steer the client away to another pod. Without the data it
                 * could result in needless link flapping.
                 */
                ow_steer_bm_policy_hwm_2g_set_list(&hwm->to_bssids, bssid, false);
                ow_steer_bm_policy_hwm_2g_set_list(&hwm->from_bssids, bssid, true);
            }
            break;
        case OSW_BAND_UNDEFINED:
            ow_steer_bm_policy_hwm_2g_set_list(&hwm->to_bssids, bssid, false);
            ow_steer_bm_policy_hwm_2g_set_list(&hwm->from_bssids, bssid, false);
            break;
    }

    ow_steer_policy_snr_level_set_from_bssids(hwm->policy, &hwm->from_bssids);
    ow_steer_policy_snr_level_set_to_bssids(hwm->policy, &hwm->to_bssids);
}

static void
ow_steer_bm_policy_hwm_2g_vif_up_cb(struct ow_steer_bm_observer *obs,
                                    struct ow_steer_bm_vif *vif)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    if (vif->bss == NULL) return;
    if (vif->group != hwm->group) return;
    const struct osw_hwaddr *bssid = &vif->bss->bssid;
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, osw_bss_get_channel(bssid), true);
}

static void
ow_steer_bm_policy_hwm_2g_vif_down_cb(struct ow_steer_bm_observer *obs,
                                      struct ow_steer_bm_vif *vif)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    if (vif->bss == NULL) return;
    if (vif->group != hwm->group) return;
    const struct osw_hwaddr *bssid = &vif->bss->bssid;
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, NULL, true);
}

static void
ow_steer_bm_policy_hwm_2g_vif_channel_cb(struct ow_steer_bm_observer *obs,
                                         struct ow_steer_bm_vif *vif,
                                         const struct osw_channel *old_channel,
                                         const struct osw_channel *new_channel)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    if (vif->bss == NULL) return;
    if (vif->group != hwm->group) return;
    const struct osw_hwaddr *bssid = vif->bss ? &vif->bss->bssid : NULL;
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, bssid ? osw_bss_get_channel(bssid) : NULL, true);
}

static void
ow_steer_bm_policy_hwm_2g_neighbor_up_cb(struct ow_steer_bm_observer *obs,
                                         struct ow_steer_bm_neighbor *neighbor)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    const struct osw_hwaddr *bssid = ow_steer_bm_neighbor_get_bssid(neighbor);
    const struct ow_steer_bm_bss *bss = ow_steer_bm_neighbor_get_bss(neighbor);
    const bool matching_group = (bss != NULL && bss->group == hwm->group);
    const struct osw_channel *channel = matching_group ? osw_bss_get_channel(bssid) : NULL;
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, channel, false);
}

static void
ow_steer_bm_policy_hwm_2g_neighbor_down_cb(struct ow_steer_bm_observer *obs,
                                           struct ow_steer_bm_neighbor *neighbor)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    const struct osw_hwaddr *bssid = ow_steer_bm_neighbor_get_bssid(neighbor);
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, NULL, false);
}

static void
ow_steer_bm_policy_hwm_2g_neighbor_channel_cb(struct ow_steer_bm_observer *obs,
                                              struct ow_steer_bm_neighbor *neighbor,
                                              const struct osw_channel *old_channel,
                                              const struct osw_channel *new_channel)
{
    struct ow_steer_bm_policy_hwm_2g *hwm = container_of(obs, struct ow_steer_bm_policy_hwm_2g, bm_obs);
    const struct osw_hwaddr *bssid = ow_steer_bm_neighbor_get_bssid(neighbor);
    const struct ow_steer_bm_bss *bss = ow_steer_bm_neighbor_get_bss(neighbor);
    const bool matching_group = (bss != NULL && bss->group == hwm->group);
    const struct osw_channel *channel = matching_group ? osw_bss_get_channel(bssid) : NULL;
    ow_steer_bm_policy_hwm_2g_set_bss(hwm, bssid, channel, false);
}

struct ow_steer_bm_policy_hwm_2g *
ow_steer_bm_policy_hwm_2g_alloc(const char *name,
                                struct ow_steer_bm_group *group,
                                const struct osw_hwaddr *sta_addr,
                                const struct ow_steer_policy_mediator *mediator)
{
    static const struct ow_steer_bm_observer bm_obs = {
        .vif_up_fn = ow_steer_bm_policy_hwm_2g_vif_up_cb,
        .vif_down_fn = ow_steer_bm_policy_hwm_2g_vif_down_cb,
        .vif_changed_channel_fn = ow_steer_bm_policy_hwm_2g_vif_channel_cb,

        .neighbor_up_fn = ow_steer_bm_policy_hwm_2g_neighbor_up_cb,
        .neighbor_down_fn = ow_steer_bm_policy_hwm_2g_neighbor_down_cb,
        .neighbor_changed_channel_fn = ow_steer_bm_policy_hwm_2g_neighbor_channel_cb,
    };
    struct ow_steer_bm_policy_hwm_2g *hwm = CALLOC(1, sizeof(*hwm));
    const enum ow_steer_policy_snr_level_mode mode = OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_ABOVE;
    hwm->policy = ow_steer_policy_snr_level_alloc(name, sta_addr, mode, mediator);
    hwm->bm_obs = bm_obs;
    hwm->group = group;

    /* The BM observer callbacks make sure to include all
     * the local (vif) and non-local (neighbor) BSSes
     * accordingly.
     *
     * This actually works well and doesn't need extra
     * consideration for non-local 2.4GHz BSSes. They never
     * can really be used for a local link, but are still
     * subject to being hard-blocked in the event of HWM
     * trigger.
     *
     */
    ow_steer_bm_observer_register(&hwm->bm_obs);
    return hwm;
}

void
ow_steer_bm_policy_hwm_2g_free(struct ow_steer_bm_policy_hwm_2g *hwm)
{
    ow_steer_bm_observer_unregister(&hwm->bm_obs);
    ow_steer_policy_snr_level_free(hwm->policy);
    FREE(hwm->from_bssids.list);
    FREE(hwm->to_bssids.list);
    FREE(hwm);
}

void
ow_steer_bm_policy_hwm_2g_update(struct ow_steer_bm_policy_hwm_2g *hwm,
                                 const unsigned int *snr,
                                 const bool *kick_upon_idle)
{
    const uint64_t some = OW_STEER_BM_POLICY_HWM_ACTIVE_BYTES;
    const uint64_t any = 0;

    ow_steer_policy_snr_level_set_sta_threshold_snr(hwm->policy, snr);
    ow_steer_policy_snr_level_set_sta_threshold_bytes(hwm->policy,
                                                      kick_upon_idle
                                                      ? (*kick_upon_idle ? &some : &any)
                                                      : NULL);
}

struct ow_steer_policy *
ow_steer_bm_policy_hwm_2g_get_base(struct ow_steer_bm_policy_hwm_2g *hwm)
{
    return ow_steer_policy_snr_level_get_base(hwm->policy);
}
