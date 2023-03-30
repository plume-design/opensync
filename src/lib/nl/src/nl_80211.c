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

/* libc */
#include <inttypes.h>

/* 3rd party */
#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>

/* opensync */
#include <memutil.h>
#include <log.h>
#include <const.h>

/* unit */
#include <nl_cmd.h>
#include <nl_conn.h>
#include <nl_80211.h>
#include "nl_80211_i.h"

/* private */
#define map_to_nl(x) ((struct nl_80211 *)container_of(x, struct nl_80211, map))
#define map_to_conn(x) (map_to_nl((x))->conn)
#define NL_80211_NESTED_OVERRUNS_MAX 10

#define NL_80211_SUB_NOTIFY(map, fn, ...) \
    do { \
        struct nl_80211_sub *sub; \
        ds_dlist_foreach(&map->subs, sub) \
            if (!WARN_ON(sub->ops->fn == NULL)) \
                sub->ops->fn(__VA_ARGS__, sub->priv); \
    } while (0)

static int
ds_sta_cmp(const void *a,
           const void *b)
{
    const struct nl_80211_sta *x = a;
    const struct nl_80211_sta *y = b;
    return memcmp(x, y, sizeof(*x));
}

static void
nl_80211_overrun_recovery_complete(struct nl_80211 *nl_80211)
{
    nl_80211->recovering_from_overrun = false;
    nl_80211->nested_overruns = 0;
}

static void
nl_80211_notify_ready_try(struct nl_80211 *nl_80211)
{
    if (nl_80211_is_ready(nl_80211) == false) return;
    nl_80211->ready_fn(nl_80211, nl_80211->ready_fn_priv);
    nl_80211_overrun_recovery_complete(nl_80211);
}

static void
nl_80211_put_cmd__(struct nl_80211 *nl_80211,
                   struct nl_msg *msg,
                   int flags,
                   uint8_t cmd)
{
    const int family_id = nl_80211->family_id;
    WARN_ON(genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, family_id, 0, flags, cmd, 0) == NULL);
}

static void
nl_80211_cmd_dump_wiphy_send(struct nl_80211 *nl,
                             struct nl_cmd *cmd)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd__(nl, msg, NLM_F_DUMP, NL80211_CMD_GET_WIPHY);
    nl_cmd_set_msg(cmd, msg);
}

static void
nl_80211_cmd_dump_interface_send(struct nl_80211 *nl,
                                 struct nl_cmd *cmd)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd__(nl, msg, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE);
    nl_cmd_set_msg(cmd, msg);
}

static void
nl_80211_cmd_dump_station_send(struct nl_80211 *nl,
                               struct nl_cmd *cmd,
                               uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd__(nl, msg, NLM_F_DUMP, NL80211_CMD_GET_STATION);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    nl_cmd_set_msg(cmd, msg);
}

static void
nl_80211_cmd_get_features_send(struct nl_80211 *nl_80211)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd__(nl_80211, msg, 0, NL80211_CMD_GET_PROTOCOL_FEATURES);
    nl_cmd_set_msg(nl_80211->cmd_get_features, msg);
}

static void
nl_80211_cmd_get_family_send(struct nl_80211 *nl_80211)
{
    struct nl_msg *msg = nlmsg_alloc();
    WARN_ON(genlmsg_put(msg,
                        NL_AUTO_PORT,
                        NL_AUTO_SEQ,
                        GENL_ID_CTRL,
                        0,
                        0,
                        CTRL_CMD_GETFAMILY,
                        0) == NULL);
    nla_put_string(msg, CTRL_ATTR_FAMILY_NAME, "nl80211");
    nl_cmd_set_msg(nl_80211->cmd_get_family, msg);
}

static void *
nl_80211_sub_get_priv(struct nl_80211_sub *sub,
                      struct ds_tree *root,
                      size_t priv_size)
{
    if (priv_size == 0) {
        return NULL;
    }

    if (sub->map == NULL) {
        /* It's already unlinked from observing, so any
         * entries that were there, were already freed and
         * no new one should be allocated anymore.
         */
        return NULL;
    }

    struct nl_80211_sub_priv *priv = ds_tree_find(root, sub);
    if (priv == NULL) {
        priv = CALLOC(1, sizeof(*priv));
        priv->data = CALLOC(1, priv_size);
        priv->root = root;
        ds_tree_insert(root, priv, sub);
    }
    return priv->data;
}

static void
nl_80211_sub_free_priv(struct nl_80211_sub_priv *priv)
{
    if (priv == NULL) return;
    ds_tree_remove(priv->root, priv);
    FREE(priv->data);
    FREE(priv);
}

static void
nl_80211_sub_free_priv_all(struct ds_tree *root)
{
    struct nl_80211_sub_priv *priv;
    while ((priv = ds_tree_head(root)) != NULL) {
        nl_80211_sub_free_priv(priv);
    }
}

static void
nl_80211_map_phy_set(struct nl_80211_map *map,
                     uint32_t wiphy,
                     const char *phy_name)
{
    struct nl_80211_phy_priv *phy = ds_tree_find(&map->phy_by_wiphy, &wiphy);

    if (phy == NULL && phy_name == NULL) return;

    if (phy == NULL) {
        phy = CALLOC(1, sizeof(*phy));
        phy->pub.wiphy = wiphy;
        phy->pub.name = STRDUP(phy_name);
        ds_tree_init(&phy->sub_privs, ds_void_cmp, struct nl_80211_sub_priv, node);
        ds_tree_insert(&map->phy_by_wiphy, phy, &phy->pub.wiphy);
        ds_tree_insert(&map->phy_by_name, phy, phy->pub.name);
        NL_80211_SUB_NOTIFY(map, phy_added_fn, &phy->pub);
    }
    else if (phy_name != NULL) {
        if (strcmp(phy->pub.name, phy_name) != 0) {
            char *old_name = (char *)phy->pub.name;
            char *new_name = STRDUP(phy_name);

            ds_tree_remove(&map->phy_by_name, phy);
            phy->pub.name = new_name;;
            ds_tree_insert(&map->phy_by_name, phy, phy->pub.name);

            NL_80211_SUB_NOTIFY(map, phy_renamed_fn, &phy->pub, old_name, new_name);
            FREE(old_name);
        }
    }
    else {
        NL_80211_SUB_NOTIFY(map, phy_removed_fn, &phy->pub);
        nl_80211_sub_free_priv_all(&phy->sub_privs);
        ds_tree_remove(&map->phy_by_name, phy);
        ds_tree_remove(&map->phy_by_wiphy, phy);
        FREE(phy->pub.name);
        FREE(phy);
    }
}

static void
nl_80211_map_sta_set(struct nl_80211_map *map,
                     uint32_t ifindex,
                     const os_macaddr_t *addr,
                     bool connected)
{
    const struct nl_80211_sta lookup = {
        .addr = *addr,
        .ifindex = ifindex,
    };
    struct nl_80211_sta_priv *sta = ds_tree_find(&map->sta_by_link, &lookup);

    if (sta == NULL && connected == false) return;
    if (sta != NULL && connected == true) return;

    if (connected == true) {
        if (WARN_ON(sta != NULL)) return;

        sta = CALLOC(1, sizeof(*sta));
        sta->pub.ifindex = ifindex;
        sta->pub.addr = *addr;
        ds_tree_init(&sta->sub_privs, ds_void_cmp, struct nl_80211_sub_priv, node);
        ds_tree_insert(&map->sta_by_link, sta, &sta->pub);
        NL_80211_SUB_NOTIFY(map, sta_added_fn, &sta->pub);
    }
    else {
        if (WARN_ON(sta == NULL)) return;
        NL_80211_SUB_NOTIFY(map, sta_removed_fn, &sta->pub);
        nl_80211_sub_free_priv_all(&sta->sub_privs);
        ds_tree_remove(&map->sta_by_link, sta);
        FREE(sta);
    }
}

static void
nl_80211_map_station(struct nl_80211_map *map,
                     struct nl_msg *msg)
{
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    const size_t mac_len = sizeof(os_macaddr_t);
    struct nla_policy policy[NL80211_ATTR_MAX + 1] = {
        [NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
        [NL80211_ATTR_MAC] = { .minlen = mac_len, .maxlen = mac_len },
        [NL80211_ATTR_GENERATION] = { .type = NLA_U32 },
    };
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, policy);
    struct nlattr *ifindex = tb[NL80211_ATTR_IFINDEX];
    struct nlattr *mac = tb[NL80211_ATTR_MAC];
    struct nlattr *generation = tb[NL80211_ATTR_GENERATION];
    if (WARN_ON(err)) return;
    if (WARN_ON(ifindex == NULL)) return;
    if (WARN_ON(mac == NULL)) return;
    if (WARN_ON(generation == NULL)) return;

    const uint32_t ifindex_u32 = nla_get_u32(ifindex);
    const os_macaddr_t *mac_addr = nla_data(mac);
    const uint32_t generation_u32 = nla_get_u32(generation);
    const uint8_t cmd = genlmsg_hdr(nlmsg_hdr(msg))->cmd;

    switch (cmd) {
        case NL80211_CMD_SET_STATION:
        case NL80211_CMD_NEW_STATION:
            LOGD("nl: 80211: map: netdev#%"PRIu32"/"PRI(os_macaddr_t)"/%"PRIu32": updating",
                 ifindex_u32, FMT(os_macaddr_t, *mac_addr), generation_u32);
            nl_80211_map_sta_set(map, ifindex_u32, mac_addr, true);
            break;
        case NL80211_CMD_DEL_STATION:
            LOGD("nl: 80211: map: netdev#%"PRIu32"/"PRI(os_macaddr_t)"/%"PRIu32": removing",
                 ifindex_u32, FMT(os_macaddr_t, *mac_addr), generation_u32);
            nl_80211_map_sta_set(map, ifindex_u32, mac_addr, false);
            break;
    }
}

static void
nl_80211_cmd_dump_station_response_cb(struct nl_cmd *cmd,
                                      struct nl_msg *msg,
                                      void *priv)
{
    struct nl_80211_map *map = priv;
    nl_80211_map_station(map, msg);
}

static void
nl_80211_cmd_dump_station_completed_cb(struct nl_cmd *cmd,
                                       void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_notify_ready_try(nl_80211);
}

static void
nl_80211_map_vif_set(struct nl_80211_map *map,
                     uint32_t ifindex,
                     uint32_t wiphy,
                     const char *vif_name)
{
    struct nl_80211_vif_priv *vif = ds_tree_find(&map->vif_by_ifindex, &ifindex);
    struct nl_80211 *nl = map_to_nl(map);
    struct nl_conn *conn = map_to_conn(map);

    if (vif == NULL && vif_name == NULL) return;

    if (vif == NULL) {
        vif = CALLOC(1, sizeof(*vif));
        vif->pub.wiphy = wiphy;
        vif->pub.ifindex = ifindex;
        vif->pub.name = STRDUP(vif_name);
        struct nl_cmd *cmd = nl_conn_alloc_cmd(conn);
        vif->cmd_dump_station = cmd;
        nl_cmd_set_response_fn(cmd, nl_80211_cmd_dump_station_response_cb, map);
        nl_cmd_set_completed_fn(cmd, nl_80211_cmd_dump_station_completed_cb, nl);
        nl_80211_cmd_dump_station_send(nl, vif->cmd_dump_station, ifindex);
        ds_tree_init(&vif->sub_privs, ds_void_cmp, struct nl_80211_sub_priv, node);
        ds_tree_insert(&map->vif_by_ifindex, vif, &vif->pub.ifindex);
        ds_tree_insert(&map->vif_by_name, vif, vif->pub.name);
        NL_80211_SUB_NOTIFY(map, vif_added_fn, &vif->pub);
    }
    else if (vif_name != NULL) {
        assert(vif->pub.wiphy == wiphy);

        if (strcmp(vif->pub.name, vif_name) != 0) {
            char *old_name = (char *)vif->pub.name;
            char *new_name = STRDUP(vif_name);

            ds_tree_remove(&map->vif_by_name, vif);
            vif->pub.name = new_name;
            ds_tree_insert(&map->vif_by_name, vif, vif->pub.name);

            NL_80211_SUB_NOTIFY(map, vif_renamed_fn, &vif->pub, old_name, new_name);
            FREE(old_name);
        }
    }
    else {
        NL_80211_SUB_NOTIFY(map, vif_removed_fn, &vif->pub);
        nl_80211_sub_free_priv_all(&vif->sub_privs);
        nl_cmd_free(vif->cmd_dump_station);
        vif->cmd_dump_station = NULL;
        ds_tree_remove(&map->vif_by_name, vif);
        ds_tree_remove(&map->vif_by_ifindex, vif);
        FREE(vif->pub.name);
        FREE(vif);
    }
}

static void
nl_80211_map_wiphy(struct nl_80211_map *map,
                   struct nl_msg *msg)
{
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nla_policy policy[NL80211_ATTR_MAX + 1] = {
        [NL80211_ATTR_WIPHY] = { .type = NLA_U32 },
        [NL80211_ATTR_WIPHY_NAME] = { .type = NLA_STRING },
    };
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, policy);
    struct nlattr *wiphy = tb[NL80211_ATTR_WIPHY];
    struct nlattr *wiphy_name = tb[NL80211_ATTR_WIPHY_NAME];
    if (WARN_ON(err)) return;
    if (WARN_ON(wiphy == NULL)) return;
    if (WARN_ON(wiphy_name == NULL)) return;

    const uint32_t wiphy_u32 = nla_get_u32(wiphy);
    const char *wiphy_str = nla_get_string(wiphy_name);
    const uint8_t cmd = genlmsg_hdr(nlmsg_hdr(msg))->cmd;

    switch (cmd) {
        case NL80211_CMD_SET_WIPHY:
        case NL80211_CMD_NEW_WIPHY:
            LOGD("nl: 80211: map: phy#%"PRIu32"/%s: updating",
                 wiphy_u32, wiphy_str);
            nl_80211_map_phy_set(map, wiphy_u32, wiphy_str);
            break;
        case NL80211_CMD_DEL_WIPHY:
            LOGD("nl: 80211: map: phy#%"PRIu32"/%s: removing",
                 wiphy_u32, wiphy_str);
            nl_80211_map_phy_set(map, wiphy_u32, NULL);
            break;
    }
}

static void
nl_80211_map_interface(struct nl_80211_map *map,
                       struct nl_msg *msg)
{
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nla_policy policy[NL80211_ATTR_MAX + 1] = {
        [NL80211_ATTR_WIPHY] = { .type = NLA_U32 },
        [NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
        [NL80211_ATTR_IFNAME] = { .type = NLA_STRING },
    };
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, policy);
    struct nlattr *wiphy = tb[NL80211_ATTR_WIPHY];
    struct nlattr *ifindex = tb[NL80211_ATTR_IFINDEX];
    struct nlattr *ifname = tb[NL80211_ATTR_IFNAME];
    if (WARN_ON(err)) return;
    if (ifindex == NULL) return; /* non-netdev, likely p2p-dev; ignore silently */
    if (WARN_ON(wiphy == NULL)) return;
    if (WARN_ON(ifname == NULL)) return;

    const uint32_t wiphy_u32 = nla_get_u32(wiphy);
    const uint32_t ifindex_u32 = nla_get_u32(ifindex);
    const char *ifname_str = nla_get_string(ifname);
    const uint8_t cmd = genlmsg_hdr(nlmsg_hdr(msg))->cmd;

    switch (cmd) {
        case NL80211_CMD_SET_INTERFACE:
        case NL80211_CMD_NEW_INTERFACE:
            LOGD("nl: 80211: map: phy#%"PRIu32"/netdev#%"PRIu32"/%s: updating",
                 wiphy_u32, ifindex_u32, ifname_str);
            nl_80211_map_vif_set(map, ifindex_u32, wiphy_u32, ifname_str);
            break;
        case NL80211_CMD_DEL_INTERFACE:
            LOGD("nl: 80211: map: phy#%"PRIu32"/netdev#%"PRIu32"/%s: removing",
                 wiphy_u32, ifindex_u32, ifname_str);
            nl_80211_map_vif_set(map, ifindex_u32, 0, NULL);
            break;
    }
}

static void
nl_80211_cmd_dump_wiphy_response_cb(struct nl_cmd *cmd,
                                    struct nl_msg *msg,
                                    void *priv)
{
    struct nl_80211_map *map = priv;
    nl_80211_map_wiphy(map, msg);
}

static void
nl_80211_cmd_dump_wiphy_completed_cb(struct nl_cmd *cmd,
                                     void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_notify_ready_try(nl_80211);
}

static void
nl_80211_cmd_dump_interface_response_cb(struct nl_cmd *cmd,
                                        struct nl_msg *msg,
                                        void *priv)
{
    struct nl_80211_map *map = priv;
    nl_80211_map_interface(map, msg);
}

static void
nl_80211_cmd_dump_interface_completed_cb(struct nl_cmd *cmd,
                                         void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_notify_ready_try(nl_80211);
}

static void
nl_80211_sub_init(struct nl_80211_sub *sub)
{
    struct nl_80211_map *map = sub->map;
    if (map == NULL) return;

    struct nl_80211_phy_priv *phy;
    struct nl_80211_vif_priv *vif;
    struct nl_80211_sta_priv *sta;

    ds_tree_foreach(&map->phy_by_name, phy) NL_80211_SUB_NOTIFY(map, phy_added_fn, &phy->pub);
    ds_tree_foreach(&map->vif_by_name, vif) NL_80211_SUB_NOTIFY(map, vif_added_fn, &vif->pub);
    ds_tree_foreach(&map->sta_by_link, sta) NL_80211_SUB_NOTIFY(map, sta_added_fn, &sta->pub);
}

static void
nl_80211_sub_fini(struct nl_80211_sub *sub)
{
    struct nl_80211_map *map = sub->map;
    if (map == NULL) return;

    struct nl_80211_phy_priv *phy;
    struct nl_80211_vif_priv *vif;
    struct nl_80211_sta_priv *sta;

    ds_tree_foreach(&map->sta_by_link, sta) {
        NL_80211_SUB_NOTIFY(map, sta_removed_fn, &sta->pub);
        nl_80211_sub_free_priv(ds_tree_find(&sta->sub_privs, sub));
    }

    ds_tree_foreach(&map->vif_by_name, vif) {
        NL_80211_SUB_NOTIFY(map, vif_removed_fn, &vif->pub);
        nl_80211_sub_free_priv(ds_tree_find(&vif->sub_privs, sub));
    }

    ds_tree_foreach(&map->phy_by_name, phy) {
        NL_80211_SUB_NOTIFY(map, phy_removed_fn, &phy->pub);
        nl_80211_sub_free_priv(ds_tree_find(&phy->sub_privs, sub));
    }
}

static void
nl_80211_sub_unlink(struct nl_80211_sub *sub)
{
    nl_80211_sub_fini(sub);

    if (sub->map != NULL) {
        ds_dlist_remove(&sub->map->subs, sub);
        sub->map = NULL;
    }
}

static void
nl_80211_sub_unlink_all(struct nl_80211 *nl_80211)
{
    struct nl_80211_sub *sub;
    while ((sub = ds_dlist_head(&nl_80211->map.subs)) != NULL) {
        nl_80211_sub_unlink(sub);
    }
}

static void
nl_80211_map_init(struct nl_80211_map *map)
{
    ds_tree_init(&map->phy_by_name, ds_str_cmp, struct nl_80211_phy_priv, node_name);
    ds_tree_init(&map->phy_by_wiphy, ds_u32_cmp, struct nl_80211_phy_priv, node_wiphy);
    ds_tree_init(&map->vif_by_name, ds_str_cmp, struct nl_80211_vif_priv, node_name);
    ds_tree_init(&map->vif_by_ifindex, ds_u32_cmp, struct nl_80211_vif_priv, node_ifindex);
    ds_tree_init(&map->sta_by_link, ds_sta_cmp, struct nl_80211_sta_priv, node);
    ds_dlist_init(&map->subs, struct nl_80211_sub, node);
}

static void
nl_80211_map_start(struct nl_80211_map *map,
                   struct nl_80211 *nl_80211,
                   struct nl_conn *conn)
{
    map->cmd_dump_wiphy = nl_conn_alloc_cmd(conn);
    nl_cmd_set_response_fn(map->cmd_dump_wiphy, nl_80211_cmd_dump_wiphy_response_cb, map);
    nl_cmd_set_completed_fn(map->cmd_dump_wiphy, nl_80211_cmd_dump_wiphy_completed_cb, nl_80211);

    map->cmd_dump_interface = nl_conn_alloc_cmd(conn);
    nl_cmd_set_response_fn(map->cmd_dump_interface, nl_80211_cmd_dump_interface_response_cb, map);
    nl_cmd_set_completed_fn(map->cmd_dump_interface, nl_80211_cmd_dump_interface_completed_cb, nl_80211);
}

static void
nl_80211_map_phy_flush(struct nl_80211_map *map)
{
    struct nl_80211_phy_priv *phy;
    while ((phy = ds_tree_head(&map->phy_by_wiphy)) != NULL) {
        nl_80211_map_phy_set(map, phy->pub.wiphy, NULL);
    }
}

static void
nl_80211_map_vif_flush(struct nl_80211_map *map)
{
    struct nl_80211_vif_priv *vif;
    while ((vif = ds_tree_head(&map->vif_by_ifindex)) != NULL) {
        nl_80211_map_vif_set(map, vif->pub.ifindex, 0, NULL);
    }
}

static void
nl_80211_map_sta_flush(struct nl_80211_map *map)
{
    struct nl_80211_sta_priv *sta;
    while ((sta = ds_tree_head(&map->sta_by_link)) != NULL) {
        nl_80211_map_sta_set(map,
                             sta->pub.ifindex,
                             &sta->pub.addr,
                             false);
    }
}

static void
nl_80211_map_flush(struct nl_80211_map *map)
{
    nl_80211_map_phy_flush(map);
    nl_80211_map_vif_flush(map);
    nl_80211_map_sta_flush(map);
}

static void
nl_80211_map_stop(struct nl_80211_map *map)
{
    if (map->cmd_dump_wiphy != NULL) {
        nl_cmd_free(map->cmd_dump_wiphy);
        map->cmd_dump_wiphy = NULL;
    }

    if (map->cmd_dump_interface != NULL) {
        nl_cmd_free(map->cmd_dump_interface);
        map->cmd_dump_interface = NULL;
    }

    nl_80211_map_flush(map);
}

static void
nl_80211_map_build(struct nl_80211_map *map)
{
    struct nl_80211 *nl = map_to_nl(map);
    nl_80211_map_flush(map);
    nl_80211_cmd_dump_wiphy_send(nl, map->cmd_dump_wiphy);
    nl_80211_cmd_dump_interface_send(nl, map->cmd_dump_interface);
}

static void
nl_80211_map_update(struct nl_80211 *nl_80211,
                    struct nl_msg *msg)
{
    struct nl_80211_map *map = &nl_80211->map;
    const uint8_t cmd = genlmsg_hdr(nlmsg_hdr(msg))->cmd;

    switch (cmd) {
        case NL80211_CMD_SET_WIPHY:
        case NL80211_CMD_NEW_WIPHY:
        case NL80211_CMD_DEL_WIPHY:
            nl_80211_map_wiphy(map, msg);
            break;
        case NL80211_CMD_SET_INTERFACE:
        case NL80211_CMD_NEW_INTERFACE:
        case NL80211_CMD_DEL_INTERFACE:
            nl_80211_map_interface(map, msg);
            break;
        case NL80211_CMD_SET_STATION:
        case NL80211_CMD_NEW_STATION:
        case NL80211_CMD_DEL_STATION:
            nl_80211_map_station(map, msg);
            break;
        default:
            break;
    }
}

static void
nl_80211_parse_group(struct nl_80211 *nl_80211,
                     struct nlattr *group)
{
    struct nl_sock *sock = nl_conn_get_sock(nl_80211->conn);
    struct nlattr *tb[CTRL_ATTR_MCAST_GRP_MAX + 1];
    struct nla_policy policy[CTRL_ATTR_MCAST_GRP_MAX + 1] = {
        [CTRL_ATTR_MCAST_GRP_NAME] = { .type = NLA_STRING },
        [CTRL_ATTR_MCAST_GRP_ID] = { .type = NLA_U32 },
    };
    const int err = nla_parse_nested(tb, CTRL_ATTR_MCAST_GRP_MAX, group, policy);
    struct nlattr *nla_name = tb[CTRL_ATTR_MCAST_GRP_NAME];
    struct nlattr *nla_id = tb[CTRL_ATTR_MCAST_GRP_ID];
    if (WARN_ON(err)) return;
    if (WARN_ON(nla_name == NULL)) return;
    if (WARN_ON(nla_id == NULL)) return;

    const char *name = nla_get_string(nla_name);
    const uint32_t id = nla_get_u32(nla_id);

    LOGI("nl: 80211: membership: %s (%"PRIu32"): adding", name, id);
    nl_socket_add_membership(sock, id);
}

static void
nl_80211_cmd_get_family_response_cb(struct nl_cmd *cmd,
                                    struct nl_msg *msg,
                                    void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    struct nlattr *tb[CTRL_ATTR_MAX + 1];
    struct nla_policy policy[CTRL_ATTR_MAX + 1] = {
        [CTRL_ATTR_FAMILY_ID] = { .type = NLA_U16 },
        [CTRL_ATTR_MCAST_GROUPS] = { .type = NLA_NESTED },
    };
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, CTRL_ATTR_MAX, policy);
    struct nlattr *id = tb[CTRL_ATTR_FAMILY_ID];
    struct nlattr *groups = tb[CTRL_ATTR_MCAST_GROUPS];
    if (WARN_ON(err)) return;
    if (WARN_ON(id == NULL)) return;

    nl_80211->family_id = nla_get_u16(id);
    LOGI("nl: 80211: family: id=%"PRIu16, nl_80211->family_id);

    if (groups != NULL) {
        struct nlattr *group;
        int rem;
        nla_for_each_nested(group, groups, rem) {
            nl_80211_parse_group(nl_80211, group);
        }
    }
}

static void
nl_80211_cmd_get_family_completed_cb(struct nl_cmd *cmd,
                                     void *priv)
{

    struct nl_80211 *nl_80211 = priv;
    nl_80211_cmd_get_features_send(nl_80211);
}

static void
nl_80211_log_features(struct nl_80211 *nl_80211)
{
    LOGI("nl: 80211: features: 0x%08x", nl_80211->feature_bitmask);

    const uint32_t bitmask = NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP;
    const bool split_supported = ((nl_80211->feature_bitmask & bitmask) != 0);
    if (split_supported) {
        LOGI("nl: 80211: features: wiphy split");
    }
}

static void
nl_80211_cmd_get_features_response_cb(struct nl_cmd *cmd,
                                      struct nl_msg *msg,
                                      void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nla_policy policy[NL80211_ATTR_MAX + 1] = {
        [NL80211_ATTR_PROTOCOL_FEATURES] = { .type = NLA_U32 },
    };
    const int err = genlmsg_parse(nlmsg_hdr(msg), 0, tb, NL80211_ATTR_MAX, policy);
    struct nlattr *features = tb[NL80211_ATTR_PROTOCOL_FEATURES];
    if (WARN_ON(err)) return;
    if (WARN_ON(features == NULL)) return;

    nl_80211->feature_bitmask = nla_get_u32(features);
}

static void
nl_80211_cmd_get_features_completed_cb(struct nl_cmd *cmd,
                                       void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    struct nl_80211_map *map = &nl_80211->map;

    nl_80211_log_features(nl_80211);
    nl_80211_map_build(map);
}

static void
nl_80211_conn_sub_started_cb(struct nl_conn_subscription *sub,
                             void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_start(nl_80211);
}

static void
nl_80211_conn_sub_stopped_cb(struct nl_conn_subscription *sub,
                             void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_stop(nl_80211);
}

static void
nl_80211_conn_sub_event_cb(struct nl_conn_subscription *sub,
                           struct nl_msg *msg,
                           void *priv)
{
    struct nl_80211 *nl_80211 = priv;
    nl_80211_map_update(nl_80211, msg);
}

static void
nl_80211_overrun_recovery_sanity(struct nl_80211 *nl_80211)
{
    const uint32_t max = NL_80211_NESTED_OVERRUNS_MAX;
    nl_80211->nested_overruns++;
    if (nl_80211->nested_overruns < max) return;

    LOGE("nl: 80211: overrun: perpetual self-induced overrun detected; giving up");
    assert(0);
}

static void
nl_80211_overrun_recovery_start(struct nl_80211 *nl_80211)
{
    struct nl_80211_map *map = &nl_80211->map;

    if (nl_80211->recovering_from_overrun == true) {
        nl_80211_overrun_recovery_sanity(nl_80211);
    }

    nl_80211->recovering_from_overrun = true;
    nl_80211_map_build(map);
}

static void
nl_80211_conn_sub_overrun_cb(struct nl_conn_subscription *sub,
                             void *priv)
{
    struct nl_80211 *nl_80211 = priv;

    LOGI("nl: 80211: overrun detected; rebuilding map");
    nl_80211_overrun_recovery_start(nl_80211);
}

static void
nl_80211_notify_ready_init(struct nl_80211 *nl_80211)
{
    const bool not_ready_yet = (nl_80211_is_ready(nl_80211) == false);

    if (nl_80211->ready_fn == NULL) return;
    if (not_ready_yet) return;

    nl_80211->ready_fn(nl_80211, nl_80211->ready_fn_priv);
}

/* protected */

/* public */
struct nl_80211 *
nl_80211_alloc(void)
{
    struct nl_80211 *nl_80211 = CALLOC(1, sizeof(*nl_80211));
    nl_80211_map_init(&nl_80211->map);
    return nl_80211;
}

void
nl_80211_free(struct nl_80211 *nl_80211)
{
    nl_80211_sub_unlink_all(nl_80211);
    nl_80211_set_conn(nl_80211, NULL);
    nl_cmd_free(nl_80211->cmd_get_family);
    nl_cmd_free(nl_80211->cmd_get_features);
    FREE(nl_80211);
}

static bool
nl_80211_cmd_is_busy(const struct nl_cmd *cmd)
{
    if (cmd == NULL) return true;
    if (nl_cmd_is_pending(cmd)) return true;
    if (nl_cmd_is_in_flight(cmd)) return true;
    return false;
}

bool
nl_80211_is_ready(const struct nl_80211 *nl_80211)
{
    const struct nl_cmd *cmds[] = {
        nl_80211->cmd_get_family,
        nl_80211->cmd_get_features,
        nl_80211->map.cmd_dump_wiphy,
        nl_80211->map.cmd_dump_interface,
    };

    size_t i;
    for (i = 0; i < ARRAY_SIZE(cmds); i++) {
        const struct nl_cmd *cmd = cmds[i];
        if (nl_80211_cmd_is_busy(cmd)) {
            return false;
        }
    }

    const struct nl_80211_map *map = &nl_80211->map;
    /* FIXME: There's no const foreach variant */
    struct ds_tree *vifs = (struct ds_tree *)&map->vif_by_ifindex;
    struct nl_80211_vif_priv *vif;
    ds_tree_foreach(vifs, vif) {
        if (nl_80211_cmd_is_busy(vif->cmd_dump_station)) {
            return false;
        }
    }

    return true;
}

void
nl_80211_stop(struct nl_80211 *nl_80211)
{
    struct nl_80211_map *map = &nl_80211->map;

    if (nl_80211->cmd_get_family != NULL) {
        nl_cmd_free(nl_80211->cmd_get_family);
        nl_80211->cmd_get_family = NULL;
    }

    if (nl_80211->cmd_get_features != NULL) {
        nl_cmd_free(nl_80211->cmd_get_features);
        nl_80211->cmd_get_features = NULL;
    }

    nl_80211_map_stop(map);
}

void
nl_80211_start(struct nl_80211 *nl_80211)
{
    struct nl_80211_map *map = &nl_80211->map;
    struct nl_conn *conn = nl_80211->conn;

    if (WARN_ON(conn == NULL)) return;

    nl_80211->cmd_get_family = nl_conn_alloc_cmd(conn);
    nl_cmd_set_response_fn(nl_80211->cmd_get_family, nl_80211_cmd_get_family_response_cb, nl_80211);
    nl_cmd_set_completed_fn(nl_80211->cmd_get_family, nl_80211_cmd_get_family_completed_cb, nl_80211);

    nl_80211->cmd_get_features = nl_conn_alloc_cmd(conn);
    nl_cmd_set_response_fn(nl_80211->cmd_get_features, nl_80211_cmd_get_features_response_cb, nl_80211);
    nl_cmd_set_completed_fn(nl_80211->cmd_get_features, nl_80211_cmd_get_features_completed_cb, nl_80211);

    nl_80211_map_start(map, nl_80211, conn);
    /* This kicks off the sequence. Once first command
     * completes the other is generated and sent. It has to
     * be done in sequence before the second command depends
     * on the result from the first one.
    */
    nl_80211_cmd_get_family_send(nl_80211);
    WARN_ON(nl_80211_is_ready(nl_80211) == true);
}

static void
nl_80211_conn_unsubscribe(struct nl_80211 *nl_80211)
{
    if (nl_80211->conn_sub == NULL) return;

    nl_conn_subscription_free(nl_80211->conn_sub);
    nl_80211->conn_sub = NULL;
}

static void
nl_80211_conn_subscribe(struct nl_80211 *nl_80211)
{
    if (nl_80211->conn == NULL) return;
    if (WARN_ON(nl_80211->conn_sub != NULL)) nl_80211_conn_unsubscribe(nl_80211);

    struct nl_conn_subscription *sub = nl_conn_subscription_alloc();
    nl_80211->conn_sub = sub;
    nl_conn_subscription_set_started_fn(sub, nl_80211_conn_sub_started_cb, nl_80211);
    nl_conn_subscription_set_stopped_fn(sub, nl_80211_conn_sub_stopped_cb, nl_80211);
    nl_conn_subscription_set_event_fn(sub, nl_80211_conn_sub_event_cb, nl_80211);
    nl_conn_subscription_set_overrun_fn(sub, nl_80211_conn_sub_overrun_cb, nl_80211);
    nl_conn_subscription_start(sub, nl_80211->conn);
}

void
nl_80211_set_conn(struct nl_80211 *nl_80211,
                  struct nl_conn *conn)
{
    nl_80211_conn_unsubscribe(nl_80211);
    nl_80211->conn = conn;
    nl_80211_conn_subscribe(nl_80211);

}

struct nl_conn *
nl_80211_get_conn(struct nl_80211 *nl_80211)
{
    return nl_80211->conn;
}

/* FIXME: This should be per-subscription to facilitate
 * multiple observers.
 */
void
nl_80211_set_ready_fn(struct nl_80211 *nl_80211,
                      nl_80211_ready_fn_t *fn,
                      void *priv)
{
    nl_80211->ready_fn = fn;
    nl_80211->ready_fn_priv = priv;

    nl_80211_notify_ready_init(nl_80211);
}

void
nl_80211_put_cmd(struct nl_80211 *nl_80211,
                 struct nl_msg *msg,
                 int flags,
                 uint8_t cmd)
{
    if (WARN_ON(nl_80211_cmd_is_busy(nl_80211->cmd_get_family))) return;
    if (WARN_ON(msg == NULL)) return;

    nl_80211_put_cmd__(nl_80211, msg, flags, cmd);
}

void
nl_80211_put_wiphy(struct nl_80211 *nl_80211,
                   struct nl_msg *msg)
{
    if (WARN_ON(nl_80211_cmd_is_busy(nl_80211->cmd_get_family))) return;
    if (WARN_ON(nl_80211_cmd_is_busy(nl_80211->cmd_get_features))) return;
    if (WARN_ON(msg == NULL)) return;

    const uint32_t bitmask = NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP;
    const bool split_not_supported = ((nl_80211->feature_bitmask & bitmask) == 0);
    if (split_not_supported) return;

    nla_put_flag(msg, NL80211_ATTR_SPLIT_WIPHY_DUMP);
}

const struct nl_80211_phy *
nl_80211_phy_by_wiphy(struct nl_80211 *nl_80211,
                      uint32_t wiphy)
{
    struct nl_80211_map *map = &nl_80211->map;
    const struct nl_80211_phy_priv *phy = ds_tree_find(&map->phy_by_wiphy, &wiphy);
    if (phy == NULL) return NULL;
    return &phy->pub;
}

const struct nl_80211_phy *
nl_80211_phy_by_name(struct nl_80211 *nl_80211,
                     const char *phy_name)
{
    struct nl_80211_map *map = &nl_80211->map;
    const struct nl_80211_phy_priv *phy = ds_tree_find(&map->phy_by_name, phy_name);
    if (phy == NULL) return NULL;
    return &phy->pub;
}

const struct nl_80211_phy *
nl_80211_phy_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[])
{
    struct nlattr *wiphy = tb[NL80211_ATTR_WIPHY];
    struct nlattr *wiphy_name = tb[NL80211_ATTR_WIPHY_NAME];

    if (wiphy != NULL) {
        const uint32_t wiphy_u32 = nla_get_u32(wiphy);
        const struct nl_80211_phy *phy = nl_80211_phy_by_wiphy(nl_80211, wiphy_u32);
        if (phy != NULL) return phy;
    }

    if (wiphy_name != NULL) {
        const char *wiphy_str = nla_get_string(wiphy_name);
        const struct nl_80211_phy *phy = nl_80211_phy_by_name(nl_80211, wiphy_str);
        if (phy != NULL) return phy;
    }

    const struct nl_80211_vif *vif = nl_80211_vif_by_nla(nl_80211, tb);
    if (vif != NULL) {
        const uint32_t ifindex = vif->ifindex;
        const struct nl_80211_phy *phy = nl_80211_phy_by_ifindex(nl_80211, ifindex);
        if (phy != NULL) return phy;
    }

    return NULL;
}

const struct nl_80211_phy *
nl_80211_phy_by_ifindex(struct nl_80211 *nl_80211,
                        uint32_t ifindex)
{
    const struct nl_80211_vif *vif = nl_80211_vif_by_ifindex(nl_80211, ifindex);
    if (vif == NULL) return NULL;
    return nl_80211_phy_by_wiphy(nl_80211, vif->wiphy);
}

const struct nl_80211_vif *
nl_80211_vif_by_ifindex(struct nl_80211 *nl_80211,
                        uint32_t ifindex)
{
    struct nl_80211_map *map = &nl_80211->map;
    const struct nl_80211_vif_priv *vif = ds_tree_find(&map->vif_by_ifindex, &ifindex);
    if (vif == NULL) return NULL;
    return &vif->pub;
}

const struct nl_80211_vif *
nl_80211_vif_by_name(struct nl_80211 *nl_80211,
                     const char *vif_name)
{
    struct nl_80211_map *map = &nl_80211->map;
    const struct nl_80211_vif_priv *vif = ds_tree_find(&map->vif_by_name, vif_name);
    if (vif == NULL) return NULL;
    return &vif->pub;
}

const struct nl_80211_vif *
nl_80211_vif_by_nla(struct nl_80211 *nl_80211,
                    struct nlattr *tb[])
{
    struct nlattr *ifindex = tb[NL80211_ATTR_IFINDEX];
    struct nlattr *ifname = tb[NL80211_ATTR_IFNAME];

    if (ifindex != NULL) {
        const uint32_t ifindex_u32 = nla_get_u32(ifindex);
        const struct nl_80211_vif *vif = nl_80211_vif_by_ifindex(nl_80211, ifindex_u32);
        if (vif != NULL) return vif;
    }

    if (ifname != NULL) {
        const char *ifname_str = nla_get_string(ifname);
        const struct nl_80211_vif *vif = nl_80211_vif_by_name(nl_80211, ifname_str);
        if (vif != NULL) return vif;
    }

    return NULL;
}

const struct nl_80211_sta *
nl_80211_sta_by_link(struct nl_80211 *nl_80211,
                     uint32_t ifindex,
                     const os_macaddr_t *addr)
{
    struct nl_80211_map *map = &nl_80211->map;
    const struct nl_80211_sta lookup = {
        .ifindex = ifindex,
        .addr = *addr,
    };
    const struct nl_80211_sta_priv *sta = ds_tree_find(&map->sta_by_link, &lookup);
    if (sta == NULL) return NULL;
    return &sta->pub;
}

const struct nl_80211_sta *
nl_80211_sta_by_link_name(struct nl_80211 *nl_80211,
                          const char *vif_name,
                          const os_macaddr_t *addr)
{
    const struct nl_80211_vif *vif = nl_80211_vif_by_name(nl_80211, vif_name);
    if (vif_name == NULL) return NULL;
    const struct nl_80211_sta *sta = nl_80211_sta_by_link(nl_80211, vif->ifindex, addr);
    if (sta == NULL) return NULL;
    return sta;
}

void
nl_80211_phy_each(struct nl_80211 *nl_80211,
                  nl_80211_phy_each_fn_t *fn,
                  void *fn_priv)
{
    struct nl_80211_map *map = &nl_80211->map;
    struct nl_80211_phy_priv *phy;
    ds_tree_foreach(&map->phy_by_wiphy, phy)
        fn(&phy->pub, fn_priv);
}

void
nl_80211_vif_each(struct nl_80211 *nl_80211,
                  const uint32_t *wiphy,
                  nl_80211_vif_each_fn_t *fn,
                  void *fn_priv)
{
    struct nl_80211_map *map = &nl_80211->map;
    struct nl_80211_vif_priv *vif;
    ds_tree_foreach(&map->vif_by_ifindex, vif)
        if (wiphy == NULL || *wiphy == vif->pub.wiphy)
            fn(&vif->pub, fn_priv);
}

void
nl_80211_sta_each(struct nl_80211 *nl_80211,
                  const uint32_t ifindex,
                  nl_80211_sta_each_fn_t *fn,
                  void *fn_priv)
{
    struct nl_80211_map *map = &nl_80211->map;
    struct nl_80211_sta_priv *sta;
    ds_tree_foreach(&map->sta_by_link, sta)
        if (ifindex == sta->pub.ifindex)
            fn(&sta->pub, fn_priv);
}

struct nl_msg *
nl_80211_alloc_get_phy(struct nl_80211 *nl_80211,
                       uint32_t wiphy)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, NLM_F_DUMP, NL80211_CMD_GET_WIPHY);
    nl_80211_put_wiphy(nl_80211, msg);
    assert(nla_put_u32(msg, NL80211_ATTR_WIPHY, wiphy) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_get_interface(struct nl_80211 *nl_80211,
                             uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, 0, NL80211_CMD_GET_INTERFACE);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_get_sta(struct nl_80211 *nl_80211,
                       uint32_t ifindex,
                       const void *mac)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, 0, NL80211_CMD_GET_STATION);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    assert(nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_dump_scan(struct nl_80211 *nl_80211,
                         uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, NLM_F_DUMP, NL80211_CMD_GET_SCAN);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_trigger_scan(struct nl_80211 *nl_80211,
                            uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, 0, NL80211_CMD_TRIGGER_SCAN);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_roc(struct nl_80211 *nl_80211,
                   uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, 0, NL80211_CMD_REMAIN_ON_CHANNEL);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    return msg;
}

struct nl_msg *
nl_80211_alloc_disconnect(struct nl_80211 *nl_80211,
                          uint32_t ifindex)
{
    struct nl_msg *msg = nlmsg_alloc();
    nl_80211_put_cmd(nl_80211, msg, 0, NL80211_CMD_DISCONNECT);
    assert(nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) == 0);
    return msg;
}

struct nl_80211_sub *
nl_80211_alloc_sub(struct nl_80211 *nl_80211,
                   const struct nl_80211_sub_ops *ops,
                   void *priv)
{
    struct nl_80211_sub *sub = CALLOC(1, sizeof(*sub));

    sub->map = &nl_80211->map;
    sub->ops = ops;
    sub->priv = priv;
    ds_dlist_insert_tail(&nl_80211->map.subs, sub);
    nl_80211_sub_init(sub);

    return sub;
}

void *
nl_80211_sub_phy_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_phy *info)
{
    struct nl_80211_phy_priv *phy = container_of(info, struct nl_80211_phy_priv, pub);
    return nl_80211_sub_get_priv(sub, &phy->sub_privs, sub->ops->priv_phy_size);
}

void *
nl_80211_sub_vif_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_vif *info)
{
    struct nl_80211_vif_priv *vif = container_of(info, struct nl_80211_vif_priv, pub);
    return nl_80211_sub_get_priv(sub, &vif->sub_privs, sub->ops->priv_vif_size);
}

void *
nl_80211_sub_sta_get_priv(struct nl_80211_sub *sub,
                          const struct nl_80211_sta *info)
{
    struct nl_80211_sta_priv *sta = container_of(info, struct nl_80211_sta_priv, pub);
    return nl_80211_sub_get_priv(sub, &sta->sub_privs, sub->ops->priv_sta_size);
}

void
nl_80211_sub_free(struct nl_80211_sub *sub)
{
    if (sub == NULL) return;
    nl_80211_sub_unlink(sub);
    FREE(sub);
}
