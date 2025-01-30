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

#include <const.h>
#include <ds_tree.h>
#include <memutil.h>
#include <os.h>
#include <util.h>

#include <osw_mld_vif.h>
#include <osw_state.h>
#include <osw_ut.h>

struct osw_mld_vif
{
    struct ds_tree mlds;
    struct ds_tree observers;
    struct osw_state_observer state_obs;
};

struct osw_mld_vif_observer
{
    struct ds_tree_node node;
    osw_mld_vif_mld_fn_t *mld_added_fn;
    osw_mld_vif_mld_fn_t *mld_removed_fn;
    osw_mld_vif_mld_fn_t *mld_connected_fn;
    osw_mld_vif_mld_fn_t *mld_disconnected_fn;
    osw_mld_vif_link_fn_t *link_added_fn;
    osw_mld_vif_link_fn_t *link_changed_fn;
    osw_mld_vif_link_fn_t *link_connected_fn;
    osw_mld_vif_link_fn_t *link_disconnected_fn;
    osw_mld_vif_link_fn_t *link_removed_fn;
    void *mld_added_fn_priv;
    void *mld_connected_fn_priv;
    void *mld_disconnected_fn_priv;
    void *mld_removed_fn_priv;
    void *link_added_fn_priv;
    void *link_changed_fn_priv;
    void *link_connected_fn_priv;
    void *link_disconnected_fn_priv;
    void *link_removed_fn_priv;
    struct osw_mld_vif *m;
};

struct osw_mld_vif_mld
{
    char *mld_if_name;
    struct ds_tree_node node;
    struct ds_tree links;
    struct osw_mld_vif *m;
    bool was_connected;
};

struct osw_mld_vif_link
{
    char *link_if_name;
    const struct osw_state_vif_info *info;
    bool was_connected;
    struct ds_tree_node node;
    struct osw_mld_vif_mld *mld;
};

#define LOG_PREFIX(fmt, ...) "osw_mld_vif: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_MLD(mld, fmt, ...) LOG_PREFIX("%s: " fmt, mld->mld_if_name, ##__VA_ARGS__)
#define LOG_PREFIX_LINK(link, fmt, ...) LOG_PREFIX_MLD(link->mld, "%s: " fmt, link->link_if_name, ##__VA_ARGS__)

#define OSW_MLD_VIF_NOTIFY(m, name, ...) \
    do \
    { \
        struct osw_mld_vif_observer *i; \
        ds_tree_foreach (&(m)->observers, i) \
        { \
            if (i->name == NULL) continue; \
            i->name(i->name##_priv, ##__VA_ARGS__); \
        } \
    } while (0)

#define OSW_MLD_VIF_NOTIFY_DEFINE(name, type) \
    void osw_mld_vif_observer_set_##name(osw_mld_vif_observer_t *obs, type *fn, void *priv) \
    { \
        obs->name = fn; \
        obs->name##_priv = priv; \
    }

OSW_MLD_VIF_NOTIFY_DEFINE(mld_added_fn, osw_mld_vif_mld_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(mld_removed_fn, osw_mld_vif_mld_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(mld_connected_fn, osw_mld_vif_mld_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(mld_disconnected_fn, osw_mld_vif_mld_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(link_added_fn, osw_mld_vif_link_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(link_changed_fn, osw_mld_vif_link_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(link_connected_fn, osw_mld_vif_link_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(link_disconnected_fn, osw_mld_vif_link_fn_t)
OSW_MLD_VIF_NOTIFY_DEFINE(link_removed_fn, osw_mld_vif_link_fn_t)

osw_mld_vif_observer_t *osw_mld_vif_observer_alloc(osw_mld_vif_t *m)
{
    osw_mld_vif_observer_t *obs = CALLOC(1, sizeof(*obs));
    obs->m = m;
    ds_tree_insert(&m->observers, obs, obs);
    return obs;
}

void osw_mld_vif_observer_drop(osw_mld_vif_observer_t *obs)
{
    ds_tree_remove(&obs->m->observers, obs);
    FREE(obs);
}

static struct osw_mld_vif_mld *osw_mld_vif_mld_alloc(struct osw_mld_vif *m, const char *mld_if_name)
{
    struct osw_mld_vif_mld *mld = CALLOC(1, sizeof(*mld));
    mld->m = m;
    mld->mld_if_name = STRDUP(mld_if_name);
    ds_tree_insert(&m->mlds, mld, mld->mld_if_name);
    ds_tree_init(&mld->links, ds_str_cmp, struct osw_mld_vif_link, node);
    LOGD(LOG_PREFIX_MLD(mld, "allocated"));
    OSW_MLD_VIF_NOTIFY(m, mld_added_fn, mld_if_name);
    return mld;
}

static bool osw_mld_vif_link_is_connected(struct osw_mld_vif_link *l)
{
    if (l == NULL) return false;
    switch (l->info->drv_state->vif_type)
    {
        case OSW_VIF_AP:
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            return (l->info->drv_state->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED);
        case OSW_VIF_UNDEFINED:
            break;
    }
    return false;
}

static bool osw_mld_vif_mld_is_connected(struct osw_mld_vif_mld *mld)
{
    if (mld == NULL) return false;
    struct osw_mld_vif_link *l;
    ds_tree_foreach (&mld->links, l)
    {
        if (osw_mld_vif_link_is_connected(l))
        {
            return true;
        }
    }
    return false;
}

static void osw_mld_vif_mld_gc(struct osw_mld_vif_mld *mld)
{
    if (mld == NULL) return;
    if (ds_tree_len(&mld->links) > 0) return;
    LOGD(LOG_PREFIX_MLD(mld, "freeing"));
    OSW_MLD_VIF_NOTIFY(mld->m, mld_removed_fn, mld->mld_if_name);
    FREE(mld->mld_if_name);
    FREE(mld);
}

static struct osw_mld_vif_link *osw_mld_vif_link_alloc(
        struct osw_mld_vif_mld *mld,
        const struct osw_state_vif_info *info)
{
    if (mld == NULL) return NULL;
    struct osw_mld_vif_link *l = CALLOC(1, sizeof(*l));
    l->link_if_name = STRDUP(info->vif_name);
    l->info = info;
    l->mld = mld;
    ds_tree_insert(&mld->links, l, l->link_if_name);
    LOGD(LOG_PREFIX_LINK(l, "allocated"));
    OSW_MLD_VIF_NOTIFY(mld->m, link_added_fn, mld->mld_if_name, info);
    return l;
}

static struct osw_mld_vif_link *osw_mld_vif_link_get_any(struct osw_mld_vif *m, const char *link_if_name)
{
    struct osw_mld_vif_mld *mld;
    ds_tree_foreach (&m->mlds, mld)
    {
        struct osw_mld_vif_link *l = ds_tree_find(&mld->links, link_if_name);
        if (l != NULL)
        {
            return l;
        }
    }
    return NULL;
}

static struct osw_mld_vif_link *osw_mld_vif_link_get(struct osw_mld_vif_mld *mld, const char *link_if_name)
{
    if (mld == NULL) return NULL;
    return ds_tree_find(&mld->links, link_if_name);
}

static void osw_mld_vif_link_drop(struct osw_mld_vif_link *l)
{
    if (l == NULL) return;
    LOGD(LOG_PREFIX_LINK(l, "freeing"));
    OSW_MLD_VIF_NOTIFY(l->mld->m, link_removed_fn, l->mld->mld_if_name, l->info);
    osw_mld_vif_mld_gc(l->mld);
    ds_tree_remove(&l->mld->links, l);
    FREE(l->link_if_name);
    FREE(l);
}

static const char *osw_mld_vif_state_get_mld_if_name(const struct osw_state_vif_info *info)
{
    switch (info->drv_state->vif_type)
    {
        case OSW_VIF_AP:
            if (osw_hwaddr_is_zero(&info->drv_state->u.ap.mld.addr))
            {
                return NULL;
            }
            if (strlen(info->drv_state->u.ap.mld.if_name.buf) > 0)
            {
                return info->drv_state->u.ap.mld.if_name.buf;
            }
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            if (osw_hwaddr_is_zero(&info->drv_state->u.sta.mld.addr))
            {
                return NULL;
            }
            if (strlen(info->drv_state->u.sta.mld.if_name.buf) > 0)
            {
                return info->drv_state->u.sta.mld.if_name.buf;
            }
            break;
        case OSW_VIF_UNDEFINED:
            break;
    }
    return NULL;
}

static void osw_mld_vif_state_vif_update(struct osw_state_observer *obs, const struct osw_state_vif_info *info)
{
    struct osw_mld_vif *m = container_of(obs, struct osw_mld_vif, state_obs);
    const char *mld_if_name = osw_mld_vif_state_get_mld_if_name(info);
    const char *link_if_name = info->vif_name;

    struct osw_mld_vif_mld *mld = (mld_if_name == NULL) ? NULL : ds_tree_find(&m->mlds, mld_if_name);
    struct osw_mld_vif_link *l = osw_mld_vif_link_get(mld, link_if_name) ?: osw_mld_vif_link_get_any(m, link_if_name);

    if (info->drv_state->exists)
    {
        if (mld_if_name == NULL)
        {
            osw_mld_vif_link_drop(l);
        }
        else if (l == NULL)
        {
            if (mld == NULL)
            {
                mld = osw_mld_vif_mld_alloc(m, mld_if_name);
            }
            l = osw_mld_vif_link_alloc(mld, info);
        }
        else
        {
            OSW_MLD_VIF_NOTIFY(m, link_changed_fn, mld_if_name, info);
        }
        if (l != NULL)
        {
            const bool is_connected = osw_mld_vif_link_is_connected(l);
            if (is_connected != l->was_connected)
            {
                l->was_connected = is_connected;
                LOGD(LOG_PREFIX_LINK(l, "connected: %d", is_connected));
                if (is_connected)
                {
                    OSW_MLD_VIF_NOTIFY(m, link_connected_fn, mld_if_name, info);
                }
                else
                {
                    OSW_MLD_VIF_NOTIFY(m, link_disconnected_fn, mld_if_name, info);
                }
            }
        }
    }
    else
    {
        if (l != NULL)
        {
            osw_mld_vif_link_drop(l);
        }
    }

    if (mld != NULL)
    {
        const bool is_connected = osw_mld_vif_mld_is_connected(mld);
        if (is_connected != mld->was_connected)
        {
            mld->was_connected = is_connected;
            LOGD(LOG_PREFIX_MLD(mld, "connected: %d", is_connected));
            if (is_connected)
            {
                OSW_MLD_VIF_NOTIFY(m, mld_connected_fn, mld_if_name);
            }
            else
            {
                OSW_MLD_VIF_NOTIFY(m, mld_disconnected_fn, mld_if_name);
            }
        }

        osw_mld_vif_mld_gc(mld);
    }
}

static void osw_mld_vif_init(struct osw_mld_vif *m)
{
    ds_tree_init(&m->mlds, ds_str_cmp, struct osw_mld_vif_mld, node);
    ds_tree_init(&m->observers, ds_void_cmp, struct osw_mld_vif_observer, node);
    MEMZERO(m->state_obs);
    m->state_obs.name = __FILE__;
    m->state_obs.vif_added_fn = osw_mld_vif_state_vif_update;
    m->state_obs.vif_changed_fn = osw_mld_vif_state_vif_update;
    m->state_obs.vif_removed_fn = osw_mld_vif_state_vif_update;
}

static void osw_mld_vif_attach(struct osw_mld_vif *m)
{
    osw_state_register_observer(&m->state_obs);
}

static struct osw_mld_vif *osw_mld_vif_alloc(void)
{
    struct osw_mld_vif *m = CALLOC(1, sizeof(*m));
    osw_mld_vif_init(m);
    osw_mld_vif_attach(m);
    return m;
}

OSW_MODULE(osw_mld_vif)
{
    OSW_MODULE_LOAD(osw_state);
    return osw_mld_vif_alloc();
}
