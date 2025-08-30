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
#include "ds_dlist.h"
#include "nf_utils.h"

static struct dpi_stats
g_dpi_stats =
{
    .initialized = false,
};

/*
 * ===========================================================================
 *  Private implementation
 * ===========================================================================
 */
static void
dpi_stats_clear_nfq_counters(ds_tree_t *tree)
{
    struct nfq_stats_counters *entry, *remove;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove);
        g_dpi_stats.num_nfqs--;
    }
}

static void
dpi_stats_clear_pcap_counters(ds_tree_t *tree)
{
    struct pcap_stats_counters *entry, *remove;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove);
        g_dpi_stats.num_pcaps--;
    }
}

static void dpi_stats_clear_trace_stats(ds_tree_t *tree)
{
    struct fn_tracer_stats *entry, *remove;

    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove->fn_name);
        FREE(remove);
        g_dpi_stats.num_fns--;
    }
}

static void
dpi_stats_free_pcap_report(Interfaces__DpiStats__PcapStatsCounters *pb)
{
    if (pb == NULL) return;

    FREE(pb->ifname);
    FREE(pb);
}

static void
dpi_stats_free_nfq_report(Interfaces__DpiStats__NfqueueStatsCounters *pb)
{
    if (pb == NULL) return;

    FREE(pb->queuenum);
    FREE(pb);
}

static void
dpi_stats_free_call_trace_stat(Interfaces__DpiStats__CallTraceCounters *pb)
{
    if (pb == NULL) return;

    FREE(pb->func_name);
    FREE(pb);
}

static void
dpi_stats_free_nfq_err_report(Interfaces__DpiStats__ErrorCounters *pb)
{
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
    if (pb == NULL) return;

    FREE(pb->observation_point);
    FREE(pb->counters);

    FREE(pb);
}

static void
dpi_stats_free_counter_report(Interfaces__DpiStats__DpiStatsReport *pb)
{
    size_t i;
    if (pb == NULL) return;

    FREE(pb->observation_point);

    /* Free nfqueue stats resources */
    for (i = 0; i < pb->n_nfqueue_stats; i++)
    {
        dpi_stats_free_nfq_report(pb->nfqueue_stats[i]);
    }

    /* Free pcap stats resources */
    for (i = 0; i < pb->n_pcap_stats; i++)
    {
        dpi_stats_free_pcap_report(pb->pcap_stats[i]);
    }

    /* Free trace stats resource */
    for (i = 0; i < pb->n_call_stats; i++)
    {
        dpi_stats_free_call_trace_stat(pb->call_stats[i]);
    }

    FREE(pb->nfqueue_stats);
    FREE(pb->pcap_stats);
    FREE(pb->call_stats);
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

static Interfaces__DpiStats__ErrorCounters *
dpi_stats_set_pb_nfq_errs(struct nfq_err_cnts *errs)
{
    Interfaces__DpiStats__ErrorCounters *pb = NULL;

    pb = CALLOC(1, sizeof(Interfaces__DpiStats__ErrorCounters));

    interfaces__dpi_stats__error_counters__init(pb);
    pb->error = errs->error_no;
    pb->count = errs->count;

    return pb;
}

static Interfaces__DpiStats__DpiStatsCounters *
dpi_stats_set_counters(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__DpiStatsCounters *pb;
    struct dpi_engine_counters *counters;


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
dpi_stats_set_pb_nfq_counters(struct nfq_stats_counters *stats)
{
    Interfaces__DpiStats__NfqueueStatsCounters *pb = NULL;
    Interfaces__DpiStats__ErrorCounters **nfqerr_pb_tbl = NULL;
    struct nfq_err_cnts *nfq_err;
    char snum[32];
    size_t err_alloc = 0;
    size_t i = 0;

    pb = CALLOC(1, sizeof(Interfaces__DpiStats__NfqueueStatsCounters));
    if (pb == NULL) return NULL;

    interfaces__dpi_stats__nfqueue_stats_counters__init(pb);

    snprintf(snum, sizeof(snum), "%d", stats->qnum);
    pb->queuenum = STRDUP(snum);
    pb->queuetotal = stats->qtotal;
    pb->queuedropped = stats->qdropped;
    pb->queueuserdropped = stats->q_user_dropped;
    pb->seqid = stats->seqid;

    if (!stats->num_errs) return pb;

    nfqerr_pb_tbl = CALLOC(stats->num_errs, sizeof(Interfaces__DpiStats__ErrorCounters*));
    ds_dlist_foreach(&stats->error_list, nfq_err)
    {
        nfqerr_pb_tbl[err_alloc] = dpi_stats_set_pb_nfq_errs(nfq_err);
        if (!nfqerr_pb_tbl[err_alloc]) goto err_free_pb_nfq_err;
        err_alloc++;
    }
    pb->n_errors = err_alloc;
    pb->errors = nfqerr_pb_tbl;
    return pb;

err_free_pb_nfq_err:
    for (i = 0; i < err_alloc; i++)
    {
        dpi_stats_free_nfq_err_report(nfqerr_pb_tbl[i]);
    }
    FREE(nfqerr_pb_tbl);
    pb->errors = NULL;
    return pb;
}

static Interfaces__DpiStats__NfqueueStatsCounters **
dpi_stats_set_nfq_counters(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__NfqueueStatsCounters **nfqstats_pb_tbl;
    struct nfq_stats_counters *nfq_stats;
    ds_tree_t *nfq_tree;
    size_t allocated;
    size_t num_stats;
    size_t i;

    if (report == NULL) return NULL;
    nfq_tree = &g_dpi_stats.nfq_stats;

    num_stats = g_dpi_stats.num_nfqs;

    if (num_stats == 0) return NULL;

    nfqstats_pb_tbl = CALLOC(num_stats, sizeof(Interfaces__DpiStats__NfqueueStatsCounters*));

    allocated = 0;
    ds_tree_foreach(nfq_tree, nfq_stats)
    {
        nfqstats_pb_tbl[allocated] = dpi_stats_set_pb_nfq_counters(nfq_stats);
        if (!nfqstats_pb_tbl[allocated]) goto err_free_pb_nfqueue_stats;

        allocated++;
    }

    return nfqstats_pb_tbl;

err_free_pb_nfqueue_stats:
    for (i = 0; i < allocated; i++)
    {
        dpi_stats_free_nfq_report(nfqstats_pb_tbl[i]);
    }

    FREE(nfqstats_pb_tbl);
    return NULL;
}

static Interfaces__DpiStats__PcapStatsCounters *
dpi_stats_set_pb_pcap_counters(struct pcap_stats_counters *stats)
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
dpi_stats_set_pcap_counters(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__PcapStatsCounters **pcapstats_pb_tbl;
    struct pcap_stats_counters *pcap_stats;
    ds_tree_t *pcap_tree;
    size_t allocated = 0;
    size_t num_stats;
    size_t i;

    if (report == NULL) return NULL;

    pcap_tree = &g_dpi_stats.pcap_stats;
    num_stats = g_dpi_stats.num_pcaps;
    if (num_stats == 0) return NULL;

    pcapstats_pb_tbl = CALLOC(num_stats, sizeof(Interfaces__DpiStats__PcapStatsCounters*));

    ds_tree_foreach(pcap_tree, pcap_stats)
    {
        pcapstats_pb_tbl[allocated] = dpi_stats_set_pb_pcap_counters(pcap_stats);
        if (pcapstats_pb_tbl[allocated] == NULL) goto err_free_pb_pcap_stats;

        allocated++;
    }

    return pcapstats_pb_tbl;

err_free_pb_pcap_stats:
    for (i = 0; i < allocated; i++)
    {
        dpi_stats_free_pcap_report(pcapstats_pb_tbl[i]);
    }

    FREE(pcapstats_pb_tbl);
    return NULL;
}

static Interfaces__DpiStats__CallTraceCounters *
dpi_stats_set_pb_trace_stats(struct fn_tracer_stats *stats)
{
    Interfaces__DpiStats__CallTraceCounters *pb = NULL;

    pb = CALLOC(1, sizeof(Interfaces__DpiStats__CallTraceCounters));

    interfaces__dpi_stats__call_trace_counters__init(pb);
    pb->max_duration = stats->max_duration;
    pb->total_duration = stats->total_duration;
    pb->call_count = stats->call_count;
    pb->func_name = STRDUP(stats->fn_name);
    return pb;
}

static Interfaces__DpiStats__CallTraceCounters **
dpi_stats_set_trace_stats(struct dpi_stats_report *report)
{
    Interfaces__DpiStats__CallTraceCounters **tracestats_pb_tbl;
    struct fn_tracer_stats *trace_stats;
    ds_tree_t *fn_stats;
    size_t allocated = 0;
    size_t num_stats;
    size_t i;

    if (report == NULL) return NULL;

    fn_stats = &g_dpi_stats.fn_tracer_stats;
    num_stats = g_dpi_stats.num_fns;
    if (num_stats == 0) return NULL;

    tracestats_pb_tbl = CALLOC(num_stats, sizeof(Interfaces__DpiStats__CallTraceCounters*));
    ds_tree_foreach(fn_stats, trace_stats)
    {
        tracestats_pb_tbl[allocated] = dpi_stats_set_pb_trace_stats(trace_stats);
        if (tracestats_pb_tbl[allocated] == NULL) goto err_free_pb_trace_stats;

        allocated++;
    }

    return tracestats_pb_tbl;

err_free_pb_trace_stats:
    for (i = 0; i < allocated; i++)
    {
        dpi_stats_free_call_trace_stat(tracestats_pb_tbl[i]);
    }

    FREE(tracestats_pb_tbl);
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

    return pb;
}

static Interfaces__DpiStats__DpiStatsReport *
dpi_stats_set_counter_report(struct dpi_stats_report *report)
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

    /* set the nfqueue counters */
    pb->n_nfqueue_stats = g_dpi_stats.num_nfqs;
    pb->nfqueue_stats = dpi_stats_set_nfq_counters(report);
    if (pb->nfqueue_stats == NULL) pb->n_nfqueue_stats = 0;

    /* set the pcap stats */
    pb->n_pcap_stats = g_dpi_stats.num_pcaps;
    pb->pcap_stats = dpi_stats_set_pcap_counters(report);
    if (pb->pcap_stats == NULL) pb->n_pcap_stats = 0;

    return pb;
}

static Interfaces__DpiStats__DpiStatsReport *
dpi_stats_set_fn_stats_report(struct dpi_stats_report *report)
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

    /* set the function tracer stats */
    pb->n_call_stats = g_dpi_stats.num_fns;
    pb->call_stats = dpi_stats_set_trace_stats(report);
    if (pb->call_stats == NULL) pb->n_call_stats = 0;

    return pb;
}

static void
dpi_stats_insert_pcap_entry(struct pcap_stat *data, char *ifname, struct pcap_stats_counters *new_stats)
{
    ds_tree_t *pcap_stats;

    pcap_stats = &g_dpi_stats.pcap_stats;

    if (!new_stats) return;

    STRSCPY(new_stats->ifname, ifname);
    new_stats->pkts_received = data->ps_recv;
    new_stats->pkts_dropped = data->ps_drop;

    ds_tree_insert(pcap_stats, new_stats, new_stats->ifname);
    return;
}

static void
dpi_stats_update_pcap_entry(struct pcap_stat *data, char *ifname, struct pcap_stats_counters *stats)
{
    if (data->ps_recv > stats->pkts_received) stats->pkts_received = data->ps_recv - stats->pkts_received;
    else stats->pkts_received = data->ps_recv;

    if (data->ps_drop > stats->pkts_dropped) stats->pkts_dropped = data->ps_drop - stats->pkts_dropped;
    else stats->pkts_dropped = data->ps_drop;

    return;
}

static void
dpi_stats_insert_nfq_entry(struct nfqnl_counters *nfq_cnt, struct nfq_stats_counters *new_stats)
{
    ds_tree_t *nfq_stats;

    nfq_stats = &g_dpi_stats.nfq_stats;

    if (!new_stats) return;

    new_stats->qnum = nfq_cnt->queue_num;
    new_stats->qtotal = nfq_cnt->queue_total;
    new_stats->qdropped = nfq_cnt->queue_dropped;
    new_stats->q_user_dropped = nfq_cnt->queue_user_dropped;
    new_stats->seqid = nfq_cnt->id_sequence;
    ds_dlist_init(&new_stats->error_list, struct nfq_err_cnts, node);

    ds_tree_insert(nfq_stats, new_stats, &new_stats->qnum);
    return;
}

static void
dpi_stats_update_nfq_entry(struct nfqnl_counters *nfq_stats, struct nfq_stats_counters *stats)
{
    stats->qtotal = nfq_stats->queue_total;
    stats->seqid = nfq_stats->id_sequence;
    if (nfq_stats->queue_dropped > stats->qdropped) stats->qdropped = nfq_stats->queue_dropped - stats->qdropped;
    else stats->qdropped = nfq_stats->queue_dropped;

    if (nfq_stats->queue_user_dropped > stats->q_user_dropped) stats->q_user_dropped = nfq_stats->queue_user_dropped - stats->q_user_dropped;
    else stats->q_user_dropped = nfq_stats->queue_user_dropped;

    return;
}

static void
dpi_stats_insert_fn_entry(struct fn_tracer_stats *trace_stats, struct fn_tracer_stats *new_stats)
{
    ds_tree_t *fn_stats;

    fn_stats = &g_dpi_stats.fn_tracer_stats;

    if (!new_stats) return;

    new_stats->fn_name = STRDUP(trace_stats->fn_name);
    new_stats->call_count = trace_stats->call_count;
    new_stats->max_duration = trace_stats->max_duration;
    new_stats->total_duration = trace_stats->total_duration;

    ds_tree_insert(fn_stats, new_stats, new_stats->fn_name);
    return;
}

static void
dpi_stats_update_fn_entry(struct fn_tracer_stats *trace_stats, struct fn_tracer_stats *stats)
{
    if (trace_stats->call_count > stats->call_count) stats->call_count = trace_stats->call_count - stats->call_count;
    else stats->call_count = trace_stats->call_count;

    stats->max_duration = trace_stats->max_duration;

    if (trace_stats->total_duration > stats->total_duration) stats->total_duration = trace_stats->total_duration - stats->total_duration;
    else stats->total_duration = trace_stats->total_duration;

    return;
}

static void
dpi_stats_insert_nfq_err_entry(struct nf_queue_err_counters *report_counter, struct nfq_stats_counters *stats)
{
    struct nfq_err_cnts *nfq_err;
    ds_dlist_t *err_list;

    if (!stats) return;
    err_list = &stats->error_list;

    nfq_err = CALLOC(1, sizeof(struct nfq_err_cnts));
    nfq_err->error_no = report_counter->error;
    nfq_err->count = report_counter->counter;
    stats->num_errs++;
    ds_dlist_insert_head(err_list, nfq_err);
    return;
}

static bool
dpi_stats_update_nfq_err_entry(struct nf_queue_err_counters *report_counter, struct nfq_stats_counters *stats)
{
    struct nfq_err_cnts *nfq_err;
    ds_dlist_t *err_list;

    if (!stats) return false;

    err_list = &stats->error_list;

    ds_dlist_foreach(err_list, nfq_err)
    {
        if (report_counter->error == nfq_err->error_no)
        {
            if (report_counter->counter > nfq_err->count) nfq_err->count = report_counter->counter - nfq_err->count;
            else nfq_err->count = report_counter->counter;
            return true;
        }
    }
    return false;
}
/*
 * ===========================================================================
 *  Public API implementation
 * ===========================================================================
 */

void
dpi_stats_log_pcap_counters(void)
{
    struct pcap_stats_counters *stats;

    ds_tree_foreach(&g_dpi_stats.pcap_stats, stats)
    {
        LOGT("%s(): ifname: %s, received %d", __func__, stats->ifname, stats->pkts_received);
    }
}


void
dpi_stats_log_nfq_counters(void)
{
    struct nfq_stats_counters *stats;
    struct nfq_err_cnts *errs;

    ds_tree_foreach(&g_dpi_stats.nfq_stats, stats)
    {
        LOGT("%s(): qnum: %d, queue total %d", __func__, stats->qnum, stats->qtotal);
        ds_dlist_foreach(&stats->error_list, errs)
        {
            LOGT("%s(): errno: %u, err_cnt: %"PRIu64" ", __func__, errs->error_no, errs->count);
        }
    }
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
    struct pcap_stats_counters *stats;
    ds_tree_t *pcap_stats;

    /* initialize the tree if not already initialized */
    if (!g_dpi_stats.initialized) dpi_stats_init_record();

    pcap_stats = &g_dpi_stats.pcap_stats;

    stats = ds_tree_find(pcap_stats, ifname);
    if (!stats)
    {
        stats = CALLOC(1, sizeof(*stats));
        dpi_stats_insert_pcap_entry(data, ifname, stats);
        g_dpi_stats.num_pcaps++;
    }
    else dpi_stats_update_pcap_entry(data, ifname, stats);

    return;
}


/**
 * @brief stores the node (nfqueue stats info) in the nfqueue stats tree
 * @param data containing the nfqueue stats
 * @param ifname of the nfqueue stats used as the key
 * @return void
 */
void
dpi_stats_store_nfq_stats(struct nfqnl_counters *nfq_cnt)
{
    struct nfq_stats_counters *stats;
    ds_tree_t *nfq_stats;

    /* initialize the tree if not already initialized */
    if (!g_dpi_stats.initialized) dpi_stats_init_record();

    nfq_stats = &g_dpi_stats.nfq_stats;

    stats = ds_tree_find(nfq_stats, &nfq_cnt->queue_num);
    if (!stats)
    {
        stats = CALLOC(1, sizeof(*stats));
        dpi_stats_insert_nfq_entry(nfq_cnt, stats);
        g_dpi_stats.num_nfqs++;

    } else dpi_stats_update_nfq_entry(nfq_cnt, stats);

    return;
}


/**
 * @brief stores the node (nfqueue error info) in the nfqueue stats tree
 * @param data containing the nfqueue error and its count
 * @param ifname of the nfqueue stats used as the key
 * @return void
 */
void
dpi_stats_store_nfq_err_cnt(int queue_num)
{
    struct nf_queue_err_counters *report_counter;
    struct nf_queue_context_errors *report;
    struct nfq_stats_counters *stats;
    ds_tree_t *nfq_stats;
    bool rc = false;
    size_t i;


    nfq_stats = &g_dpi_stats.nfq_stats;
    stats = ds_tree_find(nfq_stats, &queue_num);
    if (!stats) return;

    report = nfq_get_err_counters(queue_num);
    if (report == NULL) return;

    for (i = 0; i < report->count; i++)
    {
        report_counter = report->counters[i];
        rc = dpi_stats_update_nfq_err_entry(report_counter, stats);
        if (!rc) dpi_stats_insert_nfq_err_entry(report_counter, stats);
    }

    FREE(report->counters);
    FREE(report);
}


/**
 * @brief stores the call trace stats in the tree
 * @param trace_stats containing the call trace stats
 * @return void
 */
void dpi_stats_store_call_trace_stats(struct fn_tracer_stats *trace_stats)
{
    struct fn_tracer_stats *stats;
    ds_tree_t *fn_stats;

    /* initialize the tree if not already initialized */
    if (!g_dpi_stats.initialized) dpi_stats_init_record();

    fn_stats = &g_dpi_stats.fn_tracer_stats;

    stats = ds_tree_find(fn_stats, trace_stats->fn_name);
    if (!stats)
    {
        stats = CALLOC(1, sizeof(*stats));
        dpi_stats_insert_fn_entry(trace_stats, stats);
        g_dpi_stats.num_fns++;

    } else dpi_stats_update_fn_entry(trace_stats, stats);

    return;
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
 * @brief Serializes the data that can be consumed
 * by protobuf
 *
 * @param report structure containing the stats that
 * needs to be serialized
 * @return dpi_stats_packed_buffer pointer containing
 * the serialized data
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_counter_report(struct dpi_stats_report *report)
{
    struct dpi_stats_packed_buffer *serialized;
    Interfaces__DpiStats__DpiStatsReport *pb;
    size_t len;
    void *buf;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    pb = dpi_stats_set_counter_report(report);
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
    dpi_stats_free_counter_report(pb);
    dpi_stats_log_nfq_counters();
    return serialized;

error:
    dpi_stats_free_packed_buffer(serialized);
    dpi_stats_free_counter_report(pb);

    return NULL;
}


/**
 * @brief sends the call trace report to the specified mqtt topic
 * @param session fsm_session containing the session information
 * @param topic mqtt topic used for sending the report
 * @return none
 */
struct dpi_stats_packed_buffer *
dpi_stats_serialize_call_trace_stats(struct dpi_stats_report *report)
{
    struct dpi_stats_packed_buffer *serialized;
    Interfaces__DpiStats__DpiStatsReport *pb;
    size_t len;
    void *buf;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    pb = dpi_stats_set_fn_stats_report(report);
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
    dpi_stats_free_counter_report(pb);

    return serialized;

error:
    dpi_stats_free_packed_buffer(serialized);
    dpi_stats_free_counter_report(pb);

    return NULL;
}

void
dpi_stats_init_record(void)
{
    if (g_dpi_stats.initialized) return;

    MEMZERO(g_dpi_stats);

    /* initialize nfq and pcap tress */
    ds_tree_init(&g_dpi_stats.nfq_stats, ds_int_cmp, struct nfq_stats_counters, nfq_node);
    ds_tree_init(&g_dpi_stats.pcap_stats, ds_str_cmp, struct pcap_stats_counters, pcap_node);
    ds_tree_init(&g_dpi_stats.fn_tracer_stats, ds_str_cmp, struct fn_tracer_stats, fn_stats_node);

    g_dpi_stats.initialized = true;
}

void
dpi_stats_cleanup_record(void)
{
    ds_tree_t *nfqs;
    ds_tree_t *pcaps;
    ds_tree_t *fns;

    if (!g_dpi_stats.initialized) return;

    nfqs = &g_dpi_stats.nfq_stats;
    pcaps = &g_dpi_stats.pcap_stats;
    fns = &g_dpi_stats.fn_tracer_stats;

    dpi_stats_clear_nfq_counters(nfqs);
    dpi_stats_clear_pcap_counters(pcaps);
    dpi_stats_clear_trace_stats(fns);
    g_dpi_stats.initialized = false;
}


/**
 * @brief gets the number of pcap (pcap stats) present
 * in the nfqueue tree
 * @param void
 * @return int the number of nodes present
 */
int dpi_stats_get_pcap_stats_count(void)
{
    if (!g_dpi_stats.initialized) return 0;

    return g_dpi_stats.num_pcaps;
}


/**
 * @brief stores the node (pcap stats info) in the pcap stats tree
 * @param data containing the pcap stats
 * @param ifname of the pcap stats used as the key
 * @return void
 */
int dpi_stats_get_nfq_stats_count(void)
{
    if (!g_dpi_stats.initialized) return 0;

    return g_dpi_stats.num_nfqs;
}


/**
 * @brief gets the number of call trace stats pre
 * in the call trace tree
 * @param void
 * @return int the number of nodes present
 */
int dpi_stats_get_call_trace_stats_count(void)
{
    if (!g_dpi_stats.initialized) return 0;

    return g_dpi_stats.num_fns;
}
