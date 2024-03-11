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
#include <log.h>
#include <memutil.h>
#include <ovsdb_table.h>

#include <osw_mld_vif.h>
#include <osw_timer.h>
#include <ow_ovsdb_mld_onboard.h>

struct ow_ovsdb_mld_onboard
{
    struct ds_tree mlds;
    struct ds_tree links;
    ovsdb_table_t table; /* Wifi_Inet_Config */
    osw_mld_vif_observer_t *mld_obs;
    struct osw_timer work;
};

struct ow_ovsdb_mld_onboard_mld
{
    char *mld_if_name;
    struct ds_tree_node node;
    struct ds_tree links;
    struct ow_ovsdb_mld_onboard *m;
    struct schema_Wifi_Inet_Config *template;
    bool row_exists_in_ovsdb;
    bool handled;
};

struct ow_ovsdb_mld_onboard_link
{
    char *link_if_name;
    struct ds_tree_node mld_node;
    struct ds_tree_node m_node;
    struct ow_ovsdb_mld_onboard_mld *mld;
    bool row_exists_in_ovsdb;
    bool handled;
};

#define LOG_PREFIX(fmt, ...) "ow_ovsdb_mld_onboard: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_MLD(mld, fmt, ...) LOG_PREFIX("%s: " fmt, (mld)->mld_if_name, ##__VA_ARGS__)
#define LOG_PREFIX_LINK(link, fmt, ...) LOG_PREFIX_MLD((link)->mld, "%s: " fmt, (link)->link_if_name, ##__VA_ARGS__)

static struct ow_ovsdb_mld_onboard_mld *ow_ovsdb_mld_onboard_mld_alloc(
        struct ow_ovsdb_mld_onboard *m,
        const char *mld_if_name)
{
    struct ow_ovsdb_mld_onboard_mld *mld = CALLOC(1, sizeof(*mld));
    mld->m = m;
    mld->mld_if_name = STRDUP(mld_if_name);
    ds_tree_init(&mld->links, ds_str_cmp, struct ow_ovsdb_mld_onboard_link, mld_node);
    ds_tree_insert(&m->mlds, mld, mld->mld_if_name);
    LOGD(LOG_PREFIX_MLD(mld, "allocated"));
    return mld;
}

static void ow_ovsdb_mld_onboard_mld_drop(struct ow_ovsdb_mld_onboard_mld *mld)
{
    if (mld == NULL) return;
    if (WARN_ON(ds_tree_len(&mld->links) > 0)) return;
    if (WARN_ON(mld->m == NULL)) return;
    LOGD(LOG_PREFIX_MLD(mld, "dropping"));
    ds_tree_remove(&mld->m->mlds, mld);
    FREE(mld->mld_if_name);
    FREE(mld);
}

static struct ow_ovsdb_mld_onboard_mld *ow_ovsdb_mld_onboard_mld_get(
        struct ow_ovsdb_mld_onboard *m,
        const char *mld_if_name)
{
    if (m == NULL) return NULL;
    if (mld_if_name == NULL) return NULL;
    return ds_tree_find(&m->mlds, mld_if_name);
}

static struct ow_ovsdb_mld_onboard_link *ow_ovsdb_mld_onboard_link_alloc(
        struct ow_ovsdb_mld_onboard_mld *mld,
        const char *link_if_name)
{
    struct ow_ovsdb_mld_onboard_link *l = CALLOC(1, sizeof(*l));
    l->mld = mld;
    l->link_if_name = STRDUP(link_if_name);
    ds_tree_insert(&mld->links, l, l->link_if_name);
    ds_tree_insert(&mld->m->links, l, l->link_if_name);
    LOGD(LOG_PREFIX_LINK(l, "allocated"));
    return l;
}

static void ow_ovsdb_mld_onboard_link_drop(struct ow_ovsdb_mld_onboard_link *l)
{
    if (l == NULL) return;
    if (WARN_ON(l->mld == NULL)) return;
    if (WARN_ON(l->mld->m == NULL)) return;
    LOGD(LOG_PREFIX_LINK(l, "dropping"));
    ds_tree_remove(&l->mld->m->links, l);
    ds_tree_remove(&l->mld->links, l);
    FREE(l->link_if_name);
    FREE(l);
}

static struct ow_ovsdb_mld_onboard_link *ow_ovsdb_mld_onboard_link_get_m(
        struct ow_ovsdb_mld_onboard *m,
        const char *link_if_name)
{
    if (m == NULL) return NULL;
    if (link_if_name == NULL) return NULL;
    return ds_tree_find(&m->links, link_if_name);
}

static struct ow_ovsdb_mld_onboard_link *ow_ovsdb_mld_onboard_link_get_mld(
        struct ow_ovsdb_mld_onboard_mld *mld,
        const char *link_if_name)
{
    if (mld == NULL) return NULL;
    if (link_if_name == NULL) return NULL;
    return ds_tree_find(&mld->links, link_if_name);
}

static void ow_ovsdb_mld_onboard_mld_set_template(
        struct ow_ovsdb_mld_onboard_mld *mld,
        const struct schema_Wifi_Inet_Config *row)
{
    if (mld == NULL) return;
    if (mld->template != NULL)
    {
        LOGD(LOG_PREFIX_MLD(mld, "template: ignoring %s, already stored %s", row->if_name, mld->template->if_name));
        return;
    }
    LOGD(LOG_PREFIX_MLD(mld, "template: setting %s", row->if_name));
    mld->template = MEMNDUP(row, sizeof(*row));
    mld->handled = false;
    SCHEMA_SET_STR(mld->template->if_name, mld->mld_if_name);
}

static void ow_ovsdb_mld_onboard_link_try_set_template(struct ow_ovsdb_mld_onboard_link *l)
{
    const char *col = SCHEMA_COLUMN(Wifi_Inet_Config, if_name);
    json_t *cond = ovsdb_tran_cond(OCLM_STR, col, OFUNC_EQ, l->link_if_name);
    struct schema_Wifi_Inet_Config row;
    struct ow_ovsdb_mld_onboard *m = l->mld->m;
    l->row_exists_in_ovsdb = ovsdb_table_select_one_where(&m->table, cond, &row);
    if (l->row_exists_in_ovsdb)
    {
        ow_ovsdb_mld_onboard_mld_set_template(l->mld, &row);
        if (getenv("OW_OVSDB_MLD_ONBOARD_NO_DEL")) return;
        LOGN(LOG_PREFIX_LINK(l, "removing"));
        const int count = ovsdb_table_delete(&m->table, &row);
        WARN_ON(count != 1);
    }
}

static void ow_ovsdb_mld_onboard_link_work(struct ow_ovsdb_mld_onboard_link *l)
{
    if (l->handled == true) return;
    l->handled = true;

    LOGT(LOG_PREFIX_LINK(l, "working"));
    ow_ovsdb_mld_onboard_link_try_set_template(l);
}

static void ow_ovsdb_mld_onboard_mld_try_insert(struct ow_ovsdb_mld_onboard_mld *mld)
{
    if (mld->template == NULL) return;
    const char *col = SCHEMA_COLUMN(Wifi_Inet_Config, if_name);
    json_t *cond = ovsdb_tran_cond(OCLM_STR, col, OFUNC_EQ, mld->mld_if_name);
    struct schema_Wifi_Inet_Config row;
    struct ow_ovsdb_mld_onboard *m = mld->m;
    const bool row_exists_in_ovsdb = ovsdb_table_select_one_where(&m->table, cond, &row);
    const bool need_insert = (row_exists_in_ovsdb == false);
    if (need_insert)
    {
        if (getenv("OW_OVSDB_MLD_ONBOARD_NO_ADD")) return;
        LOGN(LOG_PREFIX_MLD(mld, "inserting"));
        const bool inserted = ovsdb_table_insert(&m->table, mld->template);
        WARN_ON(inserted == false);
    }
    else
    {
        LOGD(LOG_PREFIX_MLD(mld, "already exists"));
    }
}

static void ow_ovsdb_mld_onboard_mld_work(struct ow_ovsdb_mld_onboard_mld *mld)
{
    if (mld->handled == true) return;
    mld->handled = true;

    LOGT(LOG_PREFIX_MLD(mld, "working"));
    ow_ovsdb_mld_onboard_mld_try_insert(mld);
    FREE(mld->template);
    mld->template = NULL;
}

static void ow_ovsdb_mld_onboard_work(struct ow_ovsdb_mld_onboard *m)
{
    LOGD(LOG_PREFIX("working"));

    {
        struct ow_ovsdb_mld_onboard_link *l;
        ds_tree_foreach (&m->links, l)
        {
            ow_ovsdb_mld_onboard_link_work(l);
        }
    }

    {
        struct ow_ovsdb_mld_onboard_mld *mld;
        ds_tree_foreach (&m->mlds, mld)
        {
            ow_ovsdb_mld_onboard_mld_work(mld);
        }
    }
}

static void ow_ovsdb_mld_onboard_work_cb(struct osw_timer *t)
{
    struct ow_ovsdb_mld_onboard *m = container_of(t, struct ow_ovsdb_mld_onboard, work);
    ow_ovsdb_mld_onboard_work(m);
}

static void ow_ovsdb_mld_onboard_sched_work(struct ow_ovsdb_mld_onboard *m)
{
    osw_timer_arm_at_nsec(&m->work, 0 /* ASAP */);
}

static void ow_ovsdb_mld_onboard_mld_invalidate(struct ow_ovsdb_mld_onboard_mld *mld)
{
    if (mld == NULL) return;
    if (mld->handled == false) return;
    LOGD(LOG_PREFIX_MLD(mld, "invalidating"));
    mld->handled = false;
    ow_ovsdb_mld_onboard_sched_work(mld->m);
}

static void ow_ovsdb_mld_onboard_link_invalidate(struct ow_ovsdb_mld_onboard_link *l)
{
    if (l == NULL) return;
    if (l->handled == false) return;
    LOGD(LOG_PREFIX_LINK(l, "invalidating"));
    l->handled = false;
    ow_ovsdb_mld_onboard_sched_work(l->mld->m);
}

static void ow_ovsdb_mld_onboard_update(struct ow_ovsdb_mld_onboard *m, const char *if_name)
{
    struct ow_ovsdb_mld_onboard_mld *mld = ow_ovsdb_mld_onboard_mld_get(m, if_name);
    struct ow_ovsdb_mld_onboard_link *l = ow_ovsdb_mld_onboard_link_get_m(m, if_name);
    ow_ovsdb_mld_onboard_mld_invalidate(mld);
    ow_ovsdb_mld_onboard_link_invalidate(l);
}

static void ow_ovsdb_mld_onboard_wifi_inet_config_callback(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Master_State *old,
        struct schema_Wifi_Master_State *rec)
{
    ovsdb_table_t *table = mon->mon_data;
    struct ow_ovsdb_mld_onboard *m = container_of(table, struct ow_ovsdb_mld_onboard, table);
    const char *if_name = strlen(rec->if_name) > 0 ? rec->if_name : old->if_name;
    ow_ovsdb_mld_onboard_update(m, if_name);
}

static void ow_ovsdb_mld_onboard_init(struct ow_ovsdb_mld_onboard *m)
{
    osw_timer_init(&m->work, ow_ovsdb_mld_onboard_work_cb);
    ds_tree_init(&m->mlds, ds_str_cmp, struct ow_ovsdb_mld_onboard_mld, node);
    ds_tree_init(&m->links, ds_str_cmp, struct ow_ovsdb_mld_onboard_link, m_node);
}

static void ow_ovsdb_mld_onboard_mld_added_cb(void *priv, const char *mld_if_name)
{
    struct ow_ovsdb_mld_onboard *m = priv;
    if (WARN_ON(ow_ovsdb_mld_onboard_mld_get(m, mld_if_name))) return;
    ow_ovsdb_mld_onboard_mld_alloc(m, mld_if_name);
    ow_ovsdb_mld_onboard_sched_work(m);
}

static void ow_ovsdb_mld_onboard_mld_removed_cb(void *priv, const char *mld_if_name)
{
    struct ow_ovsdb_mld_onboard *m = priv;
    struct ow_ovsdb_mld_onboard_mld *mld = ow_ovsdb_mld_onboard_mld_get(m, mld_if_name);
    ow_ovsdb_mld_onboard_mld_drop(mld);
    ow_ovsdb_mld_onboard_sched_work(m);
}

static void ow_ovsdb_mld_onboard_link_added_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    struct ow_ovsdb_mld_onboard *m = priv;
    struct ow_ovsdb_mld_onboard_mld *mld = ow_ovsdb_mld_onboard_mld_get(m, mld_if_name);
    const char *link_if_name = info->vif_name;
    ow_ovsdb_mld_onboard_link_alloc(mld, link_if_name);
    ow_ovsdb_mld_onboard_sched_work(m);
}

static void ow_ovsdb_mld_onboard_link_removed_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    const char *link_if_name = info->vif_name;
    struct ow_ovsdb_mld_onboard *m = priv;
    struct ow_ovsdb_mld_onboard_mld *mld = ow_ovsdb_mld_onboard_mld_get(m, mld_if_name);
    struct ow_ovsdb_mld_onboard_link *l = ow_ovsdb_mld_onboard_link_get_mld(mld, link_if_name);
    ow_ovsdb_mld_onboard_link_drop(l);
    ow_ovsdb_mld_onboard_sched_work(m);
}

static void ow_ovsdb_mld_onboard_attach_obs(struct ow_ovsdb_mld_onboard *m)
{
    if (m == NULL) return;
    if (m->mld_obs) return;
    m->mld_obs = osw_mld_vif_observer_alloc(OSW_MODULE_LOAD(osw_mld_vif));
    osw_mld_vif_observer_set_mld_added_fn(m->mld_obs, ow_ovsdb_mld_onboard_mld_added_cb, m);
    osw_mld_vif_observer_set_mld_removed_fn(m->mld_obs, ow_ovsdb_mld_onboard_mld_removed_cb, m);
    osw_mld_vif_observer_set_link_added_fn(m->mld_obs, ow_ovsdb_mld_onboard_link_added_cb, m);
    osw_mld_vif_observer_set_link_removed_fn(m->mld_obs, ow_ovsdb_mld_onboard_link_removed_cb, m);
}

static void ow_ovsdb_mld_onboard_detach_obs(struct ow_ovsdb_mld_onboard *m)
{
    if (m == NULL) return;
    if (m->mld_obs == NULL) return;
    osw_mld_vif_observer_drop(m->mld_obs);
    m->mld_obs = NULL;
}

static void ow_ovsdb_mld_onboard_attach_ovsdb(struct ow_ovsdb_mld_onboard *m)
{
    /* Ideally the code should register for matching row
     * monitoring for each mld and link entity. However
     * JSON-RPC does not support that. Only entire table can
     * be monitored. The best that can be done is to filter
     * out columns and types of notifications.
     *
     * That would be too expensive to do per link or mld
     * because encoding and decoding JSON notifications N
     * times would waste a lot of CPU.
     *
     * Instead a single monitor is installed that is used
     * solely to notify about changes that later perform a
     * select operation on particular rows.
     *
     * The only alternative to be efficient _and_ have
     * fine-grained monitoring would be to introduce a
     * "global" ovsdb_table/ovsdb_cache multiplexer that
     * subscribers can hook up to - deduplicating the need
     * for every single one of them to register monitor
     * instances themselves.
     */
    OVSDB_TABLE_VAR_INIT(&m->table, Wifi_Inet_Config, if_name);
    ovsdb_table_monitor_filter(&m->table, (void *)ow_ovsdb_mld_onboard_wifi_inet_config_callback, C_VPACK("if_name"));
}

static void ow_ovsdb_mld_onboard_detach_ovsdb(struct ow_ovsdb_mld_onboard *m)
{
    ovsdb_table_fini(&m->table);
}

static void ow_ovsdb_mld_onboard_attach(struct ow_ovsdb_mld_onboard *m)
{
    ow_ovsdb_mld_onboard_attach_obs(m);
    ow_ovsdb_mld_onboard_attach_ovsdb(m);
}

static void ow_ovsdb_mld_onboard_detach(struct ow_ovsdb_mld_onboard *m)
{
    ow_ovsdb_mld_onboard_detach_obs(m);
    ow_ovsdb_mld_onboard_detach_ovsdb(m);
}

ow_ovsdb_mld_onboard_t *ow_ovsdb_mld_onboard_alloc(void)
{
    if (getenv("OW_OVSDB_MLD_ONBOARD_DISABLE")) return NULL;
    ow_ovsdb_mld_onboard_t *m = CALLOC(1, sizeof(*m));
    ow_ovsdb_mld_onboard_init(m);
    ow_ovsdb_mld_onboard_attach(m);
    return m;
}

void ow_ovsdb_mld_onboard_drop(ow_ovsdb_mld_onboard_t *m)
{
    if (m == NULL) return;
    ow_ovsdb_mld_onboard_detach(m);
    FREE(m);
}
