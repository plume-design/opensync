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

#ifndef CT_STATS_REMARK_H_INCLUDED
#define CT_STATS_REMARK_H_INCLUDED

#include <libmnl/libmnl.h>

#include "fcm.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "network_metadata_report.h"
#include "nf_utils.h"
#include "fsm_policy.h"

struct ct_app2accs
{
    char *app_name;
    size_t n_accs;
    ds_tree_t accs;
    ds_tree_node_t a2a_node;
};

struct ct_device2apps
{
    char *mac;
    size_t n_apps;
    ds_tree_t apps;
    ds_tree_node_t d2a_node;
};

void ct_stats_update_flow(struct net_md_stats_accumulator *acc, int action);

bool ct_stats_get_dev2apps(struct net_md_stats_accumulator *acc);

void ct_stats_on_destroy_acc(struct net_md_aggregator *aggr, struct net_md_stats_accumulator *acc);

int ct_stats_process_flush_cache(struct fsm_policy *policy);

bool ct_stats_get_dev2apps(struct net_md_stats_accumulator *acc);

void ct_stats_on_destroy_acc(struct net_md_aggregator *aggr, struct net_md_stats_accumulator *acc);

int ct_stats_process_flush_cache(struct fsm_policy *policy);

#endif /* CT_STATS_REMARK_H_INCLUDED */
