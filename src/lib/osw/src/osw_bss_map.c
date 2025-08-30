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

#include <assert.h>
#include <inttypes.h>
#include <ev.h>
#include <const.h>
#include <log.h>
#include <string.h>
#include <util.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_ut.h>
#include <osw_bss_map.h>
#include <osw_module.h>
#include <osw_timer.h>
#include <osw_diag.h>

struct osw_bss {
    struct osw_hwaddr bssid;
    struct ds_dlist entry_list;
    struct osw_timer work;
    bool worked_on_at_least_once;

    struct ds_tree entry_observers;
    struct ds_tree_node node;
};

struct osw_bss_entry {
    struct osw_bss *bss;

    struct osw_ssid *ssid;
    struct osw_channel *channel;
    uint8_t *op_class;
    struct osw_hwaddr *mld_addr;
    int *ft_mobility_domain;

    struct ds_tree_node bss_node;
    struct ds_dlist_node provider_node;
};

struct osw_bss_map {
    struct ds_tree bss_tree;
    struct ds_tree entry_observers;
    struct ds_dlist provider_list;
    ev_signal sigusr1;
};

struct osw_bss_provider {
    struct ds_dlist entry_list;

    struct ds_dlist_node node;
};

struct osw_bss_map_entry_observer {
    struct osw_bss *bss;
    struct ds_tree_node node_map;
    struct ds_tree_node node_bss;
    struct osw_hwaddr bssid;
    struct osw_channel last_reported;
    osw_bss_map_entry_changed_fn_t *changed_fn;
    void *changed_fn_priv;
};

static struct osw_bss_map g_bss_map;
static struct ds_dlist g_observer_list = DS_DLIST_INIT(struct osw_bss_map_observer, node);

static bool
osw_bss_provider_has_entry(struct osw_bss_provider *provider,
                               struct osw_bss_entry* entry)
{
    struct osw_bss_entry* element;

    ds_dlist_foreach(&provider->entry_list, element)
        if(entry == element)
            return true;

    return false;
}

static bool
osw_bss_has_entry(struct osw_bss *bss,
                  struct osw_bss_entry* entry)
{
    struct osw_bss_entry* element;

    ds_dlist_foreach(&bss->entry_list, element)
        if(entry == element)
            return true;

    return false;
}

static void
osw_bss_unlink_entry_observers(struct osw_bss *bss)
{
    struct osw_bss_map_entry_observer *obs;
    while ((obs = ds_tree_remove_head(&bss->entry_observers)) != NULL) {
        obs->bss = NULL;
    }
}

static void
osw_bss_free(struct osw_bss *bss)
{
    ASSERT(bss != NULL, "");

    osw_bss_unlink_entry_observers(bss);
    ASSERT(ds_dlist_is_empty(&bss->entry_list) == true, "");
    FREE(bss);
}

static void
osw_bss_map_entry_observer_notify(osw_bss_map_entry_observer_t *obs)
{
    if (obs == NULL) return;
    if (obs->changed_fn == NULL) return;

    static struct osw_channel zero;
    const struct osw_bss_entry *e = obs->bss ? ds_dlist_tail(&obs->bss->entry_list) : NULL;
    const struct osw_channel *c = e ? (e->channel ?: &zero) : &zero;
    if (osw_channel_is_equal(c, &obs->last_reported)) return;

    obs->changed_fn(obs->changed_fn_priv, &obs->last_reported, c);
    obs->last_reported = *c;
}

static void
osw_bss_map_entry_observer_link(osw_bss_map_entry_observer_t *obs,
                                struct osw_bss *bss)
{
    if (bss == NULL) return;
    if (WARN_ON(obs->bss != NULL)) return;
    obs->bss = bss;
    ds_tree_insert(&bss->entry_observers, obs, obs);
}

osw_bss_map_entry_observer_t *
osw_bss_map_entry_observer_alloc(const struct osw_hwaddr *bssid)
{
    if (bssid == NULL) return NULL;
    osw_bss_map_entry_observer_t *obs = CALLOC(1, sizeof(*obs));
    struct osw_bss *bss = ds_tree_find(&g_bss_map.bss_tree, bssid);
    obs->bssid = *bssid;
    ds_tree_insert(&g_bss_map.entry_observers, obs, obs);
    osw_bss_map_entry_observer_link(obs, bss);
    osw_bss_map_entry_observer_notify(obs);
    return obs;
}

void
osw_bss_map_entry_observer_set_changed_fn(osw_bss_map_entry_observer_t *obs,
                                          osw_bss_map_entry_changed_fn_t *fn,
                                          void *priv)
{
    if (obs == NULL) return;
    if (fn == NULL) return;
    obs->changed_fn = fn;
    obs->changed_fn_priv = priv;
    osw_bss_map_entry_observer_notify(obs);
}

void
osw_bss_map_entry_observer_drop(osw_bss_map_entry_observer_t *obs)
{
    if (obs == NULL) return;
    osw_bss_map_entry_observer_notify(obs);
    if (obs->bss != NULL) {
        ds_tree_remove(&obs->bss->entry_observers, obs);
        obs->bss = NULL;
    }
    ds_tree_remove(&g_bss_map.entry_observers, obs);
    FREE(obs);
}

static void
osw_bss_notify_entry_observers(struct osw_bss *bss)
{
    struct osw_bss_map_entry_observer *obs;
    ds_tree_foreach(&bss->entry_observers, obs) {
        osw_bss_map_entry_observer_notify(obs);
    }
}

static void
osw_bss_work_cb(struct osw_timer *t)
{
    struct osw_bss *bss = container_of(t, struct osw_bss, work);
    const struct osw_bss_entry *bss_entry = ds_dlist_tail(&bss->entry_list);
    const bool removing = (bss_entry == NULL);

    struct osw_bss_map_observer *obs;
    ds_dlist_foreach(&g_observer_list, obs) {
        if (removing) {
            if (bss->worked_on_at_least_once) {
                if (obs->unset_fn != NULL)
                    obs->unset_fn(obs, &bss->bssid);
            }
        }
        else {
            if (obs->set_fn != NULL)
                obs->set_fn(obs, &bss->bssid, bss_entry);
        }
    }

    bss->worked_on_at_least_once = true;
    osw_bss_notify_entry_observers(bss);

    if (removing) {
        ds_tree_remove(&g_bss_map.bss_tree, bss);
        osw_bss_free(bss);
    }
}

static void
osw_bss_link_entry_observers(struct osw_bss *bss)
{
    struct osw_bss_map_entry_observer *obs;
    ds_tree_foreach(&g_bss_map.entry_observers, obs) {
        if (osw_hwaddr_is_equal(&bss->bssid, &obs->bssid)) {
            osw_bss_map_entry_observer_link(obs, bss);
        }
    }
}

static struct osw_bss*
osw_bss_alloc(const struct osw_hwaddr *bssid)
{
    struct osw_bss *bss = CALLOC(1, sizeof(*bss));

    memcpy(&bss->bssid, bssid, sizeof(bss->bssid));
    ds_dlist_init(&bss->entry_list, struct osw_bss_entry, bss_node);
    ds_tree_init(&bss->entry_observers, ds_void_cmp, struct osw_bss_map_entry_observer, node_bss);
    osw_bss_link_entry_observers(bss);
    osw_timer_init(&bss->work, osw_bss_work_cb);

    return bss;
}

static void
osw_bss_entry_free(struct osw_bss_entry *entry)
{
    ASSERT(entry != NULL, "");

    FREE(entry->ssid);
    FREE(entry->channel);
    FREE(entry->op_class);
    FREE(entry->mld_addr);
    FREE(entry->ft_mobility_domain);

    FREE(entry);
}

static struct osw_bss_provider*
osw_bss_provider_alloc(void)
{
    struct osw_bss_provider *provider = CALLOC(1, sizeof(*provider));
    ds_dlist_init(&provider->entry_list, struct osw_bss_entry, provider_node);
    return provider;
}

static void
osw_bss_provider_free(struct osw_bss_provider *provider)
{
    ASSERT(provider != NULL, "");

    struct osw_bss_entry* entry;

    while ((entry = ds_dlist_remove_head(&provider->entry_list)) != NULL) {
        osw_bss_map_entry_free(provider, entry);
    }

    FREE(provider);
}

static void
osw_bss_map_dump(const struct osw_bss_map *bss_map)
{
    assert(bss_map != NULL);

    osw_diag_pipe_t *pipe = osw_diag_pipe_open();

    struct osw_bss *bss;

    osw_diag_pipe_writef(pipe, "osw: bss_map: ");
    osw_diag_pipe_writef(pipe, "osw: bss_map: bss_tree:");

    ds_tree_foreach(&g_bss_map.bss_tree, bss) {
        struct osw_bss_entry *entry;
        osw_diag_pipe_writef(pipe, "osw: bss_map:   bss: bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&bss->bssid));
        ds_dlist_foreach(&bss->entry_list, entry) {
            const char *ssid = entry->ssid ? strfmta(OSW_SSID_FMT, OSW_SSID_ARG(entry->ssid)) : "(nil)";
            const char *channel = entry->channel ? strfmta(OSW_CHANNEL_FMT, OSW_CHANNEL_ARG(entry->channel)) : "(nil)";
            const char *op_class = entry->op_class ? strfmta("%"PRIu8, *entry->op_class) : "(nil)";
            const char *ft_mobility_domain = entry->ft_mobility_domain ? strfmta("%"PRIu16, *entry->ft_mobility_domain) : "(nil)";
            osw_diag_pipe_writef(pipe, "osw: bss_map:     ssid: %s", ssid);
            osw_diag_pipe_writef(pipe, "osw: bss_map:     channel: %s", channel);
            osw_diag_pipe_writef(pipe, "osw: bss_map:     op_class: %s", op_class);
            osw_diag_pipe_writef(pipe, "osw: bss_map:     ft_mobility_domain: %s", ft_mobility_domain);
        }
    }
    osw_diag_pipe_close(pipe);
}

static void
osw_bss_map_sigusr1_cb(EV_P_ ev_signal *arg,
                      int events)
{
    struct osw_bss_map *bss_map = container_of(arg, struct osw_bss_map, sigusr1);
    osw_bss_map_dump(bss_map);
}

static void
osw_bss_map_init(void)
{
    ds_tree_init(&g_bss_map.bss_tree, (ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_bss, node);
    ds_tree_init(&g_bss_map.entry_observers, ds_void_cmp, struct osw_bss_map_entry_observer, node_map);
    ds_dlist_init(&g_bss_map.provider_list, struct osw_bss_provider, node);

    ev_signal_init(&g_bss_map.sigusr1, osw_bss_map_sigusr1_cb, SIGUSR1);
    ev_signal_start(EV_DEFAULT_ &g_bss_map.sigusr1);
    ev_unref(EV_DEFAULT);
}

struct osw_bss_provider*
osw_bss_map_register_provider(void)
{
    struct osw_bss_provider *provider = osw_bss_provider_alloc();
    ds_dlist_insert_tail(&g_bss_map.provider_list, provider);
    return provider;
}

void
osw_bss_map_unregister_provider(struct osw_bss_provider *provider)
{
    ASSERT(provider != NULL, "");
    osw_bss_provider_free(provider);
}

void
osw_bss_map_register_observer(struct osw_bss_map_observer *observer)
{
    ASSERT(observer != NULL, "");

    struct osw_bss *bss;

    ds_dlist_insert_tail(&g_observer_list, observer);

    if (observer->set_fn == NULL)
        return;

    /* Arguably this could only call this observer's set_fn()
     * but it would need to careful to do it only when
     * worked_on_at_least_once is true, because otherwise
     * the set_fn/unset_fn would go out of whack between
     * different observers if some bss entries are
     * immediately removed. Better safe then
     * sorry. The set_fn() callee needs to be
     * prepared for repeated calls, even when
     * there are no changes, anyway.
     */
    ds_tree_foreach(&g_bss_map.bss_tree, bss)
        osw_timer_arm_at_nsec(&bss->work, 0);
}

void
osw_bss_map_unregister_observer(struct osw_bss_map_observer *observer)
{
    ASSERT(observer != NULL, "");
    ds_dlist_remove(&g_observer_list, observer);
}

struct osw_bss_entry*
osw_bss_map_entry_new(struct osw_bss_provider *provider,
                      const struct osw_hwaddr *bssid)
{
    ASSERT(provider != NULL, "");
    ASSERT(bssid != NULL, "");

    struct osw_bss *bss;
    struct osw_bss_entry *entry;

    if((bss = ds_tree_find(&g_bss_map.bss_tree, bssid)) == NULL) {
        bss = osw_bss_alloc(bssid);
        ds_tree_insert(&g_bss_map.bss_tree, bss, &bss->bssid);
    }

    entry = CALLOC(1, sizeof(*entry));
    entry->bss = bss;
    ds_dlist_insert_tail(&bss->entry_list, entry);
    ds_dlist_insert_tail(&provider->entry_list, entry);
    osw_timer_arm_at_nsec(&entry->bss->work, 0);

    return entry;
}

void
osw_bss_map_entry_free(struct osw_bss_provider *provider,
                       struct osw_bss_entry* entry)
{
    ASSERT(provider != NULL, "");
    ASSERT(entry != NULL, "");

    ASSERT(entry->bss != NULL, "");
    ASSERT(osw_bss_provider_has_entry(provider, entry) == true, "");
    ASSERT(osw_bss_has_entry(entry->bss, entry) == true, "");

    osw_timer_arm_at_nsec(&entry->bss->work, 0);
    ds_dlist_remove(&entry->bss->entry_list, entry);
    ds_dlist_remove(&provider->entry_list, entry);
    osw_bss_entry_free(entry);
}

static void
osw_bss_map_get_channel(struct osw_bss *bss,
                        const struct osw_channel **c,
                        const uint8_t **op_class)
{
    struct osw_bss_entry *e;
    ds_dlist_foreach(&bss->entry_list, e) {
        if (e->channel == NULL) continue;
        *c = e->channel;
        *op_class = e->op_class;
        break;
    }
}

void
osw_bss_map_iter(osw_bss_map_iter_fn_t *iter_fn, void *priv)
{
    struct osw_bss *bss;

    ds_tree_foreach(&g_bss_map.bss_tree, bss) {
        const struct osw_hwaddr *bssid = &bss->bssid;
        const struct osw_channel *c = NULL;
        const uint8_t *op = NULL;
        osw_bss_map_get_channel(bss, &c, &op);
        iter_fn(priv, bssid, c, op);
    }
}

static const struct osw_hwaddr *
osw_bss_map_get_mld_addr(struct osw_bss *bss)
{
    struct osw_bss_entry *e;
    ds_dlist_foreach(&bss->entry_list, e) {
        if (osw_hwaddr_is_zero(e->mld_addr ?: osw_hwaddr_zero())) continue;
        return e->mld_addr;
    }
    return NULL;
}

void
osw_bss_map_iter_mld(osw_bss_map_iter_fn_t *iter_fn,
                     void *priv,
                     const struct osw_hwaddr *mld_addr)
{
    struct osw_bss *bss;

    ds_tree_foreach(&g_bss_map.bss_tree, bss) {
        const struct osw_hwaddr *bssid = &bss->bssid;
        const struct osw_hwaddr *mld = osw_bss_map_get_mld_addr(bss);
        if (mld == NULL) continue;
        if (osw_hwaddr_is_equal(mld, mld_addr) == false) continue;
        const struct osw_channel *c = NULL;
        const uint8_t *op = NULL;
        osw_bss_map_get_channel(bss, &c, &op);
        iter_fn(priv, bssid, c, op);
    }
}

#define OSW_BSS_ENTRY_SET_DEFINITION(attr_type, attr)                       \
    void                                                                    \
    osw_bss_entry_set_ ## attr(struct osw_bss_entry* entry,                 \
                               const attr_type *attr)                       \
    {                                                                       \
        ASSERT(entry != NULL, "");                                          \
        ASSERT(entry->bss != NULL, "");                                     \
        ASSERT(osw_bss_has_entry(entry->bss, entry) == true, "");           \
                                                                            \
        if (entry->attr != NULL && attr != NULL) {                          \
            if (memcmp(entry->attr, attr, sizeof(*attr)) == 0)              \
                return;                                                     \
        }                                                                   \
                                                                            \
        ds_dlist_remove(&entry->bss->entry_list, entry);                    \
        ds_dlist_insert_tail(&entry->bss->entry_list, entry);               \
                                                                            \
        FREE(entry->attr);                                                  \
        entry->attr = NULL;                                                 \
                                                                            \
        if (attr != NULL) {                                                 \
            entry->attr = CALLOC(1, sizeof(*attr));                         \
            memcpy(entry->attr, attr, sizeof(*attr));                       \
        }                                                                   \
                                                                            \
        osw_timer_arm_at_nsec(&entry->bss->work, 0);                        \
    }

#define OSW_BSS_GET_DEFINITION(attr_type, attr)                             \
    const attr_type*                                                        \
    osw_bss_get_ ## attr(const struct osw_hwaddr* bssid)                    \
    {                                                                       \
        ASSERT(bssid != NULL, "");                                          \
                                                                            \
        struct osw_bss *bss;                                                \
        struct osw_bss_entry* entry;                                        \
                                                                            \
        bss = ds_tree_find(&g_bss_map.bss_tree, bssid);                     \
        if(bss == NULL)                                                     \
            return NULL;                                                    \
                                                                            \
        entry = ds_dlist_tail(&bss->entry_list);                            \
        if(entry == NULL)                                                   \
            return NULL;                                                    \
                                                                            \
        return entry->attr;                                                 \
    }

OSW_BSS_ENTRY_SET_DEFINITION(struct osw_ssid, ssid);
OSW_BSS_ENTRY_SET_DEFINITION(struct osw_channel, channel);
OSW_BSS_ENTRY_SET_DEFINITION(uint8_t, op_class);
OSW_BSS_ENTRY_SET_DEFINITION(struct osw_hwaddr, mld_addr);
OSW_BSS_ENTRY_SET_DEFINITION(int, ft_mobility_domain);

OSW_BSS_GET_DEFINITION(struct osw_ssid, ssid);
OSW_BSS_GET_DEFINITION(struct osw_channel, channel);
OSW_BSS_GET_DEFINITION(uint8_t, op_class);
OSW_BSS_GET_DEFINITION(struct osw_hwaddr, mld_addr);
OSW_BSS_GET_DEFINITION(int, ft_mobility_domain);

struct osw_bss_map_ut_lifecycle_observer {
    struct osw_bss_map_observer obs;
    struct osw_hwaddr bssid_a;
    unsigned int set_cnt_a;
    unsigned int unset_cnt_a;
};

static void
osw_bss_map_ut_lifecycle_set_cb(struct osw_bss_map_observer *observer,
                                const struct osw_hwaddr *bssid,
                                const struct osw_bss_entry *bss_entry)
{
    struct osw_bss_map_ut_lifecycle_observer *ctx = container_of(observer, struct osw_bss_map_ut_lifecycle_observer, obs);

    if(memcmp(bssid, &ctx->bssid_a, sizeof(*bssid)) == 0)
        ctx->set_cnt_a++;
}

static void
osw_bss_map_ut_lifecycle_unset_cb(struct osw_bss_map_observer *observer,
                                  const struct osw_hwaddr *bssid)
{
    struct osw_bss_map_ut_lifecycle_observer *ctx = container_of(observer, struct osw_bss_map_ut_lifecycle_observer, obs);

    if(memcmp(bssid, &ctx->bssid_a, sizeof(*bssid)) == 0)
        ctx->unset_cnt_a++;
}

OSW_UT(osw_bss_map_ut_lifecycle)
{
    struct osw_bss_entry *entry_aa;
    struct osw_bss_entry *entry_ab;
    struct osw_bss_provider *provider_a;
    struct osw_bss_provider *provider_b;
    struct osw_bss_map_ut_lifecycle_observer ctx = {
        .obs = {
            .name = "osw_bss_map_ut_lifecycle",
            .set_fn = osw_bss_map_ut_lifecycle_set_cb,
            .unset_fn = osw_bss_map_ut_lifecycle_unset_cb,
        },
        .bssid_a.octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
        .set_cnt_a = 0,
        .unset_cnt_a = 0,
    };
    struct osw_ssid ssid_aa = {
        .buf = "Aaaaa",
        .len = 5,
    };
    struct osw_ssid ssid_ab = {
        .buf = "AaBBBB",
        .len = 6,
    };
    struct osw_channel channel_a = {
        .width = OSW_CHANNEL_20MHZ,
        .control_freq_mhz = 2412,
        .center_freq0_mhz = 2412,
        .center_freq1_mhz = 0,
    };

    osw_bss_map_init();
    osw_bss_map_register_observer(&ctx.obs);

    /* Register two providers */
    provider_a = osw_bss_map_register_provider();
    assert(provider_a != NULL);
    provider_b = osw_bss_map_register_provider();
    assert(provider_b != NULL);

    /* Simple - add new BSS */
    entry_aa = osw_bss_map_entry_new(provider_a, &ctx.bssid_a);
    assert(entry_aa != NULL);

    /* Set two BSS attributes in two ev loop iterations */
    osw_bss_entry_set_ssid(entry_aa, &ssid_aa);
    osw_timer_core_dispatch(0);
    assert(ctx.set_cnt_a == 1);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_aa, sizeof(ssid_aa)) == 0);

    osw_bss_entry_set_channel(entry_aa, &channel_a);
    osw_timer_core_dispatch(0);
    assert(ctx.set_cnt_a == 2);
    assert(memcmp(osw_bss_get_channel(&ctx.bssid_a), &channel_a, sizeof(channel_a)) == 0);

    /* Register the same BSS with different provider */
    entry_ab = osw_bss_map_entry_new(provider_b, &ctx.bssid_a);
    assert(entry_ab != NULL);

    /* Override BSS attribute */
    osw_bss_entry_set_ssid(entry_ab, &ssid_ab);
    osw_timer_core_dispatch(0);
    assert(ctx.set_cnt_a == 3);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_ab, sizeof(ssid_ab)) == 0);
    assert(osw_bss_get_channel(&ctx.bssid_a) == NULL);

    /* Remove BSS */
    osw_bss_map_entry_free(provider_b, entry_ab);
    osw_timer_core_dispatch(0);
    assert(ctx.set_cnt_a == 4);
    assert(ctx.unset_cnt_a == 0);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_aa, sizeof(ssid_aa)) == 0);
    assert(memcmp(osw_bss_get_channel(&ctx.bssid_a), &channel_a, sizeof(channel_a)) == 0);

    osw_bss_map_entry_free(provider_a, entry_aa);
    osw_timer_core_dispatch(0);
    assert(ctx.set_cnt_a == 4);
    assert(ctx.unset_cnt_a == 1);
    assert(osw_bss_get_ssid(&ctx.bssid_a) == NULL);
    assert(osw_bss_get_channel(&ctx.bssid_a) == NULL);
}

OSW_MODULE(osw_bss_map)
{
    osw_bss_map_init();
    return NULL;
}
