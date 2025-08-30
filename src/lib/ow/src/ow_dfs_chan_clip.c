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

/* opensync */
#include <log.h>
#include <const.h>
#include <util.h>
#include <os.h>

/* osw */
#include <osw_module.h>
#include <osw_ut.h>
#include <osw_types.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>

/*
 * DFS Channel Clipping
 *
 * Purpose:
 *
 * Installs an osw_conf_mutator to apply channel
 * transformations based on channel availability
 * on a given PHY.
 *
 *
 * Description:
 *
 * Sometimes the configuration data model will end
 * up requesting an invalid channel, most often
 * tied to DFS NOL states.
 *
 * Sometimes the PHY will run away to an undesired
 * channel, whereas it could've kept some
 * resemblance to the original config.
 *
 * As such it is always preferred to maintain the
 * primary channel of the original configuration
 * request, even if it means narrowing down the
 * channel bandwidth. This is done in order to
 * maintain service to possible Leaf Repetater
 * nodes which are bound to primary channels.
 *
 * If that can't be done, because primary channel
 * itself became unavailable, then the next best
 * thing is to keep the current channel the system
 * is operating on.
 *
 * If that can't be done for some reason, which is
 * unlikely, but possible in a case where state
 * report comes late, a first usable 20MHz channel
 * is picked.
 *
 * If all fails, PHY is considered unusable,
 * and is disabled, probably until one of the
 * channels comes out of NOL, at which point
 * osw_confsync and osw_conf will try again,
 * allowing this module to mutate the channel to
 * _some_ usable one.
 */

/* private */
struct ow_dfs_chan_clip {
    struct osw_conf_mutator mut;
    struct osw_state_observer obs;
    struct ds_tree phys;
    struct ds_tree vifs;
};

enum ow_dfs_chan_clip_result {
    OW_DFS_CHAN_UNSPEC,
    OW_DFS_CHAN_ORIGINAL,
    OW_DFS_CHAN_NARROWED,
    OW_DFS_CHAN_PUNCTURED,
    OW_DFS_CHAN_INHERITED_STATE,
    OW_DFS_CHAN_INHERITED_STATE_AND_NARROWED,
    OW_DFS_CHAN_INHERITED_STATE_AND_PUNCTURED,
    OW_DFS_CHAN_LAST_RESORT_WIDEST,
    OW_DFS_CHAN_LAST_RESORT,
    OW_DFS_CHAN_DISABLED,
};

struct ow_dfs_chan_clip_phy {
    struct ow_dfs_chan_clip *m;
    struct ds_tree_node node;
    char *phy_name;
    bool busy;
    struct osw_timer postpone;
    struct osw_timer backoff;
};

struct ow_dfs_chan_clip_vif {
    struct ow_dfs_chan_clip *m;
    struct ds_tree_node node;
    char *vif_name;
    enum ow_dfs_chan_clip_result last_result;
    bool removing;
    struct osw_channel orig_c;
    struct osw_channel new_c;
};

#define LOG_PREFIX(fmt, ...) "ow: dfs_chan_clip: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_PHY(phy_name, fmt, ...) LOG_PREFIX("%s: " fmt, phy_name, ##__VA_ARGS__)
#define LOG_PREFIX_VIF(phy_name, vif_name, fmt, ...) LOG_PREFIX("%s/%s: " fmt, phy_name, vif_name, ##__VA_ARGS__)
#define LOG_PREFIX_POSTPONE(phy_name, fmt, ...) LOG_PREFIX_PHY(phy_name, "postpone: " fmt, ##__VA_ARGS__)
#define mut_to_mod(mut_) container_of(mut_, struct ow_dfs_chan_clip, mut)
#define obs_to_mod(obs_) container_of(obs_, struct ow_dfs_chan_clip, obs)
#define OW_DFS_CHAN_CLIP_LAST_RESORT_POSTPONE_SEC OSW_TIME_SEC(3)
#define OW_DFS_CHAN_CLIP_LAST_RESORT_BACKOFF_SEC OSW_TIME_SEC(5)

static void
ow_dfs_chan_clip_postpone_end_cb(struct osw_timer *t)
{
    struct ow_dfs_chan_clip_phy *phy = container_of(t,
                                                    struct ow_dfs_chan_clip_phy,
                                                    postpone);
    LOGT(LOG_PREFIX("last resort postpone ended"));
    osw_conf_invalidate(&phy->m->mut);
}

static void
ow_dfs_chan_clip_backoff_end_cb(struct osw_timer *t)
{
    struct ow_dfs_chan_clip_phy *phy = container_of(t,
                                                    struct ow_dfs_chan_clip_phy,
                                                    backoff);
    LOGT(LOG_PREFIX("last resort backoff ended"));
    osw_conf_invalidate(&phy->m->mut);
}

static struct ow_dfs_chan_clip_phy *
ow_dfs_chan_clip_phy_alloc(struct ow_dfs_chan_clip *m,
                           const char *phy_name)
{
    struct ow_dfs_chan_clip_phy *phy = CALLOC(1, sizeof(*phy));
    phy->phy_name = STRDUP(phy_name);
    phy->m = m;
    osw_timer_init(&phy->postpone, ow_dfs_chan_clip_postpone_end_cb);
    osw_timer_init(&phy->backoff, ow_dfs_chan_clip_backoff_end_cb);
    ds_tree_insert(&m->phys, phy, phy->phy_name);
    return phy;
}

static void
ow_dfs_chan_clip_phy_free(struct ow_dfs_chan_clip_phy *phy)
{
    ds_tree_remove(&phy->m->phys, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

static bool
ow_dfs_chan_clip_phy_is_postponed(struct ow_dfs_chan_clip *m,
                                  const char *phy_name)
{
    struct ow_dfs_chan_clip_phy *phy = ds_tree_find(&m->phys, phy_name);
    if (phy == NULL) return false;
    return osw_timer_is_armed(&phy->postpone);
}

static void
ow_dfs_chan_clip_phy_added_cb(struct osw_state_observer *obs,
                              const struct osw_state_phy_info *info)
{
    const char *phy_name = info->phy_name;
    struct ow_dfs_chan_clip *m = obs_to_mod(obs);
    struct ow_dfs_chan_clip_phy *phy = ow_dfs_chan_clip_phy_alloc(m, phy_name);
    (void)phy;
}

static void
ow_dfs_chan_clip_phy_removed_cb(struct osw_state_observer *obs,
                                const struct osw_state_phy_info *info)
{
    const char *phy_name = info->phy_name;
    struct ow_dfs_chan_clip *m = obs_to_mod(obs);
    struct ow_dfs_chan_clip_phy *phy = ds_tree_find(&m->phys, phy_name);

    osw_timer_disarm(&phy->postpone);
    osw_timer_disarm(&phy->backoff);
    osw_conf_invalidate(&m->mut);
    ow_dfs_chan_clip_phy_free(phy);
}

static bool
ow_dfs_chan_clip_vif_try_puncture(const struct osw_channel_state *channel_states,
                                  size_t n_channel_states,
                                  struct osw_channel *src,
                                  struct osw_channel *dst)
{
    int freqs[16];
    size_t n_freqs = osw_channel_20mhz_segments(src, freqs, sizeof(freqs));
    if (n_freqs == 0) return false;

    const int control = src->control_freq_mhz;
    uint16_t puncture_bitmap = 0;
    while (n_freqs > 0) {
        const uint16_t puncture_bit = (1 << (n_freqs - 1));
        size_t i;
        for (i = 0; i < n_channel_states; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            const struct osw_channel *oc = &cs->channel;
            const int oc_freq = oc->control_freq_mhz;
            if (oc_freq != freqs[n_freqs - 1]) continue;
            if (cs->dfs_state != OSW_CHANNEL_DFS_NOL) continue;
            if (oc_freq  == control) return false;
            puncture_bitmap |= puncture_bit;
        }
        n_freqs--;
    }

    if (puncture_bitmap == 0) {
        return false;
    }

    *dst = *src;
    dst->puncture_bitmap = puncture_bitmap;
    return true;
}

static bool
ow_dfs_chan_clip_vif_try_narrow(const struct osw_channel_state *channel_states,
                                size_t n_channel_states,
                                struct osw_channel *c,
                                bool puncture,
                                bool *punctured,
                                bool *narrowed)
{
    while (osw_cs_chan_is_usable(channel_states, n_channel_states, c) == false) {
        if (puncture) {
            struct osw_channel new_c;
            *punctured = ow_dfs_chan_clip_vif_try_puncture(channel_states, n_channel_states, c, &new_c);
            if (*punctured) {
                *c = new_c;
                return true;
            }
        }
        const bool success = osw_channel_downgrade(c);
        if (!success) return false;
        *narrowed = true;
    }
    return true;
}

static bool
ow_dfs_chan_clip_vif_inherit_state(const struct osw_channel_state *channel_states,
                                   size_t n_channel_states,
                                   const struct osw_channel *state_c,
                                   struct osw_channel *c,
                                   bool puncture,
                                   bool *punctured,
                                   bool *narrowed)
{
    if (state_c == NULL) return false;
    *c = *state_c;
    return ow_dfs_chan_clip_vif_try_narrow(channel_states, n_channel_states, c, puncture, punctured, narrowed);
}

static const int *
ow_dfs_chan_clip_get_chanlist(const struct osw_channel *c,
                              const enum osw_channel_width width,
                              enum osw_band *band)
{
    const int freq = c->control_freq_mhz;
    const int chan = osw_freq_to_chan(freq);
    const int width_mhz = osw_channel_width_to_mhz(width);

    *band = osw_freq_to_band(freq);
    switch (*band) {
        case OSW_BAND_UNDEFINED:
            return NULL;
        case OSW_BAND_2GHZ:
            return NULL;
        case OSW_BAND_5GHZ:
            return unii_5g_chan2list(chan, width_mhz);
        case OSW_BAND_6GHZ:
            return unii_6g_chan2list(chan, width_mhz);
    }

    return NULL;
}

static bool
ow_dfs_chan_clip_chanlist_is_usable(const int *chanlist,
                                    const enum osw_band chanlist_band,
                                    const struct osw_channel_state *channel_states,
                                    size_t n_channel_states)
{
    if (chanlist == NULL) return false;

    while (*chanlist) {
        size_t i;
        for (i = 0; i < n_channel_states; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            const struct osw_channel *c = &cs->channel;
            const int freq = c->control_freq_mhz;
            const int chan = osw_freq_to_chan(freq);
            const enum osw_band band = osw_freq_to_band(freq);

            if (band != chanlist_band) continue;
            if (chan != *chanlist) continue;
            if (cs->dfs_state == OSW_CHANNEL_DFS_NOL) return false;
            break;
        }
        if (i == n_channel_states) return false;
        chanlist++;
    }

    return true;
}

static bool
ow_dfs_chan_clip_vif_any_widest(const struct osw_channel_state *channel_states,
                                size_t n_channel_states,
                                struct osw_channel *c)
{
    do {
        size_t i;
        for (i = 0; i < n_channel_states; i++) {
            const struct osw_channel_state *cs = &channel_states[i];
            enum osw_band band = OSW_BAND_UNDEFINED;
            const int *chanlist = ow_dfs_chan_clip_get_chanlist(&cs->channel, c->width, &band);
            if (ow_dfs_chan_clip_chanlist_is_usable(chanlist, band, channel_states, n_channel_states)) {
                c->control_freq_mhz = cs->channel.control_freq_mhz;
                // Only control_freq_mhz parameter can be taken from channel_states
                // as all the channel entries in there are 20MHz.
                const int center_freq = osw_chan_avg(chanlist);
                const int center_freq_chan = osw_chan_to_freq(band, center_freq);
                c->center_freq0_mhz = center_freq_chan;
                return true;
            }
        }
    } while (osw_channel_downgrade(c));

    return false;
}

static bool
ow_dfs_chan_clip_vif_any(const struct osw_channel_state *channel_states,
                         size_t n_channel_states,
                         struct osw_channel *c)
{
    size_t i;
    /* FIXME: This could be smarter and look for widest
     * possible channel, but this is intended to be
     * last-resort lookup to provide _any_ level of service.
     * Keep it simple for now. This won't be reached too
     * often anyway.
     */
    for (i = 0; i < n_channel_states; i++) {
        const struct osw_channel_state *cs = &channel_states[i];
        if (cs->dfs_state == OSW_CHANNEL_DFS_NOL) continue;
        const struct osw_channel *oc = &cs->channel;
        *c = *oc;
        return true;
    }
    return false;
}

static enum ow_dfs_chan_clip_result
ow_dfs_chan_clip_vif_chan(struct ow_dfs_chan_clip *m,
                          const struct osw_channel_state *channel_states,
                          size_t n_channel_states,
                          const struct osw_channel *state_c,
                          struct osw_channel *c,
                          bool *enabled,
                          bool puncture,
                          const bool postponed)
{
    bool narrowed = false;
    bool punctured = false;
    bool usable = false;
    struct osw_channel copy = *c;

    /* Can't report as DISABLED because it would impact
     * settling wait logic. Instead report unspec. This
     * virtually means no-op since the vif is already
     * intended to be disabled anyway.
     */
    if (*enabled == false) return OW_DFS_CHAN_UNSPEC;

    /* Sanity check to limit VLA below */
    if (WARN_ON(n_channel_states > 1024)) return OW_DFS_CHAN_UNSPEC;

    struct osw_channel_state postponed_states[n_channel_states];
    memcpy(postponed_states, channel_states, n_channel_states * sizeof(channel_states[0]));

    size_t i;
    for (i = 0; i < n_channel_states; i++) {
        postponed_states[i].dfs_state = OSW_CHANNEL_NON_DFS;
    }

    usable = ow_dfs_chan_clip_vif_try_narrow(channel_states,
                                             n_channel_states,
                                             c,
                                             puncture,
                                             &punctured,
                                             &narrowed);
    if (usable) {
        if (punctured) return OW_DFS_CHAN_PUNCTURED;
        if (narrowed) return OW_DFS_CHAN_NARROWED;
        return OW_DFS_CHAN_ORIGINAL;
    }

    /* PHY channel states are reported independently to VIF
     * channels. When DFS radar is seen VIFs can report
     * channel updates as staggered. osw_confsync could see
     * this inconsistency and start reconfiguring channels
     * unnecessarily. This below is used to temporarily
     * ignore DFS NOL states and stick to the VIF's channel
     * states to keep osw_confsync idle. Once system
     * converges or timers expire regular channel states
     * will be used.
     *
     * This prevents last-resort kicking in too soon.
     */
    const struct osw_channel_state *inherit_states = postponed
                                                   ? postponed_states
                                                   : channel_states;

    *c = copy;
    narrowed = false;
    usable = ow_dfs_chan_clip_vif_inherit_state(inherit_states,
                                                n_channel_states,
                                                state_c,
                                                c,
                                                puncture,
                                                &punctured,
                                                &narrowed);
    if (usable) {
        if (punctured) return OW_DFS_CHAN_INHERITED_STATE_AND_PUNCTURED;
        if (narrowed) return OW_DFS_CHAN_INHERITED_STATE_AND_NARROWED;
        return OW_DFS_CHAN_INHERITED_STATE;
    }

    *c = copy;
    usable = ow_dfs_chan_clip_vif_any_widest(channel_states,
                                             n_channel_states,
                                             c);
    if (usable) {
        return OW_DFS_CHAN_LAST_RESORT_WIDEST;
    }

    *c = copy;
    usable = ow_dfs_chan_clip_vif_any(channel_states,
                                      n_channel_states,
                                      c);
    if (usable) {
        return OW_DFS_CHAN_LAST_RESORT;
    }

    *enabled = false;
    *c = copy;
    return OW_DFS_CHAN_DISABLED;
}

static struct ow_dfs_chan_clip_vif *
ow_dfs_chan_clip_vif_alloc(struct ow_dfs_chan_clip *m,
                           const char *vif_name)
{
    struct ow_dfs_chan_clip_vif *vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    vif->m = m;
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    return vif;
}

static void
ow_dfs_chan_clip_vif_free(struct ow_dfs_chan_clip_vif *vif)
{
    ds_tree_remove(&vif->m->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static bool
ow_dfs_chan_clip_vif_log_allowed(struct ow_dfs_chan_clip *m,
                                 const char *phy_name,
                                 const char *vif_name,
                                 const struct osw_channel *orig_c,
                                 const struct osw_channel *new_c,
                                 const enum ow_dfs_chan_clip_result result)
{
    if (result == OW_DFS_CHAN_UNSPEC) return false;
    if (strlen(vif_name) == 0) return true;

    struct ow_dfs_chan_clip_vif *vif = ds_tree_find(&m->vifs, vif_name);
    const bool dont_log = (result == OW_DFS_CHAN_ORIGINAL) && (vif == NULL);
    if (dont_log) return false;

    if (vif == NULL) {
        vif = ow_dfs_chan_clip_vif_alloc(m, vif_name);
    }

    const size_t size = sizeof(*orig_c);
    const bool changed = (vif->last_result != result)
                      || (memcmp(&vif->orig_c, orig_c, size) != 0)
                      || (memcmp(&vif->new_c, new_c, size) != 0);

    vif->last_result = result;
    vif->orig_c = *orig_c;
    vif->new_c = *new_c;
    vif->removing = (vif->last_result == OW_DFS_CHAN_ORIGINAL);

    return changed;
}

static void
ow_dfs_chan_clip_vif_log_mark_removing(struct ow_dfs_chan_clip *m)
{
    struct ow_dfs_chan_clip_vif *vif;

    ds_tree_foreach(&m->vifs, vif) {
        vif->removing = true;
    }
}

static void
ow_dfs_chan_clip_vif_log_gc(struct ow_dfs_chan_clip *m)
{
    struct ow_dfs_chan_clip_vif *vif;
    struct ow_dfs_chan_clip_vif *tmp;

    ds_tree_foreach_safe(&m->vifs, vif, tmp) {
        if (vif->removing) {
            ow_dfs_chan_clip_vif_free(vif);
        }
    }
}

static void
ow_dfs_chan_clip_vif_log(struct ow_dfs_chan_clip *m,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_channel *orig_c,
                         const struct osw_channel *new_c,
                         const enum ow_dfs_chan_clip_result result)
{
    const bool allowed = ow_dfs_chan_clip_vif_log_allowed(m, phy_name, vif_name, orig_c, new_c, result);
    const bool not_allowed = !allowed;
    if (not_allowed) return;

    switch (result) {
        case OW_DFS_CHAN_UNSPEC:
            break;
        case OW_DFS_CHAN_ORIGINAL:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "original: " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c)));
            break;
        case OW_DFS_CHAN_NARROWED:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "narrowed: "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_PUNCTURED:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "punctured: "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_INHERITED_STATE:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "inheriting state: "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_INHERITED_STATE_AND_NARROWED:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "inheriting state (narrowed): "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_INHERITED_STATE_AND_PUNCTURED:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "inheriting state (punctured): "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_LAST_RESORT_WIDEST:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "last resort widest "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_LAST_RESORT:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "last resort "OSW_CHANNEL_FMT " -> " OSW_CHANNEL_FMT,
                                OSW_CHANNEL_ARG(orig_c),
                                OSW_CHANNEL_ARG(new_c)));
            break;
        case OW_DFS_CHAN_DISABLED:
            LOGN(LOG_PREFIX_VIF(phy_name, vif_name,
                                "disabling because no channels available (wanted "OSW_CHANNEL_FMT ")",
                                OSW_CHANNEL_ARG(orig_c)));
            break;
    }
}

static enum ow_dfs_chan_clip_result
ow_dfs_chan_clip_vif(struct ow_dfs_chan_clip *m,
                     struct osw_conf_phy *phy,
                     struct osw_conf_vif *vif,
                     struct osw_channel *orig_channel,
                     struct osw_channel *new_channel,
                     const bool apply)
{
    if (phy->enabled == false) return OW_DFS_CHAN_UNSPEC;
    if (vif->enabled == false) return OW_DFS_CHAN_UNSPEC;
    if (vif->vif_type != OSW_VIF_AP) return OW_DFS_CHAN_UNSPEC;

    const char *phy_name = phy->phy_name;
    const char *vif_name = vif->vif_name;
    const struct osw_state_vif_info *vif_info = osw_state_vif_lookup(phy_name, vif_name);
    const struct osw_drv_vif_state *vif_state = vif_info ? vif_info->drv_state : NULL;
    const struct osw_state_phy_info *phy_info = osw_state_phy_lookup(phy_name);
    const struct osw_drv_phy_state *phy_state = phy_info ? phy_info->drv_state : NULL;
    const struct osw_channel_state *chan_states = phy_state ? phy_state->channel_states : NULL;
    const size_t n_chan_states = phy_state ? phy_state->n_channel_states : 0;
    const struct osw_channel *state_c = (vif_state &&
                                         vif_state->vif_type == OSW_VIF_AP &&
                                         vif_state->status == OSW_VIF_ENABLED)
                                      ? &vif_state->u.ap.channel
                                      : NULL;
    const bool is_postponed = ow_dfs_chan_clip_phy_is_postponed(m, phy_name);
    *new_channel = vif->u.ap.channel;
    *orig_channel = vif->u.ap.channel;
    bool enabled = vif->enabled;
    const bool puncture = phy_state->puncture_supported;
    const enum ow_dfs_chan_clip_result result = ow_dfs_chan_clip_vif_chan(m,
                                                                          chan_states,
                                                                          n_chan_states,
                                                                          state_c,
                                                                          new_channel,
                                                                          &enabled,
                                                                          puncture,
                                                                          is_postponed);
    if (apply) {
        vif->u.ap.channel = *new_channel;
        vif->enabled = enabled;
        ow_dfs_chan_clip_vif_log(m, phy_name, vif_name, orig_channel, new_channel, result);
    }
    return result;
}

static bool
ow_dfs_chan_clip_phy_some_channels_available(const char *phy_name)
{
    const struct osw_state_phy_info *phy_info = osw_state_phy_lookup(phy_name);
    const struct osw_drv_phy_state *phy_state = phy_info ? phy_info->drv_state : NULL;
    const struct osw_channel_state *chan_states = phy_state ? phy_state->channel_states : NULL;
    size_t n_chan_states = phy_state ? phy_state->n_channel_states : 0;

    while (n_chan_states--) {
        if (chan_states->dfs_state != OSW_CHANNEL_DFS_NOL) {
            return true;
        }
        chan_states++;
    }
    return false;
}

static void
ow_dfs_chan_clip_phy(struct osw_conf_phy *phy)
{
    const char *phy_name = phy->phy_name;

    if (phy->enabled == false) return;
    if (ow_dfs_chan_clip_phy_some_channels_available(phy_name)) return;

    LOGI(LOG_PREFIX_PHY(phy_name, "no channels available, disabling"));
    phy->enabled = false;
}

static void
ow_dfs_chan_clip_postpone_process(struct ow_dfs_chan_clip *m,
                                  const char *phy_name,
                                  bool settled)
{
    struct ow_dfs_chan_clip_phy *phy = ds_tree_find(&m->phys, phy_name);
    if (phy == NULL) return;

    if (settled == false) {
        if (osw_timer_is_armed(&phy->backoff) == false) {
            const uint64_t postpone_until = osw_time_mono_clk()
                                          + OW_DFS_CHAN_CLIP_LAST_RESORT_POSTPONE_SEC;
            const uint64_t backoff_until = osw_time_mono_clk()
                                         + OW_DFS_CHAN_CLIP_LAST_RESORT_BACKOFF_SEC;
            osw_timer_arm_at_nsec(&phy->postpone, postpone_until);
            osw_timer_arm_at_nsec(&phy->backoff, backoff_until);
            LOGI(LOG_PREFIX_POSTPONE(phy_name, "starting wait for all vifs to settle"));
            return;
        }

        if (osw_timer_is_armed(&phy->postpone)) {
            LOGI(LOG_PREFIX_POSTPONE(phy_name, "waiting for all vifs to settle"));
            return;
        }

        if (osw_timer_is_armed(&phy->backoff)) {
            LOGN(LOG_PREFIX_POSTPONE(phy_name, "still not settled after postponing, "
                                               "expect possible configuration hiccups"));
        }
    }
    else {
        if (osw_timer_is_armed(&phy->postpone)) {
            return;
        }
        else if (osw_timer_is_armed(&phy->backoff)) {
            LOGI(LOG_PREFIX_POSTPONE(phy_name, "settled"));
        }

        osw_timer_disarm(&phy->backoff);
    }
}

static bool
ow_dfs_chan_clip_is_settled(struct ow_dfs_chan_clip *m,
                            struct osw_conf_phy *phy)
{
    enum ow_dfs_chan_clip_result sr = OW_DFS_CHAN_UNSPEC;
    struct ds_tree *vif_tree = &phy->vif_tree;
    struct osw_conf_vif *vif;
    ds_tree_foreach(vif_tree, vif) {
        struct osw_channel o;
        struct osw_channel n;
        const enum ow_dfs_chan_clip_result r = ow_dfs_chan_clip_vif(m, phy, vif, &o, &n, false);
        if (r == OW_DFS_CHAN_UNSPEC) continue;
        if (sr == OW_DFS_CHAN_UNSPEC) sr = r;
        if (r != sr) return false;
    }
    switch (sr) {
        case OW_DFS_CHAN_UNSPEC:
        case OW_DFS_CHAN_ORIGINAL:
        case OW_DFS_CHAN_NARROWED:
        case OW_DFS_CHAN_PUNCTURED:
        case OW_DFS_CHAN_INHERITED_STATE:
        case OW_DFS_CHAN_INHERITED_STATE_AND_NARROWED:
        case OW_DFS_CHAN_INHERITED_STATE_AND_PUNCTURED:
        case OW_DFS_CHAN_DISABLED:
            break;
        case OW_DFS_CHAN_LAST_RESORT_WIDEST:
        case OW_DFS_CHAN_LAST_RESORT:
            return false;
    }
    return true;
}

static void
ow_dfs_chan_clip_conf_mutate_cb(struct osw_conf_mutator *mut,
                                struct ds_tree *phy_tree)
{
    LOGT(LOG_PREFIX("mutating"));

    struct ow_dfs_chan_clip *m = container_of(mut, struct ow_dfs_chan_clip, mut);
    struct osw_conf_phy *phy;

    ow_dfs_chan_clip_vif_log_mark_removing(m);

    ds_tree_foreach(phy_tree, phy) {
        struct ds_tree *vif_tree = &phy->vif_tree;
        struct osw_conf_vif *vif;

        ow_dfs_chan_clip_phy(phy);

        const char *phy_name = phy->phy_name;
        const bool is_settled = ow_dfs_chan_clip_is_settled(m, phy);
        ow_dfs_chan_clip_postpone_process(m, phy_name, is_settled);

        ds_tree_foreach(vif_tree, vif) {
            struct osw_channel o;
            struct osw_channel n;
            ow_dfs_chan_clip_vif(m, phy, vif, &o, &n, true);
            (void)o;
            (void)n;
        }
    }

    ow_dfs_chan_clip_vif_log_gc(m);
}

static void
ow_dfs_chan_clip_init(struct ow_dfs_chan_clip *m)
{
    LOGT(LOG_PREFIX("initializing"));

    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .type = OSW_CONF_TAIL,
        .mutate_fn = ow_dfs_chan_clip_conf_mutate_cb,
    };

    const struct osw_state_observer obs = {
        .name = __FILE__,
        .phy_added_fn = ow_dfs_chan_clip_phy_added_cb,
        .phy_removed_fn = ow_dfs_chan_clip_phy_removed_cb,
    };

    m->mut = mut;
    m->obs = obs;

    ds_tree_init(&m->phys,
                 (ds_key_cmp_t *)strcmp,
                 struct ow_dfs_chan_clip_phy,
                 node);
    ds_tree_init(&m->vifs,
                 (ds_key_cmp_t *)strcmp,
                 struct ow_dfs_chan_clip_vif,
                 node);
}

static void
ow_dfs_chan_clip_start(struct ow_dfs_chan_clip *m)
{
    LOGT(LOG_PREFIX("starting"));
    osw_conf_register_mutator(&m->mut);
    osw_state_register_observer(&m->obs);
}

static enum ow_dfs_chan_clip_result
ow_dfs_chan_clip_vif_test(struct ow_dfs_chan_clip *m,
                          const struct osw_channel_state *channel_states,
                          size_t n_channel_states,
                          const struct osw_channel *state_c,
                          struct osw_channel *c,
                          bool *enabled,
                          bool puncture,
                          bool postponed)
{
    const struct osw_channel orig_c = *c;
    const enum ow_dfs_chan_clip_result res = ow_dfs_chan_clip_vif_chan(m,
                                                                       channel_states,
                                                                       n_channel_states,
                                                                       state_c,
                                                                       c,
                                                                       enabled,
                                                                       puncture,
                                                                       postponed);
    ow_dfs_chan_clip_vif_log(m, "", "", &orig_c, c, res);
    return res;
}

OSW_UT(nol)
{
    struct ow_dfs_chan_clip m;
    const struct osw_channel ch36 = { .control_freq_mhz = 5180, .center_freq0_mhz = 5180 };
    const struct osw_channel ch40 = { .control_freq_mhz = 5200, .center_freq0_mhz = 5200 };
    const struct osw_channel ch52 = { .control_freq_mhz = 5260, .center_freq0_mhz = 5260 };
    const struct osw_channel ch56 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5280 };
    const struct osw_channel_state cs5gl[] = {
        { .channel = ch36, .dfs_state = OSW_CHANNEL_NON_DFS },
        { .channel = ch40, .dfs_state = OSW_CHANNEL_NON_DFS },
        { .channel = ch52, .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE },
        { .channel = ch56, .dfs_state = OSW_CHANNEL_DFS_NOL },
    };
    const struct osw_channel_state cs5gl_dfs[] = {
        { .channel = ch52, .dfs_state = OSW_CHANNEL_DFS_CAC_POSSIBLE },
        { .channel = ch56, .dfs_state = OSW_CHANNEL_DFS_NOL },
    };
    const struct osw_channel_state cs5gl_nol[] = {
        { .channel = ch52, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch56, .dfs_state = OSW_CHANNEL_DFS_NOL },
    };
    const struct osw_channel ch36ht20 = { .control_freq_mhz = 5180, .center_freq0_mhz = 5180, .width = OSW_CHANNEL_20MHZ };
    const struct osw_channel ch36ht40 = { .control_freq_mhz = 5180, .center_freq0_mhz = 5190, .width = OSW_CHANNEL_40MHZ };
    const struct osw_channel ch52ht20 = { .control_freq_mhz = 5260, .center_freq0_mhz = 5260, .width = OSW_CHANNEL_20MHZ };
    const struct osw_channel ch52ht40 = { .control_freq_mhz = 5260, .center_freq0_mhz = 5270, .width = OSW_CHANNEL_40MHZ };
    const struct osw_channel ch52ht40no56 = { .control_freq_mhz = 5260, .center_freq0_mhz = 5270, .width = OSW_CHANNEL_40MHZ, .puncture_bitmap = 0x0002 };
    const struct osw_channel ch56ht20 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5280, .width = OSW_CHANNEL_20MHZ };
    const struct osw_channel ch56ht40 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5270, .width = OSW_CHANNEL_40MHZ };
    const struct osw_channel ch56ht80 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5290, .width = OSW_CHANNEL_80MHZ };

    const struct osw_channel ch100 = { .control_freq_mhz = 5500, .center_freq0_mhz = 5500 };
    const struct osw_channel ch104 = { .control_freq_mhz = 5520, .center_freq0_mhz = 5520 };
    const struct osw_channel ch108 = { .control_freq_mhz = 5540, .center_freq0_mhz = 5540 };
    const struct osw_channel ch112 = { .control_freq_mhz = 5560, .center_freq0_mhz = 5560 };
    const struct osw_channel ch116 = { .control_freq_mhz = 5580, .center_freq0_mhz = 5580 };
    const struct osw_channel ch120 = { .control_freq_mhz = 5600, .center_freq0_mhz = 5600 };
    const struct osw_channel ch116ht40 = { .control_freq_mhz = 5580, .center_freq0_mhz = 5590, .width = OSW_CHANNEL_40MHZ };
    const struct osw_channel ch108ht80 = { .control_freq_mhz = 5540, .center_freq0_mhz = 5530, .width = OSW_CHANNEL_80MHZ };
    const struct osw_channel_state cs5gu_ht20[] = {
        { .channel = ch100, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch104, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch108, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch112, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch116, .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED },
    };

    const struct osw_channel_state cs5gu_ht40[] = {
        { .channel = ch100, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch104, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch108, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch112, .dfs_state = OSW_CHANNEL_DFS_NOL },
        { .channel = ch116, .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED },
        { .channel = ch120, .dfs_state = OSW_CHANNEL_DFS_CAC_COMPLETED },
    };
    //const struct osw_channel ch60ht80no5256 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5290, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0x0003 };
    //const struct osw_channel ch56ht80no60 = { .control_freq_mhz = 5280, .center_freq0_mhz = 5290, .width = OSW_CHANNEL_80MHZ, .puncture_bitmap = 0x0004 };
    struct osw_channel c;
    bool enabled;

    ow_dfs_chan_clip_init(&m);

    c = ch36ht20;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_ORIGINAL);
    assert(enabled == true);

    c = ch36ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_ORIGINAL);
    assert(enabled == true);

    c = ch52ht20;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_ORIGINAL);
    assert(enabled == true);

    c = ch52ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_NARROWED);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch52ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch52ht40.control_freq_mhz);
    assert(c.width == OSW_CHANNEL_20MHZ);

    c = ch52ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, true, false)
        == OW_DFS_CHAN_PUNCTURED);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch52ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch52ht40.center_freq0_mhz);
    assert(c.width == ch52ht40.width);
    assert(c.puncture_bitmap == 0x0002);

    c = ch52ht40no56;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_ORIGINAL);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch52ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch52ht40.center_freq0_mhz);
    assert(c.width == OSW_CHANNEL_40MHZ);
    assert(c.puncture_bitmap == 0x0002);

    c = ch56ht20;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), &ch36ht40, &c, &enabled, false, false)
        == OW_DFS_CHAN_INHERITED_STATE);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch36ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch36ht40.center_freq0_mhz);
    assert(c.width == ch36ht40.width);

    c = ch56ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), &ch36ht40, &c, &enabled, false, false)
        == OW_DFS_CHAN_INHERITED_STATE);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch36ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch36ht40.center_freq0_mhz);
    assert(c.width == ch36ht40.width);

    c = ch56ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl_dfs, ARRAY_SIZE(cs5gl_dfs), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch52ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch52ht40.control_freq_mhz);
    assert(c.width == OSW_CHANNEL_20MHZ);

    c = ch56ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl_nol, ARRAY_SIZE(cs5gl_nol), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_DISABLED);
    assert(enabled == false);

    c = ch56ht40;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch36ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch36ht40.center_freq0_mhz);
    assert(c.width == ch36ht40.width);

    c = ch56ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, false)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch36ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch36ht40.center_freq0_mhz);
    assert(c.width == ch36ht40.width);

    c = ch56ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl, ARRAY_SIZE(cs5gl), NULL, &c, &enabled, false, true)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch36ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch36ht40.center_freq0_mhz);
    assert(c.width == ch36ht40.width);

    c = ch56ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl_nol, ARRAY_SIZE(cs5gl_nol), &ch56ht20, &c, &enabled, false, true)
        == OW_DFS_CHAN_INHERITED_STATE);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch56ht20.control_freq_mhz);
    assert(c.center_freq0_mhz == ch56ht20.center_freq0_mhz);
    assert(c.width == ch56ht20.width);

    c = ch56ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl_nol, ARRAY_SIZE(cs5gl_nol), &ch56ht80, &c, &enabled, false, true)
        == OW_DFS_CHAN_INHERITED_STATE_AND_NARROWED);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch56ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch56ht40.center_freq0_mhz);
    assert(c.width == ch56ht40.width);

    c = ch56ht80;
    enabled = false;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gl_nol, ARRAY_SIZE(cs5gl_nol), &ch56ht80, &c, &enabled, false, true)
        == OW_DFS_CHAN_UNSPEC);
    assert(enabled == false);
    assert(c.control_freq_mhz == ch56ht80.control_freq_mhz);
    assert(c.center_freq0_mhz == ch56ht80.center_freq0_mhz);
    assert(c.width == ch56ht80.width);

    c = ch108ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gu_ht20, ARRAY_SIZE(cs5gu_ht20), &ch108ht80, &c, &enabled, false, false)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch116.control_freq_mhz);
    assert(c.center_freq0_mhz == ch116.center_freq0_mhz);
    assert(c.width == ch116.width);

    c = ch108ht80;
    enabled = true;
    assert(ow_dfs_chan_clip_vif_test(&m, cs5gu_ht40, ARRAY_SIZE(cs5gu_ht40), &ch108ht80, &c, &enabled, false, false)
        == OW_DFS_CHAN_LAST_RESORT_WIDEST);
    assert(enabled == true);
    assert(c.control_freq_mhz == ch116ht40.control_freq_mhz);
    assert(c.center_freq0_mhz == ch116ht40.center_freq0_mhz);
    assert(c.width == ch116ht40.width);
}

OSW_MODULE(ow_dfs_chan_clip)
{
    static struct ow_dfs_chan_clip m;
    ow_dfs_chan_clip_init(&m);
    ow_dfs_chan_clip_start(&m);
    return &m;
}
