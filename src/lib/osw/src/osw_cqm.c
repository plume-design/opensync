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

#include <memutil.h>
#include <log.h>
#include <const.h>

#include <osw_timer.h>
#include <osw_time.h>
#include <osw_state.h>
#include <osw_ut.h>
#include <osw_cqm.h>

/*
 * Connection Quality Monitor
 *
 * Purpose:
 *
 * Provide an easy way to observe OSW_VIF_STA link changes
 * from the point of interest of losting a link for too
 * long.
 *
 * Notes:
 *
 * Arguably the module name is slightly too generic for the
 * purpose, but at the same time it's not. If this proves to
 * collide with another similar purpose then it should be
 * renamed to match more closely its task.
 */

#define LOG_PREFIX "osw: cqm: "

enum osw_cqm_link_op {
    OSW_CQM_LINK_NOP,
    OSW_CQM_LINK_DEADLINE_ARM,
    OSW_CQM_LINK_DEADLINE_DISARM,
    OSW_CQM_LINK_SET_CHANNEL,
    OSW_CQM_LINK_CLEAR_TIMEOUT,
};

struct osw_cqm_link_sm {
    enum osw_cqm_link_state state;
    struct osw_channel channel;
    struct osw_channel channel_next;
    bool connected;
    bool configured;
    bool timed_out;
};

struct osw_cqm_link {
    struct ds_tree_node node;
    struct osw_cqm *cqm;
    char *vif_name;
    const struct osw_state_vif_info *vif;
    struct osw_timer work;
    struct osw_timer deadline;
    struct osw_cqm_link_sm sm;
};

struct osw_cqm_notify {
    struct ds_dlist_node node;
    struct osw_cqm *cqm;
    char *name;
    osw_cqm_notify_fn_t *fn;
    void *fn_priv;
};

struct osw_cqm {
    struct ds_dlist_node node;
    struct osw_cqm_mod *m;
    struct ds_tree links;
    struct ds_dlist notify_fns;
    struct osw_state_observer state_obs;
    uint64_t timeout_nsec;
};

#define OPS_TO_MOD(ops) container_of(ops, struct osw_cqm_mod, ops)

struct osw_cqm_mod {
    struct osw_cqm_ops ops;
    struct ds_dlist cqms;
};

static const char *
link_state_to_str(enum osw_cqm_link_state s)
{
    switch (s) {
        case OSW_CQM_LINK_DECONFIGURED: return "deconfigured";
        case OSW_CQM_LINK_DISCONNECTED: return "disconnected";
        case OSW_CQM_LINK_CONNECTED: return "connected";
        case OSW_CQM_LINK_CHANNEL_CHANGED: return "channel_changed";
        case OSW_CQM_LINK_TIMING_OUT: return "timing_out";
        case OSW_CQM_LINK_RECOVERING: return "recovering";
        case OSW_CQM_LINK_TIMED_OUT: return "timed_out";
    }
    return "";
}

static const char *
link_op_to_str(enum osw_cqm_link_op op)
{
    switch (op) {
        case OSW_CQM_LINK_NOP: return "nop";
        case OSW_CQM_LINK_DEADLINE_ARM: return "deadline_arm";
        case OSW_CQM_LINK_DEADLINE_DISARM: return "deadline_disarm";
        case OSW_CQM_LINK_SET_CHANNEL: return "set_channel";
        case OSW_CQM_LINK_CLEAR_TIMEOUT: return "clear_timeout";
    }
    return "";
}

static void
link_notify_one(const struct osw_cqm_link *link,
                const struct osw_cqm_link_sm *sm,
                const struct osw_cqm_notify *n)
{
    const char *to = link_state_to_str(sm->state);
    LOGD(LOG_PREFIX"notify: %s: %s @ %s", n->name, link->vif_name, to);
    n->fn(link->vif_name, sm->state, &link->sm.channel, n->fn_priv);
}

static void
link_notify(struct osw_cqm_link *link)
{
    struct osw_cqm_notify *n;

    ds_dlist_foreach(&link->cqm->notify_fns, n) {
        link_notify_one(link, &link->sm, n);
    }
}

static enum osw_cqm_link_state
link_sm_next(struct osw_cqm_link_sm *sm)
{
    switch (sm->state) {
        case OSW_CQM_LINK_DECONFIGURED:
            if (sm->configured == true) return OSW_CQM_LINK_DISCONNECTED;
            break;
        case OSW_CQM_LINK_DISCONNECTED:
            if (sm->configured == false) return OSW_CQM_LINK_DECONFIGURED;
            if (sm->connected == true) return OSW_CQM_LINK_CONNECTED;
            if (sm->timed_out == true) return OSW_CQM_LINK_CONNECTED;
            break;
        case OSW_CQM_LINK_CONNECTED:
            if (sm->configured == false) return OSW_CQM_LINK_DECONFIGURED;
            if (sm->connected == false) return OSW_CQM_LINK_TIMING_OUT;
            if (osw_channel_is_equal(&sm->channel, &sm->channel_next) == false) return OSW_CQM_LINK_CHANNEL_CHANGED;
            break;
        case OSW_CQM_LINK_CHANNEL_CHANGED:
            return OSW_CQM_LINK_CONNECTED;
        case OSW_CQM_LINK_RECOVERING:
            return OSW_CQM_LINK_CONNECTED;
        case OSW_CQM_LINK_TIMING_OUT:
            if (sm->configured == false) return OSW_CQM_LINK_DECONFIGURED;
            if (sm->connected == true) return OSW_CQM_LINK_RECOVERING;
            if (sm->timed_out == true) return OSW_CQM_LINK_TIMED_OUT;
            break;
        case OSW_CQM_LINK_TIMED_OUT:
            if (sm->configured == false) return OSW_CQM_LINK_DECONFIGURED;
            if (sm->connected == true) return OSW_CQM_LINK_CONNECTED;
            break;
    }
    return sm->state;
}

static enum osw_cqm_link_state
link_sm_walk(const struct osw_cqm_link *link,
             const struct osw_cqm_link_sm *base,
             struct osw_cqm_notify *n,
             enum osw_cqm_link_state from)
{
    struct osw_cqm_link_sm sm = {0};
    if (base != NULL) sm = *base;
    sm.state = from;

    for (;;) {
        const enum osw_cqm_link_state next_state = link_sm_next(&sm);
        if (sm.state == next_state) break;
        sm.state = next_state;
        link_notify_one(link, &sm, n);
    }

    return sm.state;
}

static void
link_sm_op(struct osw_cqm_link *link,
           enum osw_cqm_link_op op)
{
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + link->cqm->timeout_nsec;
    LOGD(LOG_PREFIX"link: %s: op: %s", link->vif_name, link_op_to_str(op));
    switch (op) {
        case OSW_CQM_LINK_NOP:
            break;
        case OSW_CQM_LINK_DEADLINE_ARM:
            link->sm.timed_out = false;
            osw_timer_arm_at_nsec(&link->deadline, at);
            break;
        case OSW_CQM_LINK_DEADLINE_DISARM:
            osw_timer_disarm(&link->deadline);
            break;
        case OSW_CQM_LINK_SET_CHANNEL:
            link->sm.channel = link->sm.channel_next;
            break;
        case OSW_CQM_LINK_CLEAR_TIMEOUT:
            link->sm.timed_out = false;
            break;
    }
}

static enum osw_cqm_link_op
link_sm_leave(struct osw_cqm_link *link,
              enum osw_cqm_link_state state)
{
    switch (state) {
        case OSW_CQM_LINK_DECONFIGURED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_DISCONNECTED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_CONNECTED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_CHANNEL_CHANGED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_TIMING_OUT: return OSW_CQM_LINK_DEADLINE_DISARM;
        case OSW_CQM_LINK_RECOVERING: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_TIMED_OUT: return OSW_CQM_LINK_CLEAR_TIMEOUT;
    }
    return OSW_CQM_LINK_NOP;
}

static enum osw_cqm_link_op
link_sm_enter(struct osw_cqm_link *link,
              enum osw_cqm_link_state state)
{
    switch (state) {
        case OSW_CQM_LINK_DECONFIGURED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_DISCONNECTED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_CONNECTED: return OSW_CQM_LINK_SET_CHANNEL;
        case OSW_CQM_LINK_CHANNEL_CHANGED: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_TIMING_OUT: return OSW_CQM_LINK_DEADLINE_ARM;
        case OSW_CQM_LINK_RECOVERING: return OSW_CQM_LINK_NOP;
        case OSW_CQM_LINK_TIMED_OUT: return OSW_CQM_LINK_NOP;
    }
    return OSW_CQM_LINK_NOP;
}

static void
link_free(struct osw_cqm_link *link)
{
    assert(link->cqm != NULL);
    assert(osw_timer_is_armed(&link->deadline) == false);
    osw_timer_disarm(&link->work);
    ds_tree_remove(&link->cqm->links, link);
    FREE(link->vif_name);
    FREE(link);
}

static void
link_work(struct osw_cqm_link *link)
{
    for (;;) {
        const enum osw_cqm_link_state prev_state = link->sm.state;
        const enum osw_cqm_link_state next_state = link_sm_next(&link->sm);
        if (prev_state == next_state) break;
        const char *from = link_state_to_str(prev_state);
        const char *to = link_state_to_str(next_state);
        LOGD(LOG_PREFIX"link: %s: state: %s -> %s", link->vif_name, from, to);
        link->sm.state = next_state;
        link_sm_op(link, link_sm_leave(link, prev_state));
        link_sm_op(link, link_sm_enter(link, next_state));
        link_notify(link);
    }

    if (link->sm.state == OSW_CQM_LINK_DECONFIGURED) {
        link_free(link);
    }
}

static void
link_arm(struct osw_cqm_link *link)
{
    osw_timer_arm_at_nsec(&link->work, 0);
}

static void
link_work_cb(struct osw_timer *t)
{
    struct osw_cqm_link *link = container_of(t, struct osw_cqm_link, work);
    link_work(link);
}

static void
link_deadline_cb(struct osw_timer *t)
{
    struct osw_cqm_link *link = container_of(t, struct osw_cqm_link, deadline);
    link->sm.timed_out = true;
    link_arm(link);
}

static struct osw_cqm_link *
link_get(struct osw_cqm *cqm, const char *vif_name)
{
    struct osw_cqm_link *link = ds_tree_find(&cqm->links, vif_name);
    if (link == NULL) {
        link = CALLOC(1, sizeof(*link));
        link->cqm = cqm;
        link->vif_name = STRDUP(vif_name);
        link->deadline.cb = link_deadline_cb;
        link->work.cb = link_work_cb;
        ds_tree_insert(&cqm->links, link, link->vif_name);
    }
    return link;
}

static bool
vif_is_connected(const struct osw_state_vif_info *vif)
{
    if (vif == NULL) return false;
    if (vif->drv_state->vif_type != OSW_VIF_STA) return false;
    if (vif->drv_state->u.sta.link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return false;

    return true;
}

static bool
vif_is_configured(const struct osw_state_vif_info *vif)
{
    if (vif == NULL) return false;
    if (vif->drv_state->vif_type != OSW_VIF_STA) return false;
    if (vif->drv_state->status != OSW_VIF_ENABLED) return false;

    /* FIXME: This should look at u.sta.network but some
     * drivers currently can't guarantee to fill that in
     * properly in time.
     */
    //if (vif->drv_state->u.sta.network == NULL) return false;

    return true;
}

static void
//vif_update_cb(struct osw_state_observer *obs,
              //const struct osw_state_vif_info *vif)
sta_update_cb(struct osw_state_observer *obs,
              const struct osw_state_sta_info *sta)
{
    const struct osw_state_vif_info *vif = sta->vif;
    const char *vif_name = vif->vif_name;
    struct osw_cqm *cqm = container_of(obs, struct osw_cqm, state_obs);
    struct osw_cqm_link *link = link_get(cqm, vif_name);
    link->vif = vif->drv_state->exists == true ? vif : NULL;
    const bool configured =vif_is_configured(link->vif);
    const bool connected = vif_is_connected(link->vif);
    if (connected == true) {
        link->sm.channel_next = vif->drv_state->u.sta.link.channel;
    }
    LOGD(LOG_PREFIX"link: %s: updating: configured=%d -> %d connected=%d -> %d channel="OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
         link->vif_name,
         link->sm.configured,
         configured,
         link->sm.connected,
         connected,
         OSW_CHANNEL_ARG(&link->sm.channel),
         OSW_CHANNEL_ARG(&link->sm.channel_next));
    link->sm.configured = configured;
    link->sm.connected = connected;
    link_arm(link);
}

static void
osw_cqm_init(struct osw_cqm *cqm)
{
    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        //.vif_added_fn = vif_update_cb,
        //.vif_removed_fn = vif_update_cb,
        //.vif_changed_fn = vif_update_cb,
        .sta_connected_fn = sta_update_cb,
        .sta_disconnected_fn = sta_update_cb,
    };

    ds_tree_init(&cqm->links, ds_str_cmp, struct osw_cqm_link, node);
    ds_dlist_init(&cqm->notify_fns, struct osw_cqm_notify, node);
    cqm->state_obs = state_obs;
}

static struct osw_cqm *
op_alloc(struct osw_cqm_ops *ops)
{
    struct osw_cqm_mod *m = OPS_TO_MOD(ops);
    struct osw_cqm *cqm = CALLOC(1, sizeof(*cqm));
    osw_cqm_init(cqm);
    cqm->m = m;
    ds_dlist_insert_tail(&m->cqms, cqm);
    osw_state_register_observer(&cqm->state_obs);
    return cqm;
}

static void
op_set_timeout_sec(struct osw_cqm *cqm, float seconds)
{
    cqm->timeout_nsec = OSW_TIME_SEC(seconds);
}

static struct osw_cqm_notify *
op_add_notify(struct osw_cqm *cqm,
              const char *name,
              osw_cqm_notify_fn_t *fn,
              void *fn_priv)
{
    assert(cqm != NULL);
    assert(name != NULL);

    LOGD(LOG_PREFIX"notify: %s: allocating", name);
    struct osw_cqm_notify *n = CALLOC(1, sizeof(*n));
    n->cqm = cqm;
    n->fn = fn;
    n->fn_priv = fn_priv;
    n->name = STRDUP(name);
    ds_dlist_insert_tail(&cqm->notify_fns, n);

    struct osw_cqm_link *link;
    ds_tree_foreach(&cqm->links, link) {
        link_sm_walk(link, &link->sm, n, OSW_CQM_LINK_DECONFIGURED);
    }

    return n;
}

static void
op_del_notify(struct osw_cqm_notify *n)
{
    if (n == NULL) return;

    LOGD(LOG_PREFIX"notify: %s: freeing", n->name);
    struct osw_cqm_link *link;
    ds_tree_foreach(&n->cqm->links, link) {
        const enum osw_cqm_link_state last_state = link_sm_walk(link, NULL, n, link->sm.state);
        assert(last_state == OSW_CQM_LINK_DECONFIGURED);
    }
    ds_dlist_remove(&n->cqm->notify_fns, n);
    FREE(n->name);
    FREE(n);
}

static void
op_free(struct osw_cqm *cqm)
{
    ds_dlist_remove(&cqm->m->cqms, cqm);
    osw_state_unregister_observer(&cqm->state_obs);
    WARN_ON(ds_dlist_is_empty(&cqm->notify_fns) == true); /* memleak */
    WARN_ON(ds_tree_is_empty(&cqm->links) == true); /* memleak */
    FREE(cqm);
}

static void
mod_init(struct osw_cqm_mod *m)
{
    const struct osw_cqm_ops ops = {
        .alloc_fn = op_alloc,
        .free_fn = op_free,
        .set_timeout_sec_fn = op_set_timeout_sec,
        .add_notify_fn = op_add_notify,
        .del_notify_fn = op_del_notify,
    };
    m->ops = ops;
    ds_dlist_init(&m->cqms, struct osw_cqm, node);
}

OSW_MODULE(osw_cqm)
{
    OSW_MODULE_LOAD(osw_state);
    static struct osw_cqm_mod m;
    mod_init(&m);
    return &m.ops;
}

OSW_UT(osw_cqm_)
{
    struct osw_cqm cqm;
    osw_cqm_init(&cqm);
    struct osw_cqm_link *link = link_get(&cqm, "vif1");
    struct osw_channel ch1 = {
        .control_freq_mhz = 2412,
        .center_freq0_mhz = 2412,
    };
    struct osw_channel ch6 = {
        .control_freq_mhz = 2437,
        .center_freq0_mhz = 2437,
    };
    enum osw_cqm_link_state *s = &link->sm.state;
    assert(link != NULL);
    link->sm.configured = true;
    assert(*s == OSW_CQM_LINK_DECONFIGURED);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_DISCONNECTED);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_DISCONNECTED);
    link->sm.configured = false;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_DECONFIGURED);
    link->sm.configured = true;
    link->sm.connected = true;
    link->sm.channel_next = ch1;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_DISCONNECTED);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    link_sm_op(link, OSW_CQM_LINK_SET_CHANNEL);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    link->sm.channel_next = ch6;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CHANNEL_CHANGED);
    link_sm_op(link, OSW_CQM_LINK_SET_CHANNEL);
    assert(osw_channel_is_equal(&link->sm.channel, &ch6));
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    link->sm.connected = false;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    link->sm.timed_out = true;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMED_OUT);
    link->sm.connected = true;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    link->sm.connected = false;
    link->sm.timed_out = false;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    link->sm.connected = true;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_RECOVERING);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);

    /* check walking */
    struct osw_cqm_link_sm sm = link->sm;
    s = &sm.state;
    *s = OSW_CQM_LINK_DECONFIGURED;

    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_DISCONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_CONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_CONNECTED);

    link->sm.connected = false;
    s = &link->sm.state;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMING_OUT);
    link->sm.timed_out = true;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMED_OUT);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_TIMED_OUT);

    sm = link->sm;
    s = &sm.state;
    *s = OSW_CQM_LINK_DECONFIGURED;
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_DISCONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_CONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_TIMING_OUT);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_TIMED_OUT);

    link->sm.connected = true;
    s = &link->sm.state;
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);
    assert((*s = link_sm_next(&link->sm)) == OSW_CQM_LINK_CONNECTED);

    sm = link->sm;
    s = &sm.state;
    *s = OSW_CQM_LINK_DECONFIGURED;
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_DISCONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_CONNECTED);
    assert((*s = link_sm_next(&sm)) == OSW_CQM_LINK_CONNECTED);
}
