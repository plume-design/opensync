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

#ifndef DPI_STATS_H_INCLUDED
#define DPI_STATS_H_INCLUDED

#include <stdlib.h>
#include <stdint.h>
#include "ds_tree.h"
#include "qm_conn.h"
#include "fsm.h"

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct dpi_stats_packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};


struct dpi_engine_counters
{
    uint32_t curr_alloc;
    uint32_t peak_alloc;
    uint32_t fail_alloc;
    uint32_t mpmc_events;
    uint32_t scan_started;
    uint32_t scan_stopped;
    uint32_t scan_bytes;
    uint32_t err_incomplete;
    uint32_t err_length;
    uint32_t err_create;
    uint32_t err_scan;
    uint32_t connections;
    uint32_t streams;
};


struct nfq_err_cnts
{
    int error_no;
    uint64_t count;

    ds_dlist_node_t node;
};
struct nfq_stats_counters
{
    int qnum;
    uint32_t qtotal;
    uint32_t qdropped;
    uint32_t q_user_dropped;
    uint32_t seqid;
    ds_dlist_t error_list;
    uint32_t num_errs;
    ds_tree_node_t  nfq_node;
};

struct pcap_stats_counters
{
    char  ifname[32];
    uint32_t pkts_received;
    uint32_t pkts_dropped;
    ds_tree_node_t  pcap_node;
};


struct dpi_stats
{
    bool initialized;
    uint16_t num_nfqs;
    uint16_t num_pcaps;
    uint16_t num_fns;

    ds_tree_t  nfq_stats;          /* nfqueue_node */
    ds_tree_t  pcap_stats;         /* pcap_node */
    ds_tree_t  fn_tracer_stats;    /* fn_trace node */
};
/* stores function trace stats for later reporting */
struct fn_tracer_stats
{
    char *fn_name;
    uint64_t call_count;
    uint64_t max_duration;
    uint64_t total_duration;

    ds_tree_node_t fn_stats_node;
};
/**
 * @brief DPI stats report
 */
struct dpi_stats_report
{
    char *node_id;
    char *location_id;
    char *plugin;
    struct dpi_engine_counters counters;
};


/**
 * @brief gets the number of pcap (pcap stats) present
 * in the nfqueue tree
 * @param void
 * @return int the number of nodes present
 */
int dpi_stats_get_pcap_stats_count(void);


/**
 * @brief stores the node (pcap stats info) in the pcap stats tree
 * @param data containing the pcap stats
 * @param ifname of the pcap stats used as the key
 * @return void
 */
int dpi_stats_get_nfq_stats_count(void);


/**
 * @brief gets the number of call trace stats pre
 * in the call trace tree
 * @param void
 * @return int the number of nodes present
 */
int dpi_stats_get_call_trace_stats_count(void);


/**
 * @brief stores the node (pcap stats info) in the pcap stats tree
 * @param data containing the pcap stats
 * @param ifname of the pcap stats used as the key
 * @return void
 */
void dpi_stats_store_pcap_stats(struct pcap_stat *stats, char *if_name);

/**
 * @brief stores the node (nfqueue stats info) in the nfqueue stats tree
 * @param data containing the nfqueue stats
 * @param ifname of the nfqueue stats used as the key
 * @return void
 */
void dpi_stats_store_nfq_stats(struct nfqnl_counters *nfq_stats);


/**
 * @brief stores the node (nfqueue error info) in the nfqueue stats tree
 * @param data containing the nfqueue error and its count
 * @param ifname of the nfqueue stats used as the key
 * @return void
 */
void
dpi_stats_store_nfq_err_cnt(int queue_num);


/**
 * @brief stores the call trace stats in the tree
 * @param trace_stats containing the call trace stats
 * @return none
 */
void dpi_stats_store_call_trace_stats(struct fn_tracer_stats *trace_stats);

/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb)
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void
dpi_stats_free_packed_buffer(struct dpi_stats_packed_buffer *pb);

/**
 * @brief Generates a dpi stats serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see dpi_stats_free_packed_buffer() for this purpose.
 *
 * @param report the info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_report(struct dpi_stats_report *report);

/**
 * @brief Serializes the data that can be consumed
 * by protobuf
 *
 * @param report structure containing the stats that
 * needs to be serialized
 * @return dpi_stats_packed_buffer pointer containing
 * the serialized data
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_counter_report(struct dpi_stats_report *report);

/**
 * @brief sends the call trace report to the specified mqtt topic
 * @param session fsm_session containing the session information
 * @param topic mqtt topic used for sending the report
 * @return none
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_call_trace_stats(struct dpi_stats_report *report);

/**
 * @brief clean up all the stored records
 * @param none
 * @return none
 */
void dpi_stats_init_record(void);

/**
 * @brief initialize tables to store records
 * @param none
 * @return none
 */
void dpi_stats_cleanup_record(void);

#endif /* DPI_STATS_H_INCLUDED */
