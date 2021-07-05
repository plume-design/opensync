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
#include "dpi_stats.h"
#include "memutil.h"
#include "dpi_stats.pb-c.h"


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
    struct dpi_stats_counters *counters;

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
