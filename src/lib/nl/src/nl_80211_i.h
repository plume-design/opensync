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

#ifndef NL_80211_I_H_INCLUDED
#define NL_80211_I_H_INCLUDED

#include <ds_tree.h>

struct nl_80211_sub_priv {
    struct ds_tree *root;
    struct ds_tree_node node;
    void *data;
};

struct nl_80211_phy_priv {
    struct nl_80211_phy pub;
    struct ds_tree_node node_name;
    struct ds_tree_node node_wiphy;
    struct ds_tree sub_privs;
};

struct nl_80211_vif_priv {
    struct nl_80211_vif pub;
    struct ds_tree_node node_name;
    struct ds_tree_node node_ifindex;
    struct nl_cmd *cmd_dump_station;
    struct ds_tree sub_privs;
};

struct nl_80211_sta_priv {
    struct nl_80211_sta pub;
    struct ds_tree_node node;
    struct ds_tree sub_privs;
};

/* There are multiple scenarios where only some phy/vif/sta
 * attributes are known, and others need to be inferred. For
 * example phy_name is required, but only wiphy is known in
 * a given context. Or vice-versa. This structure is used
 * to keep track of these relations and expose nice
 * nl_80211_ lookup APIs for convenience.
*/
struct nl_80211_map {
    struct ds_tree phy_by_name;
    struct ds_tree phy_by_wiphy;
    struct ds_tree vif_by_name;
    struct ds_tree vif_by_ifindex;
    struct ds_tree sta_by_link;
    struct ds_dlist subs;
    struct nl_cmd *cmd_dump_wiphy;
    struct nl_cmd *cmd_dump_interface;
};

struct nl_80211_sub {
    struct ds_dlist_node node;
    struct nl_80211_map *map;
    const struct nl_80211_sub_ops *ops;
    void *priv;
};

struct nl_80211 {
    struct nl_conn *conn;
    struct nl_conn_subscription *conn_sub;
    struct nl_80211_map map;
    struct nl_cmd *cmd_get_family;
    struct nl_cmd *cmd_get_features;

    nl_80211_ready_fn_t *ready_fn;
    void *ready_fn_priv;

    int family_id;
    uint32_t feature_bitmask;
    bool recovering_from_overrun;
    uint32_t nested_overruns;
};

#endif /* NL_80211_I_H_INCLUDED */
