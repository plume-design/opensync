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

#ifndef LAN_STATS_H_INCLUDED
#define LAN_STATS_H_INCLUDED

#include "ds.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "os_types.h"
#include "fcm.h"

#define MAC_ADDR_STR_LEN     (18)
#define LINE_BUFF_LEN        (2048)
#define MAX_TOKENS           (30)

#define MAX_HISTOGRAMS               (1)

#define ETH_DEVICES_TAG "${@eth_devices}"

typedef union ovs_u128 {
    os_ufid_t id;
    struct {
        uint64_t lo, hi;
    } u64;
} ovs_u128_;

typedef enum
{
    IFTYPE_NONE    =  0,
    IFTYPE_ETH     =  1,
    IFTYPE_VIF     =  2,
    IFTYPE_VLAN    =  3,
    IFTYPE_BRIDGE  =  4,
    IFTYPE_GRE     =  5,
    IFTYPE_GRE6    =  6,
    IFTYPE_TAP     =  7,
    IFTYPE_PPPOE   =  8,
    IFTYPE_LTE     =  9
} uplink_iface_type;

struct iface_type
{
    char *iface_type;
    int value;
};

struct lan_stats_instance;

static const struct iface_type iface_type_map[] =
{
    {
        .value = IFTYPE_NONE,
        .iface_type = "",
    },
    {
        .value = IFTYPE_ETH,
        .iface_type = "eth",
    },
    {
        .value = IFTYPE_VIF,
        .iface_type = "vif",
    },
    {
        .value = IFTYPE_VLAN,
        .iface_type = "vlan",
    },
    {
        .value = IFTYPE_BRIDGE,
        .iface_type = "bridge",
    },
    {
        .value = IFTYPE_GRE,
        .iface_type = "gre",
    },
    {
        .value = IFTYPE_GRE6,
        .iface_type = "gre6",
    },
    {
        .value = IFTYPE_TAP,
        .iface_type = "tap",
    },
    {
        .value = IFTYPE_PPPOE,
        .iface_type = "pppoe",
    },
    {
        .value = IFTYPE_LTE,
        .iface_type = "lte",
    },
};

typedef struct lan_stats_instance
{
    fcm_collect_plugin_t *collector;
    bool            initialized;
    char            *name;
    char            *parent_tag;
    struct net_md_aggregator *aggr;
    ds_tree_node_t  lan_stats_node;
    struct fcm_session *session;
    struct fcm_filter_client *c_client;
    struct fcm_filter_client *r_client;
    uint16_t ct_zone;
    uint32_t node_count;
    ds_dlist_t ct_list;
} lan_stats_instance_t;

typedef struct lan_stats_mgr_
{
    bool initialized;
    struct ev_loop *loop;
    ds_tree_t lan_stats_sessions;
    int num_sessions;
    int max_sessions;
    lan_stats_instance_t *active;
    bool debug;
    uplink_iface_type uplink_if_type;
    bool curr_uplinked_changed;
    bool uplink_changed;
    bool report;
    void (*ovsdb_init)(void);
    void (*ovsdb_exit)(void);
} lan_stats_mgr_t;

void
lan_stats_init_mgr(struct ev_loop *loop);

lan_stats_mgr_t *
lan_stats_get_mgr(void);

lan_stats_instance_t *
lan_stats_get_active_instance(void);

int
lan_stats_plugin_init(fcm_collect_plugin_t *collector);

void
lan_stats_plugin_exit(fcm_collect_plugin_t *collector);

void
lan_stats_exit_mgr(void);

void
link_stats_collect_cb(uplink_iface_type uplink_if_type);

bool
lan_stats_is_mac_in_tag(char *tag, os_macaddr_t *mac);

void lan_stats_process_aggr(struct net_md_aggregator *to, struct net_md_aggregator *from);

void
lan_stats_collect_cb(fcm_collect_plugin_t *collector);
#endif /* LAN_STATS_H_INCLUDED */
