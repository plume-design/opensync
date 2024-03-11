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

#define __GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "log.h"
#include "dpi_stats.pb-c.h"
#include "dpi_stats.h"
#include "memutil.h"
#include "qm_conn.h"


struct dpi_stats_report g_dpi_stats_report;

/*
 * ===========================================================================
 *  Private implementation
 * ===========================================================================
 */
static bool
dpi_stats_set_node(struct fsm_session *session, struct dpi_stats_report *report)
{
    if (session == NULL) return false;
    if (session->node_id == NULL) return false;
    if (session->location_id == NULL) return false;
    if (session->name == NULL) return false;

    report->node_id = session->node_id;
    report->location_id = session->location_id;
    report->plugin = session->name;

    return true;
}

static void
dpi_stats_clear_nfq_stats(ds_tree_t *tree)
{
    struct nfq_stats_counters *entry, *remove;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

static void
dpi_stats_clear_pcap_stats(ds_tree_t *tree)
{
    struct pcap_stats_counters *entry, *remove;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

void
dpi_stats_log_pcap_stats(void)
{
    struct pcap_stats_counters *stats;

    ds_tree_foreach(&g_dpi_stats_report.pcap_stats_tree, stats)
    {
        LOGT("%s(): ifname: %s, received %d", __func__, stats->ifname, stats->pkts_received);
    }
}

void
dpi_stats_log_nfq_stats(void)
{
    struct nfq_stats_counters *stats;

    ds_tree_foreach(&g_dpi_stats_report.nfq_stats_tree, stats)
    {
        LOGT("%s(): qnum: %d, queue total %d", __func__, stats->qnum, stats->qtotal);
    }
}

static void
dpi_stats_free_pcap_stat(Interfaces__DpiStats__PcapStatsCounters *pb)
{
    if (pb == NULL) return;

    FREE(pb->ifname);
    FREE(pb);
}

static void
dpi_stats_free_nfq_stat(Interfaces__DpiStats__NfqueueStatsCounters *pb)
{
    if (pb == NULL) return;
    FREE(pb->queuenum);
    FREE(pb);
}


/**
 * @brief Free a dpi stats report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void
dpi_stats_free_pb_report(Interfaces__DpiStats__DpiStatsReport *pb)
{
    size_t i;
    if (pb == NULL) return;

    FREE(pb->observation_point);
    FREE(pb->counters);

    /* Free nfqueue stats resources */
    for (i = 0; i < pb->n_nfqueue_stats; i++)
    {
        dpi_stats_free_nfq_stat(pb->nfqueue_stats[i]);
    }

    /* Free pcap stats resources */
    for (i = 0; i < pb->n_pcap_stats; i++)
    {
        dpi_stats_free_pcap_stat(pb->pcap_stats[i]);
    }

    FREE(pb->nfqueue_stats);
    FREE(pb->pcap_stats);
    FREE(pb);
}


static Interfaces__DpiStats__DpiStatsObservationPoint *
dpi_stats_set_observation_point(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__DpiStatsObservationPoint *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    interfaces__dpi_stats__dpi_stats_observation_point__init(pb);

    pb->location_id = report->location_id;
    pb->node_id = report->node_id;

    return pb;
}


static Interfaces__DpiStats__DpiStatsCounters *
dpi_stats_set_counters(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__DpiStatsCounters *pb;
    struct dpi_engine_counters *counters;

    if (g_dpi_stats_report.dpi_stats_count == 0) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    interfaces__dpi_stats__dpi_stats_counters__init(pb);

    counters = &report->counters;
    pb->curr_alloc = counters->curr_alloc;
    pb->peak_alloc = counters->peak_alloc;
    pb->fail_alloc = counters->fail_alloc;
    pb->mpmc_events = counters->mpmc_events;
    pb->scan_started = counters->scan_started;
    pb->scan_stopped = counters->scan_stopped;
    pb->scan_bytes = counters->scan_bytes;
    pb->err_incomplete = counters->err_incomplete;
    pb->err_length = counters->err_length;
    pb->err_create = counters->err_create;
    pb->err_scan = counters->err_scan;
    pb->connections = counters->connections;
    pb->streams = counters->streams;

    return pb;
}

static Interfaces__DpiStats__NfqueueStatsCounters *
dpi_stats_set_pb_nfq_stats(struct nfq_stats_counters *stats)
{
    Interfaces__DpiStats__NfqueueStatsCounters *pb = NULL;
    char snum[32];

    pb = CALLOC(1, sizeof(Interfaces__DpiStats__NfqueueStatsCounters));
    if (pb == NULL) return NULL;

    interfaces__dpi_stats__nfqueue_stats_counters__init(pb);

    snprintf(snum, sizeof(snum), "%d", stats->qnum);
    pb->queuenum = STRDUP(snum);
    pb->portid = stats->portid;
    pb->queuetotal = stats->qtotal;
    pb->copyrange = stats->copy_range;
    pb->queuedropped = stats->qdropped;
    pb->queueuserdropped = stats->q_user_dropped;
    pb->seqid = stats->seqid;
    pb->copymode = stats->copy_mode;

    return pb;
}

static Interfaces__DpiStats__NfqueueStatsCounters **
dpi_stats_set_nfq_stats(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__NfqueueStatsCounters **nfqstats_pb_tbl;
    struct nfq_stats_counters *nfq_stats;
    ds_tree_t *nfq_tree;
    size_t allocated;
    size_t num_stats;
    size_t i;

    if (report == NULL) return NULL;
    nfq_tree = &g_dpi_stats_report.nfq_stats_tree;

    num_stats = dpi_stats_get_nfq_stats_count();
    if (num_stats == 0) return NULL;

    nfqstats_pb_tbl = CALLOC(num_stats, sizeof(Interfaces__DpiStats__NfqueueStatsCounters*));

    allocated = 0;
    ds_tree_foreach(nfq_tree, nfq_stats)
    {
        nfqstats_pb_tbl[allocated] = dpi_stats_set_pb_nfq_stats(nfq_stats);
        if (!nfqstats_pb_tbl[allocated]) goto err_free_pb_nfqueue_stats;

        allocated++;
    }

    return nfqstats_pb_tbl;

err_free_pb_nfqueue_stats:
    for (i = 0; i < allocated; i++)
    {
        dpi_stats_free_nfq_stat(nfqstats_pb_tbl[i]);
    }

    FREE(nfqstats_pb_tbl);
    return NULL;
}

static Interfaces__DpiStats__PcapStatsCounters *
dpi_stats_set_pb_pcap_stats(struct pcap_stats_counters *stats)
{
    Interfaces__DpiStats__PcapStatsCounters *pb = NULL;

    pb = CALLOC(1, sizeof(Interfaces__DpiStats__PcapStatsCounters));
    if (pb == NULL) return NULL;

    interfaces__dpi_stats__pcap_stats_counters__init(pb);
    pb->pkts_received = stats->pkts_received;
    pb->pkts_dropped = stats->pkts_dropped;
    pb->ifname = STRDUP(stats->ifname);
    return pb;
}

static Interfaces__DpiStats__PcapStatsCounters **
dpi_stats_set_pcap_stats(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__PcapStatsCounters **pcapstats_pb_tbl;
    struct pcap_stats_counters *pcap_stats;
    ds_tree_t *pcap_tree;
    size_t allocated = 0;
    size_t num_stats;
    size_t i;

    if (report == NULL) return NULL;

    pcap_tree = &g_dpi_stats_report.pcap_stats_tree;
    num_stats = dpi_stats_get_pcap_stats_count();
    if (num_stats == 0) return NULL;

    pcapstats_pb_tbl = CALLOC(num_stats, sizeof(Interfaces__DpiStats__PcapStatsCounters*));

    ds_tree_foreach(pcap_tree, pcap_stats)
    {
        pcapstats_pb_tbl[allocated] = dpi_stats_set_pb_pcap_stats(pcap_stats);
        if (pcapstats_pb_tbl[allocated] == NULL) goto err_free_pb_pcap_stats;

        allocated++;
    }

    return pcapstats_pb_tbl;

err_free_pb_pcap_stats:
    for (i = 0; i < allocated; i++)
    {
        dpi_stats_free_pcap_stat(pcapstats_pb_tbl[i]);
    }

    FREE(pcapstats_pb_tbl);
    return NULL;
}

static Interfaces__DpiStats__DpiStatsReport *
dpi_stats_set_pb_report(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__DpiStatsReport *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the protobuf structure */
    interfaces__dpi_stats__dpi_stats_report__init(pb);

    /* Add observation point */
    pb->observation_point = dpi_stats_set_observation_point(report);

    /* Set the plugin name */
    pb->plugin = report->plugin;

    /* Set the dpi counters */
    pb->counters = dpi_stats_set_counters(report);

    /* set the nfqueue counters */
    pb->n_nfqueue_stats = dpi_stats_get_nfq_stats_count();
    pb->nfqueue_stats = dpi_stats_set_nfq_stats(report);
    if (pb->nfqueue_stats == NULL) pb->n_nfqueue_stats = 0;

    /* set the pcap stats */
    pb->n_pcap_stats = dpi_stats_get_pcap_stats_count();
    pb->pcap_stats = dpi_stats_set_pcap_stats(report);
    if (pb->pcap_stats == NULL) pb->n_pcap_stats = 0;

    return pb;
}

void dpi_stats_init_record(void)
{
    if (g_dpi_stats_report.initialized) return;

    MEMZERO(g_dpi_stats_report);

    /* initialize nfq and pcap tress */
    ds_tree_init(&g_dpi_stats_report.nfq_stats_tree, ds_int_cmp, struct nfq_stats_counters, nfq_node);
    ds_tree_init(&g_dpi_stats_report.pcap_stats_tree, (ds_key_cmp_t *)strcmp, struct pcap_stats_counters, pcap_node);

    /* overwritten by UT's */
    g_dpi_stats_report.send_report = qm_conn_send_direct;
    g_dpi_stats_report.initialized = true;
}

/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */

/**
 * @brief return pointer to global report structure
 *
 * @param void the info used to fill up the protobuf.
 * @return dpi_stats_report a pointer to the global report structure.
 */
struct dpi_stats_report *
dpi_stats_get_global_report(void)
{
    return &g_dpi_stats_report;
}

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
dpi_stats_serialize_report(struct dpi_stats_report *report)
{
    struct dpi_stats_packed_buffer *serialized;
    Interfaces__DpiStats__DpiStatsReport *pb;
    size_t len;
    void *buf;

    if (report == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    pb = dpi_stats_set_pb_report(report);
    if (pb == NULL) goto error;

    /* Get serialization length */
    len = interfaces__dpi_stats__dpi_stats_report__get_packed_size(pb);
    if (len == 0) goto error;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto error;

    serialized->len = interfaces__dpi_stats__dpi_stats_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    dpi_stats_free_pb_report(pb);

    return serialized;

error:
    dpi_stats_free_packed_buffer(serialized);
    dpi_stats_free_pb_report(pb);

    return NULL;
}


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
dpi_stats_free_packed_buffer(struct dpi_stats_packed_buffer *pb)
{
    if (pb == NULL) return;

    FREE(pb->buf);
    FREE(pb);
}

/**
 * @brief Frees the stats stored in nfq and pcap tree
 *
 * @param none
 * @return none
 */
void dpi_stats_clear_stats(void)
{
    ds_tree_t *nfq_tree;
    ds_tree_t *pcap_tree;

    nfq_tree = &g_dpi_stats_report.nfq_stats_tree;
    pcap_tree = &g_dpi_stats_report.pcap_stats_tree;

    /* clear nfq tree */
    dpi_stats_clear_nfq_stats(nfq_tree);

    /* clear pcap tree */
    dpi_stats_clear_pcap_stats(pcap_tree);
}

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
dpi_stats_serialize_stats_report(struct dpi_stats_report *report)
{
    struct dpi_stats_packed_buffer *serialized;
    Interfaces__DpiStats__DpiStatsReport *pb;
    size_t len;
    void *buf;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    pb = dpi_stats_set_pb_report(report);
    if (pb == NULL) goto error;

    /* Get serialization length */
    len = interfaces__dpi_stats__dpi_stats_report__get_packed_size(pb);
    if (len == 0) goto error;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto error;

    serialized->len = interfaces__dpi_stats__dpi_stats_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    dpi_stats_free_pb_report(pb);

    return serialized;

error:
    dpi_stats_free_packed_buffer(serialized);
    dpi_stats_free_pb_report(pb);

    return NULL;
}

/**
 * @brief gets the number of nodes (pcap stats) present
 * in the pcap tree
 * @param void
 * @return int the number of nodes present
 */
int
dpi_stats_get_pcap_stats_count(void)
{
    struct pcap_stats_counters *stats;
    int count = 0;

    ds_tree_foreach(&g_dpi_stats_report.pcap_stats_tree, stats)
    {
        count++;
    }
    return count;
}

/**
 * @brief gets the number of nfq (nfqueue stats) present
 * in the nfqueue tree
 * @param void
 * @return int the number of nodes present
 */
int
dpi_stats_get_nfq_stats_count(void)
{
    struct nfq_stats_counters *stats;
    int count = 0;

    ds_tree_foreach(&g_dpi_stats_report.nfq_stats_tree, stats)
    {
        count++;
    }
    return count;
}

/**
 * @brief stores the node (pcap stats info) in the pcap stats tree
 * @param data containing the pcap stats
 * @param ifname of the pcap stats used as the key
 * @return void
 */
void
dpi_stats_store_pcap_stats(struct pcap_stat *data, char *ifname)
{
    struct pcap_stats_counters *new_stats;

    /* initialize the tree if not already initialized */
    if (!g_dpi_stats_report.initialized) dpi_stats_init_record();

    new_stats = CALLOC(1, sizeof(*new_stats));
    STRSCPY(new_stats->ifname, ifname);
    new_stats->pkts_received = data->ps_recv;
    new_stats->pkts_dropped = data->ps_drop;

    ds_tree_insert(&g_dpi_stats_report.pcap_stats_tree, new_stats, new_stats->ifname);
}

/**
 * @brief stores the node (nfqueue stats info) in the nfqueue stats tree
 * @param data containing the nfqueue stats
 * @param ifname of the nfqueue stats used as the key
 * @return void
 */
void
dpi_stats_store_nfq_stats(struct nfqnl_counters *nfq_stats)
{
    struct nfq_stats_counters *new_stats;

    /* initialize the tree if not already initialized */
    if (!g_dpi_stats_report.initialized) dpi_stats_init_record();

    new_stats = CALLOC(1, sizeof(*new_stats));
    new_stats->qnum = nfq_stats->queue_num;
    new_stats->qtotal = nfq_stats->queue_total;
    new_stats->qdropped = nfq_stats->queue_dropped;
    new_stats->q_user_dropped = nfq_stats->queue_user_dropped;
    new_stats->seqid = nfq_stats->id_sequence;

    ds_tree_insert(&g_dpi_stats_report.nfq_stats_tree, new_stats, &new_stats->qnum);
}

/**
 * @brief sends the pcap report using the specified mqtt topic
 * @param session fsm_session containing the node details
 * @param topic mqtt topic used for sending the report
 * @return void
 */
void
dpi_stats_report_pcap_stats(struct fsm_session *session, char *topic)
{
    struct dpi_stats_packed_buffer *pb;
    struct dpi_stats_report report;
    qm_response_t res;
    bool success;
    int count;

    /* check if there are stats to report */
    count = dpi_stats_get_pcap_stats_count();
    if (count == 0) return;

    /* return if topic is not configured */
    if (topic == NULL) goto cleanup;

    MEMZERO(report);
    /* set node info (pcap stats) in the report */
    success = dpi_stats_set_node(session, &report);
    if (!success) goto cleanup;

    pb = dpi_stats_serialize_stats_report(&report);
    if (pb == NULL)
    {
        LOGN("%s(): failed to serialize pcap stats data", __func__);
        goto cleanup;
    }

    if (g_dpi_stats_report.send_report)
    {
        success = g_dpi_stats_report.send_report(QM_REQ_COMPRESS_IF_CFG, topic, pb->buf, pb->len, &res);
        if (!success) LOGN("error sending mqtt with topic '%s'", topic);
    }
    dpi_stats_free_packed_buffer(pb);

cleanup:
    /* clear the pcap stats tree */
    dpi_stats_clear_stats();
}

/**
 * @brief sends the nfqueue report using the specified mqtt topic
 * @param session fsm_session containing the node details
 * @param topic mqtt topic used for sending the report
 * @return void
 */
void
dpi_stats_report_nfq_stats(struct fsm_session *session, char *topic)
{
    struct dpi_stats_packed_buffer *pb;
    struct dpi_stats_report report;
    qm_response_t res;
    bool success;
    int count;

    /* check if there are stats to report */
    count = dpi_stats_get_nfq_stats_count();
    if (count == 0) return;

    /* return if topic is not configured */
    if (topic == NULL) goto cleanup;

    MEMZERO(report);
    /* set node info (nfq stats) in the report */
    success = dpi_stats_set_node(session, &report);
    if (!success) goto cleanup;

    pb = dpi_stats_serialize_stats_report(&report);
    if (pb == NULL)
    {
        LOGN("%s(): failed to serialize nfqueue stats data", __func__);
        goto cleanup;
    }

    if (g_dpi_stats_report.send_report)
    {
        success = g_dpi_stats_report.send_report(QM_REQ_COMPRESS_IF_CFG, topic, pb->buf, pb->len, &res);
        if (!success) LOGN("error sending mqtt with topic '%s'", topic);
    }

    dpi_stats_free_packed_buffer(pb);

cleanup:
    /* clear the dpi stats tree */
    dpi_stats_clear_stats();
}

