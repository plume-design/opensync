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

#include <log.h>
#include <ds_tree.h>
#include <memutil.h>
#include <os.h>
#include <const.h>

#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>
#include <osw_types.h>
#include <osw_util.h>
#include <osw_state.h>
#include <osw_sta_chan_cap.h>

enum osw_sta_chan_cap_sta_chan_flag {
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ,
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ_OP_CLASS,
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_OP_CLASS,
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_CH_LIST,
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ,
    OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ_OP_CLASS,
};

#define LOG_PREFIX(fmt, ...) \
    "osw: sta_chan_cap: " fmt, \
    ## __VA_ARGS__

#define LOG_PREFIX_STA(sta, fmt, ...) \
    LOG_PREFIX(OSW_HWADDR_FMT" [links=%zu]: " fmt, \
        OSW_HWADDR_ARG(&(sta)->addr), \
        osw_sta_chan_cap_sta_get_num_links(sta), \
        ## __VA_ARGS__)

#define LOG_PREFIX_CHAN(c, fmt, ...) \
    LOG_PREFIX_STA((c)->sta, "%dMHz [0x%04x]: " fmt, \
        (c)->freq_mhz, \
        (c)->flags, \
        ## __VA_ARGS__)

#define BIT(x) (1 << (x))
#define OSW_STA_CHAN_CAP_STA_AGEOUT_UNKNOWN_SEC (10 * 60) /* 10 minutes */
#define OSW_STA_CHAN_CAP_STA_AGEOUT_KNOWN_SEC (60 * 60 * 24 * 7) /* 1 week */

struct osw_sta_chan_cap_sta_chan {
    struct ds_tree_node node;
    struct osw_sta_chan_cap_sta *sta;
    int flags;
    int freq_mhz;
};

struct osw_sta_chan_cap_sta_link {
    struct ds_tree_node node;
    struct osw_sta_chan_cap_sta *sta;
    const struct osw_state_vif_info *vif_info;
};

struct osw_sta_chan_cap_sta {
    struct ds_tree_node node;
    struct osw_sta_chan_cap *m;
    struct osw_hwaddr addr;
    struct ds_tree chans;
    struct ds_tree links;
    bool known;
    struct osw_timer ageout;
};

struct osw_sta_chan_cap_obs {
    struct ds_tree_node node;
    struct osw_sta_chan_cap *m;
    struct osw_hwaddr addr;
    const struct osw_sta_chan_cap_ops *ops;
    void *priv;
};

struct osw_sta_chan_cap {
    struct ds_tree stas;
    struct ds_tree obs;
    struct osw_state_observer state_obs;
};

static uint64_t
osw_sta_chan_cap_get_ageout_sec(const struct osw_sta_chan_cap_sta *sta)
{
    if (sta->known) {
        return OSW_STA_CHAN_CAP_STA_AGEOUT_KNOWN_SEC;
    }
    else {
        return OSW_STA_CHAN_CAP_STA_AGEOUT_UNKNOWN_SEC;
    }
}

static const char *
osw_sta_chan_cap_sta_chan_flag_to_cstr(enum osw_sta_chan_cap_sta_chan_flag flag)
{
    switch (flag) {
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ: return "link_freq";
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ_OP_CLASS: return "link_freq_op_class";
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_OP_CLASS: return "ies_op_class";
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_CH_LIST: return "ies_ch_list";
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ: return "probe_freq";
        case OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ_OP_CLASS: return "probe_freq_op_class";
    }
    return "?";
}

static size_t
osw_sta_chan_cap_sta_get_num_links(struct osw_sta_chan_cap_sta *sta)
{
    return ds_tree_len(&sta->links);
}

enum osw_sta_chan_cap_status
osw_sta_chan_cap_supports(osw_sta_chan_cap_obs_t *obs,
                          const struct osw_hwaddr *sta_addr,
                          int freq_mhz)
{
    struct osw_sta_chan_cap_sta *sta = ds_tree_find(&obs->m->stas, sta_addr);
    if (sta == NULL) return OSW_STA_CHAN_CAP_MAYBE;
    if (ds_tree_is_empty(&sta->chans)) return OSW_STA_CHAN_CAP_MAYBE;
    if (ds_tree_find(&sta->chans, &freq_mhz)) return OSW_STA_CHAN_CAP_SUPPORTED;
    return OSW_STA_CHAN_CAP_NOT_SUPPORTED;
}

static void
osw_sta_chan_cap_obs_notify_init(struct osw_sta_chan_cap_obs *obs)
{
    struct osw_sta_chan_cap_sta *sta = ds_tree_find(&obs->m->stas, &obs->addr);
    if (sta == NULL) return;

    struct osw_sta_chan_cap_sta_chan *c;
    ds_tree_foreach(&sta->chans, c) {
        if (obs->ops->added_fn == NULL) continue;
        obs->ops->added_fn(obs->priv, &obs->addr, c->freq_mhz);
    }
}

static void
osw_sta_chan_cap_obs_notify_fini(struct osw_sta_chan_cap_obs *obs)
{
    struct osw_sta_chan_cap_sta *sta = ds_tree_find(&obs->m->stas, &obs->addr);
    if (sta == NULL) return;

    struct osw_sta_chan_cap_sta_chan *c;
    ds_tree_foreach(&sta->chans, c) {
        if (obs->ops->removed_fn == NULL) continue;
        obs->ops->removed_fn(obs->priv, &obs->addr, c->freq_mhz);
    }
}

static void
osw_sta_chan_cap_obs_notify_added(struct osw_sta_chan_cap *m,
                                  const struct osw_sta_chan_cap_sta *sta,
                                  const struct osw_sta_chan_cap_sta_chan *c)
{
    struct osw_sta_chan_cap_obs *o;
    ds_tree_foreach(&m->obs, o) {
        if (osw_hwaddr_is_equal(&o->addr, &sta->addr)) {
            o->ops->added_fn(o->priv, &sta->addr, c->freq_mhz);
        }
    }
}

static void
osw_sta_chan_cap_obs_notify_removed(struct osw_sta_chan_cap *m,
                                    const struct osw_sta_chan_cap_sta *sta,
                                    const struct osw_sta_chan_cap_sta_chan *c)
{
    struct osw_sta_chan_cap_obs *o;
    ds_tree_foreach(&m->obs, o) {
        if (osw_hwaddr_is_equal(&o->addr, &sta->addr)) {
            o->ops->removed_fn(o->priv, &sta->addr, c->freq_mhz);
        }
    }
}

osw_sta_chan_cap_obs_t *
osw_sta_chan_cap_register(osw_sta_chan_cap_t *m,
                          const struct osw_hwaddr *sta_addr,
                          const struct osw_sta_chan_cap_ops *ops,
                          void *priv)
{
    struct osw_sta_chan_cap_obs *obs = CALLOC(1, sizeof(*obs));
    obs->m = m;
    obs->addr = *sta_addr;
    obs->ops = ops;
    obs->priv = priv;
    ds_tree_insert(&m->obs, obs, obs);
    osw_sta_chan_cap_obs_notify_init(obs);
    return obs;
}

void
osw_sta_chan_cap_obs_unregister(osw_sta_chan_cap_obs_t *obs)
{
    ds_tree_remove(&obs->m->obs, obs);
    osw_sta_chan_cap_obs_notify_fini(obs);
    FREE(obs);
}

static void
osw_sta_chan_cap_sta_chan_free(struct osw_sta_chan_cap_sta_chan *c)
{
    LOGT(LOG_PREFIX_CHAN(c, "freeing"));
    ds_tree_remove(&c->sta->chans, c);
    osw_sta_chan_cap_obs_notify_removed(c->sta->m, c->sta, c);
    FREE(c);
}

static struct osw_sta_chan_cap_sta_chan *
osw_sta_chan_cap_sta_chan_get_or_alloc(struct osw_sta_chan_cap_sta *sta,
                                       int freq_mhz)
{
    struct osw_sta_chan_cap_sta_chan *c = ds_tree_find(&sta->chans, &freq_mhz);
    if (c == NULL) {
        c = CALLOC(1, sizeof(*c));
        c->sta = sta;
        c->freq_mhz = freq_mhz;
        ds_tree_insert(&sta->chans, c, &c->freq_mhz);
        osw_sta_chan_cap_obs_notify_added(c->sta->m, sta, c);
        LOGT(LOG_PREFIX_CHAN(c, "allocated"));
    }
    return c;
}

static void
osw_sta_chan_cap_sta_free_chans(struct osw_sta_chan_cap_sta *sta)
{
    struct osw_sta_chan_cap_sta_chan *c;
    while ((c = ds_tree_head(&sta->chans)) != NULL) {
        osw_sta_chan_cap_sta_chan_free(c);
    }
}

static void
osw_sta_chan_cap_sta_free(struct osw_sta_chan_cap_sta *sta)
{
    LOGT(LOG_PREFIX_STA(sta, "freeing"));
    ds_tree_remove(&sta->m->stas, sta);
    osw_sta_chan_cap_sta_free_chans(sta);
    FREE(sta);
}

static void
osw_sta_chan_cap_sta_ageout_cb(struct osw_timer *t)
{
    struct osw_sta_chan_cap_sta *sta = container_of(t, struct osw_sta_chan_cap_sta, ageout);
    LOGT(LOG_PREFIX_STA(sta, "aged out"));
    osw_sta_chan_cap_sta_free(sta);
}

static void
osw_sta_chan_cap_sta_gc(struct osw_sta_chan_cap_sta *sta)
{
    if (osw_sta_chan_cap_sta_get_num_links(sta) > 0) {
        if (osw_timer_is_armed(&sta->ageout) == true) {
            osw_timer_disarm(&sta->ageout);
            LOGT(LOG_PREFIX_STA(sta, "disarming ageout"));
        }
    }
    else if (osw_timer_is_armed(&sta->ageout) == false) {
        const uint64_t ageout = osw_sta_chan_cap_get_ageout_sec(sta);
        const uint64_t at = osw_time_mono_clk() + OSW_TIME_SEC(ageout);
        osw_timer_arm_at_nsec(&sta->ageout, at);
        LOGT(LOG_PREFIX_STA(sta, "arming ageout: %"PRIu64, ageout));
    }
}

static struct osw_sta_chan_cap_sta *
osw_sta_chan_cap_sta_get_or_alloc(struct osw_sta_chan_cap *m,
                                  const struct osw_hwaddr *addr)
{
    struct osw_sta_chan_cap_sta *sta = ds_tree_find(&m->stas, addr);
    if (sta == NULL) {
        sta = CALLOC(1, sizeof(*sta));
        sta->m = m;
        sta->addr = *addr;
        ds_tree_insert(&m->stas, sta, &sta->addr);
        ds_tree_init(&sta->chans, ds_int_cmp, struct osw_sta_chan_cap_sta_chan, node);
        ds_tree_init(&sta->links, ds_void_cmp, struct osw_sta_chan_cap_sta_link, node);
        osw_timer_init(&sta->ageout, osw_sta_chan_cap_sta_ageout_cb);
        osw_sta_chan_cap_sta_gc(sta);
        LOGT(LOG_PREFIX_STA(sta, "allocated"));
    }
    return sta;
}

static void
osw_sta_chan_cap_sta_link_add(struct osw_sta_chan_cap_sta *sta,
                              const struct osw_state_vif_info *vif_info)
{
    struct osw_sta_chan_cap_sta_link *l = CALLOC(1, sizeof(*l));
    sta->known = true;
    l->sta = sta;
    l->vif_info = vif_info;
    ds_tree_insert(&sta->links, l, l->vif_info);
    LOGT(LOG_PREFIX_STA(sta, "link added: %s", vif_info->vif_name));
    osw_sta_chan_cap_sta_gc(sta);
}

static void
osw_sta_chan_cap_sta_link_del(struct osw_sta_chan_cap_sta *sta,
                              const struct osw_state_vif_info *vif_info)

{
    struct osw_sta_chan_cap_sta_link *l = ds_tree_find(&sta->links, vif_info);
    if (l == NULL) return;
    ds_tree_remove(&sta->links, l);
    FREE(l);
    LOGT(LOG_PREFIX_STA(sta, "link removed: %s", vif_info->vif_name));
    osw_sta_chan_cap_sta_gc(sta);
}

static void
osw_sta_chan_cap_sta_set_freq(struct osw_sta_chan_cap_sta *sta,
                              int freq_mhz,
                              enum osw_sta_chan_cap_sta_chan_flag flag)
{
    if (freq_mhz == 0) return;

    struct osw_sta_chan_cap_sta_chan *c = osw_sta_chan_cap_sta_chan_get_or_alloc(sta, freq_mhz);
    const int bit = BIT(flag);
    if ((c->flags & bit) == 0) {
        c->flags |= bit;
        LOGD(LOG_PREFIX_CHAN(c, "set %s", osw_sta_chan_cap_sta_chan_flag_to_cstr(flag)));
    }
}

static void
osw_sta_chan_cap_sta_set_op_class(struct osw_sta_chan_cap_sta *sta,
                                  uint8_t op_class,
                                  enum osw_sta_chan_cap_sta_chan_flag flag)
{
    int *freqs = osw_op_class_to_freqs(op_class);
    const int *freq = freqs;

    for (; freq != NULL && *freq != 0; freq++) {
        osw_sta_chan_cap_sta_set_freq(sta, *freq, flag);
    }

    FREE(freqs);
}

static void
osw_sta_chan_cap_sta_parse_ies(struct osw_sta_chan_cap_sta *sta,
                               const void *ies,
                               size_t len)
{
    struct osw_assoc_req_info info;
    const bool ok = osw_parse_assoc_req_ies(ies, len, &info);
    if (ok == false) return;

    size_t i;
    for (i = 0; i < info.op_class_cnt; i++) {
        const uint8_t op_class = info.op_class_list[i];
        osw_sta_chan_cap_sta_set_op_class(sta, op_class, OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_OP_CLASS);
    }

    for (i = 0; i < info.channel_cnt; i++) {
        const uint8_t chan = info.channel_list[i];
        const enum osw_band band = osw_chan_to_band_guess(chan);
        const int freq_mhz = osw_chan_to_freq(band, chan);
        if (freq_mhz == 0) continue;
        osw_sta_chan_cap_sta_set_freq(sta, freq_mhz, OSW_STA_CHAN_CAP_STA_CHAN_FLAG_IES_CH_LIST);
    }
}

static void
osw_sta_chan_cap_sta_parse_vif(struct osw_sta_chan_cap_sta *sta,
                               const struct osw_drv_vif_state *state,
                               enum osw_sta_chan_cap_sta_chan_flag flag,
                               enum osw_sta_chan_cap_sta_chan_flag flag_op_class)
{
    if (state->vif_type != OSW_VIF_AP) return;
    if (state->status != OSW_VIF_ENABLED) return;

    const int freq_mhz = state->u.ap.channel.control_freq_mhz;

    struct osw_channel c;
    MEMZERO(c);
    c.width = OSW_CHANNEL_20MHZ;
    c.control_freq_mhz = freq_mhz;
    c.center_freq0_mhz = freq_mhz;

    uint8_t op_class;
    const bool ok = osw_channel_to_op_class(&c, &op_class);
    if (ok) {
        osw_sta_chan_cap_sta_set_op_class(sta, op_class, flag_op_class);
    }

    osw_sta_chan_cap_sta_set_freq(sta, freq_mhz, flag);
}

static void
osw_sta_chan_cap_sta_parse_vif_link(struct osw_sta_chan_cap_sta *sta,
                                    const struct osw_drv_vif_state *state)
{
    osw_sta_chan_cap_sta_parse_vif(sta,
                                   state,
                                   OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ,
                                   OSW_STA_CHAN_CAP_STA_CHAN_FLAG_LINK_FREQ_OP_CLASS);
}

static void
osw_sta_chan_cap_sta_parse_vif_probe(struct osw_sta_chan_cap_sta *sta,
                                     const struct osw_drv_vif_state *state)
{
    osw_sta_chan_cap_sta_parse_vif(sta,
                                   state,
                                   OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ,
                                   OSW_STA_CHAN_CAP_STA_CHAN_FLAG_PROBE_FREQ_OP_CLASS);
}

static void
osw_sta_chan_cap_sta_connected_cb(struct osw_state_observer *obs,
                                  const struct osw_state_sta_info *sta_info)
{
    struct osw_sta_chan_cap *m = container_of(obs, struct osw_sta_chan_cap, state_obs);
    struct osw_sta_chan_cap_sta *sta = osw_sta_chan_cap_sta_get_or_alloc(m, sta_info->mac_addr);
    osw_sta_chan_cap_sta_parse_ies(sta, sta_info->assoc_req_ies, sta_info->assoc_req_ies_len);
    osw_sta_chan_cap_sta_parse_vif_link(sta, sta_info->vif->drv_state);
    osw_sta_chan_cap_sta_link_add(sta, sta_info->vif);
}

static void
osw_sta_chan_cap_sta_disconnected_cb(struct osw_state_observer *obs,
                                     const struct osw_state_sta_info *sta_info)
{
    struct osw_sta_chan_cap *m = container_of(obs, struct osw_sta_chan_cap, state_obs);
    struct osw_sta_chan_cap_sta *sta = osw_sta_chan_cap_sta_get_or_alloc(m, sta_info->mac_addr);
    osw_sta_chan_cap_sta_link_del(sta, sta_info->vif);
}

static void
osw_sta_chan_cap_sta_changed_cb(struct osw_state_observer *obs,
                                const struct osw_state_sta_info *sta_info)
{
    struct osw_sta_chan_cap *m = container_of(obs, struct osw_sta_chan_cap, state_obs);
    struct osw_sta_chan_cap_sta *sta = osw_sta_chan_cap_sta_get_or_alloc(m, sta_info->mac_addr);
    osw_sta_chan_cap_sta_parse_ies(sta, sta_info->assoc_req_ies, sta_info->assoc_req_ies_len);
    osw_sta_chan_cap_sta_parse_vif_link(sta, sta_info->vif->drv_state);
}

static void
osw_sta_chan_cap_vif_probe_req_cb(struct osw_state_observer *obs,
                                  const struct osw_state_vif_info *vif_info,
                                  const struct osw_drv_report_vif_probe_req *probe_req)
{
    struct osw_sta_chan_cap *m = container_of(obs, struct osw_sta_chan_cap, state_obs);
    struct osw_sta_chan_cap_sta *sta = osw_sta_chan_cap_sta_get_or_alloc(m, &probe_req->sta_addr);
    osw_sta_chan_cap_sta_parse_vif_probe(sta, vif_info->drv_state);
}

static void
osw_sta_chan_cap_sta_vif_changed_cb(struct osw_state_observer *obs,
                                    const struct osw_state_vif_info *vif_info)
{
    struct osw_sta_chan_cap *m = container_of(obs, struct osw_sta_chan_cap, state_obs);
    struct osw_sta_chan_cap_sta *sta;
    ds_tree_foreach(&m->stas, sta) {
        struct osw_sta_chan_cap_sta_link *l;
        ds_tree_foreach(&sta->links, l) {
            if (l->vif_info != vif_info) continue;
            osw_sta_chan_cap_sta_parse_vif_link(sta, vif_info->drv_state);
        }
    }
}

static void
osw_sta_chan_cap_init(struct osw_sta_chan_cap *m)
{
    static const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .sta_connected_fn = osw_sta_chan_cap_sta_connected_cb,
        .sta_disconnected_fn = osw_sta_chan_cap_sta_disconnected_cb,
        .sta_changed_fn = osw_sta_chan_cap_sta_changed_cb,
        .vif_changed_fn = osw_sta_chan_cap_sta_vif_changed_cb,
        .vif_probe_req_fn = osw_sta_chan_cap_vif_probe_req_cb,
    };
    m->state_obs = state_obs;
    ds_tree_init(&m->stas, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_sta_chan_cap_sta, node);
    ds_tree_init(&m->obs, ds_void_cmp, struct osw_sta_chan_cap_obs, node);
}

static void
osw_sta_chan_cap_attach(struct osw_sta_chan_cap *m)
{
    OSW_MODULE_LOAD(osw_state);
    osw_state_register_observer(&m->state_obs);
}

static struct osw_sta_chan_cap *
osw_sta_chan_cap_load(void)
{
    static struct osw_sta_chan_cap *m;
    if (m == NULL) {
        m = CALLOC(1, sizeof(*m));
        osw_sta_chan_cap_init(m);
        osw_sta_chan_cap_attach(m);
    }
    return m;
}

OSW_MODULE(osw_sta_chan_cap)
{
    return osw_sta_chan_cap_load();
}
