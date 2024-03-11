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
#include <os.h>
#include <log.h>
#include <const.h>
#include <ds_dlist.h>
#include <ds_tree.h>

#include "ow_dfs_backup.h"
#include <osw_module.h>
#include <osw_timer.h>
#include <osw_cqm.h>
#include <osw_ut.h>

#define LOG_PREFIX "ow: dfs backup: "

/* FIXME: This might need some adjusting depending on how
 * well the current system in its entirety can recover from
 * a connection loss.
 */
#define OW_DFS_BACKUP_TIMEOUT_SEC 10.

/*
 * DFS Backup
 *
 * Purpose:
 *
 * Provide input: fallback configurations
 * Provide observable: recovery request
 *
 * Description:
 *
 * The module is providing a configuration storage for data
 * model adapter(s).
 *
 * It then uses that data against link cqm reports and, when
 * the need arises, it generates an event that observer(s)
 * can listen for and act upon.
 */

enum ow_dfs_backup_phy_op {
    OW_DFS_BACKUP_PHY_NOP,
    OW_DFS_BACKUP_PHY_NOTIFY_START,
    OW_DFS_BACKUP_PHY_NOTIFY_STOP,
};

struct ow_dfs_backup_phy_conf {
    struct osw_hwaddr bssid;
    struct osw_channel channel;
};

struct ow_dfs_backup_phy_sm {
    struct ow_dfs_backup_phy_conf conf_prev;
    struct ow_dfs_backup_phy_conf conf_set;
    struct ow_dfs_backup_phy_conf conf_used;
    enum ow_dfs_backup_phy_state state;
    char *link_vif_name;
};

struct ow_dfs_backup_phy {
    struct ow_dfs_backup *b;
    struct ds_tree_node node_phy;
    struct ds_tree_node node_link;
    char *phy_name;
    struct ow_dfs_backup_phy_sm sm;
    struct osw_cqm_notify *notify;
    struct osw_timer work;
};

struct ow_dfs_backup_notify {
    struct ds_dlist_node node;
    struct ow_dfs_backup *b;
    char *name;
    ow_dfs_backup_notify_fn_t *fn;
    void *fn_priv;
};

struct ow_dfs_backup {
    struct ds_tree phys;
    struct ds_tree links;
    struct ds_dlist notify_fns;
    struct osw_cqm_ops *cqm_ops;
    struct osw_cqm *cqm;
};

static const char *
phy_state_to_str(enum ow_dfs_backup_phy_state s)
{
    switch (s) {
        case OW_DFS_BACKUP_PHY_REMOVED: return "removed";
        case OW_DFS_BACKUP_PHY_CONFIGURED: return "configured";
        case OW_DFS_BACKUP_PHY_LATCHED: return "latched";
    }
    return "";
}

static const char *
phy_op_to_str(enum ow_dfs_backup_phy_op op)
{
    switch (op) {
        case OW_DFS_BACKUP_PHY_NOP: return "nop";
        case OW_DFS_BACKUP_PHY_NOTIFY_START: return "notify_start";
        case OW_DFS_BACKUP_PHY_NOTIFY_STOP: return "notify_stop";
    }
    return "";
}

static bool
phy_is_configured(struct ow_dfs_backup_phy_sm *sm)
{
    const struct osw_channel chan0 = {0};
    const struct osw_hwaddr bssid0 = {0};
    if (memcmp(&sm->conf_set.channel, &chan0, sizeof(chan0)) == 0) return false;
    if (memcmp(&sm->conf_set.bssid, &bssid0, sizeof(bssid0)) == 0) return false;
    return true;
}

static bool
phy_is_changed(struct ow_dfs_backup_phy_sm *sm)
{
    const size_t size = sizeof(sm->conf_set);
    return memcmp(&sm->conf_set, &sm->conf_used, size) != 0;
}

static void
phy_arm(struct ow_dfs_backup_phy *phy)
{
    osw_timer_arm_at_nsec(&phy->work, 0);
}

static const char *
phy_filter_link(const char *vif_name,
                enum osw_cqm_link_state state,
                bool is_dfs)
{
    switch (state) {
        case OSW_CQM_LINK_DECONFIGURED: return NULL;
        case OSW_CQM_LINK_DISCONNECTED: return NULL;
        case OSW_CQM_LINK_CONNECTED: return NULL;
        case OSW_CQM_LINK_TIMING_OUT: return NULL;
        case OSW_CQM_LINK_RECOVERING: return NULL;
        case OSW_CQM_LINK_TIMED_OUT: return is_dfs ? vif_name : NULL;
    }

    return NULL;
}

static void
phy_set_link(struct ow_dfs_backup_phy *phy,
             const char *vif_name,
             enum osw_cqm_link_state state,
             const struct osw_channel *c)
{
    const struct osw_channel chan0 = {0};
    const struct osw_channel *chan = c ?: &chan0;
    const bool always = getenv("OW_DFS_BACKUP_ALWAYS") ? true : false;
    const bool is_dfs = osw_channel_overlaps_dfs(chan) || always;

    LOGT(LOG_PREFIX"phy: %s: set link: %s state: %d chan: "OSW_CHANNEL_FMT,
         phy ? phy->phy_name : "",
         vif_name ?: "",
         state,
         OSW_CHANNEL_ARG(chan));

    if (is_dfs == true &&
        state == OSW_CQM_LINK_RECOVERING) {
        LOGN(LOG_PREFIX"link: %s: recovered before timeout", vif_name ?: "");
    }

    vif_name = phy_filter_link(vif_name, state, is_dfs);
    if (vif_name != NULL && phy == NULL) {
        LOGN(LOG_PREFIX"link: %s: cannot recover, no backup phy bound",
             vif_name);
        return;
    }
    if (phy == NULL) {
        return;
    }
    if (phy->sm.link_vif_name != NULL) {
        LOGD(LOG_PREFIX"phy: %s: link: %s: unlatching",
             phy->phy_name, phy->sm.link_vif_name);
        ds_tree_remove(&phy->b->links, phy);
        FREE(phy->sm.link_vif_name);
        phy->sm.link_vif_name = NULL;
    }
    if (vif_name != NULL) {
        LOGD(LOG_PREFIX"phy: %s: link: %s: latching",
             phy->phy_name, vif_name);
        phy->sm.link_vif_name = STRDUP(vif_name);
        ds_tree_insert(&phy->b->links, phy, phy->sm.link_vif_name);
    }
    phy_arm(phy);
}

static void
cqm_cb(const char *vif_name,
       enum osw_cqm_link_state state,
       const struct osw_channel *c,
       void *priv)
{
    struct ow_dfs_backup_phy *phy = priv;
    phy_set_link(phy, vif_name, state, c);
}

static enum ow_dfs_backup_phy_state
phy_sm_next(struct ow_dfs_backup_phy_sm *sm)
{
    switch (sm->state) {
        case OW_DFS_BACKUP_PHY_REMOVED:
            if (phy_is_configured(sm) == true) return OW_DFS_BACKUP_PHY_CONFIGURED;
            break;
        case OW_DFS_BACKUP_PHY_CONFIGURED:
            if (phy_is_configured(sm) == false) return OW_DFS_BACKUP_PHY_REMOVED;
            if (sm->link_vif_name != NULL) return OW_DFS_BACKUP_PHY_LATCHED;
            break;
        case OW_DFS_BACKUP_PHY_LATCHED:
            if (phy_is_configured(sm) == false) return OW_DFS_BACKUP_PHY_CONFIGURED;
            if (phy_is_changed(sm) == true) return OW_DFS_BACKUP_PHY_CONFIGURED;
            if (sm->link_vif_name == NULL) return OW_DFS_BACKUP_PHY_CONFIGURED;
            break;
    }
    return sm->state;
}

static void
phy_notify_one(const struct ow_dfs_backup_phy *phy,
               const struct ow_dfs_backup_phy_sm *sm,
               const struct ow_dfs_backup_notify *n)
{
    const char *to = phy_state_to_str(sm->state);
    LOGD(LOG_PREFIX"phy: %s: notify: %s: %s", phy->phy_name, n->name, to);
    n->fn(phy->phy_name,
          sm->link_vif_name,
          sm->state,
          &sm->conf_used.bssid,
          &sm->conf_used.channel,
          n->fn_priv);
}

static void
phy_notify(struct ow_dfs_backup_phy *phy)
{
    struct ow_dfs_backup_notify *n;
    ds_dlist_foreach(&phy->b->notify_fns, n) {
        phy_notify_one(phy, &phy->sm, n);
    }
}

static enum ow_dfs_backup_phy_op
phy_sm_leave(struct ow_dfs_backup_phy_sm *sm,
             enum ow_dfs_backup_phy_state state)
{
    switch (state) {
        case OW_DFS_BACKUP_PHY_REMOVED: return OW_DFS_BACKUP_PHY_NOTIFY_START;
        case OW_DFS_BACKUP_PHY_CONFIGURED: return OW_DFS_BACKUP_PHY_NOP;
        case OW_DFS_BACKUP_PHY_LATCHED:
            memset(&sm->conf_used, 0, sizeof(sm->conf_used));
            return OW_DFS_BACKUP_PHY_NOP;
    }
    return OW_DFS_BACKUP_PHY_NOP;
}

static enum ow_dfs_backup_phy_op
phy_sm_enter(struct ow_dfs_backup_phy_sm *sm,
             enum ow_dfs_backup_phy_state state)
{
    switch (state) {
        case OW_DFS_BACKUP_PHY_REMOVED: return OW_DFS_BACKUP_PHY_NOTIFY_STOP;
        case OW_DFS_BACKUP_PHY_CONFIGURED: return OW_DFS_BACKUP_PHY_NOP;
        case OW_DFS_BACKUP_PHY_LATCHED:
            sm->conf_used = sm->conf_set;
            return OW_DFS_BACKUP_PHY_NOP;
    }
    return OW_DFS_BACKUP_PHY_NOP;
}

static void
phy_sm_walk(struct ow_dfs_backup_phy *phy,
            const struct ow_dfs_backup_phy_sm *base,
            struct ow_dfs_backup_notify *n,
            enum ow_dfs_backup_phy_state from)
{
    struct ow_dfs_backup_phy_sm sm;
    MEMZERO(sm);
    if (base != NULL) sm = *base;
    sm.state = from;

    for (;;) {
            enum ow_dfs_backup_phy_state prev_state = sm.state;
            enum ow_dfs_backup_phy_state next_state = phy_sm_next(&sm);
            if (prev_state == next_state) break;
            const char *from = phy_state_to_str(prev_state);
            const char *to = phy_state_to_str(next_state);
            LOGD(LOG_PREFIX"walk: %s: state: %s -> %s", phy->phy_name, from, to);
            sm.state = next_state;
            phy_sm_leave(&sm, prev_state);
            phy_sm_enter(&sm, next_state);
            phy_notify_one(phy, &sm, n);
    }
}

static void
phy_sm_op(struct ow_dfs_backup_phy *phy,
          enum ow_dfs_backup_phy_op op)
{
    LOGD(LOG_PREFIX"phy: %s: op: %s", phy->phy_name, phy_op_to_str(op));
    switch (op) {
        case OW_DFS_BACKUP_PHY_NOP:
            break;
        case OW_DFS_BACKUP_PHY_NOTIFY_START:
            assert(phy->notify == NULL);
            phy->notify = phy->b->cqm_ops->add_notify_fn(phy->b->cqm, __FILE__, cqm_cb, phy);
            break;
        case OW_DFS_BACKUP_PHY_NOTIFY_STOP:
            assert(phy->notify != NULL);
            phy->b->cqm_ops->del_notify_fn(phy->notify);
            phy->notify = NULL;
            break;
    }
}

static void
phy_free(struct ow_dfs_backup_phy *phy)
{
    LOGD(LOG_PREFIX"phy: %s: freeing", phy->phy_name);
    osw_timer_disarm(&phy->work);
    assert(phy->sm.link_vif_name == NULL);
    ds_tree_remove(&phy->b->phys, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

static void
phy_work(struct ow_dfs_backup_phy *phy)
{
    for (;;) {
        const enum ow_dfs_backup_phy_state prev_state = phy->sm.state;
        const enum ow_dfs_backup_phy_state next_state = phy_sm_next(&phy->sm);
        if (prev_state == next_state) break;
        const char *from = phy_state_to_str(prev_state);
        const char *to = phy_state_to_str(next_state);
        LOGD(LOG_PREFIX"phy: %s: state: %s -> %s", phy->phy_name, from, to);
        phy->sm.state = next_state;
        phy_sm_op(phy, phy_sm_leave(&phy->sm, prev_state));
        phy_sm_op(phy, phy_sm_enter(&phy->sm, next_state));
        phy_notify(phy);
    }

    if (phy->sm.state == OW_DFS_BACKUP_PHY_REMOVED) {
        phy_free(phy);
    }
}

static void
phy_work_cb(struct osw_timer *t)
{
    struct ow_dfs_backup_phy *phy = container_of(t, struct ow_dfs_backup_phy, work);
    phy_work(phy);
}

struct ow_dfs_backup_phy *
ow_dfs_backup_get_phy(struct ow_dfs_backup *b,
                      const char *phy_name)
{
    struct ow_dfs_backup_phy *phy = ds_tree_find(&b->phys, phy_name);
    if (phy == NULL) {
        LOGD(LOG_PREFIX"phy: %s: allocating", phy_name);
        phy = CALLOC(1, sizeof(*phy));
        phy->b = b;
        phy->phy_name = STRDUP(phy_name);
        phy->work.cb = phy_work_cb;
        ds_tree_insert(&b->phys, phy, phy->phy_name);
    }
    return phy;
}

void
ow_dfs_backup_phy_set_bssid(struct ow_dfs_backup_phy *phy,
                            const struct osw_hwaddr *bssid)
{
    const struct osw_hwaddr *prev = &phy->sm.conf_prev.bssid;
    if (osw_hwaddr_cmp(prev, bssid) != 0) {
        LOGI(LOG_PREFIX"phy: %s: setting bssid: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
             phy->phy_name,
             OSW_HWADDR_ARG(prev),
             OSW_HWADDR_ARG(bssid));
        phy_arm(phy);
    }
    phy->sm.conf_prev.bssid = *bssid;
    phy->sm.conf_set.bssid = *bssid;
}

void
ow_dfs_backup_phy_set_channel(struct ow_dfs_backup_phy *phy,
                              const struct osw_channel *channel)
{
    const size_t size = sizeof(*channel);
    const struct osw_channel *prev = &phy->sm.conf_prev.channel;
    if (memcmp(prev, channel, size) != 0) {
        LOGI(LOG_PREFIX"phy: %s: setting channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             phy->phy_name,
             OSW_CHANNEL_ARG(prev),
             OSW_CHANNEL_ARG(channel));
        phy_arm(phy);
    }
    phy->sm.conf_prev.channel = *channel;
    phy->sm.conf_set.channel = *channel;
}

void
ow_dfs_backup_phy_reset(struct ow_dfs_backup_phy *phy)
{
    memset(&phy->sm.conf_set, 0, sizeof(phy->sm.conf_set));
    phy_arm(phy);
}

static void
ow_dfs_backup_phy_unlatch(struct ow_dfs_backup_phy *phy)
{
    const char *vif_name = phy->sm.link_vif_name;

    if (vif_name == NULL) return;

    LOGD(LOG_PREFIX"phy: %s: unlatching from link: %s",
         phy->phy_name,
         vif_name);
    phy_set_link(phy, NULL, OSW_CQM_LINK_DECONFIGURED, 0);
}

void
ow_dfs_backup_unlatch_vif(struct ow_dfs_backup *b,
                          const char *vif_name)
{
    struct ow_dfs_backup_phy *phy;

    ds_tree_foreach(&b->phys, phy) {
        if (phy->sm.link_vif_name != NULL) {
            if (strcmp(phy->sm.link_vif_name, vif_name) == 0) {
                ow_dfs_backup_phy_unlatch(phy);
            }
        }
    }
}

struct ow_dfs_backup_notify *
ow_dfs_backup_add_notify(struct ow_dfs_backup *b,
                         const char *name,
                         ow_dfs_backup_notify_fn_t *fn,
                         void *fn_priv)
{
    assert(b != NULL);
    assert(name != NULL);

    LOGD(LOG_PREFIX"notify: %s: allocating", name);
    struct ow_dfs_backup_notify *n = CALLOC(1, sizeof(*n));
    n->b = b;
    n->fn = fn;
    n->fn_priv = fn_priv;
    n->name = STRDUP(name);
    ds_dlist_insert_tail(&b->notify_fns, n);

    struct ow_dfs_backup_phy *phy;
    ds_tree_foreach(&b->phys, phy) {
        phy_sm_walk(phy, &phy->sm, n, OW_DFS_BACKUP_PHY_REMOVED);
    }

    return n;
}

void
ow_dfs_backup_del_notify(struct ow_dfs_backup_notify *n)
{
    if (n == NULL) return;

    LOGD(LOG_PREFIX"notify: %s: freeing", n->name);
    ds_dlist_remove(&n->b->notify_fns, n);

    struct ow_dfs_backup_phy *phy;
    ds_tree_foreach(&n->b->phys, phy) {
        phy_sm_walk(phy, NULL, n, phy->sm.state);
    }

    FREE(n);
}

static void
mod_init(struct ow_dfs_backup *b)
{
    ds_tree_init(&b->phys, ds_str_cmp, struct ow_dfs_backup_phy, node_phy);
    ds_tree_init(&b->links, ds_str_cmp, struct ow_dfs_backup_phy, node_link);
    ds_dlist_init(&b->notify_fns, struct ow_dfs_backup_notify, node);
}

static void
mod_attach(struct ow_dfs_backup *b)
{
    assert(b->cqm_ops != NULL);
    if (b->cqm_ops != NULL) {
        b->cqm = b->cqm_ops->alloc_fn(b->cqm_ops);
        b->cqm_ops->set_timeout_sec_fn(b->cqm, OW_DFS_BACKUP_TIMEOUT_SEC);
    }
}

OSW_MODULE(ow_dfs_backup)
{
    static struct ow_dfs_backup m;
    m.cqm_ops = OSW_MODULE_LOAD(osw_cqm);
    mod_init(&m);
    mod_attach(&m);
    return &m;
}

static void
walk_ut_cb(const char *phy_name,
           const char *link_vif_name,
           enum ow_dfs_backup_phy_state state,
           const struct osw_hwaddr *bssid,
           const struct osw_channel *channel,
           void *priv)
{
    size_t *cnt = priv;
    (*cnt)++;
}

OSW_UT(ow_dfs_backup_walk)
{
    struct ow_dfs_backup m;
    MEMZERO(m);
    const struct osw_hwaddr bssid1 = { .octet = { 1 } };
    const struct osw_channel c1 = {
        .control_freq_mhz = 2412,
        .center_freq0_mhz = 2412,
    };
    const struct osw_channel c60 = {
        .control_freq_mhz = 5300,
        .center_freq0_mhz = 5300,
    };
    mod_init(&m);

    assert(ds_tree_is_empty(&m.phys) == true);
    struct ow_dfs_backup_phy *phy = ow_dfs_backup_get_phy(&m, "phy1");
    assert(ds_tree_is_empty(&m.phys) == false);
    ow_dfs_backup_phy_set_bssid(phy, &bssid1);
    ow_dfs_backup_phy_set_channel(phy, &c1);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_CONFIGURED);
    cqm_cb("vif1", OSW_CQM_LINK_CONNECTED, &c60, phy);
    assert(ds_tree_is_empty(&m.links) == true);
    cqm_cb("vif1", OSW_CQM_LINK_TIMED_OUT, &c60, phy);
    assert(ds_tree_is_empty(&m.links) == false);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_LATCHED);

    size_t cnt = 0;
    struct ow_dfs_backup_notify *n = ow_dfs_backup_add_notify(&m,
                                                              __FILE__,
                                                              walk_ut_cb,
                                                              &cnt);
    assert(cnt == 2);
    ow_dfs_backup_del_notify(n);
    assert(cnt == 4);

    n = ow_dfs_backup_add_notify(&m,
                                 __FILE__,
                                 walk_ut_cb,
                                 &cnt);
    assert(cnt == 6);
    cqm_cb("vif1", OSW_CQM_LINK_CONNECTED, &c60, phy);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_CONFIGURED);
    assert(ds_tree_is_empty(&m.links) == true);
    ow_dfs_backup_del_notify(n);
    assert(cnt == 7);

    ow_dfs_backup_phy_reset(phy);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_REMOVED);

    cqm_cb("vif1", OSW_CQM_LINK_TIMED_OUT, &c60, phy);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_REMOVED);
    cqm_cb("vif1", OSW_CQM_LINK_DECONFIGURED, &c60, phy);
    assert(ds_tree_is_empty(&m.links) == true);

    /*
    ow_dfs_backup_phy_set_bssid(phy, &bssid1);
    ow_dfs_backup_phy_set_channel(phy, &c1);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_CONFIGURED);
    Can't really test this without proper mock-up of CQM module.

    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_LATCHED);

    ow_dfs_backup_phy_reset(phy);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_CONFIGURED);
    assert((phy->sm.state = phy_sm_next(&phy->sm)) == OW_DFS_BACKUP_PHY_REMOVED);
    */

    phy_free(phy);
}
