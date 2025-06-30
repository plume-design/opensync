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

/**
 * osw_sta_assoc - Station Association tracking
 *
 * Historically a STA and Association were almost
 * synonymous. However with MLO this no longer holds true.
 *
 * Multiple STAs can combine into a single MLD STA, or in
 * other words, an Association.
 *
 * Stiching the MLD STA from STAs needs special care.
 * This module provides a way to observe Association state
 * changes (osw_sta_event_e) and various attributes (active
 * links, stale links, etc) instead of (re-)doing
 * that in multiple places using raw osw_state.
 */

#include <osw_sta_assoc.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>
#include <osw_state.h>
#include <osw_ut.h>

#include <memutil.h>
#include <const.h>
#include <log.h>
#include <os.h>
#include <ds_tree.h>

#define OSW_STA_SETTLE_MSEC   100
#define OSW_STA_DEADLINE_MSEC 2000

#define LOG_PREFIX(m, fmt, ...) "osw: sta_assoc: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_ENTRY(entry, fmt, ...)                                     \
    LOG_PREFIX(                                                               \
            (entry) ? (entry)->m : NULL,                                      \
            "entry: " OSW_HWADDR_FMT ": " fmt,                                \
            OSW_HWADDR_ARG((entry) ? &(entry)->sta_addr : osw_hwaddr_zero()), \
            ##__VA_ARGS__)

#define LOG_PREFIX_VIF(vif, fmt, ...) \
    LOG_PREFIX((vif) ? (vif)->m : NULL, "vif: %s: " fmt, (vif) ? (vif)->vif_name : "", ##__VA_ARGS__)

#define LOG_PREFIX_LINK(info, fmt, ...)                                      \
    LOG_PREFIX_VIF(                                                          \
            (info) ? (info)->vif : NULL,                                     \
            "info: " OSW_HWADDR_FMT ": " fmt,                                \
            OSW_HWADDR_ARG((info) ? &(info)->link_addr : osw_hwaddr_zero()), \
            ##__VA_ARGS__)

#define state_obs_to_m(obs_) container_of(obs, osw_sta_assoc_t, state_obs);

typedef struct osw_sta_assoc_params osw_sta_assoc_params_t;
typedef struct osw_sta_assoc_vif osw_sta_assoc_vif_t;
typedef struct osw_sta_assoc_link_info osw_sta_assoc_link_info_t;

struct osw_sta_assoc_observer_params
{
    struct osw_hwaddr sta_addr;
    osw_sta_assoc_observer_notify_fn_t *notify_fn;
    void *notify_fn_priv;
};

struct osw_sta_assoc_observer
{
    ds_tree_node_t node_m;
    ds_tree_node_t node_entry;
    osw_sta_assoc_observer_params_t p;
    osw_sta_assoc_entry_t *entry;
};

struct osw_sta_assoc_vif
{
    ds_tree_node_t node_m;
    ds_tree_t link_infos;
    osw_sta_assoc_t *m;
    char *vif_name;
    struct osw_hwaddr mac_addr;
    struct osw_hwaddr mld_addr;
};

struct osw_sta_assoc_link_info
{
    ds_tree_node_t node_entry;
    ds_tree_node_t node_vif;
    struct osw_hwaddr link_addr;
    struct osw_hwaddr mld_addr;
    bool connected;
    time_t connected_at;
    void *ies;
    size_t ies_len;
    osw_sta_assoc_vif_t *vif;
    osw_sta_assoc_entry_t *entry;
};

struct osw_sta_assoc_entry
{
    ds_tree_node_t node;
    osw_sta_assoc_t *m;
    ds_tree_t observers;
    ds_tree_t link_infos;
    struct osw_hwaddr sta_addr;
    osw_sta_assoc_links_t active;
    osw_sta_assoc_links_t stale;
    size_t num_links;
    size_t num_stale;
    struct osw_timer settle;
    struct osw_timer deadline;
    void *ies;
    size_t ies_len;
    time_t connected_at;
    bool is_mlo;
    bool is_notifying;
};

struct osw_sta_assoc_params
{
    int settle_msec;
    int deadline_msec;
};

struct osw_sta_assoc
{
    ds_tree_t vifs;
    ds_tree_t entries;
    osw_sta_assoc_params_t params;
    struct osw_state_observer state_obs;
    bool busy;
};

static const struct osw_hwaddr *osw_sta_assoc_state_to_mld_addr(const struct osw_drv_sta_state *s)
{
    if (s->connected == false) return NULL;
    if (osw_hwaddr_is_zero(&s->mld_addr)) return NULL;
    return &s->mld_addr;
}

static bool osw_sta_assoc_ies_changed(const void *a, size_t a_len, const void *b, size_t b_len)
{
    if (a_len != b_len) return true;
    if (a == NULL && b == NULL) return false;
    if ((a == NULL) || (b == NULL) || (memcmp(a, b, a_len) != 0)) return true;
    return false;
}

static void osw_sta_assoc_ies_assign(void **dest, size_t *dest_len, const void *src, size_t src_len)
{
    void *old = *dest;
    *dest = (src && src_len) ? MEMNDUP(src, src_len) : NULL;
    *dest_len = (src && src_len) ? src_len : 0;
    FREE(old);
}

osw_sta_assoc_observer_params_t *osw_sta_assoc_observer_params_alloc(void)
{
    osw_sta_assoc_observer_params_t *p = CALLOC(1, sizeof(*p));
    return p;
}

void osw_sta_assoc_observer_params_drop(osw_sta_assoc_observer_params_t *p)
{
    if (p == NULL) return;
    FREE(p);
}

void osw_sta_assoc_observer_params_set_changed_fn(
        osw_sta_assoc_observer_params_t *p,
        osw_sta_assoc_observer_notify_fn_t *fn,
        void *priv)
{
    if (p == NULL) return;
    p->notify_fn = fn;
    p->notify_fn_priv = priv;
}

void osw_sta_assoc_observer_params_set_addr(osw_sta_assoc_observer_params_t *p, const struct osw_hwaddr *sta_addr)
{
    if (p == NULL) return;
    p->sta_addr = *(sta_addr ?: osw_hwaddr_zero());
}

static void osw_sta_assoc_entry_drop(osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return;
    if (WARN_ON(entry->m == NULL)) return;
    LOGT(LOG_PREFIX_ENTRY(entry, "dropping"));
    ds_tree_remove(&entry->m->entries, entry);
    osw_timer_disarm(&entry->settle);
    osw_timer_disarm(&entry->deadline);
    entry->m = NULL;
    FREE(entry->ies);
    FREE(entry);
}

static bool osw_sta_assoc_global_observers_present(osw_sta_assoc_t *m)
{
    osw_sta_assoc_entry_t *entry = ds_tree_find(&m->entries, osw_hwaddr_zero());
    if (entry == NULL) return false;
    return ds_tree_len(&entry->observers) > 0;
}

static bool osw_sta_assoc_entry_settling(osw_sta_assoc_entry_t *entry)
{
    if (entry->active.count > 0) return true;
    if (osw_timer_is_armed(&entry->deadline)) return true;
    if (osw_timer_is_armed(&entry->settle)) return true;
    return false;
}

static void osw_sta_assoc_entry_gc(osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return;
    if (WARN_ON(entry->m == NULL)) return;
    if (entry->is_notifying) return;
    if (ds_tree_is_empty(&entry->link_infos) == false) return;
    if (ds_tree_is_empty(&entry->observers) == false) return;
    if (osw_sta_assoc_global_observers_present(entry->m) && osw_sta_assoc_entry_settling(entry)) return;
    osw_sta_assoc_entry_drop(entry);
}

static void osw_sta_assoc_entry_schedule_recalc(osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return;

    if (osw_timer_is_armed(&entry->deadline) == false)
    {
        LOGT(LOG_PREFIX_ENTRY(entry, "schedule: deadline"));
        const uint64_t at = osw_time_mono_clk() + OSW_TIME_MSEC(entry->m->params.deadline_msec);
        osw_timer_arm_at_nsec(&entry->deadline, at);
    }

    LOGT(LOG_PREFIX_ENTRY(entry, "schedule: settle"));
    const uint64_t at = osw_time_mono_clk() + OSW_TIME_MSEC(entry->m->params.settle_msec);
    osw_timer_arm_at_nsec(&entry->settle, at);
}

static osw_sta_assoc_link_info_t *osw_sta_assoc_link_info_alloc(
        osw_sta_assoc_vif_t *vif,
        const struct osw_hwaddr *link_addr)
{
    osw_sta_assoc_link_info_t *link = CALLOC(1, sizeof(*link));
    link->vif = vif;
    link->link_addr = *link_addr;
    ds_tree_insert(&vif->link_infos, link, &link->link_addr);
    LOGD(LOG_PREFIX_LINK(link, "allocated"));
    return link;
}

static osw_sta_assoc_link_info_t *osw_sta_assoc_link_info_get(
        osw_sta_assoc_vif_t *vif,
        const struct osw_hwaddr *link_addr)
{
    if (WARN_ON(vif == NULL)) return NULL;
    if (WARN_ON(link_addr == NULL)) return NULL;

    return ds_tree_find(&vif->link_infos, link_addr) ?: osw_sta_assoc_link_info_alloc(vif, link_addr);
}

static void osw_sta_assoc_link_info_detach(osw_sta_assoc_link_info_t *link)
{
    if (link->entry == NULL) return;
    LOGD(LOG_PREFIX_LINK(link, "detaching from " OSW_HWADDR_FMT, OSW_HWADDR_ARG(&link->entry->sta_addr)));
    ds_tree_remove(&link->entry->link_infos, link);
    osw_sta_assoc_entry_schedule_recalc(link->entry);
    osw_sta_assoc_entry_gc(link->entry);
    link->entry = NULL;
}

static void osw_sta_assoc_link_info_drop(osw_sta_assoc_link_info_t *link)
{
    if (link == NULL) return;
    if (WARN_ON(link->entry != NULL)) return;
    LOGD(LOG_PREFIX_LINK(link, "dropping"));
    ds_tree_remove(&link->vif->link_infos, link);
    FREE(link->ies);
    FREE(link);
}

static void osw_sta_assoc_link_info_attach(osw_sta_assoc_link_info_t *link, osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return;
    LOGD(LOG_PREFIX_LINK(link, "attaching to " OSW_HWADDR_FMT, OSW_HWADDR_ARG(&entry->sta_addr)));
    osw_sta_assoc_entry_schedule_recalc(entry);
    ds_tree_insert(&entry->link_infos, link, link);
    link->entry = entry;
}

static bool osw_sta_assoc_entry_link_array_contains(
        const osw_sta_assoc_link_t *links,
        size_t num_links,
        const osw_sta_assoc_link_t *link)
{
    size_t i;
    for (i = 0; i < num_links; i++)
    {
        if (osw_hwaddr_is_equal(&links[i].local_sta_addr, &link->local_sta_addr)
            && osw_hwaddr_is_equal(&links[i].remote_sta_addr, &link->remote_sta_addr))
            return true;
    }
    return false;
}

static bool osw_sta_assoc_entry_links_changed(
        osw_sta_assoc_entry_t *entry,
        const osw_sta_assoc_link_t *links,
        size_t num_links)
{
    if (entry->active.count != num_links) return true;

    size_t i;
    for (i = 0; i < num_links; i++)
    {
        if (osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &links[i]) == false)
            return true;
    }

    return false;
}

static bool osw_sta_assoc_entry_stale_changed(
        osw_sta_assoc_entry_t *entry,
        const osw_sta_assoc_link_t *stale,
        size_t num_stale)
{
    if (entry->stale.count != num_stale) return true;

    size_t i;
    for (i = 0; i < num_stale; i++)
    {
        if (osw_sta_assoc_entry_link_array_contains(entry->stale.links, entry->stale.count, &stale[i]) == false)
            return true;
    }

    return false;
}

static void osw_sta_assoc_observer_notify(
        osw_sta_assoc_observer_t *o,
        osw_sta_assoc_entry_t *entry,
        osw_sta_assoc_event_e ev)
{
    if (o == NULL) return;
    if (o->p.notify_fn == NULL) return;
    entry->is_notifying = true;
    o->p.notify_fn(o->p.notify_fn_priv, entry, ev);
    entry->is_notifying = false;
}

const char *osw_sta_assoc_event_to_cstr(osw_sta_assoc_event_e ev)
{
    switch (ev)
    {
        case OSW_STA_ASSOC_CONNECTED:
            return "connected";
        case OSW_STA_ASSOC_DISCONNECTED:
            return "disconnected";
        case OSW_STA_ASSOC_RECONNECTED:
            return "reconnected";
        case OSW_STA_ASSOC_UNDEFINED:
            return "undefined";
    }
    return "?";
}

static void osw_sta_assoc_entry_notify_global_observers(
        osw_sta_assoc_t *m,
        osw_sta_assoc_entry_t *entry,
        osw_sta_assoc_event_e ev)
{
    osw_sta_assoc_entry_t *assoc0 = ds_tree_find(&m->entries, osw_hwaddr_zero());
    if (assoc0 == NULL) return;
    osw_sta_assoc_observer_t *o;
    osw_sta_assoc_observer_t *tmp;
    ds_tree_foreach_safe (&assoc0->observers, o, tmp)
    {
        osw_sta_assoc_observer_notify(o, entry, ev);
    }
}

static void osw_sta_assoc_entry_notify_observers(osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    LOGT(LOG_PREFIX_ENTRY(entry, "notify: %s", osw_sta_assoc_event_to_cstr(ev)));
    osw_sta_assoc_observer_t *o;
    osw_sta_assoc_observer_t *tmp;
    ds_tree_foreach_safe (&entry->observers, o, tmp)
    {
        osw_sta_assoc_observer_notify(o, entry, ev);
    }
    osw_sta_assoc_entry_notify_global_observers(entry->m, entry, ev);
}

static const osw_sta_assoc_link_info_t *osw_sta_assoc_entry_newest_link_info(const osw_sta_assoc_entry_t *entry)
{
    osw_sta_assoc_link_info_t *newest = NULL;
    osw_sta_assoc_link_info_t *link;
    /* The `entry` is const. This foreach promises to not modify anything. */
    ds_tree_foreach ((ds_tree_t *)&entry->link_infos, link)
    {
        if (osw_hwaddr_is_zero(&link->vif->mac_addr)) continue;
        if (newest == NULL) newest = link;
        if (link->connected_at > newest->connected_at) newest = link;
    }
    return newest;
}

static void osw_sta_assoc_entry_log_infos(osw_sta_assoc_entry_t *entry)
{
    osw_sta_assoc_link_info_t *link;
    ds_tree_foreach (&entry->link_infos, link)
    {
        LOGD(LOG_PREFIX_ENTRY(
                     entry,
                     "info: " OSW_HWADDR_FMT " @ " OSW_HWADDR_FMT " connected_at=%" PRIu64
                     " remote_mld_addr=" OSW_HWADDR_FMT " local_mld_addr=" OSW_HWADDR_FMT),
             OSW_HWADDR_ARG(&link->link_addr),
             OSW_HWADDR_ARG(&link->vif->mac_addr),
             (uint64_t)link->connected_at,
             OSW_HWADDR_ARG(&link->mld_addr),
             OSW_HWADDR_ARG(&link->vif->mld_addr));
    }
}

static void osw_sta_assoc_entry_log_links(
        osw_sta_assoc_entry_t *entry,
        const char *prefix,
        const osw_sta_assoc_link_t *links,
        size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
    {
        LOGD(LOG_PREFIX_ENTRY(
                entry,
                "%s: links[%zu/%zu]: " OSW_HWADDR_FMT " @ " OSW_HWADDR_FMT,
                prefix,
                i,
                count,
                OSW_HWADDR_ARG(&links[i].remote_sta_addr),
                OSW_HWADDR_ARG(&links[i].local_sta_addr)));
    }
}

static void osw_sta_assoc_entry_recalc(osw_sta_assoc_entry_t *entry)
{
    if (entry->m->busy && osw_timer_is_armed(&entry->deadline)) return;

    LOGD(LOG_PREFIX_ENTRY(entry, "recalc"));

    osw_timer_disarm(&entry->settle);
    osw_timer_disarm(&entry->deadline);

    osw_sta_assoc_link_t links[OSW_STA_MAX_LINKS];
    osw_sta_assoc_link_t stale[OSW_STA_MAX_LINKS];
    const void *ies = NULL;
    size_t ies_len = 0;
    size_t num_links = 0;
    size_t num_stale = 0;

    osw_sta_assoc_entry_log_infos(entry);

    const osw_sta_assoc_link_info_t *newest = osw_sta_assoc_entry_newest_link_info(entry);
    const bool is_remote_mlo = newest && (osw_hwaddr_is_zero(&newest->mld_addr) == false);
    const bool is_local_mlo = newest && (osw_hwaddr_is_zero(&newest->vif->mld_addr) == false);
    const bool is_mlo = is_remote_mlo && is_local_mlo;

    WARN_ON(is_remote_mlo && !is_local_mlo);

    if (is_mlo)
    {
        osw_sta_assoc_link_info_t *link;
        ds_tree_foreach (&entry->link_infos, link)
        {
            if (osw_hwaddr_is_zero(&link->vif->mac_addr))
            {
                continue;
            }

            const bool same_assoc = osw_hwaddr_is_equal(&link->vif->mld_addr, &newest->vif->mld_addr)
                                    && osw_hwaddr_is_equal(&link->mld_addr, &newest->mld_addr);
            if (same_assoc)
            {
                if (WARN_ON(num_links >= ARRAY_SIZE(links)))
                {
                    break;
                }

                links[num_links].local_sta_addr = link->vif->mac_addr;
                links[num_links].remote_sta_addr = link->link_addr;
                num_links++;
                if (link->ies && link->ies_len)
                {
                    ies = link->ies;
                    ies_len = link->ies_len;
                }
            }
            else
            {
                stale[num_stale].local_sta_addr = link->vif->mac_addr;
                stale[num_stale].remote_sta_addr = link->link_addr;
                num_stale++;
            }
        }
    }
    else if (newest)
    {
        links[0].local_sta_addr = newest->vif->mac_addr;
        links[0].remote_sta_addr = newest->link_addr;
        num_links = 1;
        ies = newest->ies;
        ies_len = newest->ies_len;

        osw_sta_assoc_link_info_t *link;
        ds_tree_foreach (&entry->link_infos, link)
        {
            if (link != newest)
            {
                stale[num_stale].local_sta_addr = link->vif->mac_addr;
                stale[num_stale].remote_sta_addr = link->link_addr;
                num_stale++;
            }
        }
    }

    const bool mlo_changed = entry->is_mlo != is_mlo;
    const bool connected_at_changed = newest && (newest->connected_at != entry->connected_at);
    const bool links_changed = osw_sta_assoc_entry_links_changed(entry, links, num_links);
    const bool stale_changed = osw_sta_assoc_entry_stale_changed(entry, stale, num_stale);
    const bool ies_changed = osw_sta_assoc_ies_changed(entry->ies, entry->ies_len, ies, ies_len);
    const bool anything_changed = links_changed || stale_changed || ies_changed || connected_at_changed;
    const bool is_connected = links_changed && (entry->active.count == 0) && (num_links > 0);
    const bool is_disconnected = links_changed && (entry->active.count > 0) && (num_links == 0);
    const bool is_reconnected = (links_changed && (entry->active.count > 0) && (num_links > 0)) || connected_at_changed;

    if (mlo_changed)
    {
        LOGD(LOG_PREFIX_ENTRY(entry, "mlo: %d vs %d", entry->is_mlo, is_mlo));
        entry->is_mlo = is_mlo;
    }

    if (links_changed)
    {
        osw_sta_assoc_entry_log_links(entry, "active: old", entry->active.links, entry->active.count);
        osw_sta_assoc_entry_log_links(entry, "active: new", links, num_links);

        memcpy(entry->active.links, links, sizeof(links));
        entry->active.count = num_links;
    }

    if (stale_changed)
    {
        osw_sta_assoc_entry_log_links(entry, "stale: old: ", entry->stale.links, entry->stale.count);
        osw_sta_assoc_entry_log_links(entry, "stale: new: ", stale, num_stale);

        memcpy(entry->stale.links, stale, sizeof(stale));
        entry->stale.count = num_stale;
    }

    if (ies_changed)
    {
        LOGD(LOG_PREFIX_ENTRY(entry, "ies: %zu -> %zu", entry->ies_len, ies_len));

        FREE(entry->ies);
        entry->ies = (ies && ies_len) ? MEMNDUP(ies, ies_len) : NULL;
        entry->ies_len = ies_len;
    }

    if (connected_at_changed)
    {
        LOGD(LOG_PREFIX_ENTRY(
                entry,
                "connected_at: %" PRIu64 " -> %" PRIu64,
                (uint64_t)entry->connected_at,
                (uint64_t)newest->connected_at));

        assert(newest != NULL);
        entry->connected_at = newest->connected_at;
    }

    if (anything_changed)
    {
        const osw_sta_assoc_event_e ev = is_connected      ? OSW_STA_ASSOC_CONNECTED
                                         : is_reconnected  ? OSW_STA_ASSOC_RECONNECTED
                                         : is_disconnected ? OSW_STA_ASSOC_DISCONNECTED
                                                           : OSW_STA_ASSOC_UNDEFINED;

        LOGD(LOG_PREFIX_ENTRY(entry, "event: %s", osw_sta_assoc_event_to_cstr(ev)));
        osw_sta_assoc_entry_notify_observers(entry, ev);
    }

    osw_sta_assoc_entry_gc(entry);
}

static void osw_sta_assoc_entry_deadline_cb(struct osw_timer *t)
{
    osw_sta_assoc_entry_t *entry = container_of(t, typeof(*entry), deadline);
    osw_sta_assoc_entry_recalc(entry);
}

static void osw_sta_assoc_entry_settle_cb(struct osw_timer *t)
{
    osw_sta_assoc_entry_t *entry = container_of(t, typeof(*entry), settle);
    osw_sta_assoc_entry_recalc(entry);
}

static osw_sta_assoc_entry_t *osw_sta_assoc_entry_get(osw_sta_assoc_t *m, const struct osw_hwaddr *sta_addr)
{
    if (m == NULL) return NULL;
    if (sta_addr == NULL) return NULL;

    osw_sta_assoc_entry_t *entry = ds_tree_find(&m->entries, sta_addr);
    if (entry == NULL)
    {
        entry = CALLOC(1, sizeof(*entry));
        entry->m = m;
        entry->sta_addr = *sta_addr;
        ds_tree_init(&entry->observers, ds_void_cmp, osw_sta_assoc_observer_t, node_entry);
        ds_tree_init(&entry->link_infos, ds_void_cmp, osw_sta_assoc_link_info_t, node_entry);
        osw_timer_init(&entry->settle, osw_sta_assoc_entry_settle_cb);
        osw_timer_init(&entry->deadline, osw_sta_assoc_entry_deadline_cb);
        ds_tree_insert(&m->entries, entry, &entry->sta_addr);
    }
    return entry;
}

static void osw_sta_assoc_entry_attach_observer(osw_sta_assoc_entry_t *entry, osw_sta_assoc_observer_t *o)
{
    if (WARN_ON(entry == NULL)) return;
    if (WARN_ON(o == NULL)) return;
    if (WARN_ON(o->entry != NULL)) return;
    o->entry = entry;
    ds_tree_insert(&entry->observers, o, o);
    if (entry->active.count > 0)
    {
        const osw_sta_assoc_event_e initial_ev = OSW_STA_ASSOC_CONNECTED;
        osw_sta_assoc_observer_notify(o, entry, initial_ev);
    }
}

static void osw_sta_assoc_observer_attach_global(osw_sta_assoc_t *m, osw_sta_assoc_observer_t *o)
{
    const osw_sta_assoc_event_e initial_ev = OSW_STA_ASSOC_CONNECTED;
    osw_sta_assoc_entry_t *entry;
    ds_tree_foreach (&m->entries, entry)
    {
        if (entry->active.count == 0) continue;
        if (osw_hwaddr_is_zero(&entry->sta_addr)) continue;
        osw_sta_assoc_observer_notify(o, entry, initial_ev);
    }
}

static void osw_sta_assoc_entry_detach_observer(osw_sta_assoc_entry_t *entry, osw_sta_assoc_observer_t *o)
{
    if (WARN_ON(entry == NULL)) return;
    if (WARN_ON(o == NULL)) return;
    if (WARN_ON(o->entry != entry)) return;
    o->entry = NULL;
    ds_tree_remove(&entry->observers, o);
    osw_sta_assoc_entry_gc(entry);
}

osw_sta_assoc_observer_t *osw_sta_assoc_observer_alloc(osw_sta_assoc_t *m, osw_sta_assoc_observer_params_t *p)
{
    if (m == NULL) goto err;
    if (p == NULL) goto err;

    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(m, &p->sta_addr);
    osw_sta_assoc_observer_t *o = CALLOC(1, sizeof(*o));
    o->p = *p;
    osw_sta_assoc_observer_params_drop(p);
    osw_sta_assoc_entry_attach_observer(entry, o);
    if (osw_hwaddr_is_zero(&entry->sta_addr))
    {
        osw_sta_assoc_observer_attach_global(m, o);
    }

    return o;
err:
    osw_sta_assoc_observer_params_drop(p);
    return NULL;
}

void osw_sta_assoc_observer_drop(osw_sta_assoc_observer_t *o)
{
    if (o == NULL) return;
    if (WARN_ON(o->entry == NULL)) return;

    osw_sta_assoc_entry_detach_observer(o->entry, o);
    FREE(o);
}

const osw_sta_assoc_entry_t *osw_sta_assoc_observer_get_entry(osw_sta_assoc_observer_t *o)
{
    if (o == NULL) return NULL;
    return o->entry;
}

bool osw_sta_assoc_entry_is_connected(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return false;
    if (entry->active.count == 0) return false;
    return true;
}

bool osw_sta_assoc_entry_is_mlo(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return false;
    return entry->is_mlo;
}

size_t osw_sta_assoc_entry_get_assoc_ies_len(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return 0;
    return entry->ies_len;
}

const void *osw_sta_assoc_entry_get_assoc_ies_data(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return NULL;
    if (entry->ies == NULL) return NULL;
    if (entry->ies_len == 0) return NULL;
    return entry->ies;
}

const struct osw_hwaddr *osw_sta_assoc_entry_get_addr(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return NULL;
    return &entry->sta_addr;
}

const struct osw_hwaddr *osw_sta_assoc_entry_get_local_mld_addr(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return NULL;
    const osw_sta_assoc_link_info_t *newest = osw_sta_assoc_entry_newest_link_info(entry);
    if (newest == NULL) return NULL;
    if (osw_hwaddr_is_zero(&newest->vif->mld_addr)) return NULL;
    return &newest->vif->mld_addr;
}

const osw_sta_assoc_links_t *osw_sta_assoc_entry_get_active_links(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return NULL;
    return &entry->active;
}

const osw_sta_assoc_links_t *osw_sta_assoc_entry_get_stale_links(const osw_sta_assoc_entry_t *entry)
{
    if (entry == NULL) return NULL;
    return &entry->stale;
}

const osw_sta_assoc_link_t *osw_sta_assoc_links_lookup(
        const osw_sta_assoc_links_t *l,
        const struct osw_hwaddr *local_sta_addr,
        const struct osw_hwaddr *remote_sta_addr)
{
    if (l == NULL) return NULL;
    size_t i;
    const bool local_wildcard = (local_sta_addr == NULL);
    const bool remote_wildcard = (remote_sta_addr == NULL);
    for (i = 0; i < l->count; i++)
    {
        const osw_sta_assoc_link_t *link = &l->links[i];
        const bool local_match = local_wildcard || osw_hwaddr_is_equal(local_sta_addr, &link->local_sta_addr);
        const bool remote_match = remote_wildcard || osw_hwaddr_is_equal(remote_sta_addr, &link->remote_sta_addr);
        if (local_match && remote_match)
        {
            return link;
        }
    }
    return NULL;
}

void osw_sta_assoc_links_append_local_to(const osw_sta_assoc_links_t *l, struct osw_hwaddr_list *list)
{
    if (l == NULL) return;
    size_t i;
    for (i = 0; i < l->count; i++)
    {
        osw_hwaddr_list_append(list, &l->links[i].local_sta_addr);
    }
}

void osw_sta_assoc_links_append_remote_to(const osw_sta_assoc_links_t *l, struct osw_hwaddr_list *list)
{
    if (l == NULL) return;
    size_t i;
    for (i = 0; i < l->count; i++)
    {
        osw_hwaddr_list_append(list, &l->links[i].remote_sta_addr);
    }
}

static void osw_sta_assoc_link_info_gc(osw_sta_assoc_link_info_t *link)
{
    if (link == NULL) return;
    if (link->entry != NULL) return;
    if (link->connected) return;
    osw_sta_assoc_link_info_drop(link);
}

static void osw_sta_assoc_link_info_recalc(osw_sta_assoc_link_info_t *link)
{
    const struct osw_hwaddr *addr = osw_hwaddr_first_nonzero(&link->mld_addr, &link->link_addr);
    osw_sta_assoc_t *m = link->vif->m;
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(m, addr);
    osw_sta_assoc_entry_t *old_e = link->entry;
    osw_sta_assoc_entry_t *new_e = link->connected ? entry : NULL;

    if (old_e != new_e)
    {
        osw_sta_assoc_link_info_detach(link);
        osw_sta_assoc_link_info_attach(link, new_e);
    }
    else
    {
        osw_sta_assoc_entry_schedule_recalc(entry);
    }

    osw_sta_assoc_link_info_gc(link);
}

static osw_sta_assoc_vif_t *osw_sta_assoc_vif_alloc(osw_sta_assoc_t *m, const char *vif_name)
{
    osw_sta_assoc_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->m = m;
    vif->vif_name = STRDUP(vif_name);
    ds_tree_init(&vif->link_infos, (ds_key_cmp_t *)osw_hwaddr_cmp, osw_sta_assoc_link_info_t, node_vif);
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    LOGD(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

static void osw_sta_assoc_vif_drop(osw_sta_assoc_vif_t *vif)
{
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_VIF(vif, "dropping"));
    ds_tree_remove(&vif->m->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static void osw_sta_assoc_vif_gc(osw_sta_assoc_vif_t *vif)
{
    if (vif == NULL) return;
    if (osw_hwaddr_is_zero(&vif->mac_addr) == false) return;
    if (ds_tree_is_empty(&vif->link_infos) == false) return;
    osw_sta_assoc_vif_drop(vif);
}

static osw_sta_assoc_vif_t *osw_sta_assoc_vif_get(osw_sta_assoc_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (WARN_ON(vif_name == NULL)) return NULL;
    return ds_tree_find(&m->vifs, vif_name) ?: osw_sta_assoc_vif_alloc(m, vif_name);
}

static void osw_sta_assoc_vif_recalc_infos(osw_sta_assoc_vif_t *vif)
{
    osw_sta_assoc_link_info_t *link;
    osw_sta_assoc_link_info_t *tmp;
    ds_tree_foreach_safe (&vif->link_infos, link, tmp)
    {
        LOGD(LOG_PREFIX_VIF(vif, "updating " OSW_HWADDR_FMT, OSW_HWADDR_ARG(&link->link_addr)));
        osw_sta_assoc_link_info_recalc(link);
    }
}

static void osw_sta_assoc_vif_set_mac_addr(osw_sta_assoc_vif_t *vif, const struct osw_hwaddr *mac_addr)
{
    if (vif == NULL) return;
    if (osw_hwaddr_is_equal(&vif->mac_addr, mac_addr)) return;

    LOGD(LOG_PREFIX_VIF(
            vif,
            "mac_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&vif->mac_addr),
            OSW_HWADDR_ARG(mac_addr)));

    vif->mac_addr = *mac_addr;
    osw_sta_assoc_vif_recalc_infos(vif);
}

static void osw_sta_assoc_vif_set_mld_addr(osw_sta_assoc_vif_t *vif, const struct osw_hwaddr *mld_addr)
{
    if (vif == NULL) return;
    if (osw_hwaddr_is_equal(&vif->mld_addr, mld_addr)) return;

    LOGD(LOG_PREFIX_VIF(
            vif,
            "mld_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&vif->mld_addr),
            OSW_HWADDR_ARG(mld_addr)));

    vif->mld_addr = *mld_addr;
    osw_sta_assoc_vif_recalc_infos(vif);
}

static void osw_sta_assoc_update(
        osw_sta_assoc_vif_t *vif,
        const struct osw_hwaddr *link_addr,
        const struct osw_hwaddr *mld_addr,
        const void *ies,
        size_t ies_len,
        bool connected,
        time_t connected_at)
{
    osw_sta_assoc_link_info_t *link = osw_sta_assoc_link_info_get(vif, link_addr);
    bool changed = false;

    LOGT(LOG_PREFIX_LINK(link, "updating"));

    if (osw_hwaddr_is_equal(&link->mld_addr, mld_addr) == false)
    {
        LOGD(LOG_PREFIX_LINK(
                link,
                "mld_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
                OSW_HWADDR_ARG(&link->mld_addr),
                OSW_HWADDR_ARG(mld_addr)));
        link->mld_addr = *mld_addr;
        changed = true;
    }

    if (link->connected != connected)
    {
        LOGD(LOG_PREFIX_LINK(link, "connected: %d -> %d", link->connected, connected));
        link->connected = connected;
        changed = true;
    }

    if (link->connected_at != connected_at)
    {
        LOGD(LOG_PREFIX_LINK(
                link,
                "connected_at: %" PRIu64 " -> %" PRIu64,
                (uint64_t)link->connected_at,
                (uint64_t)connected_at));
        link->connected_at = connected_at;
        changed = true;
    }

    if (osw_sta_assoc_ies_changed(link->ies, link->ies_len, ies, ies_len))
    {
        LOGD(LOG_PREFIX_LINK(link, "ies_len: %zd -> %zd", link->ies_len, ies_len));
        osw_sta_assoc_ies_assign(&link->ies, &link->ies_len, ies, ies_len);
        changed = true;
    }

    if (changed)
    {
        osw_sta_assoc_link_info_recalc(link);
    }
}

static void osw_sta_assoc_update_cb(struct osw_state_observer *obs, const struct osw_state_sta_info *info)
{
    osw_sta_assoc_t *m = state_obs_to_m(obs);
    const char *vif_name = info->vif->vif_name;
    osw_sta_assoc_vif_t *vif = osw_sta_assoc_vif_get(m, vif_name);
    const struct osw_hwaddr *link_addr = info->mac_addr;
    const struct osw_hwaddr *mld_addr = osw_sta_assoc_state_to_mld_addr(info->drv_state) ?: osw_hwaddr_zero();
    const void *ies = (void *)info->assoc_req_ies;
    const size_t ies_len = info->assoc_req_ies_len;
    const bool connected = info->drv_state->connected;
    const time_t connected_at = info->connected_at;
    osw_sta_assoc_update(vif, link_addr, mld_addr, ies, ies_len, connected, connected_at);
    osw_sta_assoc_vif_gc(vif);
}

static void osw_sta_assoc_vif_update(
        osw_sta_assoc_t *m,
        const char *vif_name,
        const struct osw_hwaddr *mac_addr,
        const struct osw_hwaddr *mld_addr)
{
    if (WARN_ON(m == NULL)) return;
    if (WARN_ON(vif_name == NULL)) return;
    if (WARN_ON(mac_addr == NULL)) return;
    osw_sta_assoc_vif_t *vif = osw_sta_assoc_vif_get(m, vif_name);
    osw_sta_assoc_vif_set_mac_addr(vif, mac_addr);
    osw_sta_assoc_vif_set_mld_addr(vif, mld_addr);
    osw_sta_assoc_vif_gc(vif);
}

static void osw_sta_assoc_vif_update_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *info)
{
    osw_sta_assoc_t *m = state_obs_to_m(obs);
    const char *vif_name = info->vif_name;
    const bool valid = (info->drv_state->exists && info->drv_state->status == OSW_VIF_ENABLED);
    const struct osw_drv_mld_state *mld_state = osw_drv_vif_state_get_mld_state(info->drv_state);
    const struct osw_hwaddr *mac_addr = valid ? &info->drv_state->mac_addr : osw_hwaddr_zero();
    const struct osw_hwaddr *mld_addr = valid && mld_state ? &mld_state->addr : osw_hwaddr_zero();
    osw_sta_assoc_vif_update(m, vif_name, mac_addr, mld_addr);
}

static void osw_sta_assoc_idle_cb(struct osw_state_observer *obs)
{
    osw_sta_assoc_t *m = state_obs_to_m(obs);
    osw_sta_assoc_entry_t *entry;
    ds_tree_foreach (&m->entries, entry)
    {
        if (osw_timer_is_armed(&entry->deadline))
        {
            osw_sta_assoc_entry_schedule_recalc(entry);
        }
    }
    m->busy = false;
}

static void osw_sta_assoc_busy_cb(struct osw_state_observer *obs)
{
    osw_sta_assoc_t *m = state_obs_to_m(obs);
    m->busy = true;
}

static void osw_sta_assoc_init(osw_sta_assoc_t *m)
{
    const struct osw_state_observer state_obs = {
        .idle_fn = osw_sta_assoc_idle_cb,
        .busy_fn = osw_sta_assoc_busy_cb,
        .sta_connected_fn = osw_sta_assoc_update_cb,
        .sta_changed_fn = osw_sta_assoc_update_cb,
        .sta_disconnected_fn = osw_sta_assoc_update_cb,
        .vif_added_fn = osw_sta_assoc_vif_update_cb,
        .vif_removed_fn = osw_sta_assoc_vif_update_cb,
        .vif_changed_fn = osw_sta_assoc_vif_update_cb,
    };
    m->state_obs = state_obs;
    m->params.settle_msec = OSW_STA_SETTLE_MSEC;
    m->params.deadline_msec = OSW_STA_DEADLINE_MSEC;
    ds_tree_init(&m->vifs, ds_str_cmp, osw_sta_assoc_vif_t, node_m);
    ds_tree_init(&m->entries, (ds_key_cmp_t *)osw_hwaddr_cmp, osw_sta_assoc_entry_t, node);
}

static void osw_sta_assoc_attach(osw_sta_assoc_t *m)
{
    OSW_MODULE_LOAD(osw_state);
    osw_state_register_observer(&m->state_obs);
}

OSW_MODULE(osw_sta_assoc)
{
    static osw_sta_assoc_t m;
    osw_sta_assoc_init(&m);
    osw_sta_assoc_attach(&m);
    return &m;
}

static void osw_sta_assoc_test_counter_cb(void *priv, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    int *counter = priv;
    counter[ev]++;
}

OSW_UT(osw_sta_assoc_test_mlo)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    char ies1[] = "hello";
    const size_t ies1_len = strlen(ies1) + 1;
    char ies2[] = "foo";
    const size_t ies2_len = strlen(ies2) + 1;
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr sta2 = {.octet = {0, 0, 0, 0, 0, 2}};
    const struct osw_hwaddr sta3 = {.octet = {0, 0, 0, 0, 0, 3}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};
    const struct osw_hwaddr ap3 = {.octet = {0, 0, 0, 0, 1, 3}};
    const struct osw_hwaddr mld1 = {.octet = {1, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr mld2 = {.octet = {1, 0, 0, 0, 1, 1}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};
    const osw_sta_assoc_link_t link2 = {.remote_sta_addr = sta2, .local_sta_addr = ap2};
    const osw_sta_assoc_link_t link3 = {.remote_sta_addr = sta3, .local_sta_addr = ap3};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &mld2);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &mld2);
    osw_sta_assoc_vif_update(&m, "ap3", &ap3, &mld2);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");
    osw_sta_assoc_vif_t *vif3 = ds_tree_find(&m.vifs, "ap3");

    /* connect on 1+2 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &mld1, NULL, 0, true, 0);
    osw_sta_assoc_update(vif2, &sta2, &mld1, ies1, ies1_len, true, 0);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &mld1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 2);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link2));
    assert(entry->ies_len == ies1_len);
    assert(memcmp(entry->ies, ies1, ies1_len) == 0);

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_params_set_addr(p, &entry->sta_addr);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p);
    p = NULL;
    assert(o != NULL);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == true);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == true);
    assert(osw_sta_assoc_entry_get_assoc_ies_len(o->entry) == ies1_len);
    assert(o->entry->active.count == 2);
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link1.local_sta_addr, &link1.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link2.local_sta_addr, &link2.remote_sta_addr));

    /* roam to 3 */
    osw_sta_assoc_update(vif3, &sta3, &mld1, ies2, ies2_len, true, 0);
    osw_sta_assoc_update(vif1, &sta1, &mld1, NULL, 0, false, 0);
    osw_sta_assoc_update(vif2, &sta2, &mld1, NULL, 0, false, 0);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link3));
    assert(entry->ies_len == ies2_len);
    assert(memcmp(entry->ies, ies2, ies2_len) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == true);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == true);
    assert(osw_sta_assoc_entry_get_assoc_ies_len(o->entry) == ies2_len);
    assert(o->entry->active.count == 1);
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link3.local_sta_addr, &link3.remote_sta_addr));

    /* disconnect */
    entry = NULL;
    osw_sta_assoc_update(vif3, &sta3, &mld1, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(ds_tree_len(&m.entries) == 1); /* held by observer */
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 1);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == false);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == false);
    assert(o->entry->active.count == 0);

    /* drop last ref to entry */
    osw_sta_assoc_observer_drop(o);
    assert(ds_tree_len(&m.entries) == 0);
}

OSW_UT(osw_sta_assoc_test_mlo_cross)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr sta2 = {.octet = {0, 0, 0, 0, 0, 2}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};
    const struct osw_hwaddr ap3 = {.octet = {0, 0, 0, 0, 1, 3}};
    const struct osw_hwaddr ap4 = {.octet = {0, 0, 0, 0, 1, 4}};
    const struct osw_hwaddr mld1 = {.octet = {0, 0, 0, 1, 0, 1}};
    const struct osw_hwaddr mld2 = {.octet = {0, 0, 0, 1, 0, 2}};
    const struct osw_hwaddr mld3 = {.octet = {0, 0, 0, 1, 0, 3}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};
    const osw_sta_assoc_link_t link2 = {.remote_sta_addr = sta2, .local_sta_addr = ap2};
    const osw_sta_assoc_link_t link3 = {.remote_sta_addr = sta1, .local_sta_addr = ap3};
    const osw_sta_assoc_link_t link4 = {.remote_sta_addr = sta2, .local_sta_addr = ap4};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &mld2);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &mld2);
    osw_sta_assoc_vif_update(&m, "ap3", &ap3, &mld3);
    osw_sta_assoc_vif_update(&m, "ap4", &ap4, &mld3);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");
    osw_sta_assoc_vif_t *vif3 = ds_tree_find(&m.vifs, "ap3");
    osw_sta_assoc_vif_t *vif4 = ds_tree_find(&m.vifs, "ap4");

    /* connect on 1+2 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &mld1, NULL, 0, true, 0);
    osw_sta_assoc_update(vif2, &sta2, &mld1, NULL, 0, true, 0);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &mld1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 2);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link2));

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_params_set_addr(p, &entry->sta_addr);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p);
    p = NULL;
    assert(o != NULL);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == true);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == true);
    assert(o->entry->active.count == 2);
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link1.local_sta_addr, &link1.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link2.local_sta_addr, &link2.remote_sta_addr));

    /* roam to 3+4 without disconnecting */
    osw_sta_assoc_update(vif3, &sta1, &mld1, NULL, 0, true, 0); /* old timestamp intentionally */
    osw_sta_assoc_update(vif4, &sta2, &mld1, NULL, 0, true, 10);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 2);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link3));
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link4));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == true);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == true);
    assert(o->entry->stale.count == 2);
    assert(o->entry->active.count == 2);
    assert(osw_sta_assoc_links_lookup(&o->entry->stale, &link1.local_sta_addr, &link1.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->stale, &link2.local_sta_addr, &link2.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link3.local_sta_addr, &link3.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link4.local_sta_addr, &link4.remote_sta_addr));

    /* disconnect the stale 1+2 */
    osw_sta_assoc_update(vif1, &sta1, &mld1, NULL, 0, false, 0);
    osw_sta_assoc_update(vif2, &sta2, &mld1, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(ds_tree_len(&m.entries) == 1);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 1);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);
    assert(osw_sta_assoc_entry_is_connected(o->entry) == true);
    assert(osw_sta_assoc_entry_is_mlo(o->entry) == true);
    assert(o->entry->stale.count == 0);
    assert(o->entry->active.count == 2);
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link3.local_sta_addr, &link3.remote_sta_addr));
    assert(osw_sta_assoc_links_lookup(&o->entry->active, &link4.local_sta_addr, &link4.remote_sta_addr));
}

OSW_UT(osw_sta_assoc_test_nonmlo_overlap)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    char ies1[] = "hello";
    const size_t ies1_len = strlen(ies1) + 1;
    char ies2[] = "world";
    const size_t ies2_len = strlen(ies2) + 1;
    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};
    const osw_sta_assoc_link_t link2 = {.remote_sta_addr = sta1, .local_sta_addr = ap2};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &nonmld);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");

    /* connect on 1 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, ies1, ies1_len, true, 10);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &sta1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(entry->stale.count == 0);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(entry->ies_len == ies1_len);
    assert(memcmp(entry->ies, ies1, ies1_len) == 0);

    /* roam to 2, without disconnecting 1 */
    osw_sta_assoc_update(vif2, &sta1, &nonmld, ies2, ies2_len, true, 100);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(entry->stale.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link2));
    assert(osw_sta_assoc_entry_link_array_contains(entry->stale.links, entry->stale.count, &link1));
    assert(entry->ies_len == ies2_len);
    assert(memcmp(entry->ies, ies2, ies2_len) == 0);

    /* disconnect */
    entry = NULL;
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, false, 0);
    osw_sta_assoc_update(vif2, &sta1, &nonmld, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(ds_tree_len(&m.entries) == 0);
}

OSW_UT(osw_sta_assoc_test_nonmlo_overlap_stale_disappear)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    char ies1[] = "hello";
    const size_t ies1_len = strlen(ies1) + 1;
    char ies2[] = "world";
    const size_t ies2_len = strlen(ies2) + 1;
    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};
    const osw_sta_assoc_link_t link2 = {.remote_sta_addr = sta1, .local_sta_addr = ap2};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &nonmld);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_params_set_addr(p, &sta1);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* connect on 1 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, ies1, ies1_len, true, 10);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &sta1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(entry->ies_len == ies1_len);
    assert(memcmp(entry->ies, ies1, ies1_len) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* roam to 2, without disconnecting 1 */
    osw_sta_assoc_update(vif2, &sta1, &nonmld, ies2, ies2_len, true, 100);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->stale.count == 1);
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->stale.links, entry->stale.count, &link1));
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link2));
    assert(entry->ies_len == ies2_len);
    assert(memcmp(entry->ies, ies2, ies2_len) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* disconnect (stale) link 1 */
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(entry->stale.count == 0);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link2));
    assert(entry->ies_len == ies2_len);
    assert(memcmp(entry->ies, ies2, ies2_len) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 1);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* disconnect link 2 */
    entry = NULL;
    osw_sta_assoc_update(vif2, &sta1, &nonmld, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    osw_sta_assoc_observer_drop(o);
    assert(ds_tree_len(&m.entries) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 1);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 1);
}

OSW_UT(osw_sta_assoc_test_global)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr sta2 = {.octet = {0, 0, 0, 0, 0, 2}};
    const struct osw_hwaddr sta3 = {.octet = {0, 0, 0, 0, 0, 3}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &nonmld);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");

    /* connect on 1 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, true, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* connect 2 and 3 */
    osw_sta_assoc_update(vif2, &sta2, &nonmld, NULL, 0, true, 0);
    osw_sta_assoc_update(vif2, &sta3, &nonmld, NULL, 0, true, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 3);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* roam 3 */
    osw_sta_assoc_update(vif2, &sta3, &nonmld, NULL, 0, false, 0);
    osw_sta_assoc_update(vif1, &sta3, &nonmld, NULL, 0, true, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 3);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* disconnect 1 and 2 */
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, false, 0);
    osw_sta_assoc_update(vif2, &sta2, &nonmld, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 3);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 2);

    /* drop obs, should retain counters */
    assert(ds_tree_len(&m.entries) == 2); /* just global + info3roamed */
    osw_sta_assoc_observer_drop(o);
    o = NULL;
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 3);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 2);
    assert(ds_tree_len(&m.entries) == 1); /* just info3roamed */

    /* disconnect 3 */
    osw_sta_assoc_update(vif1, &sta3, &nonmld, NULL, 0, false, 0);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 3);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 2);
    assert(ds_tree_len(&m.entries) == 0);
}

OSW_UT(osw_sta_assoc_test_reconnect_timestamp)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    char ies1[] = "hello";
    const size_t ies1_len = strlen(ies1) + 1;
    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");

    /* connect on 1 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, ies1, ies1_len, true, 10);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &sta1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(entry->ies_len == ies1_len);
    assert(memcmp(entry->ies, ies1, ies1_len) == 0);

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_params_set_addr(p, &entry->sta_addr);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* reconnect "10s later" */
    osw_sta_assoc_update(vif1, &sta1, &nonmld, ies1, ies1_len, true, 20);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(entry->ies_len == ies1_len);
    assert(memcmp(entry->ies, ies1, ies1_len) == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* disconnect */
    entry = NULL;
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, false, 20);
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(ds_tree_len(&m.entries) == 1); /* held by observer */
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 1);
    osw_sta_assoc_observer_drop(o);
    assert(ds_tree_len(&m.entries) == 0);
}

OSW_UT(osw_sta_assoc_test_vif_reconfig)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr sta2 = {.octet = {0, 0, 0, 0, 0, 2}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const struct osw_hwaddr ap2 = {.octet = {0, 0, 0, 0, 1, 2}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};
    const osw_sta_assoc_link_t link2 = {.remote_sta_addr = sta2, .local_sta_addr = ap2};

    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_sta_assoc_vif_update(&m, "ap2", &ap2, &nonmld);

    osw_sta_assoc_vif_t *vif1 = ds_tree_find(&m.vifs, "ap1");
    osw_sta_assoc_vif_t *vif2 = ds_tree_find(&m.vifs, "ap2");

    /* connect both 1 and 2 */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, true, 0);
    osw_sta_assoc_update(vif2, &sta2, &nonmld, NULL, 0, true, 0);
    osw_sta_assoc_entry_t *assoc1 = osw_sta_assoc_entry_get(&m, &sta1);
    osw_sta_assoc_entry_t *assoc2 = osw_sta_assoc_entry_get(&m, &sta2);
    assert(assoc1 != NULL);
    assert(assoc2 != NULL);
    assert(osw_timer_is_armed(&assoc1->settle));
    assert(osw_timer_is_armed(&assoc1->deadline));
    assert(osw_timer_is_armed(&assoc2->settle));
    assert(osw_timer_is_armed(&assoc2->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&assoc1->settle) == false);
    assert(osw_timer_is_armed(&assoc1->deadline) == false);
    assert(osw_timer_is_armed(&assoc2->settle) == false);
    assert(osw_timer_is_armed(&assoc2->deadline) == false);
    assert(ds_tree_len(&m.entries) == 2);
    assert(assoc1->active.count == 1);
    assert(assoc2->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(assoc1->active.links, assoc1->active.count, &link1));
    assert(osw_sta_assoc_entry_link_array_contains(assoc2->active.links, assoc2->active.count, &link2));

    int counter1[OSW_STA_EVENT_COUNT];
    MEMZERO(counter1);
    osw_sta_assoc_observer_params_t *p1 = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p1, osw_sta_assoc_test_counter_cb, counter1);
    osw_sta_assoc_observer_params_set_addr(p1, &assoc1->sta_addr);
    osw_sta_assoc_observer_t *o1 = osw_sta_assoc_observer_alloc(&m, p1);
    assert(counter1[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter1[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter1[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter1[OSW_STA_ASSOC_DISCONNECTED] == 0);

    int counter2[OSW_STA_EVENT_COUNT];
    MEMZERO(counter2);
    osw_sta_assoc_observer_params_t *p2 = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p2, osw_sta_assoc_test_counter_cb, counter2);
    osw_sta_assoc_observer_params_set_addr(p2, &assoc2->sta_addr);
    osw_sta_assoc_observer_t *o2 = osw_sta_assoc_observer_alloc(&m, p2);
    assert(counter2[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter2[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter2[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter2[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* typical case is for interface to disappear and do a
     * last-breath report with 00s
     */
    osw_sta_assoc_vif_update(&m, "ap1", osw_hwaddr_zero(), &nonmld);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    /* the assoc1 should be disconnected, but assoc2 should remain connected */
    assert(counter1[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter1[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter1[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter1[OSW_STA_ASSOC_DISCONNECTED] == 1);
    assert(counter2[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter2[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter2[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter2[OSW_STA_ASSOC_DISCONNECTED] == 0);

    assert(ds_tree_len(&m.entries) == 2);
    osw_sta_assoc_observer_drop(o1);
    osw_sta_assoc_observer_drop(o2);
    assert(ds_tree_len(&m.entries) == 2);
    assert(ds_tree_find(&m.entries, &sta1) == assoc1);
    assert(ds_tree_find(&m.entries, &sta2) == assoc2);
    assert(assoc1->active.count == 0);
    assert(assoc2->active.count == 1);
    assert(assoc1->stale.count == 0);
    assert(assoc2->stale.count == 0);
}

OSW_UT(osw_sta_assoc_test_vif_config_after_sta)
{
    osw_sta_assoc_t m = {0};
    osw_sta_assoc_init(&m);

    const struct osw_hwaddr nonmld = {};
    const struct osw_hwaddr sta1 = {.octet = {0, 0, 0, 0, 0, 1}};
    const struct osw_hwaddr ap1 = {.octet = {0, 0, 0, 0, 1, 1}};
    const osw_sta_assoc_link_t link1 = {.remote_sta_addr = sta1, .local_sta_addr = ap1};

    osw_sta_assoc_vif_t *vif1 = osw_sta_assoc_vif_get(&m, "ap1");
    /* mac_addr is left as zeros */

    int counter[OSW_STA_EVENT_COUNT];
    MEMZERO(counter);
    osw_sta_assoc_observer_params_t *p1 = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p1, osw_sta_assoc_test_counter_cb, counter);
    osw_sta_assoc_observer_params_set_addr(p1, &sta1);
    osw_sta_assoc_observer_t *o = osw_sta_assoc_observer_alloc(&m, p1);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* connect on misreported ap */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    osw_sta_assoc_update(vif1, &sta1, &nonmld, NULL, 0, true, 0);
    osw_sta_assoc_entry_t *entry = osw_sta_assoc_entry_get(&m, &sta1);
    assert(entry != NULL);
    assert(osw_timer_is_armed(&entry->settle));
    assert(osw_timer_is_armed(&entry->deadline));
    osw_ut_time_advance(OSW_TIME_MSEC(m.params.deadline_msec + 1));
    assert(osw_timer_is_armed(&entry->settle) == false);
    assert(osw_timer_is_armed(&entry->deadline) == false);
    assert(ds_tree_len(&m.entries) == 1);
    assert(entry->active.count == 0);
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    /* report the vif properly now */
    osw_sta_assoc_vif_update(&m, "ap1", &ap1, &nonmld);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    /* the sta1 should now appear as connected */
    assert(entry->active.count == 1);
    assert(osw_sta_assoc_entry_link_array_contains(entry->active.links, entry->active.count, &link1));
    assert(counter[OSW_STA_ASSOC_UNDEFINED] == 0);
    assert(counter[OSW_STA_ASSOC_CONNECTED] == 1);
    assert(counter[OSW_STA_ASSOC_RECONNECTED] == 0);
    assert(counter[OSW_STA_ASSOC_DISCONNECTED] == 0);

    osw_sta_assoc_observer_drop(o);
}
