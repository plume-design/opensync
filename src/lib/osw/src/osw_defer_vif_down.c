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

#include <stdbool.h>

#include <memutil.h>
#include <ds_tree.h>
#include <ds_list.h>
#include <const.h>
#include <os.h>

#include <osw_module.h>
#include <osw_conf.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_state.h>
#include <osw_ut.h>
#include <osw_defer_vif_down.h>

#define LOG_PREFIX(fmt, ...) "osw: defer_vif_down: " fmt, ## __VA_ARGS__
#define LOG_PREFIX_RULE(rule, fmt, ...) LOG_PREFIX("%s (status: %s / period: %us / stas: %zu): " fmt, \
    rule->vif_name, \
    osw_timer_is_armed(&rule->grace_period_timer) \
    ? "started" \
    : "stopped", \
    rule->grace_period_seconds, \
    rule->num_stations, \
    ## __VA_ARGS__)

struct osw_defer_vif_down_rule {
    struct ds_tree_node node;
    struct osw_defer_vif_down *m;
    struct osw_conf_mutator mut;
    struct osw_state_observer obs;
    char *vif_name;
    size_t num_stations;
    unsigned int grace_period_seconds;
    bool was_enabled;
    struct osw_timer grace_period_timer;
    const struct osw_state_phy_info *phy_info;
    const struct osw_state_vif_info *vif_info;
    bool reason_phy;
    bool reason_vif;
    bool reason_csa;
};

struct osw_defer_vif_down_observer {
    struct ds_dlist_node node;
    struct osw_defer_vif_down *m;
    char *vif_name;
    osw_defer_vif_down_notify_fn_t *grace_period_started_fn;
    osw_defer_vif_down_notify_fn_t *grace_period_stopped_fn;
    void *fn_priv;
};

struct osw_defer_vif_down {
    struct ds_tree rules;
    struct ds_dlist observers;
};

static void
osw_defer_vif_down_notify_grace_started(struct osw_defer_vif_down_rule *r)
{
    if (r == NULL) return;
    if (r->m == NULL) return;

    LOGD(LOG_PREFIX_RULE(r, "notifying started"));

    struct osw_defer_vif_down_observer *o;
    ds_dlist_foreach(&r->m->observers, o) {
        if (o->grace_period_started_fn != NULL) {
            const bool vif_name_matches = (strcmp(r->vif_name, o->vif_name) == 0);
            if (vif_name_matches) {
                o->grace_period_started_fn(o->fn_priv);
            }
        }
    }
}

static void
osw_defer_vif_down_notify_grace_stopped(struct osw_defer_vif_down_rule *r)
{
    if (r == NULL) return;
    if (r->m == NULL) return;

    LOGD(LOG_PREFIX_RULE(r, "notifying stopped"));

    struct osw_defer_vif_down_observer *o;
    ds_dlist_foreach(&r->m->observers, o) {
        if (o->grace_period_stopped_fn != NULL) {
            const bool vif_name_matches = (strcmp(r->vif_name, o->vif_name) == 0);
            if (vif_name_matches) {
                o->grace_period_stopped_fn(o->fn_priv);
            }
        }
    }
}

static void
osw_defer_vif_down_observe_initial_fire(struct osw_defer_vif_down_observer *o)
{
    if (o->grace_period_started_fn == NULL) return;

    struct osw_defer_vif_down_rule *r = ds_tree_find(&o->m->rules, o->vif_name);
    if (r == NULL) return;
    struct osw_timer *t = &r->grace_period_timer;
    const bool grace_period_not_active = (osw_timer_is_armed(t) == false);
    if (grace_period_not_active) return;

    o->grace_period_started_fn(o->fn_priv);
}

static void
osw_defer_vif_down_observe_final_fire(struct osw_defer_vif_down_observer *o)
{
    if (o->grace_period_stopped_fn == NULL) return;

    struct osw_defer_vif_down_rule *r = ds_tree_find(&o->m->rules, o->vif_name);
    if (r == NULL) return;
    struct osw_timer *t = &r->grace_period_timer;
    const bool grace_period_not_active = (osw_timer_is_armed(t) == false);
    if (grace_period_not_active) return;

    o->grace_period_stopped_fn(o->fn_priv);
}

static const struct osw_channel *
osw_defer_vif_down_extract_vif_ap_channel(const struct osw_state_vif_info *info)
{
    if (info == NULL) return NULL;
    switch (info->drv_state->vif_type) {
        case OSW_VIF_AP:
            return &info->drv_state->u.ap.channel;
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_STA:
        case OSW_VIF_UNDEFINED:
            return NULL;
    }
    return NULL;
}

static bool
osw_defer_vif_down_csa_will_interrupt_service(struct osw_conf_phy *phy,
                                              struct osw_conf_vif *vif,
                                              struct osw_defer_vif_down_rule *r)
{
    if (r->phy_info == NULL) return false;
    if (r->vif_info == NULL) return false;
    if (r->vif_info->drv_state->vif_type != OSW_VIF_AP) return false;
    if (vif->vif_type != OSW_VIF_AP) return false;

    const struct osw_channel *c = &vif->u.ap.channel;
    const struct osw_channel_state *cs = r->phy_info->drv_state->channel_states;
    size_t n_cs = r->phy_info->drv_state->n_channel_states;
    const bool cannot_be_a_csa = (vif->enabled == false);
    const bool invalid_chan = (c->control_freq_mhz == 0);

    if (cannot_be_a_csa) return false;
    if (WARN_ON(invalid_chan)) return false;

    /* FIXME: If/when Zero Wait DFS support is added this
     * should be made aware of that.
     */
    const bool some_channels_arent_ready = osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_CAC_POSSIBLE)
                                        || osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_CAC_IN_PROGRESS)
                                        || osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_NOL);

    /* If any of the 20MHz segments is in an "offending"
     * state then the CSA is considered to be "destructive"
     * and will introduce service interruption due to
     * implicit down time.
     */
    return some_channels_arent_ready;
}

static void
osw_defer_vif_down_rule_mutate_phy_channel(struct osw_conf_phy *phy,
                                           struct osw_conf_vif *vif,
                                           struct osw_defer_vif_down_rule *r)
{
    const struct osw_channel *c = osw_defer_vif_down_extract_vif_ap_channel(r->vif_info);
    if (c == NULL) return;

    struct osw_conf_vif *phy_vif;
    ds_tree_foreach(&phy->vif_tree, phy_vif) {
        switch (phy_vif->vif_type) {
            case OSW_VIF_AP:
                phy_vif->u.ap.channel = *c;
                break;
            case OSW_VIF_AP_VLAN:
            case OSW_VIF_STA:
            case OSW_VIF_UNDEFINED:
                break;
        }
    }
}

static void
osw_defer_vif_down_rule_mutate(struct osw_conf_phy *phy,
                               struct osw_conf_vif *vif,
                               struct osw_defer_vif_down_rule *r)
{
    struct osw_timer *t = &r->grace_period_timer;

    if (phy == NULL || vif == NULL) {
        if (osw_timer_is_armed(t)) {
            LOGI(LOG_PREFIX_RULE(r, "mutating: phy/vif disappeared"));
            osw_timer_disarm(t);
            osw_defer_vif_down_notify_grace_stopped(r);
        }
    }
    else {
        const bool csa_interrupt = osw_defer_vif_down_csa_will_interrupt_service(phy, vif, r);
        const bool enabled = vif->enabled
                          && phy->enabled
                          && !csa_interrupt;
        const bool shutting_down = (r->was_enabled == true)
                                && (enabled == false);
        if (shutting_down) {
            const bool not_armed_yet = (osw_timer_is_armed(t) == false);
            if (not_armed_yet) {
                const uint64_t nsec = OSW_TIME_SEC(r->grace_period_seconds);
                const uint64_t now = osw_time_mono_clk();
                r->reason_phy = (phy->enabled == false);
                r->reason_vif = (vif->enabled == false);
                r->reason_csa = csa_interrupt;
                osw_timer_arm_at_nsec(t, now + nsec);
                osw_defer_vif_down_notify_grace_started(r);
            }
        }

        r->was_enabled = enabled;

        const bool grace_period_active = osw_timer_is_armed(t);
        if (grace_period_active) {
            const bool cancelled = (enabled == true);
            const bool unnecessary = (r->num_stations == 0);
            if (cancelled || unnecessary) {
                LOGI(LOG_PREFIX_RULE(r, "mutating: cancelled %d unnecessary %d",
                                     cancelled, unnecessary));
                osw_timer_disarm(t);
                osw_defer_vif_down_notify_grace_stopped(r);
            }
            else {
                const bool first_mutation = r->reason_phy
                                         || r->reason_vif
                                         || r->reason_csa;
                if (first_mutation) {
                    LOGI(LOG_PREFIX_RULE(r, "mutation started because of%s%s%s",
                                         r->reason_phy ? " phy" : "",
                                         r->reason_vif ? " vif" : "",
                                         r->reason_csa ? " csa" : ""));
                    r->reason_phy = false;
                    r->reason_vif = false;
                    r->reason_csa = false;
                }

                /* If vif shutdown is implicitly
                 * caused by phy getting disabled,
                 * then the phy needs to be kept
                 * enabled as well.
                 */
                phy->enabled = true;
                vif->enabled = true;
                osw_defer_vif_down_rule_mutate_phy_channel(phy, vif, r);

                LOGD(LOG_PREFIX_RULE(r, "mutating: deferral"));
            }
        }
    }
}

static void
osw_defer_vif_down_mutate_cb(struct osw_conf_mutator *mut,
                            struct ds_tree *phys)
{
    struct osw_defer_vif_down_rule *r = container_of(mut, struct osw_defer_vif_down_rule, mut);

    struct osw_conf_phy *phy;
    ds_tree_foreach(phys, phy) {
        struct osw_conf_vif *vif = ds_tree_find(&phy->vif_tree, r->vif_name);
        if (vif != NULL) {
            osw_defer_vif_down_rule_mutate(phy, vif, r);
        }
    }
}

static void
osw_defer_vif_down_phy_changed_cb(struct osw_state_observer *obs,
                                  const struct osw_state_phy_info *info)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    if (r->phy_info == NULL) return;
    const bool other_phy = (strcmp(info->phy_name, r->phy_info->phy_name) != 0);
    if (other_phy) return;
    r->phy_info = info;
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_phy_removed_cb(struct osw_state_observer *obs,
                                  const struct osw_state_phy_info *info)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    if (r->phy_info == NULL) return;
    const bool other_phy = (strcmp(info->phy_name, r->phy_info->phy_name) != 0);
    if (other_phy) return;
    r->phy_info = NULL;
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_vif_added_cb(struct osw_state_observer *obs,
                                const struct osw_state_vif_info *info)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    const bool other_vif = (strcmp(info->vif_name, r->vif_name) != 0);
    if (other_vif) return;
    if (info->phy != NULL) LOGD(LOG_PREFIX_RULE(r, "state: latched to phy %s", info->phy->phy_name));
    r->phy_info = info->phy;
    r->vif_info = info;
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_vif_changed_cb(struct osw_state_observer *obs,
                                  const struct osw_state_vif_info *info)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    const bool other_vif = (strcmp(info->vif_name, r->vif_name) != 0);
    if (other_vif) return;
    r->vif_info = info;
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_vif_removed_cb(struct osw_state_observer *obs,
                                  const struct osw_state_vif_info *info)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    const bool other_vif = (strcmp(info->vif_name, r->vif_name) != 0);
    if (other_vif) return;
    if (info->phy != NULL) LOGD(LOG_PREFIX_RULE(r, "state: unlatched from phy %s", info->phy->phy_name));
    r->vif_info = NULL;
    r->phy_info = NULL;
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_sta_count_update(struct osw_state_observer *obs,
                                    const struct osw_state_sta_info *info,
                                    ssize_t delta)
{
    struct osw_defer_vif_down_rule *r = container_of(obs, struct osw_defer_vif_down_rule, obs);
    const bool other_vif = (strcmp(r->vif_name, info->vif->vif_name) != 0);
    if (other_vif) return;

    const bool underflow = (delta < 0) && (r->num_stations < (size_t)-delta);
    LOGI(LOG_PREFIX_RULE(r, "update stations: %zd", delta));
    if (WARN_ON(underflow)) {
        r->num_stations = 0;
    }
    else {
        r->num_stations += delta;
    }
    osw_conf_invalidate(&r->mut);
}

static void
osw_defer_vif_down_sta_connected_cb(struct osw_state_observer *obs,
                                    const struct osw_state_sta_info *info)
{
    osw_defer_vif_down_sta_count_update(obs, info, 1);
}

static void
osw_defer_vif_down_sta_disconnected_cb(struct osw_state_observer *obs,
                                       const struct osw_state_sta_info *info)
{
    osw_defer_vif_down_sta_count_update(obs, info, -1);
}

static void
osw_defer_vif_down_rule_grace_expired_cb(struct osw_timer *t)
{
    struct osw_defer_vif_down_rule *r = container_of(t, struct osw_defer_vif_down_rule, grace_period_timer);
    osw_conf_invalidate(&r->mut);
    osw_defer_vif_down_notify_grace_stopped(r);

}

osw_defer_vif_down_rule_t *
osw_defer_vif_down_rule(osw_defer_vif_down_t *m,
                        const char *vif_name,
                        unsigned int grace_period_seconds)
{
    if (m == NULL) return NULL;

    const bool already_ruled = (ds_tree_find(&m->rules, vif_name) != NULL);
    if (WARN_ON(already_ruled)) return NULL;

    const struct osw_state_observer obs = {
        .name = __FILE__,
        /* phy_added_fn is not handled because phy_name is
         * not known beforehand. Once vif_added_fn happens
         * the phy_info from the vif will be pulled in
         * allowing phy_changed_fn and phy_removed_fn to
         * work.
         */
        .phy_changed_fn = osw_defer_vif_down_phy_changed_cb,
        .phy_removed_fn = osw_defer_vif_down_phy_removed_cb,

        .vif_added_fn = osw_defer_vif_down_vif_added_cb,
        .vif_changed_fn = osw_defer_vif_down_vif_changed_cb,
        .vif_removed_fn = osw_defer_vif_down_vif_removed_cb,

        .sta_connected_fn = osw_defer_vif_down_sta_connected_cb,
        .sta_disconnected_fn = osw_defer_vif_down_sta_disconnected_cb,
    };
    const struct osw_conf_mutator mut = {
        .name = __FILE__,
        .type = OSW_CONF_TAIL,
        .mutate_fn = osw_defer_vif_down_mutate_cb,
    };

    struct osw_defer_vif_down_rule *r = CALLOC(1, sizeof(*r));
    r->m = m;
    r->vif_name = STRDUP(vif_name);
    r->obs = obs;
    r->mut = mut;
    r->grace_period_seconds = grace_period_seconds;
    osw_conf_register_mutator(&r->mut);
    osw_state_register_observer(&r->obs);
    osw_timer_init(&r->grace_period_timer,
                   osw_defer_vif_down_rule_grace_expired_cb);
    ds_tree_insert(&m->rules, r, r->vif_name);
    LOGI(LOG_PREFIX_RULE(r, "created"));
    return r;
}

void
osw_defer_vif_down_rule_free(osw_defer_vif_down_rule_t *r)
{
    if (r == NULL) return;

    LOGI(LOG_PREFIX_RULE(r, "freeing"));
    osw_state_unregister_observer(&r->obs);
    osw_conf_unregister_mutator(&r->mut);
    if (osw_timer_is_armed(&r->grace_period_timer)) {
        osw_defer_vif_down_notify_grace_stopped(r);
    }
    ds_tree_remove(&r->m->rules, r);
    FREE(r->vif_name);
    FREE(r);
}

osw_defer_vif_down_observer_t *
osw_defer_vif_down_observer(osw_defer_vif_down_t *m,
                            const char *vif_name,
                            osw_defer_vif_down_notify_fn_t *grace_period_started_fn,
                            osw_defer_vif_down_notify_fn_t *grace_period_stopped_fn,
                            void *fn_priv)
{
    if (m == NULL) return NULL;

    struct osw_defer_vif_down_observer *o = CALLOC(1, sizeof(*o));
    o->m = m;
    o->vif_name = STRDUP(vif_name);
    o->grace_period_started_fn = grace_period_started_fn;
    o->grace_period_stopped_fn = grace_period_stopped_fn;
    o->fn_priv = fn_priv;
    ds_dlist_insert_tail(&m->observers, o);
    osw_defer_vif_down_observe_initial_fire(o);
    return o;
}

void
osw_defer_vif_down_observer_free(osw_defer_vif_down_observer_t *o)
{
    if (o == NULL) return;

    ds_dlist_remove(&o->m->observers, o);
    osw_defer_vif_down_observe_final_fire(o);
    FREE(o->vif_name);
    FREE(o);
}

uint64_t
osw_defer_vif_down_get_remaining_nsec(osw_defer_vif_down_t *m,
                                      const char *vif_name)
{
    if (m == NULL) return 0;
    struct osw_defer_vif_down_rule *r = ds_tree_find(&m->rules, vif_name);
    const uint64_t now_nsec = osw_time_mono_clk();
    struct osw_timer *t = &r->grace_period_timer;
    if (osw_timer_is_armed(t) == false) return 0;
    return osw_timer_get_remaining_nsec(t, now_nsec);
}

static void
osw_defer_vif_down_init(struct osw_defer_vif_down *m)
{
    ds_tree_init(&m->rules, ds_str_cmp, struct osw_defer_vif_down_rule, node);
    ds_dlist_init(&m->observers, struct osw_defer_vif_down_observer, node);
}

static void
osw_defer_vif_down_attach(struct osw_defer_vif_down *m)
{
}

OSW_MODULE(osw_defer_vif_down)
{
    static struct osw_defer_vif_down m;
    osw_defer_vif_down_init(&m);
    osw_defer_vif_down_attach(&m);
    return &m;
}

OSW_UT(osw_defer_vif_down_test_1)
{
    struct osw_conf_phy phy1;
    struct osw_conf_vif vif1;
    struct osw_defer_vif_down_rule r;

    MEMZERO(phy1);
    MEMZERO(vif1);
    MEMZERO(r);

    osw_timer_init(&r.grace_period_timer, osw_defer_vif_down_rule_grace_expired_cb);
    r.num_stations = 1;

    phy1.enabled = true;
    vif1.enabled = true;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);

    phy1.enabled = false;
    vif1.enabled = false;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == true);

    phy1.enabled = true;
    vif1.enabled = false;
    osw_timer_disarm(&r.grace_period_timer);
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == false);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);
}

OSW_UT(osw_defer_vif_down_test_2)
{
    struct osw_conf_phy phy1;
    struct osw_conf_vif vif1;
    struct osw_defer_vif_down_rule r;

    MEMZERO(phy1);
    MEMZERO(vif1);
    MEMZERO(r);

    osw_timer_init(&r.grace_period_timer, osw_defer_vif_down_rule_grace_expired_cb);
    r.num_stations = 1;

    phy1.enabled = true;
    vif1.enabled = true;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);

    phy1.enabled = true;
    vif1.enabled = false;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == true);

    phy1.enabled = true;
    vif1.enabled = false;
    r.num_stations = 0;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == false);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);
}

OSW_UT(osw_defer_vif_down_test_3)
{
    struct osw_conf_phy phy1;
    struct osw_conf_vif vif1;
    struct osw_defer_vif_down_rule r;

    MEMZERO(phy1);
    MEMZERO(vif1);
    MEMZERO(r);

    osw_timer_init(&r.grace_period_timer, osw_defer_vif_down_rule_grace_expired_cb);
    r.num_stations = 1;

    phy1.enabled = true;
    vif1.enabled = true;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);

    phy1.enabled = false;
    vif1.enabled = false;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == true);

    phy1.enabled = true;
    vif1.enabled = true;
    osw_defer_vif_down_rule_mutate(&phy1, &vif1, &r);
    assert(phy1.enabled == true);
    assert(vif1.enabled == true);
    assert(osw_timer_is_armed(&r.grace_period_timer) == false);
}
