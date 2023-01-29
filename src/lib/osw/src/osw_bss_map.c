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

struct osw_bss {
    struct osw_hwaddr bssid;
    struct ds_dlist entry_list;
    ev_timer work_timer;

    struct ds_tree_node node;
};

struct osw_bss_entry {
    struct osw_bss *bss;

    struct osw_ssid *ssid;
    struct osw_channel *channel;
    uint8_t *op_class;

    struct ds_tree_node bss_node;
    struct ds_dlist_node provider_node;
};

struct osw_bss_map {
    struct ds_tree bss_tree;
    struct ds_dlist provider_list;
    ev_timer gc_timer;
    ev_signal sigusr1;
};

struct osw_bss_provider {
    struct ds_dlist entry_list;

    struct ds_dlist_node node;
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
ow_bss_work_timer_cb(EV_P_ ev_timer *arg,
                     int events)
{
    struct osw_bss *bss = container_of(arg, struct osw_bss, work_timer);
    struct osw_bss_map_observer *obs;

    ds_dlist_foreach(&g_observer_list, obs)
        if (obs->set_fn != NULL)
            obs->set_fn(obs, &bss->bssid, ds_dlist_tail(&g_observer_list));
}

static struct osw_bss*
osw_bss_alloc(const struct osw_hwaddr *bssid)
{
    struct osw_bss *bss = CALLOC(1, sizeof(*bss));

    memcpy(&bss->bssid, bssid, sizeof(bss->bssid));
    ds_dlist_init(&bss->entry_list, struct osw_bss_entry, bss_node);
    ev_timer_init(&bss->work_timer, ow_bss_work_timer_cb, 0., 0.);

    return bss;
}

static void
osw_bss_free(struct osw_bss *bss)
{
    ASSERT(bss != NULL, "");

    ASSERT(ds_dlist_is_empty(&bss->entry_list) == true, "");
    FREE(bss);
}

static void
osw_bss_entry_free(struct osw_bss_entry *entry)
{
    ASSERT(entry != NULL, "");

    FREE(entry->ssid);
    FREE(entry->channel);
    FREE(entry->op_class);

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
osw_bss_schedule_work(ev_timer *timer)
{
    ASSERT(timer != NULL, "");

    ev_timer_stop(EV_DEFAULT_ timer);
    ev_timer_set(timer, 0., 0.);
    ev_timer_start(EV_DEFAULT_ timer);
}

static void
osw_bss_provider_free(struct osw_bss_provider *provider)
{
    ASSERT(provider != NULL, "");

    struct osw_bss_entry* entry;
    struct osw_bss_entry* tmp_entry;

    ds_dlist_foreach_safe(&provider->entry_list, entry, tmp_entry) {
        ASSERT(entry->bss != NULL, "");
        ds_dlist_remove(&entry->bss->entry_list, entry);
        if (ds_dlist_is_empty(&entry->bss->entry_list) == true)
            osw_bss_schedule_work(&g_bss_map.gc_timer);

        ds_dlist_remove(&provider->entry_list, entry);
        osw_bss_entry_free(entry);
    }
}

static void
ow_bss_map_gc_timer_cb(EV_P_ ev_timer *arg,
                       int events)
{
    struct osw_bss *bss;
    struct osw_bss *tmp_bss;

    ds_tree_foreach_safe(&g_bss_map.bss_tree, bss, tmp_bss) {
        struct osw_bss_map_observer *obs;

        if (ds_dlist_is_empty(&bss->entry_list) == false)
            continue;

        ds_tree_remove(&g_bss_map.bss_tree, bss);

        ds_dlist_foreach(&g_observer_list, obs)
            if (obs->unset_fn != NULL)
                obs->unset_fn(obs, &bss->bssid);

        osw_bss_free(bss);
    }
}

static void
osw_bss_map_dump(const struct osw_bss_map *bss_map)
{
    assert(bss_map != NULL);

    struct osw_bss *bss;

    LOGI("osw: bss_map: ");
    LOGI("osw: bss_map: bss_tree:");

    ds_tree_foreach(&g_bss_map.bss_tree, bss) {
        struct osw_bss_entry *entry;
        LOGI("osw: bss_map:   bss: bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&bss->bssid));
        ds_dlist_foreach(&bss->entry_list, entry) {
            const char *ssid = entry->ssid ? strfmta(OSW_SSID_FMT, OSW_SSID_ARG(entry->ssid)) : "(nil)";
            const char *channel = entry->channel ? strfmta(OSW_CHANNEL_FMT, OSW_CHANNEL_ARG(entry->channel)) : "(nil)";
            const char *op_class = entry->op_class ? strfmta("%"PRIu8, *entry->op_class) : "(nil)";
            LOGI("osw: bss_map:     ssid: %s", ssid);
            LOGI("osw: bss_map:     channel: %s", channel);
            LOGI("osw: bss_map:     op_class: %s", op_class);
        }
    }
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
    ds_dlist_init(&g_bss_map.provider_list, struct osw_bss_provider, node);
    ev_timer_init(&g_bss_map.gc_timer, ow_bss_map_gc_timer_cb, 0., 0.);

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

    ds_tree_foreach(&g_bss_map.bss_tree, bss)
        observer->set_fn(observer, &bss->bssid, ds_dlist_tail(&bss->entry_list));
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

    osw_bss_schedule_work(&entry->bss->work_timer);

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

    ds_dlist_remove(&entry->bss->entry_list, entry);
    if (ds_dlist_is_empty(&entry->bss->entry_list) == true)
        osw_bss_schedule_work(&g_bss_map.gc_timer);
    else
        osw_bss_schedule_work(&entry->bss->work_timer);

    ds_dlist_remove(&provider->entry_list, entry);
    osw_bss_entry_free(entry);
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
        osw_bss_schedule_work(&entry->bss->work_timer);                     \
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

OSW_BSS_GET_DEFINITION(struct osw_ssid, ssid);
OSW_BSS_GET_DEFINITION(struct osw_channel, channel);
OSW_BSS_GET_DEFINITION(uint8_t, op_class);

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
    ev_run(EV_DEFAULT_ 0);
    assert(ctx.set_cnt_a == 1);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_aa, sizeof(ssid_aa)) == 0);

    osw_bss_entry_set_channel(entry_aa, &channel_a);
    ev_run(EV_DEFAULT_ 0);
    assert(ctx.set_cnt_a == 2);
    assert(memcmp(osw_bss_get_channel(&ctx.bssid_a), &channel_a, sizeof(channel_a)) == 0);

    /* Register the same BSS with different provider */
    entry_ab = osw_bss_map_entry_new(provider_b, &ctx.bssid_a);
    assert(entry_ab != NULL);

    /* Override BSS attribute */
    osw_bss_entry_set_ssid(entry_ab, &ssid_ab);
    ev_run(EV_DEFAULT_ 0);
    assert(ctx.set_cnt_a == 3);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_ab, sizeof(ssid_ab)) == 0);
    assert(osw_bss_get_channel(&ctx.bssid_a) == NULL);

    /* Remove BSS */
    osw_bss_map_entry_free(provider_b, entry_ab);
    ev_run(EV_DEFAULT_ 0);
    assert(ctx.set_cnt_a == 4);
    assert(ctx.unset_cnt_a == 0);
    assert(memcmp(osw_bss_get_ssid(&ctx.bssid_a), &ssid_aa, sizeof(ssid_aa)) == 0);
    assert(memcmp(osw_bss_get_channel(&ctx.bssid_a), &channel_a, sizeof(channel_a)) == 0);

    osw_bss_map_entry_free(provider_a, entry_aa);
    ev_run(EV_DEFAULT_ 0);
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
