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


struct dpi_stats_counters
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


/**
 * @brief DPI stats report
 */
struct dpi_stats_report
{
    char *node_id;
    char *location_id;
    char *plugin;
    struct dpi_stats_counters counters;
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
#endif /* DPI_STATS_H_INCLUDED */
