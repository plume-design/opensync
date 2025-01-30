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
#include <inttypes.h>

#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <os.h>
#include <os_time.h>
#include <osn_netif.h>

#include "sm_lat_core.h"
#include "sm_lat_sys.h"

struct sm_lat_core
{
    ds_tree_t streams; /* sm_lat_core_stream (node) */
    ds_tree_t ifnames; /* sm_lat_core_ifname (node) */
    ds_tree_t vifs;    /* sm_lat_core_vif (node) */
    ds_tree_t mlds;    /* sm_lat_core_mld (node) */
    sm_lat_sys_t *sys;
    sm_lat_sys_poll_t *poll;
    ev_async poll_async;
    struct ev_loop *loop;
    uint32_t dscp_enabled_count;
    uint32_t min_enabled_count;
    uint32_t max_enabled_count;
    uint32_t avg_enabled_count;
    uint32_t num_pkts_enabled_count;
    uint32_t last_enabled_count;
};

struct sm_lat_core_netdev
{
    ds_tree_node_t node;
    char *name;
};
typedef struct sm_lat_core_netdev sm_lat_core_netdev_t;

struct sm_lat_core_ifname
{
    ds_tree_node_t node; /* sm_lat_core_t (ifnames) */
    ds_tree_t refs;      /* sm_lat_core_stream_ifname_t (node_shared) */
    ds_tree_t *netdevs;
    sm_lat_core_t *c;
    char *name;
};
typedef struct sm_lat_core_ifname sm_lat_core_ifname_t;

struct sm_lat_core_mld
{
    ds_tree_node_t node;
    ds_tree_t vifs;
    sm_lat_core_t *c;
    char *name;
};
typedef struct sm_lat_core_mld sm_lat_core_mld_t;

struct sm_lat_core_vif
{
    ds_tree_node_t node;     /* sm_lat_core_t (vifs) */
    ds_tree_node_t node_mld; /* sm_lat_core_mld_t (vifs) */
    sm_lat_core_t *c;
    sm_lat_core_mld_t *mld;
    osn_netif_t *netif;
    bool exists;
    char *name;
};
typedef struct sm_lat_core_vif sm_lat_core_vif_t;

struct sm_lat_core_stream
{
    ds_tree_node_t node; /* sm_lat_core_t (streams) */
    ds_tree_t ifnames;   /* sm_lat_core_stream_ifname_t (node_stream) */
    sm_lat_core_t *c;
    ev_periodic report_periodic;
    ev_periodic poll_periodic;
    ev_async report_async;
    bool poll_pending;
    bool poll_running;
    bool report_pending;
    sm_lat_core_report_fn_t *report_fn;
    void *report_fn_priv;

    uint32_t report_ms;
    uint32_t poll_ms;
    bool dscp_enabled;
    bool min_enabled;
    bool max_enabled;
    bool avg_enabled;
    bool num_pkts_enabled;
    bool last_enabled;
    enum sm_lat_core_sampling sampling;

    /* sys report_fn can be called multiple times
     * for the same host within a single poll
     * period so it's necessary to keep track of
     * current poll hosts, and the previous poll
     * hosts to respect different sampling modes.
     */
    ds_tree_t hosts_open;   /* sm_lat_core_entry_t (node) */
    ds_tree_t hosts_closed; /* sm_lat_core_entry_t (node) */
};

struct sm_lat_core_stream_ifname
{
    ds_tree_node_t node_shared; /* sm_lat_core_ifname_t (refs) */
    ds_tree_node_t node_stream; /* sm_lat_core_stream_t (ifnames) */
    sm_lat_core_stream_t *st;
    sm_lat_core_ifname_t *shared;
};
typedef struct sm_lat_core_stream_ifname sm_lat_core_stream_ifname_t;

struct sm_lat_core_entry
{
    ds_tree_node_t node; /* sm_lat_core_stream_t (hosts_open, hosts_closed) */
    sm_lat_core_stream_t *st;
    ds_tree_t *root; /* sm_lat_core_stream_t (hosts_open, hosts_closed) */
    struct sm_lat_core_host host;
};
typedef struct sm_lat_core_entry sm_lat_core_entry_t;

#define SM_LAT_CORE_REPORT_HOST_MAX 64

#define LOG_PREFIX(fmt, ...) "sm: lat: core: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_IFNAME(i, fmt, ...) LOG_PREFIX("ifname: %s: " fmt, (i)->name, ##__VA_ARGS__)

#define LOG_PREFIX_STREAM(st, fmt, ...) LOG_PREFIX("%p: " fmt, (st), ##__VA_ARGS__)

#define LOG_PREFIX_VIF(st, fmt, ...) \
    LOG_PREFIX("vif: %s (%s): " fmt, (vif)->name, (vif)->mld ? (vif)->mld->name : "?", ##__VA_ARGS__)

#define LOG_PREFIX_MLD(st, fmt, ...) LOG_PREFIX("mld: %s: " fmt, (mld)->name, ##__VA_ARGS__)

#define LOG_PREFIX_STREAM_IFNAME(si, fmt, ...) \
    LOG_PREFIX_STREAM((si)->st, "ifname: %s: " fmt, (si)->shared->name, ##__VA_ARGS__)

#define LOG_PREFIX_ENTRY(e, fmt, ...)                          \
    LOG_PREFIX_STREAM(                                         \
            (e)->st,                                           \
            "%s: %02x:%02x:%02x:%02x:%02x:%02x: %02hhx: " fmt, \
            e->host.if_name,                                   \
            (int)e->host.mac_address[0],                       \
            (int)e->host.mac_address[1],                       \
            (int)e->host.mac_address[2],                       \
            (int)e->host.mac_address[3],                       \
            (int)e->host.mac_address[4],                       \
            (int)e->host.mac_address[5],                       \
            e->host.dscp,                                      \
            ##__VA_ARGS__)

static const char *sm_lat_core_sampling_to_cstr(enum sm_lat_core_sampling s)
{
    switch (s)
    {
        case SM_LAT_CORE_SAMPLING_SEPARATE:
            return "separate";
        case SM_LAT_CORE_SAMPLING_MERGE:
            return "merge";
    }
    return "";
}

static sm_lat_core_netdev_t *sm_lat_core_netdev_tree_spawn(ds_tree_t *netdevs, const char *name)
{
    sm_lat_core_netdev_t *n = CALLOC(1, sizeof(*n));
    n->name = STRDUP(name);
    ds_tree_insert(netdevs, n, n->name);
    return n;
}

static ds_tree_t *sm_lat_core_netdev_tree_generate(sm_lat_core_t *c, const char *name)
{
    sm_lat_core_mld_t *mld = ds_tree_find(&c->mlds, name);
    ds_tree_t *netdevs = CALLOC(1, sizeof(*netdevs));
    ds_tree_init(netdevs, ds_str_cmp, sm_lat_core_netdev_t, node);

    /* If the "bind to MLD netdev" behaviour is
     * desired to be forced, this is the place to
     * extend with, eg. kconfig_enabled().
     */
    if (mld == NULL /* .. here */)
    {
        sm_lat_core_netdev_tree_spawn(netdevs, name);
    }
    else
    {
        size_t n_vifs = 0;
        {
            sm_lat_core_vif_t *vif;
            ds_tree_foreach (&mld->vifs, vif)
            {
                if (vif->exists)
                {
                    sm_lat_core_netdev_tree_spawn(netdevs, vif->name);
                    n_vifs++;
                }
            }
        }
        if (n_vifs == 0)
        {
            sm_lat_core_netdev_tree_spawn(netdevs, name);
        }
    }

    return netdevs;
}

static bool sm_lat_core_netdev_tree_is_equal(ds_tree_t *a, ds_tree_t *b)
{
    if (a == NULL && b == NULL) return true;
    if (a == NULL && b != NULL) return false;
    if (a != NULL && b == NULL) return false;
    if (ds_tree_len(a) != ds_tree_len(b)) return false;

    sm_lat_core_netdev_t *x;
    ds_tree_foreach (a, x)
    {
        sm_lat_core_netdev_t *y = ds_tree_find(b, x->name);
        if (y == NULL) return false;
    }

    return true;
}

static void sm_lat_core_netdev_tree_drop(ds_tree_t *netdevs)
{
    if (netdevs == NULL) return;

    sm_lat_core_netdev_t *n;
    while ((n = ds_tree_remove_head(netdevs)) != NULL)
    {
        FREE(n->name);
        FREE(n);
    }
    FREE(netdevs);
}

static void sm_lat_core_netdevs_tree_set(sm_lat_core_t *c, ds_tree_t *netdevs, const bool enable)
{
    if (netdevs == NULL) return;

    sm_lat_core_netdev_t *n;
    ds_tree_foreach (netdevs, n)
    {
        sm_lat_sys_ifname_set(c->sys, n->name, enable);
    }
}

static void sm_lat_core_ifname_enable(sm_lat_core_ifname_t *i, const bool enable)
{
    sm_lat_core_t *c = i->c;
    ds_tree_t *netdevs = enable ? sm_lat_core_netdev_tree_generate(c, i->name) : NULL;

    if (sm_lat_core_netdev_tree_is_equal(i->netdevs, netdevs))
    {
        sm_lat_core_netdev_tree_drop(netdevs);
        return;
    }

    sm_lat_core_netdevs_tree_set(c, i->netdevs, false);
    sm_lat_core_netdevs_tree_set(c, netdevs, true);

    sm_lat_core_netdev_tree_drop(i->netdevs);
    i->netdevs = netdevs;
    netdevs = NULL;
}

static sm_lat_core_ifname_t *sm_lat_core_ifname_alloc(sm_lat_core_t *c, const char *name)
{
    sm_lat_core_ifname_t *i = CALLOC(1, sizeof(*c));
    i->c = c;
    i->name = STRDUP(name);
    ds_tree_init(&i->refs, ds_void_cmp, sm_lat_core_stream_ifname_t, node_shared);
    ds_tree_insert(&c->ifnames, i, i->name);
    LOGI(LOG_PREFIX_IFNAME(i, "set"));
    sm_lat_core_ifname_enable(i, true);
    return i;
}

static void sm_lat_core_ifname_drop(sm_lat_core_ifname_t *i)
{
    if (i == NULL) return;
    if (WARN_ON(ds_tree_is_empty(&i->refs) == false)) return;
    LOGI(LOG_PREFIX_IFNAME(i, "unset"));
    sm_lat_core_ifname_enable(i, false);
    assert(i->netdevs == NULL);
    ds_tree_remove(&i->c->ifnames, i);
    FREE(i->name);
    FREE(i);
}

static void sm_lat_core_ifname_gc(sm_lat_core_ifname_t *i)
{
    if (i == NULL) return;
    if (ds_tree_is_empty(&i->refs) == false) return;
    sm_lat_core_ifname_drop(i);
}

static sm_lat_core_stream_ifname_t *sm_lat_core_stream_ifname_alloc(sm_lat_core_stream_t *st, sm_lat_core_ifname_t *i)
{
    sm_lat_core_stream_ifname_t *si = CALLOC(1, sizeof(*si));
    si->st = st;
    si->shared = i;
    ds_tree_insert(&i->refs, si, si);
    ds_tree_insert(&st->ifnames, si, si->shared->name);
    LOGI(LOG_PREFIX_STREAM_IFNAME(si, "set"));
    return si;
}

static sm_lat_core_stream_ifname_t *sm_lat_core_stream_ifname_lookup_or_alloc(
        sm_lat_core_stream_t *st,
        sm_lat_core_ifname_t *i)
{
    return ds_tree_find(&st->ifnames, i->name) ?: sm_lat_core_stream_ifname_alloc(st, i);
}

static void sm_lat_core_stream_ifname_drop(sm_lat_core_stream_ifname_t *si)
{
    if (si == NULL) return;
    LOGI(LOG_PREFIX_STREAM_IFNAME(si, "unset"));
    ds_tree_remove(&si->shared->refs, si);
    ds_tree_remove(&si->st->ifnames, si);
    sm_lat_core_ifname_gc(si->shared);
    FREE(si);
}

void sm_lat_core_stream_set_ifname(sm_lat_core_stream_t *st, const char *if_name, bool enable)
{
    if (st == NULL) return;
    if (enable)
    {
        sm_lat_core_ifname_t *i = ds_tree_find(&st->c->ifnames, if_name) ?: sm_lat_core_ifname_alloc(st->c, if_name);
        sm_lat_core_stream_ifname_lookup_or_alloc(st, i);
    }
    else
    {
        sm_lat_core_stream_ifname_t *si = ds_tree_find(&st->ifnames, if_name);
        sm_lat_core_stream_ifname_drop(si);
    }
}

enum sm_lat_core_boolref_transition
{
    SM_LAT_CORE_BOOLREF_NONE,
    SM_LAT_CORE_BOOLREF_FIRST,
    SM_LAT_CORE_BOOLREF_LAST,
};

static uint32_t sm_lat_core_boolref_set(
        sm_lat_core_stream_t *st,
        const char *name,
        uint32_t *counter,
        bool *toggle,
        bool desired)
{
    if (*toggle != desired)
    {
        LOGI(LOG_PREFIX_STREAM(st, "%s: %s -> %s", name, *toggle ? "true" : "false", desired ? "true" : "false"));
        *toggle = desired;
        if (desired)
        {
            if (WARN_ON(*counter == UINT32_MAX)) return SM_LAT_CORE_BOOLREF_NONE;
            (*counter)++;
            if (*counter == 1) return SM_LAT_CORE_BOOLREF_FIRST;
        }
        else
        {
            if (WARN_ON(*counter == 0)) return SM_LAT_CORE_BOOLREF_NONE;
            (*counter)--;
            if (*counter == 0) return SM_LAT_CORE_BOOLREF_LAST;
        }
    }
    return SM_LAT_CORE_BOOLREF_NONE;
}

#define DEFINE_SET_BOOL(NAME, VAR, COUNTER, TOGGLE, SYS_FN)                             \
    void NAME(sm_lat_core_stream_t *st, bool enable)                                    \
    {                                                                                   \
        if (st == NULL) return;                                                         \
        switch (sm_lat_core_boolref_set(st, VAR, &st->c->COUNTER, &st->TOGGLE, enable)) \
        {                                                                               \
            case SM_LAT_CORE_BOOLREF_NONE:                                              \
                break;                                                                  \
            case SM_LAT_CORE_BOOLREF_FIRST:                                             \
                LOGI(LOG_PREFIX("%s: enabling (first)", VAR));                          \
                SYS_FN(st->c->sys, true);                                               \
                break;                                                                  \
            case SM_LAT_CORE_BOOLREF_LAST:                                              \
                LOGI(LOG_PREFIX("%s: disabling (last)", VAR));                          \
                SYS_FN(st->c->sys, false);                                              \
                break;                                                                  \
        }                                                                               \
    }

DEFINE_SET_BOOL(sm_lat_core_stream_set_dscp, "dscp", dscp_enabled_count, dscp_enabled, sm_lat_sys_dscp_set);
DEFINE_SET_BOOL(sm_lat_core_stream_set_kind_min, "min", min_enabled_count, min_enabled, sm_lat_sys_kind_set_min);
DEFINE_SET_BOOL(sm_lat_core_stream_set_kind_max, "max", max_enabled_count, max_enabled, sm_lat_sys_kind_set_max);
DEFINE_SET_BOOL(sm_lat_core_stream_set_kind_avg, "avg", avg_enabled_count, avg_enabled, sm_lat_sys_kind_set_avg);
DEFINE_SET_BOOL(sm_lat_core_stream_set_kind_last, "last", last_enabled_count, last_enabled, sm_lat_sys_kind_set_last);
DEFINE_SET_BOOL(
        sm_lat_core_stream_set_kind_num_pkts,
        "num_pkts",
        num_pkts_enabled_count,
        num_pkts_enabled,
        sm_lat_sys_kind_set_num_pkts);

static void sm_lat_core_sample_u32_add(uint32_t **dst, const uint32_t *src)
{
    if (src == NULL) return;
    if (*dst == NULL) *dst = CALLOC(1, sizeof(*dst));
    **dst += *src;
}

static void sm_lat_core_sample_u32_set(uint32_t **dst, const uint32_t *src)
{
    if (src == NULL) return;
    if (*dst == NULL) *dst = CALLOC(1, sizeof(*dst));
    **dst = *src;
}

static void sm_lat_core_sample_u32_set_if_lt(uint32_t **dst, const uint32_t *src)
{
    if (src == NULL) return;
    if (*dst == NULL) *dst = MEMNDUP(src, sizeof(*src));
    if (*src < **dst) **dst = *src;
}

static void sm_lat_core_sample_u32_set_if_gt(uint32_t **dst, const uint32_t *src)
{
    if (src == NULL) return;
    if (*dst == NULL) *dst = MEMNDUP(src, sizeof(*src));
    if (*src > **dst) **dst = *src;
}

static sm_lat_core_sample_t *sm_lat_core_host_grow_samples(sm_lat_core_host_t *h)
{
    const size_t last = h->n_samples++;
    const size_t size = h->n_samples * sizeof(h->samples[0]);
    h->samples = REALLOC(h->samples, size);
    MEMZERO(h->samples[last]);
    return &h->samples[last];
}

static sm_lat_core_sample_t *sm_lat_core_entry_get_sample(sm_lat_core_entry_t *e)
{
    sm_lat_core_host_t *h = &e->host;
    switch (e->st->sampling)
    {
        case SM_LAT_CORE_SAMPLING_SEPARATE:
            sm_lat_core_host_grow_samples(h);
            break;
        case SM_LAT_CORE_SAMPLING_MERGE:
            if (h->n_samples == 0)
            {
                /* Make sure there's at least, and
                 * at most 1 sample that we'll be
                 * accumulating data into.
                 */
                sm_lat_core_host_grow_samples(h);
            }
            break;
    }
    if (h->n_samples == 0) return NULL;
    return &h->samples[h->n_samples - 1];
}

static sm_lat_core_entry_t *sm_lat_core_entry_alloc(sm_lat_core_stream_t *st, sm_lat_core_host_t *host)
{
    sm_lat_core_entry_t *e = CALLOC(1, sizeof(*e));
    e->host = *host;
    e->st = st;
    e->root = &st->hosts_open;
    ds_tree_insert(e->root, e, &e->host);
    return e;
}

static sm_lat_core_entry_t *sm_lat_core_entry_lookup_or_alloc(sm_lat_core_stream_t *st, sm_lat_core_host_t *host)
{
    return ds_tree_find(&st->hosts_open, host) ?: sm_lat_core_entry_alloc(st, host);
}

static void sm_lat_core_sample_drop(sm_lat_core_sample_t *s)
{
    if (s == NULL) return;
    FREE(s->min_ms);
    FREE(s->max_ms);
    FREE(s->avg_sum_ms);
    FREE(s->avg_cnt);
    FREE(s->last_ms);
    FREE(s->num_pkts);
    MEMZERO(*s);
}

static void sm_lat_core_host_drop_samples(sm_lat_core_host_t *h)
{
    size_t i;
    for (i = 0; i < h->n_samples; i++)
    {
        sm_lat_core_sample_drop(&h->samples[i]);
    }
}

static void sm_lat_core_entry_drop(sm_lat_core_entry_t *e)
{
    if (e == NULL) return;
    sm_lat_core_host_drop_samples(&e->host);
    if (e->root) ds_tree_remove(e->root, e);
    FREE(e->host.samples);
    FREE(e);
}

static void sm_lat_core_entry_update(sm_lat_core_entry_t *e, const sm_lat_sys_sample_t *ss)
{
    if (e == NULL) return;
    sm_lat_core_sample_t *cs = sm_lat_core_entry_get_sample(e);
    const uint32_t one = 1;
    cs->timestamp_ms = clock_real_ms();
    if (e->st->min_enabled)
    {
        sm_lat_core_sample_u32_set_if_lt(&cs->min_ms, sm_lat_sys_sample_get_min(ss));
    }
    if (e->st->max_enabled)
    {
        sm_lat_core_sample_u32_set_if_gt(&cs->max_ms, sm_lat_sys_sample_get_max(ss));
    }
    if (e->st->last_enabled)
    {
        sm_lat_core_sample_u32_set(&cs->last_ms, sm_lat_sys_sample_get_last(ss));
    }
    if (e->st->num_pkts_enabled)
    {
        sm_lat_core_sample_u32_add(&cs->num_pkts, sm_lat_sys_sample_get_num_pkts(ss));
    }
    if (e->st->avg_enabled)
    {
        const uint32_t *avg = sm_lat_sys_sample_get_avg(ss);
        if (avg != NULL)
        {
            const uint32_t cnt = *(sm_lat_sys_sample_get_num_pkts(ss) ?: &one);
            const uint32_t sum = (*avg) * cnt;
            sm_lat_core_sample_u32_add(&cs->avg_sum_ms, &sum);
            sm_lat_core_sample_u32_add(&cs->avg_cnt, &cnt);
        }
    }
}

static int sm_lat_core_entry_cmp(const void *a, const void *b)
{
    const sm_lat_core_host_t *x = a;
    const sm_lat_core_host_t *y = b;
    const int r1 = memcmp(x->mac_address, y->mac_address, sizeof(x->mac_address));
    const int r2 = strncmp(x->if_name, y->if_name, sizeof(x->if_name));
    const int r3 = (int)x->dscp - (int)y->dscp;
    if (r1) return r1;
    if (r2) return r2;
    if (r3) return r3;
    return 0;
}

static bool sm_lat_core_stream_interested_in(sm_lat_core_stream_t *st, sm_lat_core_host_t *host)
{
    const bool if_name_matches = ds_tree_is_empty(&st->ifnames) || (ds_tree_find(&st->ifnames, host->if_name) != NULL);
    return if_name_matches;
}

static const char *sm_lat_core_vif_name_to_mld_name(sm_lat_core_t *c, const char *vif_name)
{
    if (c == NULL) return NULL;
    if (vif_name == NULL) return NULL;

    sm_lat_core_vif_t *vif = ds_tree_find(&c->vifs, vif_name);
    if (vif == NULL) return NULL;
    if (vif->mld == NULL) return NULL;
    return vif->mld->name;
}

static void sm_lat_core_report_cb(void *priv, const sm_lat_sys_sample_t *sample)
{
    sm_lat_core_t *c = priv;
    const char *if_name = sm_lat_sys_sample_get_ifname(sample);
    const uint8_t *mac = sm_lat_sys_sample_get_mac_address(sample);
    const uint8_t *dscp = sm_lat_sys_sample_get_dscp(sample);

    sm_lat_core_host_t host;
    MEMZERO(host);
    if (mac != NULL) memcpy(host.mac_address, mac, sizeof(host.mac_address));  // FIXME

    sm_lat_core_stream_t *st;
    ds_tree_foreach (&c->streams, st)
    {
        const char *mld_name = sm_lat_core_vif_name_to_mld_name(c, if_name);
        STRSCPY(host.if_name, mld_name ?: (if_name ?: ""));
        if (sm_lat_core_stream_interested_in(st, &host) == false) continue;
        if (st->dscp_enabled)
        {
            host.dscp = dscp ? *dscp : SM_LAT_CORE_DSCP_MISSING;
        }
        else
        {
            host.dscp = SM_LAT_CORE_DSCP_NONE;
        }
        sm_lat_core_entry_t *e = sm_lat_core_entry_lookup_or_alloc(st, &host);
        sm_lat_core_entry_update(e, sample);
    }
}

static void sm_lat_core_entry_extend(sm_lat_core_entry_t *dst, sm_lat_core_entry_t *src)
{
    size_t i;
    for (i = 0; i < src->host.n_samples; i++)
    {
        sm_lat_core_sample_t *from = &src->host.samples[i];
        sm_lat_core_sample_t *to = sm_lat_core_host_grow_samples(&dst->host);
        *to = *from;
        /* ptrs from `from` are moved to `to`. The
         * src->host.samples array is later
         * dropped without freeing inner
         * attributes as they've been transferred
         * to dst->host.samples.
         */
    }

    FREE(src->host.samples);
    src->host.n_samples = 0;
    src->host.samples = NULL;
}

static void sm_lat_core_stream_hosts_close(sm_lat_core_stream_t *st)
{
    sm_lat_core_entry_t *src;
    while ((src = ds_tree_remove_head(&st->hosts_open)) != NULL)
    {
        sm_lat_core_entry_t *dst = ds_tree_find(&st->hosts_closed, &src->host);
        src->root = NULL;
        if (dst == NULL)
        {
            src->root = &st->hosts_closed;
            ds_tree_insert(src->root, src, &src->host);
        }
        else
        {
            sm_lat_core_entry_extend(dst, src);
            sm_lat_core_entry_drop(src);
        }
    }
}

static void sm_lat_core_rearm(sm_lat_core_stream_t *st)
{
    ev_periodic_stop(st->c->loop, &st->report_periodic);
    ev_periodic_stop(st->c->loop, &st->poll_periodic);

    if (st->report_ms == 0) return;
    if (st->poll_ms == 0) return;

    const ev_tstamp report_sec = (ev_tstamp)st->report_ms / (ev_tstamp)1000;
    const ev_tstamp poll_sec = (ev_tstamp)st->poll_ms / (ev_tstamp)1000;

    ev_periodic_set(&st->report_periodic, 0, report_sec, NULL);
    ev_periodic_set(&st->poll_periodic, 0, poll_sec, NULL);

    ev_periodic_start(st->c->loop, &st->report_periodic);
    ev_periodic_start(st->c->loop, &st->poll_periodic);
}

void sm_lat_core_stream_set_report_fn(sm_lat_core_stream_t *st, sm_lat_core_report_fn_t *fn, void *priv)
{
    if (st == NULL) return;
    st->report_fn = fn;
    st->report_fn_priv = priv;
}

void sm_lat_core_stream_set_report_ms(sm_lat_core_stream_t *st, uint32_t ms)
{
    if (st == NULL) return;
    if (st->report_ms == ms) return;
    LOGI(LOG_PREFIX_STREAM(st, "report_ms: %" PRIu32 " -> %" PRIu32, st->report_ms, ms));
    st->report_ms = ms;
    sm_lat_core_rearm(st);
}

void sm_lat_core_stream_set_poll_ms(sm_lat_core_stream_t *st, uint32_t ms)
{
    if (st == NULL) return;
    if (st->poll_ms == ms) return;
    LOGI(LOG_PREFIX_STREAM(st, "poll_ms: %" PRIu32 " -> %" PRIu32, st->poll_ms, ms));
    st->poll_ms = ms;
    sm_lat_core_rearm(st);
}

void sm_lat_core_stream_set_sampling(sm_lat_core_stream_t *st, enum sm_lat_core_sampling sampling)
{
    if (st == NULL) return;
    if (st->sampling == sampling) return;
    LOGI(LOG_PREFIX_STREAM(
            st,
            "sampling: %s -> %s",
            sm_lat_core_sampling_to_cstr(st->sampling),
            sm_lat_core_sampling_to_cstr(sampling)));
    st->sampling = sampling;
}

static void sm_lat_core_stream_report_hosts(
        sm_lat_core_stream_t *st,
        const sm_lat_core_host_t *const *hosts,
        size_t count)
{
    if (st->report_fn == NULL) return;
    LOGD(LOG_PREFIX_STREAM(st, "report: %zu hosts", count));
    st->report_fn(st->report_fn_priv, hosts, count);
}

static void sm_lat_core_entry_log(sm_lat_core_entry_t *e, const sm_lat_core_host_t *host)
{
    size_t i;
    char buf[512];
    for (i = 0; i < host->n_samples; i++)
    {
        sm_lat_core_sample_t *s = &host->samples[i];
        char *log = buf;
        size_t len = sizeof(buf);
        csnprintf(&log, &len, "ts: %" PRIu64, s->timestamp_ms);
        if (s->min_ms != NULL) csnprintf(&log, &len, " min: %" PRIu32, *s->min_ms);
        if (s->max_ms != NULL) csnprintf(&log, &len, " max: %" PRIu32, *s->max_ms);
        if (s->avg_sum_ms != NULL) csnprintf(&log, &len, " avg_sum: %" PRIu32, *s->avg_sum_ms);
        if (s->avg_cnt != NULL) csnprintf(&log, &len, " avg_cnt: %" PRIu32, *s->avg_cnt);
        if (s->last_ms != NULL) csnprintf(&log, &len, " last: %" PRIu32, *s->last_ms);
        if (s->num_pkts != NULL) csnprintf(&log, &len, " pkts: %" PRIu32, *s->num_pkts);
        LOGT(LOG_PREFIX_ENTRY(e, "%s", buf));
    }
}

static void sm_lat_core_stream_flush_closed_hosts(sm_lat_core_stream_t *st)
{
    sm_lat_core_entry_t *e;
    while ((e = ds_tree_head(&st->hosts_closed)) != NULL)
    {
        sm_lat_core_entry_drop(e);
    }
}

static void sm_lat_core_stream_report(sm_lat_core_stream_t *st)
{
    LOGD(LOG_PREFIX_STREAM(st, "report"));

    sm_lat_core_stream_hosts_close(st);

    sm_lat_core_host_t *hosts[SM_LAT_CORE_REPORT_HOST_MAX];
    size_t count = 0;
    sm_lat_core_entry_t *e;
    ds_tree_iter_t iter;
    for (e = ds_tree_ifirst(&iter, &st->hosts_closed); e != NULL; e = ds_tree_inext(&iter))
    {
        if (count == ARRAY_SIZE(hosts))
        {
            sm_lat_core_stream_report_hosts(st, (const sm_lat_core_host_t *const *)hosts, count);
            count = 0;
        }
        sm_lat_core_entry_log(e, &e->host);
        hosts[count] = &e->host;
        count++;
    }

    if (count > 0)
    {
        sm_lat_core_stream_report_hosts(st, (const sm_lat_core_host_t *const *)hosts, count);
        count = 0;
    }

    sm_lat_core_stream_flush_closed_hosts(st);
    st->report_pending = false;
}

static void sm_lat_core_stream_report_try(sm_lat_core_stream_t *st)
{
    if (WARN_ON(st == NULL)) return;
    if (st->report_pending == false) return;
    sm_lat_core_stream_report(st);
}

static void sm_lat_core_stream_report_async_cb(struct ev_loop *l, ev_async *a, int mask)
{
    sm_lat_core_stream_t *st = a->data;
    if (st->poll_running) return;
    if (st->poll_pending) return;
    sm_lat_core_stream_report_try(st);
}

static void sm_lat_core_stream_report_schedule(sm_lat_core_stream_t *st)
{
    LOGD(LOG_PREFIX_STREAM(st, "report: scheduling"));
    st->report_pending = true;
    ev_async_send(st->c->loop, &st->report_async);
}

static void sm_lat_core_stream_poll_done(sm_lat_core_stream_t *st)
{
    if (st->poll_running == false) return;
    st->poll_running = false;
    LOGD(LOG_PREFIX_STREAM(st, "poll: done"));

    switch (st->sampling)
    {
        case SM_LAT_CORE_SAMPLING_SEPARATE:
            sm_lat_core_stream_hosts_close(st);
            break;
        case SM_LAT_CORE_SAMPLING_MERGE:
            break;
    }

    sm_lat_core_stream_report_try(st);
}

static bool sm_lat_core_stream_poll_start(sm_lat_core_stream_t *st)
{
    if (st->poll_running == true) return false;
    if (st->poll_pending == false) return false;
    st->poll_pending = false;
    st->poll_running = true;
    LOGD(LOG_PREFIX_STREAM(st, "poll: start"));
    return true;
}

static void sm_lat_core_poll_done_cb(void *priv)
{
    sm_lat_core_t *c = priv;
    LOGD(LOG_PREFIX("poll: done"));
    sm_lat_sys_poll_drop(c->poll);
    sm_lat_core_stream_t *st;
    ds_tree_foreach (&c->streams, st)
    {
        sm_lat_core_stream_poll_done(st);
    }
    ev_async_send(c->loop, &c->poll_async);
}

static void sm_lat_core_poll(sm_lat_core_t *c)
{
    const bool already_running = (c->poll != NULL);
    if (already_running)
    {
        LOGD(LOG_PREFIX("poll: already running"));
        return;
    }
    bool start = false;
    sm_lat_core_stream_t *st;
    ds_tree_foreach (&c->streams, st)
    {
        if (sm_lat_core_stream_poll_start(st))
        {
            start = true;
        }
    }
    if (start)
    {
        LOGD(LOG_PREFIX("poll: starting"));
        c->poll = sm_lat_sys_poll(c->sys, sm_lat_core_poll_done_cb, c);
    }
}

static void sm_lat_core_poll_async_cb(struct ev_loop *l, ev_async *a, int mask)
{
    sm_lat_core_t *c = a->data;
    sm_lat_core_poll(c);
}

static void sm_lat_core_stream_poll_schedule(sm_lat_core_stream_t *st)
{
    if (st->poll_pending) return;
    LOGD(LOG_PREFIX_STREAM(st, "poll: scheduling"));
    st->poll_pending = true;
    ev_async_send(st->c->loop, &st->c->poll_async);
}

static void sm_lat_core_stream_report_periodic_cb(struct ev_loop *l, ev_periodic *p, int mask)
{
    sm_lat_core_stream_t *st = p->data;
    sm_lat_core_stream_report_schedule(st);
}

static void sm_lat_core_stream_poll_periodic_cb(struct ev_loop *l, ev_periodic *p, int mask)
{
    sm_lat_core_stream_t *st = p->data;
    sm_lat_core_stream_poll_schedule(st);
}

sm_lat_core_stream_t *sm_lat_core_stream_alloc(sm_lat_core_t *c)
{
    sm_lat_core_stream_t *st = CALLOC(1, sizeof(*st));
    st->c = c;
    ds_tree_init(&st->ifnames, ds_str_cmp, sm_lat_core_stream_ifname_t, node_stream);
    ds_tree_init(&st->hosts_open, sm_lat_core_entry_cmp, sm_lat_core_entry_t, node);
    ds_tree_init(&st->hosts_closed, sm_lat_core_entry_cmp, sm_lat_core_entry_t, node);
    ev_periodic_init(&st->report_periodic, sm_lat_core_stream_report_periodic_cb, 0, 0, NULL);
    ev_periodic_init(&st->poll_periodic, sm_lat_core_stream_poll_periodic_cb, 0, 0, NULL);
    ev_async_init(&st->report_async, sm_lat_core_stream_report_async_cb);
    ev_async_start(c->loop, &st->report_async);
    st->report_periodic.data = st;
    st->poll_periodic.data = st;
    st->report_async.data = st;
    ds_tree_insert(&c->streams, st, st);
    LOGI(LOG_PREFIX_STREAM(st, "allocated"));
    return st;
}

static void sm_lat_core_stream_drop_ifnames(sm_lat_core_stream_t *st)
{
    sm_lat_core_stream_ifname_t *si;
    while ((si = ds_tree_head(&st->ifnames)) != NULL)
    {
        sm_lat_core_stream_ifname_drop(si);
    }
}

static void sm_lat_core_stream_reset(sm_lat_core_stream_t *st)
{
    sm_lat_core_stream_set_report_fn(st, NULL, NULL);
    sm_lat_core_stream_set_report_ms(st, 0);
    sm_lat_core_stream_set_poll_ms(st, 0);
    sm_lat_core_stream_set_dscp(st, false);
    sm_lat_core_stream_set_kind_min(st, false);
    sm_lat_core_stream_set_kind_max(st, false);
    sm_lat_core_stream_set_kind_avg(st, false);
    sm_lat_core_stream_set_kind_last(st, false);
    sm_lat_core_stream_set_kind_num_pkts(st, false);
    sm_lat_core_stream_drop_ifnames(st);
}

void sm_lat_core_stream_drop(sm_lat_core_stream_t *st)
{
    if (st == NULL) return;
    LOGI(LOG_PREFIX_STREAM(st, "dropping"));
    sm_lat_core_stream_report(st);
    sm_lat_core_stream_reset(st);
    ev_async_stop(st->c->loop, &st->report_async);
    ds_tree_remove(&st->c->streams, st);
    WARN_ON(ds_tree_is_empty(&st->hosts_open) == false);
    WARN_ON(ds_tree_is_empty(&st->hosts_closed) == false);
    FREE(st);
}

sm_lat_core_t *sm_lat_core_alloc(void)
{
    sm_lat_core_t *c = CALLOC(1, sizeof(*c));
    c->sys = sm_lat_sys_alloc();
    c->loop = EV_DEFAULT;
    sm_lat_sys_set_report_fn_t(c->sys, sm_lat_core_report_cb, c);
    ev_async_init(&c->poll_async, sm_lat_core_poll_async_cb);
    ev_async_start(c->loop, &c->poll_async);
    c->poll_async.data = c;
    ds_tree_init(&c->streams, ds_void_cmp, sm_lat_core_stream_t, node);
    ds_tree_init(&c->ifnames, ds_str_cmp, sm_lat_core_ifname_t, node);
    ds_tree_init(&c->vifs, ds_str_cmp, sm_lat_core_vif_t, node);
    ds_tree_init(&c->mlds, ds_str_cmp, sm_lat_core_mld_t, node);
    LOGI(LOG_PREFIX("allocated"));
    return c;
}

static void sm_lat_core_drop_vifs(sm_lat_core_t *c)
{
    sm_lat_core_vif_t *vif;
    while ((vif = ds_tree_head(&c->vifs)) != NULL)
    {
        sm_lat_core_set_vif_mld_if_name(c, vif->name, NULL);
    }
}

void sm_lat_core_drop(sm_lat_core_t *c)
{
    if (c == NULL) return;
    LOGI(LOG_PREFIX("dropping"));
    sm_lat_core_drop_vifs(c);
    assert(ds_tree_is_empty(&c->streams));
    assert(ds_tree_is_empty(&c->ifnames));
    assert(ds_tree_is_empty(&c->vifs));
    assert(ds_tree_is_empty(&c->mlds));
    sm_lat_sys_poll_drop(c->poll);
    sm_lat_sys_drop(c->sys);
    ev_async_stop(c->loop, &c->poll_async);
    FREE(c);
}

static void sm_lat_core_vif_invalidate(sm_lat_core_vif_t *vif)
{
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->mld == NULL)) return;

    LOGD(LOG_PREFIX_VIF(vif, "invalidating"));

    sm_lat_core_ifname_t *i = ds_tree_find(&vif->mld->c->ifnames, vif->mld->name);
    if (i == NULL) return;

    sm_lat_core_ifname_enable(i, true);
}

static void sm_lat_core_vif_netif_status_cb(osn_netif_t *netif, struct osn_netif_status *status)
{
    sm_lat_core_vif_t *vif = osn_netif_data_get(netif);
    const bool wrong_ifname = (strcmp(vif->name, status->ns_ifname) != 0);
    if (wrong_ifname) return;

    vif->exists = status->ns_exists;
    sm_lat_core_vif_invalidate(vif);
}

static sm_lat_core_vif_t *sm_lat_core_vif_alloc(sm_lat_core_t *c, const char *vif_name)
{
    sm_lat_core_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->c = c;
    vif->name = STRDUP(vif_name);
    ds_tree_insert(&c->vifs, vif, vif->name);
    LOGD(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

static sm_lat_core_vif_t *sm_lat_core_vif_get(sm_lat_core_t *c, const char *vif_name)
{
    if (vif_name == NULL) return NULL;
    return ds_tree_find(&c->vifs, vif_name) ?: sm_lat_core_vif_alloc(c, vif_name);
}

static void sm_lat_core_vif_drop(sm_lat_core_vif_t *vif)
{
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_VIF(vif, "dropping"));
    ds_tree_remove(&vif->c->vifs, vif);
    FREE(vif->name);
    FREE(vif);
}

static void sm_lat_core_vif_gc(sm_lat_core_vif_t *vif)
{
    if (vif == NULL) return;
    if (vif->mld != NULL) return;
    sm_lat_core_vif_drop(vif);
}

static sm_lat_core_mld_t *sm_lat_core_mld_alloc(sm_lat_core_t *c, const char *name)
{
    sm_lat_core_mld_t *mld = CALLOC(1, sizeof(*mld));
    mld->name = STRDUP(name);
    mld->c = c;
    ds_tree_insert(&c->mlds, mld, mld->name);
    ds_tree_init(&mld->vifs, ds_str_cmp, sm_lat_core_vif_t, node_mld);
    LOGD(LOG_PREFIX_MLD(mld, "allocated"));
    return mld;
}

static sm_lat_core_mld_t *sm_lat_core_mld_get(sm_lat_core_t *c, const char *name)
{
    if (name == NULL) return NULL;
    return ds_tree_find(&c->mlds, name) ?: sm_lat_core_mld_alloc(c, name);
}

static void sm_lat_core_mld_drop(sm_lat_core_mld_t *mld)
{
    if (mld == NULL) return;
    LOGD(LOG_PREFIX_MLD(mld, "dropping"));
    ds_tree_remove(&mld->c->mlds, mld);
    FREE(mld->name);
    FREE(mld);
}

static void sm_lat_core_mld_gc(sm_lat_core_mld_t *mld)
{
    if (ds_tree_is_empty(&mld->vifs) == false) return;
    sm_lat_core_mld_drop(mld);
}

static void sm_lat_core_vif_detach_mld(sm_lat_core_vif_t *vif)
{
    if (vif == NULL) return;
    if (vif->mld == NULL) return;
    if (WARN_ON(vif->netif == NULL)) return;

    LOGD(LOG_PREFIX_VIF(vif, "detaching"));
    osn_netif_status_notify(vif->netif, NULL);
    osn_netif_del(vif->netif);

    ds_tree_remove(&vif->mld->vifs, vif);
    sm_lat_core_mld_gc(vif->mld);
    vif->mld = NULL;
}

static void sm_lat_core_vif_attach_mld(sm_lat_core_vif_t *vif, sm_lat_core_mld_t *mld)
{
    if (vif == NULL) return;
    if (WARN_ON(vif->mld != NULL)) return;
    if (WARN_ON(vif->netif != NULL)) return;
    if (mld == NULL) return;

    LOGD(LOG_PREFIX_VIF(vif, "attaching to %s", mld->name));
    vif->netif = osn_netif_new(vif->name);
    osn_netif_data_set(vif->netif, vif);
    osn_netif_status_notify(vif->netif, sm_lat_core_vif_netif_status_cb);

    ds_tree_insert(&mld->vifs, vif, vif->name);
    vif->mld = mld;
}

void sm_lat_core_set_vif_mld_if_name(sm_lat_core_t *c, const char *vif_name, const char *mld_name)
{
    if (c == NULL) return;
    if (vif_name == NULL) return;

    sm_lat_core_vif_t *vif = sm_lat_core_vif_get(c, vif_name);
    sm_lat_core_mld_t *mld_old = vif->mld;
    sm_lat_core_mld_t *mld_new = sm_lat_core_mld_get(c, mld_name);

    if (mld_old == mld_new) return;

    sm_lat_core_vif_detach_mld(vif);
    sm_lat_core_vif_attach_mld(vif, mld_new);
    sm_lat_core_vif_gc(vif);
}
