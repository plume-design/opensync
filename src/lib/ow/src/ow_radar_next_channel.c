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
#include <util.h>
#include <osw_conf.h>
#include <osw_drv.h>
#include <osw_etc.h>
#include <osw_module.h>
#include <osw_phy_chan_observer.h>
#include <osw_state.h>
#include <osw_types.h>
#include <osw_ut.h>

#include "ow_conf.h"
#include "ds_tree.h"
#include "os.h"

/*
 * Radar event Next Channel
 *
 * Purpose:
 *
 * Chooses the runaway channel for the DFS radar event.
 * The chosen value (frequency and width) goes into OL_ATH_PARAM_NXT_RDR_FREQ
 * and OL_ATH_PARAM_NXT_RDR_WIDTH.
 *
 * Description:
 *
 * Without this mutator module, the radar runaway channel is chosen randomly.
 * Favorable case: phy runs to the NON-DFS channel.
 * Worst case: phy runs to CAC-READY channel (resulting in 60s CAC)
 *
 * This module aims at this problem, by choosing the NON-DFS or CAC-COMPLETED
 * channel as runaway. The algorithm takes the following input:
 * - configured channel (in the data model i.e. OVSDB)
 * - currently operating channel (this is delivered by osw_phy_chan_observer)
 * - DFS channel states (from osw_state_phy_info)
 *
 *       ┌───────────────────────────────────┐
 * ┌─────┤ osw_state_observer.phy_changed_fn │
 * │     └───────────────────────────────────┘
 * │
 * │     ┌───────────────────────────────────┐      ┌───────────────────────┐
 * │     │ osw_state_observer.vif_changed_fn ├─────►│ osw_phy_chan_observer │
 * │     └───────────────────────────────────┘      └───────────┬───────────┘
 * │                                                            │
 * │     ┌───────────────────────────────────┐                  │
 * │  ┌──┤  ow_conf_observer.phy_changed_fn  │                  │
 * │  │  └───────────────────────────────────┘                  │
 * │  │                                                         │
 * │  │                                                         │
 * │  │                                                         │
 * │  │                                                         │
 * │  │ struct ow_radar_next_channel_phy                        │
 * │  │ {                                                       │
 * │  │     struct ds_tree_node node;                           │
 * │  │     struct ow_radar_next_channel *m;                    │
 * │  │     char *phy_name;                                     │
 * │  │     bool found_in_config;                               │
 * │  └────►struct osw_channel configured_channel;              │
 * │        bool found_in_state;                                │
 * │        struct osw_channel operating_channel;◄──────────────┘
 * └─────┬─►struct osw_channel_state *channel_states;
 *       └─►int n_channel_states;
 *          osw_phy_chan_observer_t *operating_channel_obs;
 *      };
 *
 * Rust pseudocode:
 *     enum Width { Width20, Width40, Width80, Width80p80, ...  }
 *     enum DfsState { NonDfs, CacRequire, CacComplete, CacRunning, Nol }
 *     struct ChannelState(PrimaryFreqMHz, DfsState);
 *     struct Channel(PrimaryFreqMhz, CenterFreqMhz, Width)
 *
 *     impl Ord for Width {...}
 *     impl PartialOrd for Width {...}
 *
 *     fn unii_generate_from(list: Vec<PrimaryFreqMhz>, w: Width)
 *     -> Vec<Channel> { todo!() }
 *
 *     let channel_states: Vec<ChannelState> = todo!(); // from phy state
 *     let ready_pri_freqs: Vec<PrimaryFreqMHz> = channel_states
 *         .iter()
 *         .filter(|c| c.1 == DfsState::NonDfs ||
 *                     c.1 == DfsState::CacCompleted)
 *         .map(|c| c.0)
 *         .collect();
 *
 *     let max_width = [
 *             phy.get_max_width(),
 *             ovsdb.configured_width(),
 *         ]
 *         .iter()
 *         .filter(|w| w != Width80p80)
 *         .min()
 *         .unwrap_or(Width20);
 *
 *     let next_chan: Option<Channel> = [
 *             // wider first
 *             Width80,
 *             Width40,
 *             Width20,
 *         ]
 *         .iter()
 *         .filter(|w| w <= max_width)
 *         .map(|w| unii_generate_from(ready_pri_freqs, w) )
 *         .filter(|chans| chans.len() > 0)
 *         .take(1)
 *         .next()
 *         .map(|mut chans| shuffle(&mut chans))
 *         .and_then(|chans| chans.get(0));
 */

struct ow_radar_next_channel
{
    bool enabled;
    struct ow_conf_observer conf_obs;
    struct osw_state_observer state_obs;
    struct osw_conf_mutator mut;
    struct ds_tree phy_tree;
};

struct ow_radar_next_channel_phy
{
    struct ds_tree_node node;
    struct ow_radar_next_channel *m;
    char *phy_name;
    struct osw_channel configured_channel;
    struct osw_channel operating_channel;
    struct osw_channel_state *channel_states;
    int n_channel_states;
    osw_phy_chan_observer_t *operating_channel_obs;
};

enum ow_radar_next_channel_priority
{
    OW_RADAR_NEXT_CHANNEL_UNSET,
    OW_RADAR_NEXT_CHANNEL_UNUSABLE,
    OW_RADAR_NEXT_CHANNEL_NON_DFS_OR_CAC_COMPLETED,
    OW_RADAR_NEXT_CHANNEL_NON_DFS,
    OW_RADAR_NEXT_CHANNEL_NON_DFS_AND_OPERATING,
};

/*
 * Warning:
 * Sieve is constructed without assumption that:
 * - elements are ordered by UNII or <> of primary channel frequency
 * - channels are not overlapping
 */
struct ow_radar_next_channel_sieve
{
    struct osw_channel channel;
    enum ow_radar_next_channel_priority priority;
};

#define OW_RADAR_NEXT_CHANNEL_ENABLED_DEFAULT true
#define LOG_PREFIX(fmt, ...)                  "ow: radar_next_channel: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_PHY(phy_name, fmt, ...)    LOG_PREFIX("%s: " fmt, phy_name, ##__VA_ARGS__)
#define mut_to_m(x)                           container_of(x, struct ow_radar_next_channel, mut)
#define state_obs_to_m(obs_)                  container_of(obs_, struct ow_radar_next_channel, state_obs)
#define conf_obs_to_m(obs_)                   container_of(obs_, struct ow_radar_next_channel, conf_obs)

static char *ow_radar_next_channel_priority_to_str(enum ow_radar_next_channel_priority priority)
{
    switch (priority)
    {
        case OW_RADAR_NEXT_CHANNEL_UNSET:
            return "unset";
        case OW_RADAR_NEXT_CHANNEL_UNUSABLE:
            return "unusable";
        case OW_RADAR_NEXT_CHANNEL_NON_DFS_OR_CAC_COMPLETED:
            return "non_dfs_or_cac_completed";
        case OW_RADAR_NEXT_CHANNEL_NON_DFS:
            return "non_dfs";
        case OW_RADAR_NEXT_CHANNEL_NON_DFS_AND_OPERATING:
            return "non_dfs_and_operating";
    }
    return "unknown";
}

static struct ow_radar_next_channel_phy *ow_radar_next_channel_alloc_new_phy(
        struct ow_radar_next_channel *m,
        const char *phy_name)
{
    struct ow_radar_next_channel_phy *p;
    p = CALLOC(1, sizeof(*p));
    p->phy_name = STRDUP(phy_name);
    p->configured_channel = *osw_channel_none();
    p->operating_channel = *osw_channel_none();
    p->operating_channel_obs = NULL;
    p->channel_states = NULL;
    p->n_channel_states = 0;
    ds_tree_insert(&m->phy_tree, p, p->phy_name);

    return p;
}

static struct ow_radar_next_channel_phy *ow_radar_next_channel_create_new_phy(
        struct ow_radar_next_channel *m,
        const char *phy_name)
{
    struct ow_radar_next_channel_phy *p;
    p = ow_radar_next_channel_alloc_new_phy(m, phy_name);
    osw_conf_invalidate(&m->mut);
    return p;
}

static void ow_radar_next_channel_update_configured_channel_set(
        struct ow_radar_next_channel_phy *phy,
        const struct osw_channel *c)
{
    if (osw_channel_is_equal(&phy->configured_channel, c)) return;
    phy->configured_channel = c ? *c : *osw_channel_none();
}

static void ow_radar_next_channel_update_configured_channel(
        struct ow_radar_next_channel_phy *phy,
        const struct osw_channel *c)
{
    if (WARN_ON(phy == NULL)) return;
    ow_radar_next_channel_update_configured_channel_set(phy, c);

    osw_conf_invalidate(&phy->m->mut);
}

static void ow_radar_next_channel_update_operating_channel(
        struct ow_radar_next_channel_phy *phy,
        const struct osw_channel *c)
{
    if (WARN_ON(phy == NULL)) return;
    phy->operating_channel = c ? *c : *osw_channel_none();
}

static void ow_radar_next_channel_update_operating_channel_cb(void *data, const struct osw_channel *c)
{
    if (WARN_ON(data == NULL)) return;

    struct ow_radar_next_channel_phy *phy = (struct ow_radar_next_channel_phy *)data;
    ow_radar_next_channel_update_operating_channel(phy, c);

    osw_conf_invalidate(&phy->m->mut);
}

static void ow_radar_next_channel_setup_phy_chan_obs(struct ow_radar_next_channel_phy *phy)
{
    if (WARN_ON(phy == NULL)) return;
    phy->operating_channel_obs =
            osw_phy_chan_observer_setup(phy->phy_name, &ow_radar_next_channel_update_operating_channel_cb, phy);
}

static void ow_radar_next_channel_update_channel_states(
        struct ow_radar_next_channel_phy *phy,
        struct osw_channel_state *cs,
        const int n_cs)
{
    if (WARN_ON(phy == NULL)) return;

    if (cs == NULL)
    {
        FREE(phy->channel_states);
        phy->channel_states = NULL;
        phy->n_channel_states = 0;
    }
    else
    {
        const size_t size_cs = n_cs * sizeof(*cs);
        if (phy->channel_states != NULL && phy->n_channel_states == n_cs)
        {
            const int are_cs_different = memcmp(phy->channel_states, cs, size_cs) != 0;
            if (!are_cs_different) return;
        }

        FREE(phy->channel_states);
        phy->channel_states = MEMNDUP(cs, size_cs);
        phy->n_channel_states = n_cs;
    }

    osw_conf_invalidate(&phy->m->mut);
}

static void ow_radar_next_channel_garbage_collect(
        struct ow_radar_next_channel *m,
        struct ow_radar_next_channel_phy *phy)
{
    if (WARN_ON(phy == NULL)) return;
    if (!osw_channel_is_none(&phy->operating_channel)) return;
    if (!osw_channel_is_none(&phy->configured_channel)) return;
    if (phy->channel_states != NULL) return;

    osw_phy_chan_observer_dismantle(phy->operating_channel_obs);
    FREE(phy->channel_states);
    ds_tree_remove(&m->phy_tree, phy);
    FREE(phy->phy_name);
    FREE(phy);

    osw_conf_invalidate(&m->mut);
}

static void ow_radar_next_channel_conf_vif_changed_cb(struct ow_conf_observer *obs, const char *vif_name)
{
    if (vif_name == NULL) return;
    const struct osw_channel *chan = ow_conf_vif_get_ap_channel(vif_name);
    if (chan != NULL) LOGW("radar_next_channel doesn't support channel change per VIF, only per PHY");
}

static void ow_radar_next_channel_conf_phy_changed_cb(struct ow_conf_observer *obs, const char *phy_name)
{
    struct ow_radar_next_channel *m = conf_obs_to_m(obs);
    if (phy_name == NULL) return;
    const struct osw_channel *conf_chan = ow_conf_phy_get_ap_channel(phy_name);
    struct ow_radar_next_channel_phy *phy;
    phy = ds_tree_find(&m->phy_tree, phy_name);

    if (phy == NULL) phy = ow_radar_next_channel_create_new_phy(m, phy_name);
    if (WARN_ON(phy == NULL)) return;

    ow_radar_next_channel_update_configured_channel(phy, conf_chan);
    ow_radar_next_channel_garbage_collect(m, phy);
}

static void ow_radar_next_channel_state_phy_added_cb(
        struct osw_state_observer *obs,
        const struct osw_state_phy_info *phy_info)
{
    if (phy_info == NULL) return;
    const char *phy_name = phy_info->phy_name;
    struct ow_radar_next_channel *m = state_obs_to_m(obs);
    struct ds_tree phy_tree = m->phy_tree;
    struct ow_radar_next_channel_phy *phy;
    phy = ds_tree_find(&phy_tree, phy_name);

    if (phy == NULL) phy = ow_radar_next_channel_create_new_phy(m, phy_name);
    if (WARN_ON(phy == NULL)) return;

    if (phy_info->drv_state != NULL)
    {
        ow_radar_next_channel_update_channel_states(
                phy,
                phy_info->drv_state->channel_states,
                phy_info->drv_state->n_channel_states);
    }
    ow_radar_next_channel_setup_phy_chan_obs(phy);
}

static void ow_radar_next_channel_state_phy_changed_cb(
        struct osw_state_observer *obs,
        const struct osw_state_phy_info *phy_info)
{
    struct ow_radar_next_channel *m = state_obs_to_m(obs);
    struct ow_radar_next_channel_phy *phy;
    if (WARN_ON(phy_info == NULL)) return;
    phy = ds_tree_find(&m->phy_tree, phy_info->phy_name);

    if (WARN_ON(phy == NULL)) return;

    ow_radar_next_channel_update_channel_states(
            phy,
            phy_info->drv_state->channel_states,
            phy_info->drv_state->n_channel_states);
}

static void ow_radar_next_channel_state_phy_removed_cb(
        struct osw_state_observer *obs,
        const struct osw_state_phy_info *phy_info)
{
    struct ow_radar_next_channel *m = state_obs_to_m(obs);
    struct ow_radar_next_channel_phy *phy;
    phy = ds_tree_find(&m->phy_tree, phy_info->phy_name);

    if (WARN_ON(phy == NULL)) return;
    ow_radar_next_channel_update_configured_channel(phy, osw_channel_none());
    ow_radar_next_channel_update_channel_states(phy, NULL, 0);
    ow_radar_next_channel_garbage_collect(m, phy);
}

static enum osw_channel_width ow_radar_next_channel_get_max_width(struct ow_radar_next_channel *m, const char *phy_name)
{
    // fixme: add width to phy_capab
    enum osw_channel_width ow_conf_width = OSW_CHANNEL_20MHZ;
    struct ow_radar_next_channel_phy *phy;

    phy = ds_tree_find(&m->phy_tree, phy_name);
    if (phy != NULL)
    {
        if (!osw_channel_is_none(&phy->configured_channel))
            ow_conf_width = phy->configured_channel.width;
        else if (!osw_channel_is_none(&phy->operating_channel))
            ow_conf_width = phy->operating_channel.width;
    }
    if (ow_conf_width == OSW_CHANNEL_80P80MHZ) ow_conf_width = OSW_CHANNEL_20MHZ;

    return ow_conf_width;
}

static size_t ow_radar_next_channel_count_channels_of_width(
        const int *chanlist,
        const enum osw_channel_width width,
        const int last_channel_number)
{
    if (chanlist == NULL) return 0;
    size_t cnt = 0;
    const int *p;
    const int width_mhz = osw_channel_width_to_mhz(width);

    for (p = chanlist; *p != -1 && *p == width_mhz; p++)
    {
        while (*p != 0)
        {
            p++;
            if ((*p) > last_channel_number)
            {
                return cnt;
            }
        }
        cnt++;
    }
    return cnt;
}

static void ow_radar_next_channel_fill_channel(
        struct osw_channel *channel,
        const int *p_chanlist_subchannel,
        const enum osw_channel_width width,
        const enum osw_band band)
{
    if (channel == NULL) return;
    if (p_chanlist_subchannel == NULL) return;
    const int center_freq_chan = osw_chan_avg(p_chanlist_subchannel);
    const int center_freq = osw_chan_to_freq(band, center_freq_chan);

    channel->control_freq_mhz = osw_chan_to_freq(band, *p_chanlist_subchannel);
    channel->center_freq0_mhz = center_freq;
    channel->width = width;
    channel->puncture_bitmap = 0;
}

static int ow_radar_next_channel_fill_sieve_channels(
        struct ow_radar_next_channel_sieve *sieve,
        const int n_sieve,
        const int *chanlist_5g,
        enum osw_channel_width width)
{
    if (chanlist_5g == NULL || sieve == NULL) return 0;
    int n_filled_channels = 0;
    for (int i = 0; i < n_sieve; i++)
    {
        chanlist_5g++;  // move from width to 20mhz segment number
        ow_radar_next_channel_fill_channel(&(sieve[i].channel), chanlist_5g, width, OSW_BAND_5GHZ);
        n_filled_channels++;
        while (*chanlist_5g != 0)
            chanlist_5g++;                                 // move to the end of the row
        chanlist_5g++;                                     // move to the next row
        if (*chanlist_5g == -1) return n_filled_channels;  // it's the last row
    }
    return n_filled_channels;
}

static enum ow_radar_next_channel_priority ow_radar_next_channel_fill_sieve_priorities_segments_intersection_priority(
        struct osw_channel_state *segments_a,
        size_t n_segments_a,
        struct osw_channel_state *segments_b,
        size_t n_segments_b)
{
    n_segments_a = segments_a ? n_segments_a : 0;
    n_segments_b = segments_b ? n_segments_b : 0;

    enum ow_radar_next_channel_priority priority = OW_RADAR_NEXT_CHANNEL_NON_DFS;
    for (size_t i_a = 0; i_a < n_segments_a; i_a++)
    {
        for (size_t i_b = 0; i_b < n_segments_b; i_b++)
        {
            // i'm worried that control or puncturing may turn out a bit off.
            if (osw_channel_is_equal(&segments_a[i_a].channel, &segments_b[i_b].channel))
            {
                // if intersection of operating channel and sieve_channel is not full NON-DFS then such sieve_channel is
                // unusable
                if (segments_a[i_a].dfs_state == OSW_CHANNEL_NON_DFS
                    && segments_b[i_b].dfs_state == OSW_CHANNEL_NON_DFS)
                    priority = OW_RADAR_NEXT_CHANNEL_NON_DFS;
                else
                    return OW_RADAR_NEXT_CHANNEL_UNUSABLE;
            }
        }
    }
    return priority;
}

static void ow_radar_next_channel_fill_sieve_priorities_update(
        struct ow_radar_next_channel_sieve *sieve_elem,
        const struct osw_channel_state *segments,
        size_t n_segments,
        const struct osw_channel *operating_channel)
{
    if (sieve_elem == NULL) return;
    n_segments = segments ? n_segments : 0;
    if (operating_channel == NULL) operating_channel = osw_channel_none();

    for (size_t i = 0; i < n_segments && sieve_elem->priority != OW_RADAR_NEXT_CHANNEL_UNUSABLE; i++)
    {
        if (segments[i].dfs_state == OSW_CHANNEL_NON_DFS && sieve_elem->priority == OW_RADAR_NEXT_CHANNEL_NON_DFS)
        {
            if (!osw_channel_is_none(operating_channel)
                && operating_channel->control_freq_mhz == segments[i].channel.control_freq_mhz)
            {
                sieve_elem->priority = OW_RADAR_NEXT_CHANNEL_NON_DFS_AND_OPERATING;
                sieve_elem->channel.control_freq_mhz = operating_channel->control_freq_mhz;
                // TODO: this loop requires a refactor
                // TODO: and this module requires UTs
                break;
            }
            else
            {
                sieve_elem->priority = OW_RADAR_NEXT_CHANNEL_NON_DFS;
            }
        }
        else if (segments[i].dfs_state == OSW_CHANNEL_DFS_CAC_COMPLETED || segments[i].dfs_state == OSW_CHANNEL_NON_DFS)
        {
            sieve_elem->priority = OW_RADAR_NEXT_CHANNEL_NON_DFS_OR_CAC_COMPLETED;
        }
        else
        {
            sieve_elem->priority = OW_RADAR_NEXT_CHANNEL_UNUSABLE;
            break;
        }
    }
}

static void ow_radar_next_channel_fill_sieve_priorities(
        struct ow_radar_next_channel_sieve *sieve,
        int n_sieve,
        const struct osw_channel_state *channel_states,
        int n_channel_states,
        const struct osw_channel *operating_channel)
{
    n_sieve = sieve ? n_sieve : 0;
    n_channel_states = channel_states ? n_channel_states : 0;
    operating_channel = operating_channel ?: osw_channel_none();
    size_t n_operating_segments;
    struct osw_channel_state *operating_segments =
            osw_cs_chan_get_segments_states(channel_states, n_channel_states, operating_channel, &n_operating_segments);
    // maybe i should ignore such situation and fill the sieve anyways?
    // if (n_operating_segments == 0) return;
    for (int i = 0; i < n_sieve; i++)
    {
        size_t n_segments;
        struct osw_channel_state *segments =
                osw_cs_chan_get_segments_states(channel_states, n_channel_states, &(sieve[i].channel), &n_segments);
        if (n_segments > 0)
        {
            sieve[i].priority = ow_radar_next_channel_fill_sieve_priorities_segments_intersection_priority(
                    segments,
                    n_segments,
                    operating_segments,
                    n_operating_segments);
            ow_radar_next_channel_fill_sieve_priorities_update(&sieve[i], segments, n_segments, operating_channel);
        }
        FREE(segments);
    }
    FREE(operating_segments);
}

static int ow_radar_next_channel_fill_sieve(
        struct ow_radar_next_channel_sieve *sieve,
        int n_sieve,
        const int *chanlist_5g,
        enum osw_channel_width width,
        const struct osw_channel_state *cs,
        int n_cs,
        const struct osw_channel *operating_channel)
{
    n_sieve = sieve ? n_sieve : 0;
    n_cs = cs ? n_cs : 0;

    n_sieve = ow_radar_next_channel_fill_sieve_channels(sieve, n_sieve, chanlist_5g, width);
    ow_radar_next_channel_fill_sieve_priorities(sieve, n_sieve, cs, n_cs, operating_channel);
    return n_sieve;
}

static struct osw_channel *ow_radar_next_channel_select_first_with_priority(
        struct ow_radar_next_channel_sieve *sieve,
        size_t n_sieve,
        enum ow_radar_next_channel_priority priority)
{
    n_sieve = sieve ? n_sieve : 0;
    for (size_t i = 0; i < n_sieve; i++)
        if (sieve[i].priority == priority) return &(sieve[i].channel);
    return NULL;
}

static const struct ow_radar_next_channel_phy *ow_radar_next_channel_get_phy(
        struct ow_radar_next_channel *m,
        const char *phy_name)
{
    const struct ow_radar_next_channel_phy *phy = ds_tree_find(&m->phy_tree, phy_name);
    return phy;
}

static bool ow_radar_next_channel_preliminary_check(const struct ow_radar_next_channel_phy *phy)
{
    if (phy == NULL) return false;

    const struct osw_state_phy_info *phy_info = osw_state_phy_lookup(phy->phy_name);
    const struct osw_drv_phy_state *phy_state = phy_info ? phy_info->drv_state : NULL;
    if (phy_state == NULL || !phy_state->exists) return false;
    if (phy_state->radar != OSW_RADAR_DETECT_ENABLED) return false;
    return true;
}

static struct osw_channel ow_radar_next_channel_select(
        struct ow_radar_next_channel *m,
        const struct ow_radar_next_channel_phy *phy)
{
    const char *phy_name = phy->phy_name;
    const struct osw_channel_state *channel_states = phy->channel_states;
    const int n_channel_states = phy->n_channel_states;
    if (channel_states == NULL || n_channel_states == 0)  // This module can't help without channel_states
        return *osw_channel_none();

    const enum osw_band radio_band = osw_cs_chan_get_band(channel_states, n_channel_states);
    switch (radio_band)
    {
        case OSW_BAND_UNDEFINED:
            LOGW(LOG_PREFIX(
                    LOG_PREFIX_PHY(phy_name, "has OSW_BAND_UNDEFINED, multiband is not supported by this mutator")));
            return *osw_channel_none();
        case OSW_BAND_2GHZ:
        case OSW_BAND_6GHZ:
            return *osw_channel_none();
        case OSW_BAND_5GHZ:
            break;
    }

    const struct osw_channel *currently_operating_channel = &phy->operating_channel;
    if (currently_operating_channel == NULL) return *osw_channel_none();
    // Skip NON-DFS parts of bands 5G band
    if (!osw_channel_overlaps_dfs(currently_operating_channel)) return *osw_channel_none();

    int first_chan_num, last_chan_num;
    osw_channel_state_get_min_max(channel_states, n_channel_states, &first_chan_num, &last_chan_num);

    enum osw_channel_width width_to_try = ow_radar_next_channel_get_max_width(m, phy_name);

    do
    {
        int width_to_try_mhz = osw_channel_width_to_mhz(width_to_try);
        // note: Here the ordering of array returned by unii_5g_chan2list is abused.
        const int *chanlist = unii_5g_chan2list(first_chan_num, width_to_try_mhz);

        if (chanlist == NULL) continue;
        while (*chanlist != width_to_try_mhz && *chanlist != 0)
            chanlist--;  // chanlist is returned pointing to chan_num, I want it pointing towards width

        const size_t n_chanlist = ow_radar_next_channel_count_channels_of_width(chanlist, width_to_try, last_chan_num);
        if (n_chanlist == 0) continue;

        size_t n_sieve = n_chanlist;
        struct ow_radar_next_channel_sieve sieve[n_sieve];
        MEMZERO(sieve);
        n_sieve = ow_radar_next_channel_fill_sieve(
                sieve,
                n_sieve,
                chanlist,
                width_to_try,
                channel_states,
                n_channel_states,
                currently_operating_channel);

        for (size_t i = 0; i < n_sieve; i++)
        {
            LOGT(LOG_PREFIX_PHY(phy_name, "sieve[%zu]: control_freq_mhz: %d, width: %s, priority: %s"),
                 i,
                 sieve[i].channel.control_freq_mhz,
                 osw_channel_width_to_str(sieve[i].channel.width),
                 ow_radar_next_channel_priority_to_str(sieve[i].priority));
        }

        const enum ow_radar_next_channel_priority priorities[] = {
            OW_RADAR_NEXT_CHANNEL_NON_DFS_AND_OPERATING,
            OW_RADAR_NEXT_CHANNEL_NON_DFS,
            OW_RADAR_NEXT_CHANNEL_NON_DFS_OR_CAC_COMPLETED};
        const size_t n = ARRAY_SIZE(priorities);
        for (size_t i = 0; i < n; i++)
        {
            const struct osw_channel *selected =
                    ow_radar_next_channel_select_first_with_priority(sieve, n_sieve, priorities[i]);
            if (selected != NULL) return *selected;
        }
    } while (osw_channel_width_down(&width_to_try));
    return *osw_channel_none();
}

static void ow_radar_next_channel_mutate_cb(struct osw_conf_mutator *mut, struct ds_tree *phy_tree)
{
    struct ow_radar_next_channel *m = mut_to_m(mut);
    if (m == NULL) return;
    if (m->enabled == false) return;

    struct osw_conf_phy *conf_phy;
    ds_tree_foreach (phy_tree, conf_phy)
    {
        const struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_get_phy(m, conf_phy->phy_name);
        struct osw_channel c = *osw_channel_none();
        if (ow_radar_next_channel_preliminary_check(phy))
        {
            c = ow_radar_next_channel_select(m, phy);
        }
        conf_phy->radar_next_channel = c;
    }
}

static void ow_radar_next_channel_init(struct ow_radar_next_channel *m)
{
    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .mutate_fn = ow_radar_next_channel_mutate_cb,
        .type = OSW_CONF_TAIL,
    };

    const struct ow_conf_observer conf_obs = {
        .name = __FILE__,
        .phy_changed_fn = ow_radar_next_channel_conf_phy_changed_cb,
        .vif_changed_fn = ow_radar_next_channel_conf_vif_changed_cb,
    };
    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .phy_added_fn = ow_radar_next_channel_state_phy_added_cb,
        .phy_changed_fn = ow_radar_next_channel_state_phy_changed_cb,
        .phy_removed_fn = ow_radar_next_channel_state_phy_removed_cb,
    };

    m->enabled = OW_RADAR_NEXT_CHANNEL_ENABLED_DEFAULT;
    m->conf_obs = conf_obs;
    m->state_obs = state_obs;
    m->mut = mut;
    ds_tree_init(&m->phy_tree, ds_str_cmp, struct ow_radar_next_channel_phy, node);
}

static void ow_radar_next_channel_attach(struct ow_radar_next_channel *m)
{
    OSW_MODULE_LOAD(osw_conf);
    osw_conf_register_mutator(&m->mut);
    ow_conf_register_observer(&m->conf_obs);
    osw_state_register_observer(&m->state_obs);
}

OSW_MODULE(ow_radar_next_channel)
{
    static struct ow_radar_next_channel m;
    ow_radar_next_channel_init(&m);

    if (osw_etc_get("OW_RADAR_NEXT_CHANNEL_DISABLE")) m.enabled = false;
    LOGI(LOG_PREFIX("enabled: %d", m.enabled));

    ow_radar_next_channel_attach(&m);
    return &m;
}

OSW_UT(ow_radar_next_channel_36_40)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5260, .center_freq0_mhz = 5270, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5180, .center_freq0_mhz = 5190, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5180}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5200}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5220}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5240}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5260},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5280},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5300},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5320},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_44_80)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5220, .center_freq0_mhz = 5250, .width = OSW_CHANNEL_160MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5220, .center_freq0_mhz = 5210, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5180}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5200}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5220}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5240}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5260},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5280},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5300},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5320},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_120_160)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5600, .center_freq0_mhz = 5570, .width = OSW_CHANNEL_160MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5180, .center_freq0_mhz = 5250, .width = OSW_CHANNEL_160MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5180}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5200}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5220}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5240}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5260},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5280},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5300},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5320},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_116_40)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5500, .center_freq0_mhz = 5530, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5580, .center_freq0_mhz = 5590, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5500},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5520},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5540},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5560},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5580},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5600},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5620},
         .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5640},
         .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_116_80)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5560, .center_freq0_mhz = 5530, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5580, .center_freq0_mhz = 5610, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5500},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5520},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5540},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5560},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5580},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5600},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5620},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5640},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5745}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_149_40)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5540, .center_freq0_mhz = 5550, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5745, .center_freq0_mhz = 5755, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5500},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5520},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5540},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5560},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5580},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5600},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5620},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5640},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5745}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5765}, .dfs_state = OSW_CHANNEL_NON_DFS, .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_108_20)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5500, .center_freq0_mhz = 5510, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5540, .center_freq0_mhz = 5540, .width = OSW_CHANNEL_20MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5500},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5520}, .dfs_state = OSW_CHANNEL_DFS_NOL, .dfs_nol_remaining_seconds = 1800},
        {.channel = {.control_freq_mhz = 5540},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5560},
         .dfs_state = OSW_CHANNEL_DFS_CAC_IN_PROGRESS,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5580},
         .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5600},
         .dfs_state = OSW_CHANNEL_DFS_CAC_IN_PROGRESS,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}

OSW_UT(ow_radar_next_channel_116_20)
{
    struct ow_radar_next_channel m = {0};
    ow_radar_next_channel_init(&m);

    struct ow_radar_next_channel_phy *phy = ow_radar_next_channel_alloc_new_phy(&m, "UT_PHY");

    struct osw_channel operating =
            {.control_freq_mhz = 5500, .center_freq0_mhz = 5530, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0};
    const struct osw_channel expected =
            {.control_freq_mhz = 5600, .center_freq0_mhz = 5600, .width = OSW_CHANNEL_20MHZ, .puncture_bitmap = 0};

    ow_radar_next_channel_update_operating_channel(phy, &operating);
    ow_radar_next_channel_update_configured_channel_set(phy, &operating);

    phy->channel_states = NULL;
    phy->n_channel_states = 0;

    struct osw_channel_state cs_5gl[] = {
        {.channel = {.control_freq_mhz = 5500},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5520}, .dfs_state = OSW_CHANNEL_DFS_NOL, .dfs_nol_remaining_seconds = 1800},
        {.channel = {.control_freq_mhz = 5540},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5560}, .dfs_state = OSW_CHANNEL_DFS_NOL, .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5580},
         .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE,
         .dfs_nol_remaining_seconds = 0},
        {.channel = {.control_freq_mhz = 5600},
         .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED,
         .dfs_nol_remaining_seconds = 0}};
    phy->channel_states = cs_5gl;
    phy->n_channel_states = ARRAY_SIZE(cs_5gl);

    const struct osw_channel c = ow_radar_next_channel_select(&m, phy);
    assert(osw_channel_cmp(&c, &expected) == 0);
}
