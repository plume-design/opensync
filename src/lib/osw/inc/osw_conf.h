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

#ifndef OSW_CONF_H_INCLUDED
#define OSW_CONF_H_INCLUDED

#include <osw_types.h>
#include <ds_dlist.h>
#include <ds_tree.h>

struct osw_conf_acl {
    struct ds_tree_node node;
    struct osw_hwaddr mac_addr;
};

struct osw_conf_psk {
    struct ds_tree_node node;
    struct osw_ap_psk ap_psk;
};

struct osw_conf_neigh {
    struct ds_tree_node node;
    struct osw_neigh neigh;
};

struct osw_conf_vif_ap {
    enum osw_acl_policy acl_policy;
    struct osw_ssid ssid;
    struct osw_channel channel;
    struct osw_ifname bridge_if_name;
    struct osw_wpa wpa;
    struct ds_tree acl_tree; /* osw_conf_acl */
    struct ds_tree psk_tree; /* osw_conf_psk */
    struct ds_tree neigh_tree; /* osw_conf_neigh */
    struct osw_ap_mode mode;
    int beacon_interval_tu;
    bool ssid_hidden;
    bool isolated;
    bool mcast2ucast;
};

struct osw_conf_net {
    struct ds_dlist_node node;
    struct osw_ssid ssid;
    struct osw_hwaddr bssid;
    struct osw_psk psk;
    struct osw_wpa wpa;
};

struct osw_conf_vif_sta {
    /* FIXME: for multi-ap, this will need:
       struct osw_ifname bridge_if_name;
       bool multi_ap;
     */
    struct ds_dlist net_list;
};

struct osw_conf_vif {
    struct ds_tree_node phy_node;
    struct osw_conf_phy *phy;
    struct osw_hwaddr mac_addr;
    char *vif_name;
    bool enabled;
    enum osw_vif_type vif_type;
    union {
        struct osw_conf_vif_ap ap;
        struct osw_conf_vif_sta sta;
    } u;
};

struct osw_conf_phy {
    struct ds_tree_node conf_node;
    struct ds_tree *phy_tree;
    char *phy_name;
    bool enabled;
    int tx_chainmask;
    enum osw_radar_detect radar;
    struct osw_reg_domain reg_domain;
    struct ds_tree vif_tree;
};

struct osw_conf_mutator;
struct osw_conf_observer;

typedef void osw_conf_mutate_fn_t(struct osw_conf_mutator *mutator,
                                  struct ds_tree *phy_tree);

typedef void osw_conf_mutated_fn_t(struct osw_conf_observer *observer);

enum osw_conf_type {
    OSW_CONF_TAIL,
    OSW_CONF_HEAD,
};

struct osw_conf_mutator {
    struct ds_dlist_node node;
    const char *name;
    enum osw_conf_type type;
    osw_conf_mutate_fn_t *mutate_fn;
};

struct osw_conf_observer {
    struct ds_dlist_node node;
    const char *name;
    osw_conf_mutated_fn_t *mutated_fn;
};

void
osw_conf_register_mutator(struct osw_conf_mutator *mutator);

void
osw_conf_unregister_mutator(struct osw_conf_mutator *mutator);

void
osw_conf_register_observer(struct osw_conf_observer *observer);

struct ds_tree *
osw_conf_build_from_state(void);

struct ds_tree *
osw_conf_build(void);

void
osw_conf_free(struct ds_tree *phy_tree);

void
osw_conf_invalidate(struct osw_conf_mutator *mutator);

bool
osw_conf_ap_psk_tree_changed(struct ds_tree *a, struct ds_tree *b);

bool
osw_conf_ap_acl_tree_changed(struct ds_tree *a, struct ds_tree *b);

void
osw_conf_ap_psk_tree_to_str(char *out, size_t len, const struct ds_tree *a);

void
osw_conf_neigh_tree_to_str(char *out, size_t len, const struct ds_tree *a);

void
osw_conf_ap_acl_tree_to_str(char *out, size_t len, const struct ds_tree *a);

#endif /* OSW_CONF_H_INCLUDED */
