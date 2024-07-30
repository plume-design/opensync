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

#include <ev.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dpi_stats.h"
#include "ds_tree.h"
#include "log.h"
#include "memutil.h"
#include "os.h"
#include "os_ev_trace.h"

struct fsm_fn_tracer
{
    void *fn_ptr;
    const char *fn_name;
    ds_tree_node_t node;
    uint64_t prev_counter;
    uint64_t counter;
    uint64_t ts_enter;
    uint64_t ts_exit;
    uint64_t max_timespent;
    uint64_t max_timespent_ts;
    uint64_t period_time_spent;
};

struct fsm_fn_tracer_mgr
{
    int initialized;
    ds_tree_t tracer_tree;
    struct fsm_fn_tracer *tracer;
    uint64_t periodic_ts;
};

static struct fsm_fn_tracer_mgr fsm_tracer = {
    .initialized = 0,
};

static const char *default_fn_name = "no_fn_registered";

void fsm_fn_trace(void *p, int trace)
{
    struct fsm_fn_tracer *tracer;
    struct timespec now;
    uint64_t ts;

    tracer = ds_tree_find(&fsm_tracer.tracer_tree, p);
    if (trace == OS_EV_ENTER)
    {
        if (tracer == NULL)
        {
            struct fsm_fn_tracer *to_add;

            to_add = CALLOC(1, sizeof(*to_add));
            to_add->fn_ptr = p;
            to_add->fn_name = default_fn_name;
            ds_tree_insert(&fsm_tracer.tracer_tree, to_add, to_add->fn_ptr);

            tracer = to_add;
        }
        fsm_tracer.tracer = tracer;
    }

    if (tracer == NULL) return;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    if (trace == OS_EV_ENTER)
    {
        tracer->ts_enter = ts;
    }
    else
    {
        uint64_t time_spent;

        tracer->ts_exit = ts;
        time_spent = tracer->ts_exit - tracer->ts_enter;
        if (time_spent > tracer->max_timespent)
        {
            tracer->max_timespent = time_spent;
            tracer->max_timespent_ts = tracer->ts_enter;
        }
        tracer->period_time_spent += time_spent;
        tracer->counter++;
    }
}

void fsm_fn_map(void *p, const char *fname)
{
    struct fsm_fn_tracer *to_add;

    to_add = ds_tree_find(&fsm_tracer.tracer_tree, p);
    if (to_add != NULL)
    {
        to_add->fn_name = fname;
        return;
    }

    to_add = CALLOC(1, sizeof(*to_add));
    to_add->fn_ptr = p;
    to_add->fn_name = fname;
    ds_tree_insert(&fsm_tracer.tracer_tree, to_add, to_add->fn_ptr);
}

static int cmp_fn(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

void fsm_fn_tracer_init(void)
{
    struct os_ev_trace_setting setting;

    ds_tree_init(&fsm_tracer.tracer_tree, cmp_fn, struct fsm_fn_tracer, node);

    MEMZERO(setting);
    setting.tracer = fsm_fn_trace;
    setting.mapper = fsm_fn_map;

    os_ev_trace_init(&setting);
    fsm_tracer.initialized = 1;
}

static void fsm_fn_fill_tracer_stats(struct fn_tracer_stats *tracer_stats, struct fsm_fn_tracer *tracer)
{
    if (!tracer_stats || !tracer) return;

    tracer_stats->call_count = tracer->counter;
    tracer_stats->fn_name = STRDUP(tracer->fn_name);
    tracer_stats->max_duration = tracer->max_timespent;
    tracer_stats->total_duration = tracer->period_time_spent;
    return;
}

#define FSM_FN_TRACER_INTERVAL 30

void fsm_fn_periodic(struct fsm_session *session)
{
    struct dpi_stats_packed_buffer *pb;
    struct fn_tracer_stats trace_stats;
    struct fsm_fn_tracer *tracer;
    struct dpi_stats_report report;
    time_t now = time(NULL);

    if ((now - fsm_tracer.periodic_ts) < FSM_FN_TRACER_INTERVAL) return;

    fsm_tracer.periodic_ts = now;

    tracer = ds_tree_head(&fsm_tracer.tracer_tree);
    if (tracer == NULL) return;

    MEMZERO(report);
    report.location_id = session->location_id;
    report.node_id = session->node_id;

    while (tracer != NULL)
    {
        if (tracer->counter == tracer->prev_counter)
        {
            tracer = ds_tree_next(&fsm_tracer.tracer_tree, tracer);
            continue;
        }
        LOGI("%s: function: %s (%p): call count: %" PRIu64 ", max duration: %" PRIu64 "us, total duration: %" PRIu64
             "us",
             __func__,
             tracer->fn_name,
             tracer->fn_ptr,
             tracer->counter - tracer->prev_counter,
             tracer->max_timespent,
             tracer->period_time_spent);

        fsm_fn_fill_tracer_stats(&trace_stats, tracer);
        /* store the call trace stats */
        dpi_stats_store_call_trace_stats(&trace_stats);

        tracer->prev_counter = tracer->counter;
        tracer->max_timespent = 0;
        tracer->period_time_spent = 0;
        tracer = ds_tree_next(&fsm_tracer.tracer_tree, tracer);
        FREE(trace_stats.fn_name);
        MEMZERO(trace_stats);
    }

    pb = dpi_stats_serialize_call_trace_stats(&report);
    if (pb == NULL) return;

    session->ops.send_pb_report(session, session->dpi_stats_report_topic, pb->buf, pb->len);
    dpi_stats_free_packed_buffer(pb);
}
