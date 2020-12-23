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

#ifndef NETWORK_METADATA_REPORT_H_INCLUDED
#define NETWORK_METADATA_REPORT_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#include "ds_list.h"
#include "ds_tree.h"
#include "os_types.h"

#include "network_metadata.h"
#include "network_metadata_utils.h"


/**
 * @brief flow key lookup structure
 */
struct net_md_flow_key
{
    os_macaddr_t *smac;
    bool          isparent_of_smac;
    os_macaddr_t *dmac;
    bool          isparent_of_dmac;
    int16_t vlan_id;      /* Host representation of the 12 bits vid */
    uint16_t ethertype;   /* Network byte order */
    uint8_t ip_version;   /* No ip (0), ipv4 (4), ipv6 (6) */
    uint8_t *src_ip;      /* Network byte order */
    uint8_t *dst_ip;      /* Network byte order */
    uint8_t ipprotocol;   /* IP protocol */
    uint16_t ip_id;       /* IP id */
    int fragment;         /* fragment indicator */
    uint16_t sport;       /* Network byte order */
    uint16_t dport;       /* Network byte order */
    bool fstart;          /* Flow start */
    bool fend;            /* Flow end */
};


/**
 * @brief Accumulates stats of a flow for the current observation window
 */
struct net_md_stats_accumulator
{
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    struct flow_counters first_counters;   /* first reported counters */
    struct flow_counters counters;         /* current accumulated stats */
    struct flow_counters report_counters;  /* reported stats */
    int state;                             /* State in the current window */
    time_t last_updated;
    void (*free_plugins)(struct net_md_stats_accumulator *);
    ds_tree_t *dpi_plugins;
    int dpi_done;                          /* All dpi engines are done */
    int refcnt;                            /* # of entities accessing the acc */
    bool report;                           /* send a report */
};



/**
 * @brief Report type: absolute counters or relative to their revious values
 */
enum {
    NET_MD_REPORT_ABSOLUTE = 0,
    NET_MD_REPORT_RELATIVE = 1,
};


/**
 * @brief stats aggregator
 *
 * The report_filter callback is executed when checking if an flow accumulator
 * should be added to the current observation window of the report.
 */
struct net_md_aggregator
{
    ds_tree_t eth_pairs;          /* tracked flows projected at the eth level */
    ds_tree_t five_tuple_flows;   /* 5 tuple only flows */
    bool report_all_samples;      /* Do not aggregate ethernet samples */
    struct flow_report *report;   /* report to serialize */
    size_t max_windows;           /* maximum number of windows */
    size_t windows_cur_idx;       /* current observation window index */
    size_t stats_cur_idx;         /* current stats index in the current window */
    size_t active_accs;           /* active flows in the current window */
    int acc_ttl;                  /* flow accumulator time to live */
    int report_type;              /* absolute or relative to previous values */
    size_t total_report_flows;    /* total flows to be reported */
    size_t total_flows;           /* # of flows tracked by the aggregator */
    size_t held_flows;            /* # of inactive flows with a ref count > 0 */
    size_t max_reports;           /* Max # of flows to report per window */
    bool (*report_filter)(struct net_md_stats_accumulator *);
    bool (*collect_filter)(struct net_md_aggregator *, struct net_md_flow_key *);
    bool (*send_report)(struct net_md_aggregator *, char *);
    bool (*neigh_lookup)(struct sockaddr_storage *, os_macaddr_t *);
    bool (*process)(struct net_md_stats_accumulator *);
    void (*on_acc_create)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_destroy)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
};


/**
 * @brief aggregator init structure
 *
 * The net_md_aggregator_set structure contains all the information needed to
 * initialize an aggregator
 */
struct net_md_aggregator_set
{
    struct node_info *info; /* pointer to the node info */
    size_t num_windows;     /* the max # of windows the report will contain */
    int acc_ttl;            /* how long an incative accumulator is kept around */
    int report_type;        /* absolute or relative */

    /* a collector filter routine */
    bool (*collect_filter)(struct net_md_aggregator *aggr,
                           struct net_md_flow_key *);

    /* a report filter routine */
    bool (*report_filter)(struct net_md_stats_accumulator *);

    /* a report emitter routine */
    bool (*send_report)(struct net_md_aggregator *, char *);

    /* a report IP to mac mapping routine */
    bool (*neigh_lookup)(struct sockaddr_storage *, os_macaddr_t *);

    /* callback on accumulator creation */
    void (*on_acc_create)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_destroy)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
};


/**
 * @brief allocates a stats aggregator
 *
 * @param aggr_set a pointer to the structure containing all init params
 * @return a pointer to an aggregator if the allocation succeeded,
 *         NULL otherwise.
 *         The caller is responsible to free the returned pointer.
 */
struct net_md_aggregator *
net_md_allocate_aggregator(struct net_md_aggregator_set *aggr_set);


/**
 * @brief frees a stats aggregator
 *
 * @param aggr the aggregator to free
 */
void net_md_free_aggregator(struct net_md_aggregator *aggr);


/**
 * @brief Activates the aggregator's current observation window
 *
 * @param aggr the aggregator
 * @return true if the current window was activated, false otherwise
 */
bool net_md_activate_window(struct net_md_aggregator *aggr);


/**
 * @brief Add sampled stats to the aggregator
 *
 * Called to add system level sampled data from a flow
 * within the current observation window
 *
 * @param aggr the aggregator
 * @param key the lookup flow key
 * @param counters the stat counters to aggregate
 * @return true if successful, false otherwise
 */
bool net_md_add_sample(struct net_md_aggregator *aggr,
                       struct net_md_flow_key *key,
                       struct flow_counters *counters);

/**
 * @brief generates report content for the current window
 *
 * Walks through aggregated stats and generates the current observation window
 * report content. Prepares next window.
 * TBD: execute filtering
 *
 * @param aggr the aggregator
 * @return true if successful, false otherwise
 */
bool net_md_close_active_window(struct net_md_aggregator *aggregator);

/**
 * @brief send report from aggregator
 *
 * @param aggr the aggregator
 * @return true if the report was successfully sent, false otherwise
 */
bool net_md_send_report(struct net_md_aggregator *aggr, char *mqtt_topic);

/**
 * @brief get total flows to be reported from aggregator
 *
 * @param aggr the aggregator
 * @return total number of flows to be reported
 */
size_t net_md_get_total_flows(struct net_md_aggregator *aggr);

/**
 * @brief logs the content of an accumulator
 *
 * @param acc the accumulator to log
 */
void
net_md_log_acc(struct net_md_stats_accumulator *acc);

/**
 * @brief logs the content of an aggregator
 *
 * Walks the aggregator and logs its accumulators
 * @param aggr the accumulator to log
 */
void
net_md_log_aggr(struct net_md_aggregator *aggr);

/**
 * @brief process an accumulator
 *
 * Process an accumulator
 * @param aggr the accumulator to process
 * @param acc the accumulator to process
 */
void
net_md_process_acc(struct net_md_aggregator *aggr,
                   struct net_md_stats_accumulator *acc);

/**
 * @brief logs the content of an accumulator
 *
 * Walks an aggregator and processes its accumulators
 * @param acc the accumulator to process
 */
void
net_md_process_aggr(struct net_md_aggregator *aggr);

#endif /* NETWORK_METADATA_REPORT_H_INCLUDED */
