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
 * ow_mld_redir_sta - MLD Redirection for associated stations
 *
 * Some drivers that support MLD APs present to
 * the OS Affiliated APs as separate netdevs
 * without a dedicated netdev for data path. They
 * select one of the netdevs (that also represents
 * one of Affiliated AP) as a "main" interface.
 *
 * However they don't fully commit to it. Instead,
 * they use that interface as traffic termination
 * for:
 *
 *  - MLO associated clients
 *  - multicast traffic
 *
 * This means that non-MLO associations on
 * non-main interfaces will not have their unicast
 * traffic delivered to the main interface. This
 * presents a consistency problem from networking
 * point of view: IP networking, filtering, etc.
 *
 * Vendors typically solve(d) this by (implicitly)
 * expecting that all AP interfaces of a given
 * SSID are put into a bridge. Given APs can
 * freely differentiate 4 addresses when
 * transmitting frames, the bridging works fine.
 *
 * However for interfaces that are not intended to
 * be bridged this naturally introduces a problem.
 * Either you start using a bridge to simply clump
 * them up, or you need to do something else.
 *
 * To keep consistency with systems where there's
 * a single MLD netdev termination point (either
 * separate one, or elected), this module
 * attempts at using TC mirred redirect to
 * redirect unicast traffic between the main
 * interface and non-main interfaces.
 *
 * This is also solving a problem for the cloud
 * controller by not requiring it to orchestrate
 * networking configurations differently for such
 * systems.
 *
 * The rules therefore are:
 *
 *  - ingress on non-main interfaces is redirected
 *    to the main interface unless the DA is
 *    matching the non-main interface MAC. This is
 *    necessary for things like EAPOL to continue
 *    to work properly.
 *
 *    - on top of that, packets with DA matching
 *      the main AP interface have their sk_buff
 *      packet_type set to host so that they can
 *      be processed by things like ARP, ICMPv6
 *      which is required for proper IP operation
 *      and therefore for GRE tunnels.
 *
 *  - main interface egress redirected to non-main
 *    interface if DA is matching a non-MLO
 *    associated STA MAC
 *
 * As a consequence:
 *
 *  - non-MLO associated STAs on main interfaces
 *    are subjected to no redirects. Only the ones
 *    connected to non-main interfaces are.
 *
 *  - MLO associated STAs are subjected to no
 *    redirects.
 */

#include "osw_state.h"
#include <log.h>
#include <const.h>
#include <os.h>
#include <schema.h>
#include <ovsdb_sync.h>
#include <jansson.h>
#include <ds_tree.h>
#include <osw_types.h>
#include <osw_etc.h>
#include <osw_module.h>
#include <osw_sta_assoc.h>
#include <osw_mld_vif.h>

typedef struct ow_mld_redir_sta ow_mld_redir_sta_t;
typedef struct ow_mld_redir_sta_assoc ow_mld_redir_sta_assoc_t;
typedef struct ow_mld_redir_sta_mld_ap ow_mld_redir_sta_mld_ap_t;
typedef struct ow_mld_redir_sta_link_ap ow_mld_redir_sta_link_ap_t;
typedef struct ow_mld_redir_sta_tc ow_mld_redir_sta_tc_t;

struct ow_mld_redir_sta
{
    ds_tree_t non_mlo_assocs;
    ds_tree_t mld_aps;
    ds_tree_t link_aps;
    ds_tree_t tcs;
    osw_mld_vif_observer_t *mld_vif_obs;
    osw_sta_assoc_observer_t *sta_assoc_obs;
};

#define OW_MLD_REDIR_STA_ACTION_SKBEDIT       "action skbedit ptype host continue"
#define OW_MLD_REDIR_STA_ACTION_REDIR_INGRESS "action mirred ingress redirect dev %s"
#define OW_MLD_REDIR_STA_ACTION_REDIR_EGRESS  "action mirred egress redirect dev %s"
#define OW_MLD_REDIR_STA_ACTION_LOCAL         "action pass"
#define OW_MLD_REDIR_STA_MATCH_ALL            "protocol all u32 match u32 0 0"
#define OW_MLD_REDIR_STA_MATCH_ETHER_DEST     "protocol all u32 match ether dst " OSW_HWADDR_FMT

/* Priorities are arranged such that, for ingress:
 *
 *  - ingress skbedit happens first - any packet
 *    that has DA matching the main AP interface
 *    vif_addr is marked as a local host packet so
 *    that things like ARP or ICMPv6 can work out
 *    the addresses, among other things. This is
 *    done before any of the redirections.
 *
 *  - ingress local pass happens second - any
 *    packet that has DA matching the actual
 *    non-main AP interface is exempted from
 *    redirection to allow things like EAPOL to
 *    work.
 *
 *  - ingress redirection happens third - any
 *    other packet ends up on the main AP
 *    interface. This includes, but isn't limited
 *    to packets with DA matching the main AP
 *    interface vif_addr, but also others.
 *    However, the other non-main AP interface
 *    shouldn't reach this point, see above.
 *
 * For egress there's only a single rule type to
 * be expected - any packet that has DA matching a
 * non-MLO associated STA gets redirected to the
 * non-main AP interface that it is connected to.
 * Clients that are connected to the main AP do
 * not have any special handling, they just get
 * egressed normally.
 */
#define OW_MLD_REDIR_STA_PRIORITY_SKBEDIT  40
#define OW_MLD_REDIR_STA_PRIORITY_LOCAL    45
#define OW_MLD_REDIR_STA_PRIORITY_REDIRECT 50

#define OW_MLD_REDIR_STA_TOKEN "ow_mld_redir_sta"

#define LOG_PREFIX(fmt, ...) "ow_mld_redir_sta: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_TC(tc, fmt, ...)                             \
    LOG_PREFIX(                                                 \
            "tc: %s: priority=%d match='%s' action='%s': " fmt, \
            tc->uuid.uuid,                                      \
            tc->priority,                                       \
            tc->match,                                          \
            tc->action,                                         \
            ##__VA_ARGS__)

#define LOG_PREFIX_MLD(mld, fmt, ...) LOG_PREFIX("mld_ap: %s: " fmt, mld->vif_name, ##__VA_ARGS__)

#define LOG_PREFIX_LINK(link, fmt, ...) \
    LOG_PREFIX_MLD(link->mld_ap, "link_ap: %s%s: " fmt, link->vif_name, link->is_main ? " (main)" : "", ##__VA_ARGS__)

#define LOG_PREFIX_ASSOC(assoc, fmt, ...)                           \
    LOG_PREFIX(                                                     \
            "assoc: %s%s%s%s: " OSW_HWADDR_FMT ": " fmt,            \
            assoc->vif_name,                                        \
            assoc->link_ap ? " (" : "",                             \
            assoc->link_ap ? assoc->link_ap->mld_ap->vif_name : "", \
            assoc->link_ap ? ")" : "",                              \
            OSW_HWADDR_ARG(&assoc->assoc_addr),                     \
            ##__VA_ARGS__)

struct ow_mld_redir_sta_tc
{
    ds_tree_node_t node;
    ow_mld_redir_sta_t *m;
    ovs_uuid_t uuid;
    int priority;
    char *action;
    char *match;
};

/* egress from mld_ap matching assoc's assoc_addr needs to be:
 *  - redirected as egress to vif_name
 *
 * this structure is expected to be instantiated
 * only for non-mlo associated clients.
 */
struct ow_mld_redir_sta_assoc
{
    ds_tree_node_t node_m;       /* key: assoc_addr @ ow_mld_redir_sta::assocs */
    ds_tree_node_t node_link_ap; /* key: assoc_addr @ ow_mld_redir_sta_link_ap::assocs */
    ow_mld_redir_sta_t *m;
    ow_mld_redir_sta_link_ap_t *link_ap;
    struct osw_hwaddr assoc_addr;
    char *vif_name;
    ow_mld_redir_sta_tc_t *egress_redirect;
};

struct ow_mld_redir_sta_mld_ap
{
    ds_tree_node_t node_m; /* key: vif_name @ ow_mld_redir_sta::mld_aps */
    ow_mld_redir_sta_t *m;
    ds_tree_t link_aps;
    bool is_redirecting;
    size_t num_link_aps; /* used to track changes to the link_aps list */
    struct osw_hwaddr vif_addr;
    char *vif_name;
};

/* ingress from link_ap matching mld_ap's
 * vif_addr on ether_dest needs to be:
 *
 *  - redirected as ingress to mld_ap
 *  - marked as ptype host
 */
struct ow_mld_redir_sta_link_ap
{
    ds_tree_node_t node_m;      /* key: vif_name @ ow_mld_redir_sta::link_aps */
    ds_tree_node_t node_mld_ap; /* key: vif_name @ ow_mld_redir_sta_mld_ap::link_aps */
    ow_mld_redir_sta_t *m;
    ow_mld_redir_sta_mld_ap_t *mld_ap;
    ds_tree_t assocs;
    struct osw_hwaddr vif_addr;
    char *vif_name;
    bool is_main;
    bool is_in_bridge;
    ow_mld_redir_sta_tc_t *ingress_skbedit;
    ow_mld_redir_sta_tc_t *ingress_local;
    ow_mld_redir_sta_tc_t *ingress_redirect;
};

/* OVSDB helpers */

static void ow_mld_redir_sta_ovsdb_unlink_classifier(const char *uuid)
{
    if (WARN_ON(uuid == NULL)) return;
    if (WARN_ON(strlen(uuid) == 0)) return;

    ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(IP_Interface),
            NULL,
            SCHEMA_COLUMN(IP_Interface, ingress_classifier),
            OTR_DELETE,
            uuid);
    ovsdb_sync_mutate_uuid_set(
            SCHEMA_TABLE(IP_Interface),
            NULL,
            SCHEMA_COLUMN(IP_Interface, egress_classifier),
            OTR_DELETE,
            uuid);
}

static void ow_mld_redir_sta_ovsdb_upsert_ip_interface(const char *if_name)
{
    if (WARN_ON(if_name == NULL)) return;

    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(IP_Interface, name), "==", if_name);
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(IP_Interface), where);
    const bool already_exists = (rows != NULL);
    json_decref(rows);
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

static void ow_mld_redir_sta_ovsdb_mutate_ip_interface(
        const char *if_name,
        const char *column,
        const char *uuid,
        bool add)
{
    if (WARN_ON(if_name == NULL)) return;
    if (WARN_ON(column == NULL)) return;
    if (WARN_ON(uuid == NULL)) return;

    if (add)
    {
        ow_mld_redir_sta_ovsdb_upsert_ip_interface(if_name);
    }

    json_t *where = json_pack("[[s, s, s]]", SCHEMA_COLUMN(IP_Interface, if_name), "==", if_name);
    const int count =
            ovsdb_sync_mutate_uuid_set(SCHEMA_TABLE(IP_Interface), where, column, add ? OTR_INSERT : OTR_DELETE, uuid);
    (void)count;
}

static void ow_mld_redir_sta_ovsdb_delete_classifier(const char *uuid)
{
    if (WARN_ON(uuid == NULL)) return;
    if (WARN_ON(strlen(uuid) == 0)) return;

    /* This is using strong ref, so need to get rid of the
     * references first.
     */
    ow_mld_redir_sta_ovsdb_unlink_classifier(uuid);

    json_t *where = ovsdb_where_uuid("_uuid", uuid);
    const int count = ovsdb_sync_delete_where(SCHEMA_TABLE(Interface_Classifier), where);
    (void)count;
}

static bool ow_mld_redir_sta_ovsdb_upsert_classifier(
        ovs_uuid_t *out_uuid,
        const char *token,
        const char *action,
        const char *match,
        int priority)
{
    if (WARN_ON(out_uuid == NULL)) return false;
    if (WARN_ON(token == NULL)) return false;
    if (WARN_ON(action == NULL)) return false;
    if (WARN_ON(match == NULL)) return false;

    const char *column_token = SCHEMA_COLUMN(Interface_Classifier, token);
    const char *column_action = SCHEMA_COLUMN(Interface_Classifier, action);
    const char *column_match = SCHEMA_COLUMN(Interface_Classifier, match);
    const char *column_priority = SCHEMA_COLUMN(Interface_Classifier, priority);
    json_t *where = json_array();
    json_t *where_token = json_pack("[[s, s, s]]", column_token, "==", token);
    json_t *where_action = json_pack("[[s, s, s]]", column_action, "==", action);
    json_t *where_match = json_pack("[[s, s, s]]", column_match, "==", match);
    json_t *where_priority = json_pack("[[s, s, i]]", column_priority, "==", priority);
    json_array_append_new(where, where_token);
    json_array_append_new(where, where_action);
    json_array_append_new(where, where_match);
    json_array_append_new(where, where_priority);
    json_t *row = json_pack(
            "{s:s, s:s, s:s, s:i}",
            column_action,
            action,
            column_match,
            match,
            column_token,
            token,
            column_priority,
            priority);
    return ovsdb_sync_upsert_where(SCHEMA_TABLE(Interface_Classifier), where, row, out_uuid);
}

/* ow_mld_redir_sta_tc_t */

static ow_mld_redir_sta_tc_t *ow_mld_redir_sta_tc_alloc(
        ow_mld_redir_sta_t *m,
        const char *match,
        const char *action,
        const int priority)
{
    if (m == NULL) return NULL;
    if (match == NULL) return NULL;
    if (action == NULL) return NULL;

    const char *token = OW_MLD_REDIR_STA_TOKEN;
    ovs_uuid_t uuid;
    MEMZERO(uuid);

    const bool ok = ow_mld_redir_sta_ovsdb_upsert_classifier(&uuid, token, action, match, priority);
    if (WARN_ON(ok == false)) return NULL;

    ow_mld_redir_sta_tc_t *tc = CALLOC(1, sizeof(*tc));
    tc->m = m;
    tc->uuid = uuid;
    tc->action = STRDUP(action);
    tc->match = STRDUP(match);
    tc->priority = priority;
    ds_tree_insert(&m->tcs, tc, tc);
    LOGD(LOG_PREFIX_TC(tc, "allocated"));
    return tc;
}

static void ow_mld_redir_sta_tc_drop(ow_mld_redir_sta_tc_t **tc_p)
{
    if (tc_p == NULL) return;

    ow_mld_redir_sta_tc_t *tc = *tc_p;
    if (tc == NULL) return;
    if (tc->m == NULL) return;
    if (WARN_ON(strlen(tc->uuid.uuid) == 0)) return;

    LOGD(LOG_PREFIX_TC(tc, "dropping"));
    ow_mld_redir_sta_ovsdb_delete_classifier(tc->uuid.uuid);
    ds_tree_remove(&tc->m->tcs, tc);
    FREE(tc->action);
    FREE(tc->match);
    FREE(tc);
    *tc_p = NULL;
}

static void ow_mld_redir_sta_tc_attach_ingress(ow_mld_redir_sta_tc_t *tc, const char *if_name)
{
    if (tc == NULL) return;
    if (WARN_ON(if_name == NULL)) return;

    const char *column = SCHEMA_COLUMN(IP_Interface, ingress_classifier);
    ow_mld_redir_sta_ovsdb_mutate_ip_interface(if_name, column, tc->uuid.uuid, true);
}

static void ow_mld_redir_sta_tc_attach_egress(ow_mld_redir_sta_tc_t *tc, const char *if_name)
{
    if (tc == NULL) return;
    if (WARN_ON(if_name == NULL)) return;

    const char *column = SCHEMA_COLUMN(IP_Interface, egress_classifier);
    ow_mld_redir_sta_ovsdb_mutate_ip_interface(if_name, column, tc->uuid.uuid, true);
}

/* ow_mld_redir_sta_assoc_t */

static ow_mld_redir_sta_assoc_t *ow_mld_redir_sta_assoc_alloc(
        ow_mld_redir_sta_t *m,
        const struct osw_hwaddr *assoc_addr,
        const char *vif_name)
{
    if (m == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    if (WARN_ON(assoc_addr == NULL)) return NULL;
    if (ds_tree_find(&m->non_mlo_assocs, assoc_addr) != NULL) return NULL;

    ow_mld_redir_sta_assoc_t *assoc = CALLOC(1, sizeof(*assoc));
    assoc->m = m;
    assoc->assoc_addr = *assoc_addr;
    assoc->vif_name = STRDUP(vif_name);
    ds_tree_insert(&m->non_mlo_assocs, assoc, &assoc->assoc_addr);
    LOGD(LOG_PREFIX_ASSOC(assoc, "allocated"));
    return assoc;
}

static void ow_mld_redir_sta_assoc_update(ow_mld_redir_sta_assoc_t *assoc)
{
    ow_mld_redir_sta_t *m = assoc->m;
    const bool is_redirecting = assoc->link_ap && assoc->link_ap->mld_ap && assoc->link_ap->mld_ap->is_redirecting
                                && !assoc->link_ap->is_main;

    if (is_redirecting)
    {
        if (assoc->egress_redirect == NULL)
        {
            LOGI(LOG_PREFIX_ASSOC(assoc, "attaching egress redirect tc"));

            const char *match = strfmta(OW_MLD_REDIR_STA_MATCH_ETHER_DEST, OSW_HWADDR_ARG(&assoc->assoc_addr));
            const char *action = strfmta(OW_MLD_REDIR_STA_ACTION_REDIR_EGRESS, assoc->vif_name);
            const int priority = OW_MLD_REDIR_STA_PRIORITY_REDIRECT;
            assoc->egress_redirect = ow_mld_redir_sta_tc_alloc(m, match, action, priority);

            ow_mld_redir_sta_tc_attach_egress(assoc->egress_redirect, assoc->link_ap->mld_ap->vif_name);
        }
    }
    else
    {
        if (assoc->egress_redirect != NULL)
        {
            LOGI(LOG_PREFIX_ASSOC(assoc, "detaching egress redirect tc"));
            ow_mld_redir_sta_tc_drop(&assoc->egress_redirect);
        }
    }
}

static void ow_mld_redir_sta_assoc_set_link_ap(ow_mld_redir_sta_assoc_t *assoc, ow_mld_redir_sta_link_ap_t *link_ap)
{
    if (assoc == NULL) return;
    if (assoc->link_ap == link_ap) return;

    LOGD(LOG_PREFIX_ASSOC(
            assoc,
            "link_ap: %s -> %s",
            assoc->link_ap ? assoc->link_ap->vif_name : "(null)",
            link_ap ? link_ap->vif_name : "(null)"));

    if (assoc->link_ap != NULL)
    {
        ds_tree_remove(&assoc->link_ap->assocs, assoc);
        assoc->link_ap = NULL;
        ow_mld_redir_sta_assoc_update(assoc);
    }

    assoc->link_ap = link_ap;
    if (link_ap != NULL)
    {
        ds_tree_insert(&link_ap->assocs, assoc, &assoc->assoc_addr);
    }

    ow_mld_redir_sta_assoc_update(assoc);
}

static void ow_mld_redir_sta_assoc_drop(ow_mld_redir_sta_assoc_t **assoc_ptr)
{
    if (assoc_ptr == NULL) return;

    ow_mld_redir_sta_assoc_t *assoc = *assoc_ptr;
    if (assoc == NULL) return;
    if (WARN_ON(assoc->m == NULL)) return;

    LOGD(LOG_PREFIX_ASSOC(assoc, "dropping"));
    ow_mld_redir_sta_assoc_set_link_ap(assoc, NULL);

    ds_tree_remove(&assoc->m->non_mlo_assocs, assoc);
    FREE(assoc->vif_name);
    FREE(assoc);

    *assoc_ptr = NULL;
}

/* ow_mld_redir_sta_link_ap_t */

static ow_mld_redir_sta_link_ap_t *ow_mld_redir_sta_link_ap_alloc(
        ow_mld_redir_sta_t *m,
        const char *vif_name,
        ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    if (m == NULL) return NULL;
    if (WARN_ON(vif_name == NULL)) return NULL;
    if (WARN_ON(mld_ap == NULL)) return NULL;
    if (WARN_ON(ds_tree_find(&mld_ap->link_aps, vif_name) != NULL)) return NULL;
    if (WARN_ON(ds_tree_find(&m->link_aps, vif_name) != NULL)) return NULL;

    ow_mld_redir_sta_link_ap_t *link_ap = CALLOC(1, sizeof(*link_ap));
    link_ap->m = m;
    link_ap->vif_name = STRDUP(vif_name);
    link_ap->mld_ap = mld_ap;
    link_ap->is_in_bridge = false;
    link_ap->is_main = strcmp(vif_name, mld_ap->vif_name) == 0;
    ds_tree_init(&link_ap->assocs, (ds_key_cmp_t *)osw_hwaddr_cmp, ow_mld_redir_sta_assoc_t, node_link_ap);
    ds_tree_insert(&mld_ap->link_aps, link_ap, link_ap->vif_name);
    ds_tree_insert(&m->link_aps, link_ap, link_ap->vif_name);
    LOGD(LOG_PREFIX_LINK(link_ap, "allocated"));
    return link_ap;
}

static void ow_mld_redir_sta_link_ap_attach(ow_mld_redir_sta_link_ap_t *link_ap)
{
    ow_mld_redir_sta_t *m = link_ap->m;

    if (link_ap->ingress_skbedit == NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "attaching ingress skbedit tc"));

        const char *match = strfmta(OW_MLD_REDIR_STA_MATCH_ETHER_DEST, OSW_HWADDR_ARG(&link_ap->mld_ap->vif_addr));
        const char *action = strfmta(OW_MLD_REDIR_STA_ACTION_SKBEDIT);
        const int priority = OW_MLD_REDIR_STA_PRIORITY_SKBEDIT;
        link_ap->ingress_skbedit = ow_mld_redir_sta_tc_alloc(m, match, action, priority);

        ow_mld_redir_sta_tc_attach_ingress(link_ap->ingress_skbedit, link_ap->vif_name);
    }

    if (link_ap->ingress_local == NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "attaching ingress local tc"));

        const char *match = strfmta(OW_MLD_REDIR_STA_MATCH_ETHER_DEST, OSW_HWADDR_ARG(&link_ap->vif_addr));
        const char *action = strfmta(OW_MLD_REDIR_STA_ACTION_LOCAL);
        const int priority = OW_MLD_REDIR_STA_PRIORITY_LOCAL;
        link_ap->ingress_local = ow_mld_redir_sta_tc_alloc(m, match, action, priority);

        ow_mld_redir_sta_tc_attach_ingress(link_ap->ingress_local, link_ap->vif_name);
    }

    if (link_ap->ingress_redirect == NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "attaching ingress redirect tc"));

        const char *match = strfmta(OW_MLD_REDIR_STA_MATCH_ALL);
        const char *action = strfmta(OW_MLD_REDIR_STA_ACTION_REDIR_INGRESS, link_ap->mld_ap->vif_name);
        const int priority = OW_MLD_REDIR_STA_PRIORITY_REDIRECT;
        link_ap->ingress_redirect = ow_mld_redir_sta_tc_alloc(m, match, action, priority);

        ow_mld_redir_sta_tc_attach_ingress(link_ap->ingress_redirect, link_ap->vif_name);
    }
}

static void ow_mld_redir_sta_link_ap_detach(ow_mld_redir_sta_link_ap_t *link_ap)
{
    if (link_ap->ingress_skbedit != NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "detaching ingress skbedit tc"));
        ow_mld_redir_sta_tc_drop(&link_ap->ingress_skbedit);
    }

    if (link_ap->ingress_local != NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "detaching ingress local tc"));
        ow_mld_redir_sta_tc_drop(&link_ap->ingress_local);
    }

    if (link_ap->ingress_redirect != NULL)
    {
        LOGI(LOG_PREFIX_LINK(link_ap, "detaching ingress redirect tc"));
        ow_mld_redir_sta_tc_drop(&link_ap->ingress_redirect);
    }
}

static void ow_mld_redir_sta_link_ap_update(ow_mld_redir_sta_link_ap_t *link_ap)
{
    if (link_ap == NULL) return;
    if (WARN_ON(link_ap->mld_ap == NULL)) return;

    const bool is_redirecting =
            link_ap->mld_ap->is_redirecting && !osw_hwaddr_is_zero(&link_ap->mld_ap->vif_addr) && !link_ap->is_main;

    if (is_redirecting)
    {
        ow_mld_redir_sta_link_ap_attach(link_ap);
    }
    else
    {
        ow_mld_redir_sta_link_ap_detach(link_ap);
    }

    ow_mld_redir_sta_assoc_t *assoc;
    ds_tree_foreach (&link_ap->assocs, assoc)
    {
        ow_mld_redir_sta_assoc_update(assoc);
    }
}

static void ow_mld_redir_sta_mld_ap_update(ow_mld_redir_sta_mld_ap_t *mld_ap);

static void ow_mld_redir_sta_link_ap_set_in_bridge(ow_mld_redir_sta_link_ap_t *link_ap, const bool is_in_bridge)
{
    if (link_ap == NULL) return;
    if (link_ap->is_in_bridge == is_in_bridge) return;

    LOGD(LOG_PREFIX_LINK(
            link_ap,
            "is_in_bridge: %s -> %s",
            link_ap->is_in_bridge ? "true" : "false",
            is_in_bridge ? "true" : "false"));
    link_ap->is_in_bridge = is_in_bridge;

    ow_mld_redir_sta_mld_ap_update(link_ap->mld_ap);
}

static void ow_mld_redir_sta_link_ap_set_vif_addr(
        ow_mld_redir_sta_link_ap_t *link_ap,
        const struct osw_hwaddr *vif_addr)
{
    if (link_ap == NULL) return;
    if (vif_addr == NULL) return;
    if (osw_hwaddr_is_equal(&link_ap->vif_addr, vif_addr)) return;

    LOGD(LOG_PREFIX_LINK(link_ap, "vif_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT),
         OSW_HWADDR_ARG(&link_ap->vif_addr),
         OSW_HWADDR_ARG(vif_addr));

    link_ap->vif_addr = *vif_addr;
    ow_mld_redir_sta_mld_ap_update(link_ap->mld_ap);
}

static void ow_mld_redir_sta_link_ap_drop(ow_mld_redir_sta_link_ap_t **link_ap_ptr)
{
    if (link_ap_ptr == NULL) return;

    ow_mld_redir_sta_link_ap_t *link_ap = *link_ap_ptr;
    if (link_ap == NULL) return;
    if (WARN_ON(link_ap->m == NULL)) return;
    if (WARN_ON(link_ap->mld_ap == NULL)) return;

    LOGD(LOG_PREFIX_LINK(link_ap, "dropping"));

    ow_mld_redir_sta_link_ap_set_in_bridge(link_ap, false);

    ds_tree_remove(&link_ap->mld_ap->link_aps, link_ap);
    ds_tree_remove(&link_ap->m->link_aps, link_ap);
    link_ap->mld_ap = NULL;

    ow_mld_redir_sta_assoc_t *assoc;
    ow_mld_redir_sta_assoc_t *tmp;
    ds_tree_foreach_safe (&link_ap->assocs, assoc, tmp)
    {
        ow_mld_redir_sta_assoc_set_link_ap(assoc, NULL);
    }

    FREE(link_ap->vif_name);
    FREE(link_ap);

    *link_ap_ptr = NULL;
}

static ow_mld_redir_sta_tc_t *ow_mld_redir_sta_lookup_tc(
        ow_mld_redir_sta_t *m,
        const char *action,
        const char *match,
        const int priority)
{
    if (action == NULL) return NULL;
    if (match == NULL) return NULL;

    ow_mld_redir_sta_tc_t *tc;
    ds_tree_foreach (&m->tcs, tc)
    {
        const bool action_eq = (strcmp(tc->action, action) == 0);
        const bool match_eq = (strcmp(tc->match, match) == 0);
        const bool priority_eq = (tc->priority == priority);
        if (action_eq && match_eq && priority_eq)
        {
            return tc;
        }
    }
    return NULL;
}

/* ow_mld_redir_sta_mld_ap_t */

static bool ow_mld_redir_sta_mld_ap_can_redirect(const ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    size_t num_is_in_bridge = 0;
    size_t num_aps = 0;
    size_t num_mains = 0;

    ow_mld_redir_sta_link_ap_t *link_ap;
    ds_tree_foreach ((ds_tree_t *)&mld_ap->link_aps, link_ap)
    {
        if (link_ap->is_in_bridge)
        {
            num_is_in_bridge++;
        }

        if (link_ap->is_main)
        {
            num_mains++;
        }

        num_aps++;
    }

    /* This shouldn't really happen. If it does, something
     * is very wrong.
     */
    WARN_ON(num_mains > 1);

    const bool is_in_bridge = (num_aps > 0) && (num_is_in_bridge == num_aps);
    return !osw_hwaddr_is_zero(&mld_ap->vif_addr) && num_mains > 0 && !is_in_bridge;
}

static ow_mld_redir_sta_mld_ap_t *ow_mld_redir_sta_mld_ap_alloc(ow_mld_redir_sta_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (WARN_ON(vif_name == NULL)) return NULL;
    if (WARN_ON(ds_tree_find(&m->mld_aps, vif_name) != NULL)) return NULL;

    ow_mld_redir_sta_mld_ap_t *mld_ap = CALLOC(1, sizeof(*mld_ap));
    mld_ap->m = m;
    mld_ap->vif_name = STRDUP(vif_name);
    ds_tree_init(&mld_ap->link_aps, ds_str_cmp, ow_mld_redir_sta_link_ap_t, node_mld_ap);
    ds_tree_insert(&m->mld_aps, mld_ap, mld_ap->vif_name);
    LOGD(LOG_PREFIX_MLD(mld_ap, "allocated"));
    return mld_ap;
}

static void ow_mld_redir_sta_mld_ap_drop(ow_mld_redir_sta_mld_ap_t **mld_ap_ptr)
{
    if (mld_ap_ptr == NULL) return;

    ow_mld_redir_sta_mld_ap_t *mld_ap = *mld_ap_ptr;
    if (mld_ap == NULL) return;
    if (WARN_ON(mld_ap->m == NULL)) return;

    LOGD(LOG_PREFIX_MLD(mld_ap, "dropping"));
    ASSERT(ds_tree_len(&mld_ap->link_aps) == 0, "");

    ds_tree_remove(&mld_ap->m->mld_aps, mld_ap);
    FREE(mld_ap->vif_name);
    FREE(mld_ap);

    *mld_ap_ptr = NULL;
}

static const ow_mld_redir_sta_link_ap_t *ow_mld_redir_sta_mld_ap_get_main_link_ap(ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    if (mld_ap == NULL) return NULL;

    ow_mld_redir_sta_link_ap_t *link_ap;
    ds_tree_foreach (&mld_ap->link_aps, link_ap)
    {
        if (link_ap->is_main)
        {
            return link_ap;
        }
    }

    return NULL;
}

static const struct osw_hwaddr *ow_mld_redir_sta_mld_ap_get_vif_addr(ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    const ow_mld_redir_sta_link_ap_t *main_link_ap = ow_mld_redir_sta_mld_ap_get_main_link_ap(mld_ap);
    return main_link_ap ? &main_link_ap->vif_addr : osw_hwaddr_zero();
}

static void ow_mld_redir_sta_mld_ap_invalidate_link_ingress(ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    if (mld_ap == NULL) return;

    ow_mld_redir_sta_link_ap_t *link_ap;
    ds_tree_foreach (&mld_ap->link_aps, link_ap)
    {
        ow_mld_redir_sta_link_ap_detach(link_ap);
    }
}

static void ow_mld_redir_sta_mld_ap_update(ow_mld_redir_sta_mld_ap_t *mld_ap)
{
    const struct osw_hwaddr *vif_addr = ow_mld_redir_sta_mld_ap_get_vif_addr(mld_ap);
    const bool vif_addr_changed = osw_hwaddr_is_equal(&mld_ap->vif_addr, vif_addr) == false;
    if (vif_addr_changed)
    {
        LOGD(LOG_PREFIX_MLD(
                mld_ap,
                "vif_addr: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
                OSW_HWADDR_ARG(&mld_ap->vif_addr),
                OSW_HWADDR_ARG(vif_addr)));
        mld_ap->vif_addr = *vif_addr;
        ow_mld_redir_sta_mld_ap_invalidate_link_ingress(mld_ap);
    }

    const bool redirecting = ow_mld_redir_sta_mld_ap_can_redirect(mld_ap);
    const bool redirecting_changed = (redirecting != mld_ap->is_redirecting);
    if (redirecting_changed)
    {
        LOGD(LOG_PREFIX_MLD(
                mld_ap,
                "redirecting: %s -> %s",
                mld_ap->is_redirecting ? "true" : "false",
                redirecting ? "true" : "false"));
        mld_ap->is_redirecting = redirecting;
    }

    const size_t num_link_aps = ds_tree_len(&mld_ap->link_aps);
    const bool num_link_aps_changed = (num_link_aps != mld_ap->num_link_aps);
    if (num_link_aps_changed)
    {
        LOGD(LOG_PREFIX_MLD(mld_ap, "num_link_aps: %zu -> %zu", mld_ap->num_link_aps, num_link_aps));
        mld_ap->num_link_aps = num_link_aps;
    }

    const bool anything_changed = vif_addr_changed || redirecting_changed || num_link_aps_changed;
    if (anything_changed)
    {
        ow_mld_redir_sta_link_ap_t *link_ap;
        ds_tree_foreach (&mld_ap->link_aps, link_ap)
        {
            ow_mld_redir_sta_link_ap_update(link_ap);
        }
    }
}

/* observer glue */

static const char *ow_mld_redir_sta_assoc_get_vif_name(const osw_sta_assoc_entry_t *e)
{
    if (e == NULL) return NULL;
    if (osw_sta_assoc_entry_is_mlo(e)) return NULL;

    const osw_sta_assoc_links_t *links = osw_sta_assoc_entry_get_active_links(e);
    if (WARN_ON(links->count > 1)) return NULL;

    if (links->count == 1)
    {
        const struct osw_hwaddr *bssid = &links->links[0].local_sta_addr;
        const struct osw_state_vif_info *vif_info = osw_state_vif_lookup_by_mac_addr(bssid);
        if (vif_info == NULL) return NULL;

        return vif_info->vif_name;
    }
    else
    {
        return NULL;
    }
}

static void ow_mld_redir_sta_assoc_changed_cb(void *priv, const osw_sta_assoc_entry_t *e, osw_sta_assoc_event_e ev)
{
    ow_mld_redir_sta_t *m = priv;
    const struct osw_hwaddr *assoc_addr = osw_sta_assoc_entry_get_addr(e);
    if (WARN_ON(assoc_addr == NULL)) return;

    const char *vif_name = ow_mld_redir_sta_assoc_get_vif_name(e);
    ow_mld_redir_sta_assoc_t *assoc = ds_tree_find(&m->non_mlo_assocs, assoc_addr);

    switch (ev)
    {
        case OSW_STA_ASSOC_CONNECTED:
            WARN_ON(assoc != NULL);
            assoc = ow_mld_redir_sta_assoc_alloc(m, assoc_addr, vif_name);
            break;
        case OSW_STA_ASSOC_RECONNECTED:
            WARN_ON(assoc == NULL);
            ow_mld_redir_sta_assoc_drop(&assoc);
            assoc = ow_mld_redir_sta_assoc_alloc(m, assoc_addr, vif_name);
            break;
        case OSW_STA_ASSOC_DISCONNECTED:
            ow_mld_redir_sta_assoc_drop(&assoc);
            break;
        case OSW_STA_ASSOC_UNDEFINED:
            break;
    }

    if (assoc == NULL) return;

    ow_mld_redir_sta_link_ap_t *link_ap = vif_name ? ds_tree_find(&m->link_aps, vif_name) : NULL;
    ow_mld_redir_sta_assoc_set_link_ap(assoc, link_ap);
}

static void ow_mld_redir_sta_mld_added_cb(void *priv, const char *mld_if_name)
{
    ow_mld_redir_sta_t *m = priv;
    ow_mld_redir_sta_mld_ap_t *mld_ap = ow_mld_redir_sta_mld_ap_alloc(m, mld_if_name);
    (void)mld_ap;
}

static void ow_mld_redir_sta_mld_removed_cb(void *priv, const char *mld_if_name)
{
    ow_mld_redir_sta_t *m = priv;
    ow_mld_redir_sta_mld_ap_t *mld_ap = ds_tree_find(&m->mld_aps, mld_if_name);
    ow_mld_redir_sta_mld_ap_drop(&mld_ap);
}

static bool ow_mld_redir_sta_info_is_in_bridge(const struct osw_state_vif_info *info)
{
    if (info == NULL) return false;
    if (info->drv_state == NULL) return false;

    switch (info->drv_state->vif_type)
    {
        case OSW_VIF_AP:
            if (strlen(info->drv_state->u.ap.bridge_if_name.buf) > 0)
            {
                return true;
            }
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            break;
        case OSW_VIF_UNDEFINED:
            break;
    }

    return false;
}

static void ow_mld_redir_sta_try_attach_dangling_assocs(ow_mld_redir_sta_t *m, ow_mld_redir_sta_link_ap_t *link_ap)
{
    if (m == NULL) return;
    if (link_ap == NULL) return;
    if (link_ap->mld_ap == NULL) return;

    ow_mld_redir_sta_assoc_t *assoc;
    ds_tree_foreach (&m->non_mlo_assocs, assoc)
    {
        if (strcmp(assoc->vif_name, link_ap->vif_name) == 0)
        {
            ow_mld_redir_sta_assoc_set_link_ap(assoc, link_ap);
        }
    }
}

static void ow_mld_redir_sta_mld_link_added_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    if (WARN_ON(info == NULL)) return;
    if (WARN_ON(info->drv_state == NULL)) return;
    const bool is_in_bridge = ow_mld_redir_sta_info_is_in_bridge(info);
    const struct osw_hwaddr *vif_addr = &info->drv_state->mac_addr;
    ow_mld_redir_sta_t *m = priv;
    ow_mld_redir_sta_mld_ap_t *mld_ap = ds_tree_find(&m->mld_aps, mld_if_name);
    ow_mld_redir_sta_link_ap_t *link_ap = mld_ap ? ow_mld_redir_sta_link_ap_alloc(m, info->vif_name, mld_ap) : NULL;
    ow_mld_redir_sta_link_ap_set_in_bridge(link_ap, is_in_bridge);
    ow_mld_redir_sta_link_ap_set_vif_addr(link_ap, vif_addr);
    ow_mld_redir_sta_try_attach_dangling_assocs(m, link_ap);
}

static void ow_mld_redir_sta_mld_link_changed_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    if (WARN_ON(info == NULL)) return;
    if (WARN_ON(info->drv_state == NULL)) return;
    const bool is_in_bridge = ow_mld_redir_sta_info_is_in_bridge(info);
    const struct osw_hwaddr *vif_addr = &info->drv_state->mac_addr;
    ow_mld_redir_sta_t *m = priv;
    ow_mld_redir_sta_mld_ap_t *mld_ap = ds_tree_find(&m->mld_aps, mld_if_name);
    ow_mld_redir_sta_link_ap_t *link_ap = mld_ap ? ds_tree_find(&mld_ap->link_aps, info->vif_name) : NULL;
    ow_mld_redir_sta_link_ap_set_in_bridge(link_ap, is_in_bridge);
    ow_mld_redir_sta_link_ap_set_vif_addr(link_ap, vif_addr);
}

static void ow_mld_redir_sta_mld_link_removed_cb(
        void *priv,
        const char *mld_if_name,
        const struct osw_state_vif_info *info)
{
    if (WARN_ON(info == NULL)) return;
    if (WARN_ON(info->drv_state == NULL)) return;
    ow_mld_redir_sta_t *m = priv;
    ow_mld_redir_sta_mld_ap_t *mld_ap = ds_tree_find(&m->mld_aps, mld_if_name);
    ow_mld_redir_sta_link_ap_t *link_ap = mld_ap ? ds_tree_find(&mld_ap->link_aps, info->vif_name) : NULL;
    ow_mld_redir_sta_link_ap_drop(&link_ap);
}

/* module setup */

static void ow_mld_redir_sta_prune_tcs(ow_mld_redir_sta_t *m)
{
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Interface_Classifier), NULL);
    json_t *row;
    size_t i;
    json_array_foreach(rows, i, row)
    {
        const char *token = json_string_value(json_object_get(row, "token"));
        if (token == NULL) continue;

        const bool our_token = (strcmp(token, OW_MLD_REDIR_STA_TOKEN) == 0);
        if (our_token)
        {
            const char *action = json_string_value(json_object_get(row, "action"));
            const char *match = json_string_value(json_object_get(row, "match"));
            const int priority = (int)json_integer_value(json_object_get(row, "priority"));
            ow_mld_redir_sta_tc_t *tc = ow_mld_redir_sta_lookup_tc(m, action, match, priority);
            const bool found = (tc != NULL);
            if (found)
            {
                /* keep it in OVSDB */
            }
            else
            {
                /* { "_uuid": [ "uuid", "some-uuid-string" ], ... } */
                const char *uuid = json_string_value(json_array_get(json_object_get(row, "_uuid"), 1));
                WARN_ON(uuid == NULL); /* shouldn't really happen, kept for correctness */
                if (uuid != NULL)
                {
                    LOGD(LOG_PREFIX(
                            "pruning unused tc: uuid=%s action='%s' match='%s' priority=%d",
                            uuid,
                            action,
                            match,
                            priority));
                    ow_mld_redir_sta_ovsdb_delete_classifier(uuid);
                }
            }
        }
    }
    json_decref(rows);
}

static void ow_mld_redir_sta_init(ow_mld_redir_sta_t *m)
{
    ds_tree_init(&m->non_mlo_assocs, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_mld_redir_sta_assoc, node_m);
    ds_tree_init(&m->mld_aps, ds_str_cmp, struct ow_mld_redir_sta_mld_ap, node_m);
    ds_tree_init(&m->link_aps, ds_str_cmp, struct ow_mld_redir_sta_link_ap, node_m);
    ds_tree_init(&m->tcs, ds_void_cmp, struct ow_mld_redir_sta_tc, node);
}

static void ow_mld_redir_sta_attach_assoc_obs(ow_mld_redir_sta_t *m)
{
    osw_sta_assoc_t *m_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    osw_sta_assoc_observer_params_t *params = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(params, ow_mld_redir_sta_assoc_changed_cb, m);
    m->sta_assoc_obs = osw_sta_assoc_observer_alloc(m_assoc, params);
}

static void ow_mld_redir_sta_attach_mld_vif_obs(ow_mld_redir_sta_t *m)
{
    osw_mld_vif_t *m_mld_vif = OSW_MODULE_LOAD(osw_mld_vif);
    m->mld_vif_obs = osw_mld_vif_observer_alloc(m_mld_vif);
    osw_mld_vif_observer_set_mld_added_fn(m->mld_vif_obs, ow_mld_redir_sta_mld_added_cb, m);
    osw_mld_vif_observer_set_mld_removed_fn(m->mld_vif_obs, ow_mld_redir_sta_mld_removed_cb, m);
    osw_mld_vif_observer_set_link_added_fn(m->mld_vif_obs, ow_mld_redir_sta_mld_link_added_cb, m);
    osw_mld_vif_observer_set_link_changed_fn(m->mld_vif_obs, ow_mld_redir_sta_mld_link_changed_cb, m);
    osw_mld_vif_observer_set_link_removed_fn(m->mld_vif_obs, ow_mld_redir_sta_mld_link_removed_cb, m);
}

static void ow_mld_redir_sta_attach(ow_mld_redir_sta_t *m)
{
    LOGI(LOG_PREFIX("enabling"));
    ow_mld_redir_sta_prune_tcs(m);
    ow_mld_redir_sta_attach_assoc_obs(m);
    ow_mld_redir_sta_attach_mld_vif_obs(m);
}

static void ow_mld_redir_sta_prune_when_disabled(void)
{
    /* This allows idempotency of starting OWM
     * with this module enabled and disabled in
     * sucessions and alternations.
     */
    ow_mld_redir_sta_t m;
    MEMZERO(m);
    ow_mld_redir_sta_init(&m);
    ow_mld_redir_sta_prune_tcs(&m);
}

OSW_MODULE(ow_mld_redir_sta)
{
    if (osw_etc_get("OW_MLD_REDIR_STA_ENABLED"))
    {
        ow_mld_redir_sta_t *m = CALLOC(1, sizeof(*m));
        ow_mld_redir_sta_init(m);
        ow_mld_redir_sta_attach(m);
        return m;
    }
    else
    {
        LOGI(LOG_PREFIX("disabled"));
        ow_mld_redir_sta_prune_when_disabled();
        return NULL;
    }
}
