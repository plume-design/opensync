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

#ifndef CT_STATS_H_INCLUDED
#define CT_STATS_H_INCLUDED

#include <libmnl/libmnl.h>

#include "fcm.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "network_metadata_report.h"
#include "nf_utils.h"

#define MAX_CT_STATS        (256)
#define MAX_IPV4_IPV6_LEN    (46)


typedef struct flow_stats_
{
    fcm_collect_plugin_t *collector;
    char node_info_id[32];
    char node_info_location_id[32];
    char mqtt_topic[256];
    char *collect_filter;
    struct net_md_aggregator *aggr;
    uint16_t window_active_flag;
    uint16_t report_send_flag;

    uint16_t node_count;
    uint32_t report_type;
    uint32_t acc_ttl;
    uint16_t ct_zone; // CT_ZONE at connection level
    ds_dlist_t ctflow_list;
    size_t index;
    bool active;
    ds_tree_node_t ct_stats_node;
    bool initialized;
    char *name;
    struct fcm_session *session;
    struct fcm_filter_client *c_client;
    struct fcm_filter_client *r_client;
} flow_stats_t;


typedef struct flow_stats_mgr_
{
    bool initialized;
    struct ev_loop *loop;
    ds_tree_t ct_stats_sessions;
    int num_sessions;
    int max_sessions;
    flow_stats_t *active;
    bool debug;
} flow_stats_mgr_t;


void
ct_stats_init_mgr(struct ev_loop *loop);

flow_stats_mgr_t *
ct_stats_get_mgr(void);

flow_stats_t *
ct_stats_get_active_instance(void);


int
ct_stats_get_ct_flow(int af_family);

int
ct_get_stats(void);

void
ct_stats_collect_cb(fcm_collect_plugin_t *collector);

void
ct_flow_add_sample(flow_stats_t *ct_stats);

int
ct_stats_activate_window(fcm_collect_plugin_t *collector);

void
ct_stats_close_window(fcm_collect_plugin_t *collector);

void
ct_stats_send_aggr_report(fcm_collect_plugin_t *collector);

void
ct_stats_report_cb(fcm_collect_plugin_t *collector);

int
ct_stats_plugin_init(fcm_collect_plugin_t *collector);

void
ct_stats_plugin_exit(fcm_collect_plugin_t *collector);

void
ct_stats_exit_mgr(void);


void
ct_free_ct_entries(ds_dlist_t *nf_ct_list);

/**
 * @brief collector filter callback processing flows pushed from fsm
 *
 * This routine applies the collector filter on flows provided by FSM.
 *
 * @param aggr the aggregator processed in the network_metadata library
 * @key the flow key pushed by fsm
 */
bool
ct_stats_collect_filter_cb(struct net_md_aggregator *aggr,
                           struct net_md_flow_key *key, char *app_name);

#endif /* CT_STATS_H_INCLUDED */
