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


struct nfq_stats_counters
{
    uint32_t qnum;
    uint32_t portid;
    uint32_t qtotal;
    uint32_t copy_mode;
    uint32_t copy_range;
    uint32_t qdropped;
    uint32_t q_user_dropped;
    uint32_t seqid;
    ds_tree_node_t  nfq_node;
};

struct pcap_stats_counters
{
    char  ifname[32];
    uint32_t pkts_received;
    uint32_t pkts_dropped;
    ds_tree_node_t  pcap_node;
};


/**
 * @brief DPI stats report
 */
struct dpi_stats_report
{
    char *node_id;
    char *location_id;
    char *plugin;
    int dpi_stats_count;
    bool initialized;
    struct dpi_engine_counters counters;
    ds_tree_t  nfq_stats_tree;          /* nfqueue_node */
    ds_tree_t  pcap_stats_tree;         /* pcap_node */

    /** Helper for UT (initialized to @see qm_conn_send_direct() */
    bool (*send_report)(qm_compress_t compress, char *topic,
                        void *data, int data_size, qm_response_t *res);
};


/**
 * @brief Generates a dpi stats serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see dpi_stats_free_packed_buffer() for this purpose.
 *
 * @param report the info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_report(struct dpi_stats_report *report);


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
 * @brief sends the pcap report using the specified mqtt topic
 * @param session fsm_session containing the node details
 * @param topic mqtt topic used for sending the report
 * @return void
 */
void dpi_stats_report_pcap_stats(struct fsm_session *session, char *topic);

/**
 * @brief sends the nfqueue report using the specified mqtt topic
 * @param session fsm_session containing the node details
 * @param topic mqtt topic used for sending the report
 * @return void
 */
void dpi_stats_report_nfq_stats(struct fsm_session *session, char *topic);

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
 * @brief Frees the stats stored in nfq and pcap tree
 *
 * @param none
 * @return none
 */
void dpi_stats_clear_stats(void);

/**
 * @brief return pointer to global report structure
 *
 * @param void the info used to fill up the protobuf.
 * @return dpi_stats_report a pointer to the global report structure.
 */
struct dpi_stats_report* dpi_stats_get_global_report(void);

#endif /* DPI_STATS_H_INCLUDED */
