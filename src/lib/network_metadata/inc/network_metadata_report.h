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
#include "nfe.h"

/**
 * @brief flow key lookup structure
 */
struct net_md_flow_key
{
    os_ufid_t *ufid;     /* Unique id of the flow */
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
    uint16_t tcp_flags;   /* Network byte order */
    bool fstart;          /* Flow start */
    bool fend;            /* Flow end */
    uint32_t flags;       /* key flags */
    uint16_t direction;   /* flow direction */
    uint16_t originator;  /* flow originator */
    uint8_t icmp_type;
    uint32_t icmp_idt;
    uint32_t flowmarker;  /* ct_mark */
    uint16_t ct_zone;    /* CT_ZONE at connection level */
    uint16_t rx_idx;
    uint16_t tx_idx;
};

/**
 * net_md_flow_key anc acc flags values influencing the accumulator lookup
 */
enum
{
    NET_MD_ACC_CREATE = 0,
    NET_MD_ACC_LOOKUP_ONLY = 1 << 0,
    NET_MD_ACC_ETH = 1 << 1,
    NET_MD_ACC_FIVE_TUPLE = 1 << 2,
    NET_MD_ACC_FIRST_LEG = 1 << 3,
    NET_MD_ACC_SECOND_LEG = 1 << 4,
};


/**
 * @brief Accumulates stats of a flow for the current observation window
 */
struct net_md_stats_accumulator
{
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    ds_tree_node_t net_md_acc_node;
    struct flow_counters first_counters;   /* first reported counters */
    struct flow_counters counters;         /* current accumulated stats */
    struct flow_counters report_counters;  /* reported stats */
    int state;                             /* State in the current window */
    uint32_t flow_marker;                  /* conntrack mark for the flow */
    time_t last_updated;
    void (*free_plugins)(struct net_md_stats_accumulator *);
    ds_tree_t *dpi_plugins;
    int dpi_done;                          /* All dpi engines are done */
    int mark_done;                         /* last known pushed mark to ct() */
    int refcnt;                            /* # of entities accessing the acc */
    bool report;                           /* send a report */
    uint16_t direction;                    /* flow direction */
    uint16_t originator;                   /* flow originator */
    struct net_md_stats_accumulator *rev_acc;
    uint32_t flags;
    bool dpi_always;
    bool initialized;
    void *dpi;
    struct nfe_packet *packet;
    uint16_t ct_zone; /* CT_ZONE at connection level */

    /* The private nfe conn data */
    unsigned char priv[] __attribute__((aligned(sizeof(ptrdiff_t))));
};


/**
 * @brief local and remote fields of a flow
 */
struct net_md_flow_info
{
    os_macaddr_t *local_mac;
    os_macaddr_t *remote_mac;
    uint8_t *local_ip;
    uint8_t *remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t direction;
    uint8_t ip_version;
    uint8_t ipprotocol;
    uint16_t ethertype;
    int16_t vlan_id;
};


/**
 * net_md_flow_key direction values influencing the accumulator lookup
 */
enum
{
    NET_MD_ACC_UNSET_DIR = 0,
    NET_MD_ACC_OUTBOUND_DIR,
    NET_MD_ACC_INBOUND_DIR,
    NET_MD_ACC_LAN2LAN_DIR,
};


/**
 * net_md_flow_key originator values influencing the accumulator lookup
 */
enum
{
    NET_MD_ACC_UNKNOWN_ORIGINATOR = 0,
    NET_MD_ACC_ORIGINATOR_SRC,
    NET_MD_ACC_ORIGINATOR_DST,
};


/**
 * @brief Report type: absolute counters or relative to their previous values
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
    ds_tree_t *eth_pairs;         /* tracked flows projected at the eth level */
    ds_tree_t *five_tuple_flows;  /* 5 tuple only flows */
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
    size_t total_eth_pairs;       /* # of eth pairs tracked by the aggregator */
    int report_flow_type;
    bool (*report_filter)(struct net_md_stats_accumulator *);
    bool (*collect_filter)(struct net_md_aggregator *, struct net_md_flow_key *, char *);
    bool (*send_report)(struct net_md_aggregator *, char *);
    bool (*neigh_lookup)(int, void *, os_macaddr_t *);
    bool (*process)(struct net_md_stats_accumulator *);
    void (*on_acc_create)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_destroy)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_report)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void *context;
    nfe_conntrack_t nfe_ct;
};

enum net_md_report_stats_type {
    NET_MD_LAN_FLOWS = (1 << 1),
    NET_MD_IP_FLOWS = (1 << 2),
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
    int acc_ttl;            /* how long an inactive accumulator is kept around */
    int report_type;        /* absolute or relative */
    int report_stats_type;

    /* a collector filter routine */
    bool (*collect_filter)(struct net_md_aggregator *aggr,
                           struct net_md_flow_key *, char *);

    /* a report filter routine */
    bool (*report_filter)(struct net_md_stats_accumulator *);

    /* a report emitter routine */
    bool (*send_report)(struct net_md_aggregator *, char *);

    /* a report IP to mac mapping routine */
    bool (*neigh_lookup)(int, void *, os_macaddr_t *);

    /* callback on accumulator creation */
    bool (*process)(struct net_md_stats_accumulator *);
    void (*on_acc_create)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_destroy)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void (*on_acc_report)(struct net_md_aggregator *, struct net_md_stats_accumulator *);
    void *context;
};

enum
{
    PKT_VERDICT_ONLY = 1 << 0,
};

struct dpi_mark_policy
{
    int flow_mark;
    uint32_t mark_policy;
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
 * @brief Add uplink stats to the accumulator
 *
 * @param aggr the aggregator
 * @param uplink the flow uplink
 * @return true if successful, false otherwise
 */
bool net_md_add_uplink(struct net_md_aggregator *aggr,
                       struct flow_uplink *uplink);

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
 * @param caller the calling function
 */
void
net_md_log_acc(struct net_md_stats_accumulator *acc, const char *caller);

/**
 * @brief logs the content of an aggregator
 *
 * Walks the aggregator and logs its accumulators
 * @param aggr the accumulator to log
 */
void
net_md_log_aggr(struct net_md_aggregator *aggr);

/**
 * @brief provides local and remote info
 *
 * @param acc the accumulator
 * @param info the returning info
 *
 * @return true if filled, false otherwise
 */
bool
net_md_get_flow_info(struct net_md_stats_accumulator *acc,
                     struct net_md_flow_info *info);

/**
 * @brief logs the content of a network_metadata key
 *
 * @param key the network_metadata key to log
 * @param caller the calling function
 */
void
net_md_log_key(struct net_md_flow_key *key, const char *caller);

/**
 * @brief logs the content of a flow key
 *
 * @param fkey the flow key to log
 * @param caller the calling function
 */
void
net_md_log_fkey(struct flow_key *fkey, const char *caller);

/**
 * @brief provides local and remote info
 *
 * @param key the network metadata key
 * @param info the returning info
 *
 * @return true if filled, false otherwise
 */
bool
net_md_get_key_info(struct net_md_flow_key *key,
                    struct net_md_flow_info *info);

#endif /* NETWORK_METADATA_REPORT_H_INCLUDED */
