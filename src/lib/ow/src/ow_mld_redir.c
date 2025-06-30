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
 * ow_mld_redir - MLD redirection of datapath
 *
 * Some MLO implementations retain their legacy
 * STA iftype netdevs and spin up an extra MLD
 * specific netdev that bonds the legacy ones.
 *
 * This is presumably done to ease out transition
 * and maintain backward compat with userspace for
 * a bit longer for older tooling.
 *
 * Normally the expectation would be that the MLD
 * netdev is the only traffic termination/datapath
 * point.
 *
 * However some implementations mix up datapath
 * for STA iftype - for associations with MLD AP
 * the MLD netdev is used for datapath, but for
 * non-MLD AP associations the legacy netdevs are
 * used.
 *
 * The naive approach is to juggle the netdevs -
 * unbond them, move DHCP client between netdevs,
 * etc. That's troublesome for Opensync because
 * that forces the device to self-modify the
 * configuration it possibly got from the
 * controller. Moreover, the unbonding process
 * itself may be disruptive and/or done in advance
 * of associating to a non-MLD AP, making it more
 * difficult work with.
 *
 * The alternative approach - the one used in this
 * module - is to use TC mirred redirect. This is
 * done via Interface_Classifier and IP_Interface
 * tables.
 *
 * The module keeps track of the MLD groupings of
 * OSW (through osw_mld_vif). When enabled it
 * selects (when applicable) one of the legacy STA
 * iftype netdevs as the leader and redirects
 * traffic between it and MLD netdev it is bonded
 * to. It also changes the MAC on the MLD netdev
 * to match the legacy netdev to allow 3addr SA=TA
 * requirement to be fulfilled.
 *
 * When associating to an MLD AP the redirection
 * is undone (TC mirred is uninstalled, MLD netdev
 * MAC is reset back).
 *
 * The module is inactive by default. It needs to
 * be enabled per-platform by the integrator by
 * relying on the osw_etc component.
 */
#include <ds_tree.h>
#include <memutil.h>
#include <os.h>
#include <os_types.h>
#include <os_nif.h>
#include <log.h>
#include <const.h>
#include <schema.h>
#include <ovsdb_sync.h>
#include <osn_netif.h>

#include <jansson.h>

#include <osw_etc.h>
#include <osw_timer.h>
#include <osw_mld_vif.h>
#include <osw_module.h>
#include "ow_mld_redir.h"

#define OW_MLD_REDIR_TOKEN_PREFIX     "ow_mld_redir,"
#define OW_MLD_REDIR_MLD_TOKEN(mld)   strfmta(OW_MLD_REDIR_TOKEN_PREFIX "ingress,%s", mld->mld_name)
#define OW_MLD_REDIR_VIF_TOKEN(vif)   strfmta(OW_MLD_REDIR_TOKEN_PREFIX "egress,%s", vif->vif_name)
#define OW_MLD_REDIR_FILTER_PRIO      100
#define LOG_PREFIX(m, fmt, ...)       "ow_mld_redir: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_MLD(mld, fmt, ...) LOG_PREFIX((mld)->m, "%s: " fmt, (mld)->mld_name, ##__VA_ARGS__)
#define LOG_PREFIX_VIF(vif, fmt, ...) LOG_PREFIX_MLD((vif)->mld, "%s: " fmt, (vif)->vif_name, ##__VA_ARGS__)

typedef struct ow_mld_redir_vif ow_mld_redir_vif_t;
typedef struct ow_mld_redir_mld ow_mld_redir_mld_t;

struct ow_mld_redir_vif
{
    ds_tree_node_t node_vifs;
    ds_tree_node_t node_connected;
    ow_mld_redir_mld_t *mld;
    char *vif_name;
    struct osw_hwaddr addr;
    struct osw_hwaddr ap_mld_addr;
};

struct ow_mld_redir_mld
{
    ds_tree_node_t node;
    ds_tree_t vifs;
    ds_tree_t connected;
    ow_mld_redir_t *m;
    ow_mld_redir_vif_t *redir_vif;
    osn_netif_t *netif;
    char *mld_name;
    struct osw_hwaddr mld_addr;
    struct osw_hwaddr reported_addr;
    struct osw_timer recalc;
};

struct ow_mld_redir_observer
{
    ds_tree_node_t node;
    ow_mld_redir_t *m;
    ow_mld_redir_changed_fn_t *fn;
    void *priv;
};

struct ow_mld_redir
{
    ds_tree_t mlds;
    ds_tree_t observers;
    osw_mld_vif_observer_t *mld_vif_obs;
};

/* OVSDB specifics */

static void ow_mld_redir_unlink_classifier(const char *token)
{
    ovs_uuid_t uuid;
    const bool found = ovsdb_sync_get_uuid(
            SCHEMA_TABLE(Interface_Classifier),
            SCHEMA_COLUMN(Interface_Classifier, token),
            token,
            &uuid);
    if (found == false) return;
    ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(IP_Interface),
            NULL,
            SCHEMA_COLUMN(IP_Interface, ingress_classifier),
            OTR_DELETE,
            uuid.uuid);
    ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(IP_Interface),
            NULL,
            SCHEMA_COLUMN(IP_Interface, egress_classifier),
            OTR_DELETE,
            uuid.uuid);
}

static void ow_mld_redir_delete_classifier(const char *token)
{
    /* This is using strong ref, so need to get rid of the
     * references first.
     */
    ow_mld_redir_unlink_classifier(token);

    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(Interface_Classifier, token), "==", token);
    const int count = ovsdb_sync_delete_where(SCHEMA_TABLE(Interface_Classifier), where);
    (void)count;
}

static void ow_mld_redir_insert_classifier(const char *token, const char *action, const char *match, int priority)
{
    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(Interface_Classifier, token), "==", token);
    json_t *row = json_pack(
            "{s:s, s:s, s:s, s:i}",
            SCHEMA_COLUMN(Interface_Classifier, action),
            action,
            SCHEMA_COLUMN(Interface_Classifier, match),
            match,
            SCHEMA_COLUMN(Interface_Classifier, token),
            token,
            SCHEMA_COLUMN(Interface_Classifier, priority),
            priority);
    const bool ok = ovsdb_sync_upsert_where(SCHEMA_TABLE(Interface_Classifier), where, row, NULL);
    WARN_ON(!ok);
}

static void ow_mld_redir_mld_delete_classifier(ow_mld_redir_mld_t *mld)
{
    LOGD(LOG_PREFIX_MLD(mld, "deleting classifier"));
    const char *token = OW_MLD_REDIR_MLD_TOKEN(mld);
    ow_mld_redir_delete_classifier(token);
}

static void ow_mld_redir_mld_insert_classifier(ow_mld_redir_mld_t *mld)
{
    LOGD(LOG_PREFIX_MLD(mld, "inserting classifier"));
    const char *action = strfmta("action mirred ingress redirect dev %s", mld->mld_name);
    const char *match = "protocol all u32 match u32 0 0";
    const char *token = OW_MLD_REDIR_MLD_TOKEN(mld);
    const int priority = OW_MLD_REDIR_FILTER_PRIO;
    ow_mld_redir_insert_classifier(token, action, match, priority);
}

static void ow_mld_redir_vif_delete_classifier(ow_mld_redir_vif_t *vif)
{
    LOGD(LOG_PREFIX_VIF(vif, "deleting classifier"));
    const char *token = OW_MLD_REDIR_VIF_TOKEN(vif);
    ow_mld_redir_delete_classifier(token);
}

static void ow_mld_redir_vif_insert_classifier(ow_mld_redir_vif_t *vif)
{
    LOGD(LOG_PREFIX_VIF(vif, "inserting classifier"));
    const char *action = strfmta("action mirred egress redirect dev %s", vif->vif_name);
    const char *match = "protocol all u32 match u32 0 0";
    const char *token = OW_MLD_REDIR_VIF_TOKEN(vif);
    const int priority = OW_MLD_REDIR_FILTER_PRIO;
    ow_mld_redir_insert_classifier(token, action, match, priority);
}

static void ow_mld_redir_upsert_ip_interface(const char *if_name)
{
    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(IP_Interface, name), "==", if_name);
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(IP_Interface), where);
    const bool already_exists = (rows != NULL);
    if (already_exists) return;
    json_t *row = json_pack(
            "{s:s, s:s, s:b}",
            SCHEMA_COLUMN(IP_Interface, name),
            if_name,
            SCHEMA_COLUMN(IP_Interface, if_name),
            if_name,
            SCHEMA_COLUMN(IP_Interface, enable),
            true);
    const bool ok = ovsdb_sync_insert(SCHEMA_TABLE(IP_Interface), row, NULL);
    WARN_ON(!ok);
}

static void ow_mld_redir_flush_ip_interface(const char *if_name)
{
    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(IP_Interface, name), "==", if_name);
    json_t *row = json_pack(
            "{ s: [s: []], s: [s: []] }",
            SCHEMA_COLUMN(IP_Interface, ingress_classifier),
            "set",
            SCHEMA_COLUMN(IP_Interface, egress_classifier),
            "set");
    const int count = ovsdb_sync_update_where(SCHEMA_TABLE(IP_Interface), where, row);
    const bool ok = (count == 1);
    WARN_ON(!ok);
}

static void ow_mld_redir_mutate_ip_interface(const char *if_name, const char *column, const char *token, bool add)
{
    ovs_uuid_t uuid;
    const bool found = ovsdb_sync_get_uuid(
            SCHEMA_TABLE(Interface_Classifier),
            SCHEMA_COLUMN(Interface_Classifier, token),
            token,
            &uuid);
    if (found == false) return;
    if (add) ow_mld_redir_upsert_ip_interface(if_name);
    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(IP_Interface, if_name), "==", if_name);
    const int count = ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(IP_Interface),
            where,
            column,
            add ? OTR_INSERT : OTR_DELETE,
            uuid.uuid);
    (void)count;
}

static void ow_mld_redir_vif_detach_ingress(ow_mld_redir_vif_t *vif)
{
    LOGD(LOG_PREFIX_VIF(vif, "detaching ingress"));
    const char *token = OW_MLD_REDIR_MLD_TOKEN(vif->mld);
    ow_mld_redir_mutate_ip_interface(vif->vif_name, SCHEMA_COLUMN(IP_Interface, ingress_classifier), token, false);
}

static void ow_mld_redir_vif_attach_ingress(ow_mld_redir_vif_t *vif)
{
    LOGD(LOG_PREFIX_VIF(vif, "attaching ingress"));
    const char *token = OW_MLD_REDIR_MLD_TOKEN(vif->mld);
    ow_mld_redir_mutate_ip_interface(vif->vif_name, SCHEMA_COLUMN(IP_Interface, ingress_classifier), token, true);
}

static void ow_mld_redir_mld_detach_egress(ow_mld_redir_mld_t *mld, ow_mld_redir_vif_t *vif_old)
{
    LOGD(LOG_PREFIX_MLD(mld, "detaching egress"));
    const char *token = OW_MLD_REDIR_VIF_TOKEN(vif_old);
    ow_mld_redir_mutate_ip_interface(mld->mld_name, SCHEMA_COLUMN(IP_Interface, egress_classifier), token, false);
}

static void ow_mld_redir_mld_attach_egress(ow_mld_redir_mld_t *mld, ow_mld_redir_vif_t *vif_new)
{
    LOGD(LOG_PREFIX_MLD(mld, "attaching egress"));
    const char *token = OW_MLD_REDIR_VIF_TOKEN(vif_new);
    ow_mld_redir_mutate_ip_interface(mld->mld_name, SCHEMA_COLUMN(IP_Interface, egress_classifier), token, true);
}

/* Main logic */

static ow_mld_redir_vif_t *ow_mld_redir_mld_get_non_mlo_assoc_vif(ow_mld_redir_mld_t *mld)
{
    ow_mld_redir_vif_t *vif;
    ds_tree_foreach (&mld->connected, vif)
    {
        if (osw_hwaddr_is_zero(&vif->ap_mld_addr))
        {
            return vif;
        }
    }
    return NULL;
}

static void ow_mld_redir_mld_sched_recalc(ow_mld_redir_mld_t *mld)
{
    osw_timer_arm_at_nsec(&mld->recalc, 0);
}

static void ow_mld_redir_mld_set_reported_addr(ow_mld_redir_mld_t *mld, const struct osw_hwaddr *addr)
{
    if (mld == NULL) return;
    if (addr == NULL) return;
    if (osw_hwaddr_is_equal(&mld->reported_addr, addr)) return;
    LOGI(LOG_PREFIX_MLD(
            mld,
            "reported_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&mld->reported_addr),
            OSW_HWADDR_ARG(addr)));
    mld->reported_addr = *addr;
    ow_mld_redir_mld_sched_recalc(mld);
}

static void ow_mld_redir_mld_update_reported_addr(ow_mld_redir_mld_t *mld)
{
    os_macaddr_t mac;
    const struct osw_hwaddr *addr = osw_hwaddr_from_cptr_unchecked(mac.addr);
    if (WARN_ON(os_nif_macaddr_get(mld->mld_name, &mac) == false)) return;
    ow_mld_redir_mld_set_reported_addr(mld, addr);
}

static void ow_mld_redir_mld_set_hw_addr(ow_mld_redir_mld_t *mld, const struct osw_hwaddr *addr)
{
    if (osw_hwaddr_is_equal(&mld->reported_addr, addr)) return;
    if (osw_hwaddr_is_zero(addr)) return;
    LOGI(LOG_PREFIX_MLD(
            mld,
            "addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&mld->reported_addr),
            OSW_HWADDR_ARG(addr)));
    const os_macaddr_t mac = {
        .addr[0] = addr->octet[0],
        .addr[1] = addr->octet[1],
        .addr[2] = addr->octet[2],
        .addr[3] = addr->octet[3],
        .addr[4] = addr->octet[4],
        .addr[5] = addr->octet[5],
    };
    const bool ok = os_nif_macaddr_set(mld->mld_name, mac);
    WARN_ON(ok == false);
    ow_mld_redir_mld_update_reported_addr(mld);
}

static const struct osw_hwaddr *ow_mld_redir_mld_derive_desired_addr(ow_mld_redir_mld_t *mld)
{
    if (mld == NULL) return osw_hwaddr_zero();
    if (mld->redir_vif == NULL) return &mld->mld_addr;
    return &mld->redir_vif->addr;
}

static void ow_mld_redir_mld_notify_observers(ow_mld_redir_mld_t *mld)
{
    ow_mld_redir_observer_t *o;
    ow_mld_redir_vif_t *vif;
    const char *redir_vif = mld->redir_vif ? mld->redir_vif->vif_name : NULL;
    char **vifs = NULL;
    size_t n = 0;
    ds_tree_foreach (&mld->vifs, vif)
    {
        n++;
        vifs = REALLOC(vifs, (n + 1) * sizeof(*vifs));
        vifs[n - 1] = STRDUP(vif->vif_name);
        vifs[n - 0] = NULL;
    }
    ds_tree_foreach (&mld->m->observers, o)
    {
        o->fn(o->priv, mld->mld_name, redir_vif, vifs);
    }
    FREE(vifs);
}

static void ow_mld_redir_mld_recalc_vif(ow_mld_redir_mld_t *mld)
{
    ow_mld_redir_vif_t *vif_new = ow_mld_redir_mld_get_non_mlo_assoc_vif(mld);
    ow_mld_redir_vif_t *vif_old = mld->redir_vif;
    if (vif_new == vif_old) return;
    mld->redir_vif = vif_new;
    LOGI(LOG_PREFIX_MLD(mld, "vif: %s -> %s", vif_old ? vif_old->vif_name : "()", vif_new ? vif_new->vif_name : "()"));
    if (vif_old)
    {
        ow_mld_redir_vif_detach_ingress(vif_old);
        ow_mld_redir_mld_detach_egress(mld, vif_old);
    }
    if (vif_new)
    {
        ow_mld_redir_vif_attach_ingress(vif_new);
        ow_mld_redir_mld_attach_egress(mld, vif_new);
    }
    ow_mld_redir_mld_notify_observers(mld);
}

static void ow_mld_redir_mld_recalc_desired_addr(ow_mld_redir_mld_t *mld)
{
    const struct osw_hwaddr *desired_addr = ow_mld_redir_mld_derive_desired_addr(mld);
    ow_mld_redir_mld_set_hw_addr(mld, desired_addr);
}

static void ow_mld_redir_mld_recalc(ow_mld_redir_mld_t *mld)
{
    ow_mld_redir_mld_recalc_vif(mld);
    ow_mld_redir_mld_recalc_desired_addr(mld);
}

static void ow_mld_redir_mld_recalc_cb(struct osw_timer *t)
{
    ow_mld_redir_mld_t *mld = container_of(t, typeof(*mld), recalc);
    ow_mld_redir_mld_recalc(mld);
}

static void ow_mld_redir_netif_status_cb(osn_netif_t *netif, struct osn_netif_status *status)
{
    ow_mld_redir_mld_t *mld = osn_netif_data_get(netif);
    ow_mld_redir_mld_update_reported_addr(mld);
}

static ow_mld_redir_mld_t *ow_mld_redir_mld_alloc(
        ow_mld_redir_t *m,
        const char *mld_if_name,
        const struct osw_hwaddr *mld_addr)
{
    if (mld_if_name == NULL) return NULL;
    ow_mld_redir_mld_t *mld = CALLOC(1, sizeof(*mld));
    mld->mld_name = STRDUP(mld_if_name);
    mld->mld_addr = *mld_addr;
    mld->m = m;
    mld->netif = osn_netif_new(mld_if_name);
    osn_netif_data_set(mld->netif, mld);
    osn_netif_status_notify(mld->netif, ow_mld_redir_netif_status_cb);
    osw_timer_init(&mld->recalc, ow_mld_redir_mld_recalc_cb);
    ds_tree_init(&mld->vifs, ds_str_cmp, ow_mld_redir_vif_t, node_vifs);
    ds_tree_init(&mld->connected, ds_str_cmp, ow_mld_redir_vif_t, node_connected);
    ow_mld_redir_mld_delete_classifier(mld);
    ow_mld_redir_mld_insert_classifier(mld);
    ow_mld_redir_upsert_ip_interface(mld->mld_name);
    ow_mld_redir_flush_ip_interface(mld->mld_name);
    ds_tree_insert(&m->mlds, mld, mld->mld_name);
    LOGD(LOG_PREFIX_MLD(mld, "allocated"));
    return mld;
}

static ow_mld_redir_mld_t *ow_mld_redir_mld_lookup(ow_mld_redir_t *m, const char *mld_if_name)
{
    if (m == NULL) return NULL;
    if (mld_if_name == NULL) return NULL;
    return ds_tree_find(&m->mlds, mld_if_name);
}

static void ow_mld_redir_mld_drop(ow_mld_redir_mld_t *mld)
{
    if (mld == NULL) return;
    LOGD(LOG_PREFIX_MLD(mld, "dropping"));
    ASSERT(ds_tree_is_empty(&mld->vifs), "observer code bugged");
    osn_netif_del(mld->netif);
    ow_mld_redir_mld_delete_classifier(mld);
    ds_tree_remove(&mld->m->mlds, mld);
    osw_timer_disarm(&mld->recalc);
    FREE(mld->mld_name);
    FREE(mld);
}

static void ow_mld_redir_vif_set_addr(ow_mld_redir_vif_t *vif, const struct osw_hwaddr *addr)
{
    if (osw_hwaddr_is_equal(&vif->addr, addr)) return;
    LOGI(LOG_PREFIX_VIF(
            vif,
            "addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&vif->addr),
            OSW_HWADDR_ARG(addr)));
    vif->addr = *addr;
    ow_mld_redir_mld_sched_recalc(vif->mld);
}

static void ow_mld_redir_vif_set_ap_mld_addr(ow_mld_redir_vif_t *vif, const struct osw_hwaddr *addr)
{
    if (osw_hwaddr_is_equal(&vif->ap_mld_addr, addr)) return;
    LOGI(LOG_PREFIX_VIF(
            vif,
            "ap_mld_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&vif->ap_mld_addr),
            OSW_HWADDR_ARG(addr)));
    vif->ap_mld_addr = *addr;
    ow_mld_redir_mld_sched_recalc(vif->mld);
}

static ow_mld_redir_vif_t *ow_mld_redir_vif_alloc(ow_mld_redir_mld_t *mld, const char *vif_name)
{
    if (vif_name == NULL) return NULL;
    ow_mld_redir_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    vif->mld = mld;
    ow_mld_redir_vif_delete_classifier(vif);
    ow_mld_redir_vif_insert_classifier(vif);
    ow_mld_redir_upsert_ip_interface(vif->vif_name);
    ow_mld_redir_flush_ip_interface(vif->vif_name);
    ds_tree_insert(&mld->vifs, vif, vif->vif_name);
    LOGD(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

static ow_mld_redir_vif_t *ow_mld_redir_vif_lookup(ow_mld_redir_mld_t *mld, const char *vif_name)
{
    if (mld == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    return ds_tree_find(&mld->vifs, vif_name);
}

static void ow_mld_redir_vif_detach(ow_mld_redir_vif_t *vif)
{
    if (vif->mld->redir_vif != vif) return;
    ow_mld_redir_vif_set_addr(vif, osw_hwaddr_zero());
    ow_mld_redir_vif_set_ap_mld_addr(vif, osw_hwaddr_zero());
    ow_mld_redir_mld_recalc(vif->mld);
}

static void ow_mld_redir_vif_drop(ow_mld_redir_vif_t *vif)
{
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_VIF(vif, "dropping"));
    ow_mld_redir_vif_detach(vif);
    ow_mld_redir_vif_delete_classifier(vif);
    ds_tree_remove(&vif->mld->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static void ow_mld_redir_mld_added_cb(void *priv, const char *mld_if_name)
{
    ow_mld_redir_t *m = priv;
    const struct osw_hwaddr *mld_addr = osw_mld_vif_get_mld_addr(m->mld_vif_obs, mld_if_name);
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_alloc(m, mld_if_name, mld_addr);
    (void)mld;
}

static void ow_mld_redir_mld_removed_cb(void *priv, const char *mld_if_name)
{
    ow_mld_redir_t *m = priv;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    ow_mld_redir_mld_drop(mld);
}

static const struct osw_hwaddr *ow_mld_redir_state_get_ap_mld_addr(const struct osw_drv_vif_state *state)
{
    switch (state->vif_type)
    {
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_UNDEFINED:
            return NULL;
        case OSW_VIF_STA:
            if (state->u.sta.link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return NULL;
            if (osw_hwaddr_is_zero(&state->u.sta.link.mld_addr)) return NULL;
            return &state->u.sta.link.mld_addr;
    }
    return NULL;
}

static void ow_mld_redir_vif_set_state(ow_mld_redir_vif_t *vif, const struct osw_drv_vif_state *state)
{
    const struct osw_hwaddr *ap_mld_addr = ow_mld_redir_state_get_ap_mld_addr(state) ?: osw_hwaddr_zero();
    ow_mld_redir_vif_set_addr(vif, &state->mac_addr);
    ow_mld_redir_vif_set_ap_mld_addr(vif, ap_mld_addr);
}

static void ow_mld_redir_link_added_cb(void *priv, const char *mld_if_name, const struct osw_state_vif_info *info)
{
    ow_mld_redir_t *m = priv;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    const char *vif_name = info->vif_name;
    ow_mld_redir_vif_t *vif = ow_mld_redir_vif_alloc(mld, vif_name);
    ow_mld_redir_vif_set_state(vif, info->drv_state);
}

static void ow_mld_redir_link_changed_cb(void *priv, const char *mld_if_name, const struct osw_state_vif_info *info)
{
    ow_mld_redir_t *m = priv;
    const char *vif_name = info->vif_name;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    ow_mld_redir_vif_t *vif = ow_mld_redir_vif_lookup(mld, vif_name);
    ow_mld_redir_vif_set_state(vif, info->drv_state);
}

static void ow_mld_redir_link_removed_cb(void *priv, const char *mld_if_name, const struct osw_state_vif_info *info)
{
    ow_mld_redir_t *m = priv;
    const char *vif_name = info->vif_name;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    ow_mld_redir_vif_t *vif = ow_mld_redir_vif_lookup(mld, vif_name);
    ow_mld_redir_vif_drop(vif);
}

static void ow_mld_redir_link_connected_cb(void *priv, const char *mld_if_name, const struct osw_state_vif_info *info)
{
    ow_mld_redir_t *m = priv;
    const char *vif_name = info->vif_name;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    ow_mld_redir_vif_t *vif = ow_mld_redir_vif_lookup(mld, vif_name);
    ASSERT(ds_tree_find(&mld->connected, vif->vif_name) == NULL, "observer code bugged");
    ds_tree_insert(&mld->connected, vif, vif->vif_name);
    ow_mld_redir_mld_sched_recalc(mld);
}

static void ow_mld_redir_link_disconnected_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    ow_mld_redir_t *m = priv;
    const char *vif_name = info->vif_name;
    ow_mld_redir_mld_t *mld = ow_mld_redir_mld_lookup(m, mld_if_name);
    ow_mld_redir_vif_t *vif = ow_mld_redir_vif_lookup(mld, vif_name);
    ASSERT(ds_tree_find(&mld->connected, vif->vif_name) != NULL, "observer code bugged");
    ds_tree_remove(&mld->connected, vif);
    ow_mld_redir_mld_sched_recalc(mld);
}

ow_mld_redir_observer_t *ow_mld_redir_observer_alloc(ow_mld_redir_t *m, ow_mld_redir_changed_fn_t *fn, void *priv)
{
    if (m == NULL) return NULL;
    if (fn == NULL) return NULL;
    ow_mld_redir_observer_t *o = CALLOC(1, sizeof(*o));
    o->m = m;
    o->fn = fn;
    o->priv = priv;
    ds_tree_insert(&m->observers, o, o);
    return o;
}

const char *ow_mld_redir_get_mld_redir_vif_name(ow_mld_redir_t *m, const char *mld_name)
{
    if (m == NULL) return NULL;
    if (mld_name == NULL) return NULL;
    ow_mld_redir_mld_t *mld = ds_tree_find(&m->mlds, mld_name);
    if (mld == NULL) return NULL;
    if (mld->redir_vif == NULL) return NULL;
    return mld->redir_vif->vif_name;
}

void ow_mld_redir_observer_drop(ow_mld_redir_observer_t *o)
{
    if (o == NULL) return;
    ds_tree_remove(&o->m->observers, o);
    FREE(o);
}

static void ow_mld_redir_init(ow_mld_redir_t *m)
{
    ds_tree_init(&m->mlds, ds_str_cmp, ow_mld_redir_mld_t, node);
    ds_tree_init(&m->observers, ds_void_cmp, ow_mld_redir_observer_t, node);
}

static osw_mld_vif_observer_t *ow_mld_redir_attach_observer(ow_mld_redir_t *m)
{
    osw_mld_vif_t *mod = OSW_MODULE_LOAD(osw_mld_vif);
    osw_mld_vif_observer_t *obs = osw_mld_vif_observer_alloc(mod);
    osw_mld_vif_observer_set_mld_added_fn(obs, ow_mld_redir_mld_added_cb, m);
    osw_mld_vif_observer_set_mld_removed_fn(obs, ow_mld_redir_mld_removed_cb, m);
    osw_mld_vif_observer_set_link_added_fn(obs, ow_mld_redir_link_added_cb, m);
    osw_mld_vif_observer_set_link_changed_fn(obs, ow_mld_redir_link_changed_cb, m);
    osw_mld_vif_observer_set_link_connected_fn(obs, ow_mld_redir_link_connected_cb, m);
    osw_mld_vif_observer_set_link_disconnected_fn(obs, ow_mld_redir_link_disconnected_cb, m);
    osw_mld_vif_observer_set_link_removed_fn(obs, ow_mld_redir_link_removed_cb, m);
    return obs;
}

static void ow_mld_redir_flush(ow_mld_redir_t *m)
{
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Interface_Classifier), NULL);
    json_t *row;
    size_t i;
    json_array_foreach(rows, i, row)
    {
        const char *token = json_string_value(json_object_get(row, "token"));
        const bool our_token = (strstr(token, OW_MLD_REDIR_TOKEN_PREFIX) == token);
        if (our_token)
        {
            LOGI(LOG_PREFIX(m, "classifier: %s: flushing", token));
            ow_mld_redir_delete_classifier(token);
        }
    }
    json_decref(rows);
}

static void ow_mld_redir_attach(ow_mld_redir_t *m)
{
    if (osw_etc_get("OW_MLD_REDIR_ENABLE"))
    {
        ow_mld_redir_flush(m);
        m->mld_vif_obs = ow_mld_redir_attach_observer(m);
    }
}

OSW_MODULE(ow_mld_redir)
{
    static ow_mld_redir_t m;
    ow_mld_redir_init(&m);
    ow_mld_redir_attach(&m);
    return &m;
}
