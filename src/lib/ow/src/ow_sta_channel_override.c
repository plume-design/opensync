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
#include <os.h>
#include <memutil.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>
#include <osw_ut.h>

/* Purpose:
 *
 * This module provides logic that mutates the
 * osw_conf/osw_confsync so that if there's an active
 * OSW_VIF_STA link then any OSW_VIF_AP channel
 * configurations will be overridden with the root AP
 * operational channel.
 *
 * Rationale:
 *
 * This is currently desired (and implied in the design of
 * the entire system). If desired AP channels are different
 * than where the STA link currently resides on, then its
 * better to stick to the STA's channel. Otherwise, moving
 * away will sever the connecting to STA's root AP.
 * Consequently this will break internet connectivity and
 * cloud connection. Extender will be essentially orphaned
 * and will enter recovery procedures.
 *
 * Observable effect:
 *
 * The module will take effect whenever underlying data
 * model (eg. ovsdb) doesn't get updated with a new channel
 * while the sta link is on another channel that what the
 * data model wants.
 *
 * Another case is when CSA is in progress. As soon as CSA
 * intention gets processed by the driver, and somehow data
 * model gets updated fast enough, then data model would be
 * pointing to a future state of the STA interface (that is
 * still on old channel, waiting for CSA countdown to
 * finish).
 *
 * Future:
 *
 * Channel stickiness should be an explicitly configured
 * hint per PHY (this still assumes single-channel radios
 * and 1xSTA + NxAP). Possible options would be:
 * inherit_from_sta and inherit_from_ap.
 */

#define m_obs_to_m(obs_) container_of(obs_, struct ow_sta_channel_override, obs)
#define ap_obs_to_ap(obs_) container_of(obs_, struct ow_sta_channel_override_ap, obs)
#define ap_mut_to_ap(mut_) container_of(mut_, struct ow_sta_channel_override_ap, mut)
#define settling_to_ap(t_) container_of(t_, struct ow_sta_channel_override_ap, settling)
#define OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC 5.0
#define LOG_PREFIX(fmt, ...) "ow: sta_channel_override: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_AP(ap, fmt, ...) LOG_PREFIX("%s/%s: " fmt, ap->phy_name, ap->vif_name, ##__VA_ARGS__)
#define LOG_PREFIX_STA(sta, fmt, ...) LOG_PREFIX_AP(sta->ap, "%s: " fmt, sta->vif_name, ##__VA_ARGS__)
#define LOG_PREFIX_LATCH(ap, oc, nc, fmt, ...) \
    LOG_PREFIX_AP(ap, \
                  fmt ": "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,\
                  ##__VA_ARGS__, \
                  OSW_CHANNEL_ARG(oc), \
                  OSW_CHANNEL_ARG(nc))

struct ow_sta_channel_override {
    struct osw_state_observer obs;
    struct ds_tree aps;
    bool attach;
};

enum ow_sta_channel_override_action {
    OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE,
    OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN,
    OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN,
};

struct ow_sta_channel_override_ap {
    struct ds_tree_node node;
    struct ow_sta_channel_override *m;
    struct osw_state_observer obs;
    struct osw_conf_mutator mut;
    struct osw_timer settling;
    struct osw_channel prev_channel; /* previously seen */
    const struct osw_state_vif_info *info;
    struct ds_tree stas;
    struct osw_channel last_channel; /* previously applied ; rename FIXME */
    enum ow_sta_channel_override_action last_action;
    unsigned long settle_timeout_count;
    char *phy_name;
    char *vif_name;
};

struct ow_sta_channel_override_sta {
    struct ds_tree_node node;
    struct ow_sta_channel_override_ap *ap;
    char *phy_name;
    char *vif_name;
    struct osw_channel prev_channel;
    const struct osw_state_vif_info *info;
};

static struct ow_sta_channel_override_sta *
ow_sta_channel_override_ap_get_sta(const struct ow_sta_channel_override_ap *ap)
{
    struct ow_sta_channel_override_sta *sta;
    ds_tree_foreach((struct ds_tree *)&ap->stas, sta) {
        if (sta->info != NULL) return sta;
    }
    return NULL;
}

static const struct osw_channel *
ow_sta_channel_override_ap_get_sta_chan(const struct ow_sta_channel_override_ap *ap)
{
    struct ow_sta_channel_override_sta *sta = ow_sta_channel_override_ap_get_sta(ap);
    if (sta == NULL) return NULL;
    if (sta->info->drv_state->vif_type != OSW_VIF_STA) return NULL;
    return &sta->info->drv_state->u.sta.link.channel;
}

static void
ow_sta_channel_override_ap_settling_cb(struct osw_timer *t)
{
    struct ow_sta_channel_override_ap *ap = settling_to_ap(t);
    osw_conf_invalidate(&ap->mut);

    /* If this timeout happens it means that, eg. AP channel
     * was reported to have changed but the STA did not. The
     * STA could get disconnected in which case - currently
     * - that is expected to disarm the settling.
     */
    ap->settle_timeout_count++;
    LOGN(LOG_PREFIX_AP(ap, "failed to converge channels (total occurrences = %lu",
                       ap->settle_timeout_count));
}

static void
ow_sta_channel_override_ap_start_settling(struct ow_sta_channel_override_ap *ap)
{
    const uint64_t duration_nsec = OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC);
    const uint64_t at_nsec = osw_time_mono_clk() + duration_nsec;
    LOGD(LOG_PREFIX_AP(ap, "%ssettling",
                       osw_timer_is_armed(&ap->settling) ? "re-" : ""));
    osw_timer_arm_at_nsec(&ap->settling, at_nsec);
}

static void
ow_sta_channel_override_ap_cancel_settling(struct ow_sta_channel_override_ap *ap,
                                           const char *reason)
{
    if (osw_timer_is_armed(&ap->settling) == false) return;

    const uint64_t now_nsec = osw_time_mono_clk();
    const uint64_t rem_nsec = osw_timer_get_remaining_nsec(&ap->settling, now_nsec);
    const double rem_sec = OSW_TIME_TO_DBL(rem_nsec);

    LOGD(LOG_PREFIX_AP(ap, "cancelling settling because %s with %lf seconds remaining",
                       reason, rem_sec));
    osw_timer_disarm(&ap->settling);
}

static void
ow_sta_channel_override_ap_set_info__(struct ow_sta_channel_override_ap *ap,
                                      const struct osw_state_vif_info *info)
{
    const struct osw_channel *prev = ap->info != NULL
                                   ? &ap->prev_channel
                                   : NULL;
    const struct osw_channel *next = info != NULL
                                   ? &info->drv_state->u.ap.channel
                                   : NULL;
    const bool channel_changed = (prev == NULL && next != NULL)
                              || (prev != NULL && next == NULL)
                              || ((prev != NULL) &&
                                  (next != NULL) &&
                                  (memcmp(prev, next, sizeof(*prev)) != 0));

    struct osw_channel ch0;
    MEMZERO(ch0);

    LOGT(LOG_PREFIX_AP(ap, "info = %p -> %p, channel changed = %d, channel = "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
                       ap->info,
                       info,
                       channel_changed,
                       OSW_CHANNEL_ARG(prev ?: &ch0),
                       OSW_CHANNEL_ARG(next ?: &ch0)));

    ap->info = info;
    ap->prev_channel = *(next ?: &ch0);

    if (channel_changed) {
        const struct osw_channel *sta_chan = ow_sta_channel_override_ap_get_sta_chan(ap);
        if (sta_chan == NULL) return;

        const bool divergent_channels = (next == NULL)
                                     || (memcmp(sta_chan, next, sizeof(*sta_chan)) != 0);
        if (divergent_channels) {
            ow_sta_channel_override_ap_start_settling(ap);
        }
        else {
            ow_sta_channel_override_ap_cancel_settling(ap, "not divergent");
        }

        osw_conf_invalidate(&ap->mut);
    }
}

static bool
ow_sta_channel_override_ap_is_info_valid(const struct osw_state_vif_info *info)
{
    return (info != NULL)
        && (info->drv_state != NULL)
        && (info->drv_state->exists)
        && (info->drv_state->status == OSW_VIF_ENABLED)
        && (info->drv_state->vif_type == OSW_VIF_AP);
}

static void
ow_sta_channel_override_ap_set_info(struct ow_sta_channel_override_ap *ap,
                                    const struct osw_state_vif_info *info)
{
    if (ow_sta_channel_override_ap_is_info_valid(info)) {
        ow_sta_channel_override_ap_set_info__(ap, info);
    }
    else {
        ow_sta_channel_override_ap_set_info__(ap, NULL);
    }
}

static struct ow_sta_channel_override_sta *
ow_sta_channel_override_sta_alloc(struct ow_sta_channel_override_ap *ap,
                                  const char *phy_name,
                                  const char *vif_name)
{
    struct ow_sta_channel_override_sta *sta = CALLOC(1, sizeof(*sta));
    sta->phy_name = STRDUP(phy_name);
    sta->vif_name = STRDUP(vif_name);
    sta->ap = ap;
    ds_tree_insert(&ap->stas, sta, sta->vif_name);
    return sta;
}

static void
ow_sta_channel_override_sta_free(struct ow_sta_channel_override_sta *sta)
{
    LOGT(LOG_PREFIX_STA(sta, "freeing"));
    ds_tree_remove(&sta->ap->stas, sta);
    FREE(sta->phy_name);
    FREE(sta->vif_name);
    FREE(sta);
}

static void
ow_sta_channel_override_sta_set_info__(struct ow_sta_channel_override_sta *sta,
                                       const struct osw_state_vif_info *info)
{
    const struct osw_channel *prev = sta->info != NULL
                                   ? &sta->prev_channel
                                   : NULL;
    const struct osw_channel *next = info != NULL
                                   ? (info->drv_state->vif_type == OSW_VIF_STA
                                      ? &info->drv_state->u.sta.link.channel
                                      : NULL)
                                   : NULL;
    const bool channel_changed = (prev == NULL && next != NULL)
                              || (prev != NULL && next == NULL)
                              || ((prev != NULL) &&
                                  (next != NULL) &&
                                  (memcmp(prev, next, sizeof(*prev)) != 0));

    struct osw_channel ch0;
    MEMZERO(ch0);

    LOGT(LOG_PREFIX_STA(sta, "info = %p -> %p, channel changed = %d, channel = "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
                        sta->info,
                        info,
                        channel_changed,
                        OSW_CHANNEL_ARG(prev ?: &ch0),
                        OSW_CHANNEL_ARG(next ?: &ch0)));

    sta->info = info;
    sta->prev_channel = *(next ?: &ch0);

    if (channel_changed) {
        const struct osw_channel *ap_chan = (sta->ap->info != NULL)
                                          ? (sta->ap->info->drv_state->vif_type == OSW_VIF_AP
                                             ? &sta->ap->info->drv_state->u.ap.channel
                                             : NULL)
                                          : NULL;
        const bool converged_channels = (next != NULL)
                                     && (ap_chan != NULL)
                                     && (memcmp(next, ap_chan, sizeof(*next)) == 0);
        if (converged_channels) {
            ow_sta_channel_override_ap_cancel_settling(sta->ap, "converged");
        }

        const bool sta_link_went_away = (next == NULL);
        if (sta_link_went_away) {
            ow_sta_channel_override_ap_cancel_settling(sta->ap, "sta link dropped");
        }

        osw_conf_invalidate(&sta->ap->mut);
    }
}

static bool
ow_sta_channel_overide_sta_is_info_valid(struct ow_sta_channel_override_ap *ap,
                                         const struct osw_state_vif_info *info)
{
    return (ap != NULL)
        && (info != NULL)
        && (info->phy != NULL)
        && (info->phy->phy_name != NULL)
        && (strcmp(info->phy->phy_name, ap->phy_name) == 0)
        && (info->drv_state != NULL)
        && (info->drv_state->exists)
        && (info->drv_state->status == OSW_VIF_ENABLED)
        && (info->drv_state->vif_type == OSW_VIF_STA)
        && (info->drv_state->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
}

static void
ow_sta_channel_override_sta_set_info(struct ow_sta_channel_override_sta *sta,
                                     const struct osw_state_vif_info *info)
{
    if (ow_sta_channel_overide_sta_is_info_valid(sta->ap, info)) {
        ow_sta_channel_override_sta_set_info__(sta, info);
    }
    else {
        ow_sta_channel_override_sta_set_info__(sta, NULL);
    }
}

static void
ow_sta_channel_override_sta_update(struct osw_state_observer *obs,
                                   const struct osw_state_vif_info *info,
                                   const bool exists)
{
    struct ow_sta_channel_override_ap *ap = ap_obs_to_ap(obs);
    if (WARN_ON(info->phy == NULL)) return;
    const char *phy_name = info->phy->phy_name;
    const char *vif_name = info->vif_name;
    if (WARN_ON(phy_name == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_sta_channel_override_sta *sta = ds_tree_find(&ap->stas, vif_name)
                                           ?: ow_sta_channel_override_sta_alloc(ap, phy_name, vif_name);
    ow_sta_channel_override_sta_set_info(sta, exists ? info : NULL);

    if (sta->info == NULL) {
        ow_sta_channel_override_sta_free(sta);
    }
}

static void
ow_sta_channel_override_sta_added_cb(struct osw_state_observer *obs,
                                      const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_sta_update(obs, info, true);
}

static void
ow_sta_channel_override_sta_changed_cb(struct osw_state_observer *obs,
                                       const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_sta_update(obs, info, true);
}

static void
ow_sta_channel_override_sta_removed_cb(struct osw_state_observer *obs,
                                       const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_sta_update(obs, info, false);
}

static enum ow_sta_channel_override_action
ow_sta_channel_override_ap_get_action(struct ow_sta_channel_override_ap *ap,
                                      struct osw_conf_vif *vif)
{
    if (ap->info == NULL) {
        return OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE;
    }

    const bool no_valid_sta = (ow_sta_channel_override_ap_get_sta(ap) == NULL);
    if (no_valid_sta) {
        return OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE;
    }

    const bool ap_chan_recently_changed = (osw_timer_is_armed(&ap->settling));
    if (ap_chan_recently_changed) {
        return OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN;
    }

    return OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN;
}

static const struct osw_channel *
ow_sta_channel_override_action_get_channel(const struct ow_sta_channel_override_ap *ap,
                                           const struct osw_conf_vif *vif,
                                           const enum ow_sta_channel_override_action action)
{
    switch (action) {
        case OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE:
            if (vif->vif_type != OSW_VIF_AP) return NULL;
            return &vif->u.ap.channel;
        case OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN:
            if (ap->info->drv_state->vif_type != OSW_VIF_AP) return NULL;
            return &ap->info->drv_state->u.ap.channel;
        case OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN:
            return ow_sta_channel_override_ap_get_sta_chan(ap);
    }
    return NULL;
}

static void
ow_sta_channel_override_ap_mutate_ap_log(const struct ow_sta_channel_override_ap *ap,
                                         const enum ow_sta_channel_override_action old_action,
                                         const enum ow_sta_channel_override_action new_action,
                                         const struct osw_channel *last_chan,
                                         const struct osw_channel *old_chan,
                                         const struct osw_channel *new_chan)
{
    if (last_chan->control_freq_mhz == 0) {
        last_chan = old_chan;
    }

    const bool channel_changed = (memcmp(last_chan, new_chan, sizeof(*old_chan)) != 0);
    switch (old_action) {
        case OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE:
            switch (new_action) {
                case OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE:
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN:
                    LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "latching onto ap state"));
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN:
                    LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "latching onto sta state"));
                    break;
            }
            break;
        case OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN:
            switch (new_action) {
                case OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE:
                    LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "unlatching from ap state"));
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN:
                    if (channel_changed) {
                        LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "re-latching the ap state"));
                    }
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN:
                    LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "re-latching from ap state onto sta state"));
                    break;
            }
            break;
        case OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN:
            switch (new_action) {
                case OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE:
                    LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "unlatching from sta state"));
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN:
                    if (channel_changed) {
                        LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "re-latching from sta state onto ap state"));
                    }
                    break;
                case OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN:
                    if (channel_changed) {
                        LOGI(LOG_PREFIX_LATCH(ap, old_chan, new_chan, "re-latching the sta state"));
                    }
                    break;
            }
            break;
    }
}

static void
ow_sta_channel_override_ap_mutate_vif(struct ow_sta_channel_override_ap *ap,
                                      struct osw_conf_vif *vif)
{
    if (ap == NULL) return;
    if (vif == NULL) return;
    if (vif->vif_type != OSW_VIF_AP) return;

    const enum ow_sta_channel_override_action action = ow_sta_channel_override_ap_get_action(ap, vif);
    const struct osw_channel *new_chan = ow_sta_channel_override_action_get_channel(ap, vif, action);

    ow_sta_channel_override_ap_mutate_ap_log(ap,
                                             ap->last_action,
                                             action,
                                             &ap->last_channel,
                                             &vif->u.ap.channel,
                                             new_chan);

    vif->u.ap.channel = *new_chan;
    ap->last_action = action;
    ap->last_channel = vif->u.ap.channel;
}

static void
ow_sta_channel_override_ap_mutate_cb(struct osw_conf_mutator *mut,
                                     struct ds_tree *phy_tree)
{
    struct ow_sta_channel_override_ap *ap = ap_mut_to_ap(mut);
    struct ow_sta_channel_override *m = ap->m;
    struct osw_conf_phy *phy;
    ds_tree_foreach(phy_tree, phy) {
        struct osw_conf_vif *vif;
        ds_tree_foreach(&phy->vif_tree, vif) {
            const char *vif_name = vif->vif_name;
            struct ow_sta_channel_override_ap *ap = ds_tree_find(&m->aps, vif_name);
            ow_sta_channel_override_ap_mutate_vif(ap, vif);
        }
    }
}

static void
ow_sta_channel_override_ap_attach(struct ow_sta_channel_override_ap *ap)
{
    if (ap->m->attach == false) return;
    osw_state_register_observer(&ap->obs);
    osw_conf_register_mutator(&ap->mut);
}

static void
ow_sta_channel_override_ap_detach(struct ow_sta_channel_override_ap *ap)
{
    if (ap->m->attach == false) return;
    osw_state_unregister_observer(&ap->obs);
    osw_conf_unregister_mutator(&ap->mut);
}

static struct ow_sta_channel_override_ap *
ow_sta_channel_override_ap_alloc(struct ow_sta_channel_override *m,
                                 const char *phy_name,
                                 const char *vif_name)
{
    struct ow_sta_channel_override_ap *ap = CALLOC(1, sizeof(*ap));
    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .type = OSW_CONF_TAIL,
        .mutate_fn = ow_sta_channel_override_ap_mutate_cb,
    };
    const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_added_fn = ow_sta_channel_override_sta_added_cb,
        .vif_changed_fn = ow_sta_channel_override_sta_changed_cb,
        .vif_removed_fn = ow_sta_channel_override_sta_removed_cb,
    };

    ap->phy_name = STRDUP(phy_name);
    ap->vif_name = STRDUP(vif_name);
    ap->obs = obs;
    ap->mut = mut;
    ap->m = m;
    osw_timer_init(&ap->settling, ow_sta_channel_override_ap_settling_cb);
    ds_tree_init(&ap->stas, (ds_key_cmp_t *)strcmp, struct ow_sta_channel_override_sta, node);
    ds_tree_insert(&m->aps, ap, ap->vif_name);
    ow_sta_channel_override_ap_attach(ap);
    return ap;
}

static void
ow_sta_channel_override_ap_free(struct ow_sta_channel_override_ap *ap)
{
    LOGT(LOG_PREFIX_AP(ap, "freeing"));
    ow_sta_channel_override_ap_detach(ap);
    assert(ds_tree_is_empty(&ap->stas)); /* detach/unregister should clean it up */
    ow_sta_channel_override_ap_cancel_settling(ap, "freeing");
    ds_tree_remove(&ap->m->aps, ap);
    FREE(ap->phy_name);
    FREE(ap->vif_name);
    FREE(ap);
}

static void
ow_sta_channel_override_ap_update(struct osw_state_observer *obs,
                                  const struct osw_state_vif_info *info,
                                  const bool exists)
{
    struct ow_sta_channel_override *m = m_obs_to_m(obs);
    if (WARN_ON(info->phy == NULL)) return;
    const char *phy_name = info->phy->phy_name;
    const char *vif_name = info->vif_name;
    if (WARN_ON(phy_name == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    struct ow_sta_channel_override_ap *ap = ds_tree_find(&m->aps, vif_name)
                                         ?: ow_sta_channel_override_ap_alloc(m, phy_name, vif_name);
    ow_sta_channel_override_ap_set_info(ap, exists ? info : NULL);

    if (ap->info == NULL) {
        ow_sta_channel_override_ap_free(ap);
    }
}

static void
ow_sta_channel_override_ap_added_cb(struct osw_state_observer *obs,
                                    const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_ap_update(obs, info, true);
}

static void
ow_sta_channel_override_ap_changed_cb(struct osw_state_observer *obs,
                                      const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_ap_update(obs, info, true);
}

static void
ow_sta_channel_override_ap_removed_cb(struct osw_state_observer *obs,
                                      const struct osw_state_vif_info *info)
{
    ow_sta_channel_override_ap_update(obs, info, false);
}

static void
ow_sta_channel_override_init(struct ow_sta_channel_override *m)
{
    const struct osw_state_observer obs = {
        .name = __FILE__,
        .vif_added_fn = ow_sta_channel_override_ap_added_cb,
        .vif_changed_fn = ow_sta_channel_override_ap_changed_cb,
        .vif_removed_fn = ow_sta_channel_override_ap_removed_cb,
    };

    m->obs = obs;
    ds_tree_init(&m->aps,
                 (ds_key_cmp_t *)strcmp,
                 struct ow_sta_channel_override_ap,
                 node);
}

static void
ow_sta_channel_override_attach(struct ow_sta_channel_override *m)
{
    OSW_MODULE_LOAD(osw_conf);
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_timer);
    osw_state_register_observer(&m->obs);
    m->attach = true;
}

OSW_MODULE(ow_sta_channel_override)
{
    static struct ow_sta_channel_override m;
    ow_sta_channel_override_init(&m);
    ow_sta_channel_override_attach(&m);
    return &m;
}

struct ow_sta_channel_override_ut {
    struct ow_sta_channel_override m;
    struct osw_drv_phy_state phy1_state;
    struct osw_drv_phy_state phy2_state;
    struct osw_drv_vif_state sta1_state;
    struct osw_drv_vif_state sta2_state;
    struct osw_drv_vif_state ap1_state;
    struct osw_drv_vif_state ap2_state;
    struct osw_state_phy_info phy1_info;
    struct osw_state_phy_info phy2_info;
    struct osw_state_vif_info sta1_info;
    struct osw_state_vif_info sta2_info;
    struct osw_state_vif_info ap1_info;
    struct osw_state_vif_info ap2_info;
    struct osw_conf_vif vif1;
    struct osw_conf_vif vif2;
};

static void
ow_sta_channel_override_ut_init(struct ow_sta_channel_override_ut *ut)
{
    osw_ut_time_init();

    memset(ut, 0, sizeof(*ut));
    ow_sta_channel_override_init(&ut->m);

    ut->sta1_state.vif_type = OSW_VIF_STA;
    ut->sta1_state.exists = true;
    ut->sta1_state.status = OSW_VIF_ENABLED;
    ut->sta1_state.u.sta.link.status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
    ut->sta1_state.u.sta.link.channel.control_freq_mhz = 2412;

    ut->sta2_state.vif_type = OSW_VIF_STA;
    ut->sta2_state.exists = true;
    ut->sta2_state.status = OSW_VIF_ENABLED;
    ut->sta2_state.u.sta.link.status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
    ut->sta2_state.u.sta.link.channel.control_freq_mhz = 5180;

    ut->ap1_state.vif_type = OSW_VIF_AP;
    ut->ap1_state.exists = true;
    ut->ap1_state.status = OSW_VIF_ENABLED;
    ut->ap1_state.u.ap.channel.control_freq_mhz = 2437;

    ut->ap2_state.vif_type = OSW_VIF_AP;
    ut->ap2_state.exists = true;
    ut->ap2_state.status = OSW_VIF_ENABLED;
    ut->ap2_state.u.ap.channel.control_freq_mhz = 5200;

    ut->phy1_info.phy_name = "phy1";
    ut->phy1_info.drv_state = &ut->phy1_state;

    ut->phy2_info.phy_name = "phy2";
    ut->phy2_info.drv_state = &ut->phy2_state;

    ut->sta1_info.vif_name = "sta1";
    ut->sta1_info.drv_state = &ut->sta1_state;
    ut->sta1_info.phy = &ut->phy1_info;

    ut->sta2_info.vif_name = "sta2";
    ut->sta2_info.drv_state = &ut->sta2_state;
    ut->sta2_info.phy = &ut->phy2_info;

    ut->ap1_info.vif_name = "ap1";
    ut->ap1_info.drv_state = &ut->ap1_state;
    ut->ap1_info.phy = &ut->phy1_info;

    ut->ap2_info.vif_name = "ap2";
    ut->ap2_info.drv_state = &ut->ap2_state;
    ut->ap2_info.phy = &ut->phy2_info;

    ut->vif1.vif_type = OSW_VIF_AP;
    ut->vif1.u.ap.channel.control_freq_mhz = 2462;

    ut->vif2.vif_type = OSW_VIF_AP;
    ut->vif2.u.ap.channel.control_freq_mhz = 5220;
}

static bool
ow_sta_channel_override_ut_assert(struct ow_sta_channel_override_ut *ut,
                                  struct ow_sta_channel_override_ap *ap,
                                  const enum ow_sta_channel_override_action action,
                                  const struct osw_channel *chan)
{
    ow_sta_channel_override_ap_mutate_vif(ap, &ut->vif1);
    LOGT("%s: observed: last_action = %d", __func__, ap->last_action);
    LOGT("%s: observed: ut->vif.u.ap.channel = "OSW_CHANNEL_FMT, __func__, OSW_CHANNEL_ARG(&ut->vif1.u.ap.channel));
    LOGT("%s: expected: action = %d", __func__,action);
    LOGT("%s: expected: chan = "OSW_CHANNEL_FMT, __func__, OSW_CHANNEL_ARG(chan));
    if (WARN_ON(action != ap->last_action)) return false;
    if (WARN_ON(memcmp(chan, &ut->vif1.u.ap.channel, sizeof(*chan)) != 0)) return false;
    return true;
}

static bool
ow_sta_channel_override_ut_assert_nop1(struct ow_sta_channel_override_ut *ut,
                                       struct ow_sta_channel_override_ap *ap)
{
    return ow_sta_channel_override_ut_assert(ut,
                                             ap,
                                             OW_STA_CHANNEL_OVERRIDE_DONT_CHANGE,
                                             &ut->vif1.u.ap.channel);
}

static bool
ow_sta_channel_override_ut_assert_ap1(struct ow_sta_channel_override_ut *ut,
                                      struct ow_sta_channel_override_ap *ap)
{
    return ow_sta_channel_override_ut_assert(ut,
                                             ap,
                                             OW_STA_CHANNEL_OVERRIDE_USE_AP_STATE_CHAN,
                                             &ut->ap1_state.u.ap.channel);
}

static bool
ow_sta_channel_override_ut_assert_sta1(struct ow_sta_channel_override_ut *ut,
                                       struct ow_sta_channel_override_ap *ap)
{
    return ow_sta_channel_override_ut_assert(ut,
                                             ap,
                                             OW_STA_CHANNEL_OVERRIDE_USE_STA_STATE_CHAN,
                                             &ut->sta1_state.u.sta.link.channel);
}

OSW_UT(ow_sta_channel_override_no_sta)
{
    struct ow_sta_channel_override_ut ut;
    ow_sta_channel_override_ut_init(&ut);
    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    struct ow_sta_channel_override_ap *ap = ds_tree_head(&ut.m.aps);
    OSW_UT_EVAL(ap != NULL);
    OSW_UT_EVAL(strcmp(ap->phy_name, "phy1") == 0);
    OSW_UT_EVAL(strcmp(ap->vif_name, "ap1") == 0);
    OSW_UT_EVAL(osw_timer_is_armed(&ap->settling) == false);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_nop1(&ut, ap));
    OSW_UT_EVAL(osw_timer_is_armed(&ap->settling) == false);
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_nop1(&ut, ap));
    ut.m.obs.vif_removed_fn(&ut.m.obs, &ut.ap1_info);
}

OSW_UT(ow_sta_channel_override_ap_sta)
{
    struct ow_sta_channel_override_ut ut;
    ow_sta_channel_override_ut_init(&ut);
    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    struct ow_sta_channel_override_ap *ap = ds_tree_head(&ut.m.aps);
    ap->obs.vif_added_fn(&ap->obs, &ut.sta1_info);
    ap->obs.vif_added_fn(&ap->obs, &ut.ap1_info); /* it sees itself */
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));
    ap->obs.vif_changed_fn(&ap->obs, &ut.ap1_info); /* no-op, nothing changed */
    OSW_UT_EVAL(osw_timer_is_armed(&ap->settling) == false);
    ap->obs.vif_removed_fn(&ap->obs, &ut.sta1_info);
    ap->obs.vif_removed_fn(&ap->obs, &ut.ap1_info);
    ut.m.obs.vif_removed_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 0);
}

OSW_UT(ow_sta_channel_override_delayed_csa)
{
    struct ow_sta_channel_override_ut ut;
    ow_sta_channel_override_ut_init(&ut);
    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    struct ow_sta_channel_override_ap *ap = ds_tree_head(&ut.m.aps);
    ap->obs.vif_added_fn(&ap->obs, &ut.sta1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));

    LOGT("making AP to respect the STA channel");
    ut.ap1_state.u.ap.channel = ut.sta1_state.u.sta.link.channel;
    ut.m.obs.vif_changed_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));

    LOGT("presumably STA moved due to CSA, "
         "and local APs are reported to have moved first");
    ut.ap1_state.u.ap.channel.control_freq_mhz = 2422;
    ut.m.obs.vif_changed_fn(&ut.m.obs, &ut.ap1_info);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC / 4));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_ap1(&ut, ap));

    /* This assumes that if AP and STA channels converge then the settling
     * grace period can be finished immediately */
    LOGT("the STA finally is reported to have moved too");
    ut.sta1_state.u.sta.link.channel.control_freq_mhz = 2422;
    ap->obs.vif_changed_fn(&ap->obs, &ut.sta1_info);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC / 4));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));

    LOGT("remains unchanged after grace period finishes");
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));
}

OSW_UT(ow_sta_channel_override_sta_stops_after_ap_csa)
{
    struct ow_sta_channel_override_ut ut;
    ow_sta_channel_override_ut_init(&ut);
    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    struct ow_sta_channel_override_ap *ap = ds_tree_head(&ut.m.aps);
    ap->obs.vif_added_fn(&ap->obs, &ut.sta1_info);
    OSW_UT_EVAL(ds_tree_len(&ut.m.aps) == 1);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));

    LOGT("making AP to respect the STA channel");
    ut.ap1_state.u.ap.channel = ut.sta1_state.u.sta.link.channel;
    ut.m.obs.vif_changed_fn(&ut.m.obs, &ut.ap1_info);
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC * 2));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_sta1(&ut, ap));

    LOGT("presumably STA moved due to CSA, "
         "and local APs are reported to have moved first");
    ut.ap1_state.u.ap.channel.control_freq_mhz = 2422;
    ut.m.obs.vif_changed_fn(&ut.m.obs, &ut.ap1_info);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC / 4));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_ap1(&ut, ap));

    LOGT("the STA drops the link for whatever reason");
    ut.sta1_state.u.sta.link.status = OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
    ap->obs.vif_changed_fn(&ap->obs, &ut.sta1_info);
    osw_ut_time_advance(OSW_TIME_SEC(OW_STA_CHANNEL_OVERRIDE_SETTLE_GRACE_SEC / 4));
    OSW_UT_EVAL(ow_sta_channel_override_ut_assert_nop1(&ut, ap));
    OSW_UT_EVAL(osw_timer_is_armed(&ap->settling) == false);
}

OSW_UT(ow_sta_channel_override_phy_grouping)
{
    struct ow_sta_channel_override_ut ut;
    ow_sta_channel_override_ut_init(&ut);

    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap1_info);
    ut.m.obs.vif_added_fn(&ut.m.obs, &ut.ap2_info);

    struct ow_sta_channel_override_ap *ap1 = ds_tree_find(&ut.m.aps, "ap1");
    struct ow_sta_channel_override_ap *ap2 = ds_tree_find(&ut.m.aps, "ap2");

    OSW_UT_EVAL(ap1 != NULL);
    OSW_UT_EVAL(ap2 != NULL);

    ap1->obs.vif_added_fn(&ap1->obs, &ut.sta1_info);
    ap1->obs.vif_added_fn(&ap1->obs, &ut.sta2_info);

    ap2->obs.vif_added_fn(&ap2->obs, &ut.sta1_info);
    ap2->obs.vif_added_fn(&ap2->obs, &ut.sta2_info);

    LOGT("checking if STA-AP is within a PHY");
    OSW_UT_EVAL(ds_tree_find(&ap1->stas, "sta1") != NULL);
    OSW_UT_EVAL(ds_tree_find(&ap1->stas, "sta2") == NULL);
    OSW_UT_EVAL(ds_tree_find(&ap2->stas, "sta1") == NULL);
    OSW_UT_EVAL(ds_tree_find(&ap2->stas, "sta2") != NULL);
}
