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
#include "qm_conn.h"
#include "intf_stats.h"

/*****************************************************************************
 * Helper functions
 ****************************************************************************/

/**
 * @brief duplicates a string and returns true if successful
 *
 * wrapper around string duplication when the source string might be
 * a null pointer.
 *
 * @param src source string to duplicate. Might be NULL.
 * @param dst destination string pointer
 * @return true if duplicated, false otherwise
 */
static bool 
intf_stats_str_duplicate(char *src, char **dst)
{
    if (src == NULL)
    {
        *dst = NULL;
        return true;
    }

    *dst = strndup(src, MAX_STRLEN);
    if (*dst == NULL)
    {
        LOGE("%s: could not duplicate %s", __func__, src);
        return false;
    }

    return true;
}

/*****************************************************************************
 * Observation Point(Node Info)
 ****************************************************************************/

/**
 * @brief Allocates and sets an observation point protobuf.
 *
 * Uses the node info to fill a dynamically allocated
 * observation point protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see intf_stats_free_pb_op() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to a observation point protobuf structure
 */
static Intf__Stats__ObservationPoint *
intf_stats_set_node_info(node_info_t *node)
{
    Intf__Stats__ObservationPoint *pb = NULL;
    bool ret;

    // Allocate the protobuf structure
    pb = calloc(1, sizeof(Intf__Stats__ObservationPoint));
    if (!pb)
    {
        LOGE("%s: ObservationPoint protobuf struct allocation"
             " failed", __func__);
        return NULL;
    }

    /* Initialize the protobuf structure */
    intf__stats__observation_point__init(pb);

    /* Set the protobuf fields */
    ret = intf_stats_str_duplicate(node->node_id, &pb->nodeid);
    if (!ret) goto err_free_pb;

    ret = intf_stats_str_duplicate(node->location_id, &pb->locationid);
    if (!ret) goto err_free_node_id;

    return pb;

err_free_node_id:
    free(pb->nodeid);

err_free_pb:
    free(pb);

    return NULL;
}

/**
 * @brief Free an observation point protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb observation point structure to free
 * @return none
 */
static void
intf_stats_free_pb_op(Intf__Stats__ObservationPoint *pb)
{
    if (!pb) return;

    free(pb->nodeid);
    free(pb->locationid);

    free(pb);

    return;
}

/**
 * @brief Generates an observation point serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_node_info(node_info_t *node)
{
    Intf__Stats__ObservationPoint *pb;
    packed_buffer_t               *serialized;
    void                          *buf;
    size_t                         len;

    if (!node) return NULL;

    /* Allocate serialization output container */
    serialized = calloc(1, sizeof(packed_buffer_t));
    if (!serialized) return NULL;

    /* Allocate and set observation point protobuf */
    pb = intf_stats_set_node_info(node);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = intf__stats__observation_point__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (!buf) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = intf__stats__observation_point__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_op(pb);

    return serialized;

err_free_pb:
    intf_stats_free_pb_op(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}

/*****************************************************************************
 * Interface associated stats
 ****************************************************************************/
/**
 * @brief Allocates and sets a intf stats protobuf.
 *
 * Uses the intfstats info to fill a dynamically allocated
 * intf stats protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see free_pb_stats() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a Intf Stats protobuf structure
 */
static Intf__Stats__IntfStats *
intf_stats_set_intf_stats(intf_stats_t *intf)
{
    Intf__Stats__IntfStats *pb = NULL;
    bool ret = false;

    // Allocate the protobuf structure
    pb = calloc(1, sizeof(Intf__Stats__IntfStats));
    if (!pb)
    {
        LOGE("%s:intferface struct allocation failed", __func__);
        return NULL;
    }

    // Initialize the protobuf structure
    intf__stats__intf_stats__init(pb);

    // Set the protobuf fields
    ret = intf_stats_str_duplicate(intf->ifname, &pb->ifname);
    if (!ret) goto err_free_pb;

    ret = intf_stats_str_duplicate(intf->role, &pb->role);
    if (!ret) goto err_free_ifname;

    pb->txbytes = intf->tx_bytes;
    pb->has_txbytes = true;

    pb->rxbytes = intf->rx_bytes;
    pb->has_rxbytes = true;

    pb->txpackets = intf->tx_packets;
    pb->has_txpackets = true;

    pb->rxpackets = intf->rx_packets;
    pb->has_rxpackets = true;

    return pb;

err_free_ifname:
    free(pb->ifname);

err_free_pb:
    free(pb);

    return NULL;
}

/**
 * @brief Free a intf stats protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow stats structure to free
 * @return none
 */
void
intf_stats_free_pb_intf_stats(Intf__Stats__IntfStats *pb)
{
    if (!pb) return;

    free(pb->ifname);
    free(pb->role);
    free(pb);
}

/**
 * @brief Generates a intf stats serialized protobuf.
 *
 * Uses the information pointed by the stats parameter to generate
 * a serialized intf stats buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see intf_stats_free_packed_buffer() for this purpose.
 *
 * @param stats info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_intf_stats(intf_stats_t *intf)
{
    Intf__Stats__IntfStats *pb;
    packed_buffer_t        *serialized;
    void                   *buf;
    size_t                  len;

    if (!intf) return NULL;

    /* Allocate serialization output container */
    serialized = calloc(1, sizeof(packed_buffer_t));
    if (!serialized) return NULL;

    /* Allocate and set the intf stats container */
    pb = intf_stats_set_intf_stats(intf);
    if (!pb) goto err_free_serialized;

    /* get serialization length */
    len = intf__stats__intf_stats__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (!buf) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = intf__stats__intf_stats__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_intf_stats(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    intf_stats_free_pb_intf_stats(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets table of intf stats protobufs
 *
 * Uses the window info to fill a dynamically allocated
 * table of intf stats protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Intf__Stats__IntfStats  **
intf_stats_set_pb_intf_stats(intf_stats_window_t *window)
{
    Intf__Stats__IntfStats **intfs_pb_tbl = NULL;
    size_t i, allocated = 0;

    ds_dlist_t          *window_intf_list = &window->intf_list;
    intf_stats_t        *intf = NULL;
    ds_dlist_iter_t     intf_iter;

    if (!window) return NULL;

    if (window->num_intfs == 0) return NULL;

    // Allocate the array of interfaces
    intfs_pb_tbl = calloc(window->num_intfs, sizeof(Intf__Stats__IntfStats *));
    if (!intfs_pb_tbl)
    {
        LOGE("%s:intfs_pb_tbl allocation failed", __func__);
        return NULL;
    }

    for ( intf = ds_dlist_ifirst(&intf_iter, window_intf_list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        if (allocated > window->num_intfs)
        {
            // This should never happen
            LOGE("%s: Excess intfs being allocated", __func__);
            goto err_free_pb_intfs;
        }

        intfs_pb_tbl[allocated] = intf_stats_set_intf_stats(intf);
        if (!intfs_pb_tbl[allocated]) goto err_free_pb_intfs;

        allocated++;
    }

    return intfs_pb_tbl;

err_free_pb_intfs:
    for (i = 0; i < allocated; i++)
    {
        intf_stats_free_pb_intf_stats(intfs_pb_tbl[i]);
    }

    free(intfs_pb_tbl);

    return NULL;
}

/*****************************************************************************
 * Observation Window
 ****************************************************************************/

/**
 * @brief Allocates and sets an observation window protobuf.
 *
 * Uses the stats info to fill a dynamically allocated
 * observation window protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see intf_stats_free_pb_window() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Intf__Stats__ObservationWindow *
intf_stats_set_pb_window(intf_stats_window_t *window)
{
    Intf__Stats__ObservationWindow *pb = NULL;

    // Allocate protobuf
    pb = calloc(1, sizeof(Intf__Stats__ObservationWindow));
    if (!pb)
    {
        LOGE("%s: observation window allocation failed", __func__);
        return NULL;
    }

    intf__stats__observation_window__init(pb);

    // Set protobuf fields
    pb->startedat     = window->started_at;
    pb->has_startedat = true;

    pb->endedat       = window->ended_at;
    pb->has_endedat   = true;

    // Accept window with no interfaces
    if (window->num_intfs == 0) return pb;

    // Allocate interface stats container
    pb->intfstats = intf_stats_set_pb_intf_stats(window);
    if (!pb->intfstats) goto err_free_pb_window;

    pb->n_intfstats = window->num_intfs;

    return pb;

err_free_pb_window:
    free(pb);

    return NULL;
}

/**
 * @brief Free an observation window protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flows window structure to free
 * @return none
 */
void
intf_stats_free_pb_window(Intf__Stats__ObservationWindow *pb)
{
    size_t i;

    if (!pb) return;

    for (i = 0; i < pb->n_intfstats; i++)
    {
        intf_stats_free_pb_intf_stats(pb->intfstats[i]);
    }

    free(pb->intfstats);
    free(pb);

    return;
}

/**
 * @brief Generates an observation window serialized protobuf
 *
 * Uses the information pointed by the window parameter to generate
 * a serialized obervation window buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param window info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_window(intf_stats_window_t *window)
{
    Intf__Stats__ObservationWindow *pb;
    packed_buffer_t                *serialized;
    void                           *buf;
    size_t                          len;

    if (!window) return NULL;

    /* Allocate serialization output container */
    serialized = calloc(1, sizeof(packed_buffer_t));
    if (!serialized) return NULL;

    /* Allocate and set the intf stats protobuf */
    pb = intf_stats_set_pb_window(window);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = intf__stats__observation_window__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (!buf) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = intf__stats__observation_window__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_window(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    intf_stats_free_pb_window(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets table of observation window protobufs
 *
 * Uses the report info to fill a dynamically allocated
 * table of observation window protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Intf__Stats__ObservationWindow **
intf_stats_set_pb_windows(intf_stats_report_data_t *report)
{
    Intf__Stats__ObservationWindow **windows_pb_tbl;
    size_t  i, allocated = 0;

    intf_stats_list_t           *window_list = &report->window_list;
    intf_stats_window_list_t    *window = NULL;
    intf_stats_window_t         *window_entry = NULL;

    ds_dlist_iter_t             win_iter;

    if (!report) return NULL;

    if (report->num_windows == 0) return NULL;


    windows_pb_tbl = calloc(report->num_windows,
                            sizeof(Intf__Stats__ObservationWindow *));
    if (!windows_pb_tbl)
    {
        LOGE("%s: windows_pb_tbl allocation failed", __func__);
        return NULL;
    }

    for ( window = ds_dlist_ifirst(&win_iter, window_list);
          window != NULL;
          window = ds_dlist_inext(&win_iter))
    {
        if (allocated > report->num_windows)
        {
            // This should never happen
            LOGE("%s: Excess windows being allocated", __func__);
            goto err_free_pb_windows;
        }

        window_entry = &window->entry;

        windows_pb_tbl[allocated] = intf_stats_set_pb_window(window_entry);
        if (!windows_pb_tbl[allocated]) goto err_free_pb_windows;

        allocated++;
    }

    return windows_pb_tbl;

err_free_pb_windows:
    for (i = 0; i < allocated; i++)
    {
        intf_stats_free_pb_window(windows_pb_tbl[i]);
    }

    free(windows_pb_tbl);

    return NULL;
}

/*****************************************************************************
 * Intf Report
 ****************************************************************************/

/**
 * @brief Allocates and sets a flow report protobuf.
 *
 * Uses the report info to fill a dynamically allocated
 * intf stat report protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see intf_stats_free_pb_report() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Intf__Stats__IntfReport *
intf_stats_set_pb_report(intf_stats_report_data_t *report)
{
    Intf__Stats__IntfReport *pb = NULL;

    pb = calloc(1, sizeof(Intf__Stats__IntfReport));
    if (!pb)
    {
        LOGE("%s: Intf__Stats__IntfReport alloc failed", __func__);
        return NULL;
    }

    // Initialize protobuf
    intf__stats__intf_report__init(pb);

    // Set protobuf fields
    pb->reportedat     = report->reported_at;
    pb->has_reportedat = true;

    pb->observationpoint = intf_stats_set_node_info(&report->node_info);
    if (!pb->observationpoint)
    {
        LOGE("%s: set observationpoint failed", __func__);
        goto err_free_pb_report;
    }

    // Accept report with no windows
    if (report->num_windows == 0) return pb;

    // Allocate observation windows container
    pb->observationwindow = intf_stats_set_pb_windows(report);
    if (!pb->observationwindow)
    {
        LOGE("%s: observation windows container allocation failed", __func__);
        goto err_free_pb_op;
    }

    pb->n_observationwindow = report->num_windows;

    return pb;

err_free_pb_op:
    intf_stats_free_pb_op(pb->observationpoint);

err_free_pb_report:
    free(pb);

    return NULL;
}

/**
 * @brief Free a flow report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void
intf_stats_free_pb_report(Intf__Stats__IntfReport *pb)
{
    size_t i;

    if (!pb) return;

    intf_stats_free_pb_op(pb->observationpoint);

    for (i = 0; i < pb->n_observationwindow; i++)
    {
        intf_stats_free_pb_window(pb->observationwindow[i]);
    }

    free(pb->observationwindow);
    free(pb);

    return;
}

/*****************************************************************************
 * Report serialization
 ****************************************************************************/

/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see intf_stats_free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_report(intf_stats_report_data_t *report)
{
    Intf__Stats__IntfReport *pb = NULL;
    packed_buffer_t         *serialized = NULL;
    void                    *buf;
    size_t                   len;

    if (!report)
    {
        LOGE("%s: Intf Stats report is NULL", __func__);
        return NULL;
    }

    // Allocate serialization output structure
    serialized = calloc(1,sizeof(packed_buffer_t));
    if (!serialized)
    {
        LOGE("%s: packed_buffer memory allocation failed", __func__);
        return NULL;
    }

    // Allocate and set the IntfReport protobuf
    pb = intf_stats_set_pb_report(report);
    if (!pb)
    {
        LOGE("%s: set_pb_report failed", __func__);
        goto err_free_serialized;
    }

    // Get serialized length
    len = intf__stats__intf_report__get_packed_size(pb);
    if (len == 0)
    {
        LOGE("%s: Failed to get serialized report len", __func__);
        goto err_free_pb;
    }

    // Allocate space for the serialized buffer
    buf = malloc(len);
    if (!buf)
    {
        LOGE("%s: failed to allocate serialized buf", __func__);
        goto err_free_pb;
    }

    serialized->len = intf__stats__intf_report__pack(pb, buf);
    serialized->buf = buf;

    // Free the protobuf structure
    intf_stats_free_pb_report(pb);

    return serialized;

err_free_pb:
    free(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}

/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb).
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void
intf_stats_free_packed_buffer(packed_buffer_t *pb)
{
    if (!pb) return;

    if (pb->buf) free(pb->buf);

    free(pb);
}

/**
 * @brief Prepares the serialized intf stat report and sends it over mqtt
 *
 * Converts the intf stat report information into serialized content, and
 * sends it over MQTT.
 *
 * @param report a pointer to intf stats report container
 *        mqtt_topic a pointer to the mqtt topic
 * @return result of mqtt send
 */
bool
intf_stats_send_report(intf_stats_report_data_t *report, char *mqtt_topic)
{
    packed_buffer_t     *pb = NULL;
    qm_response_t       res;
    bool                ret = false;

    if (!report)
    {
        LOGE("%s: Intf Stats report is NULL", __func__);
        return ret;
    }

    if (!mqtt_topic)
    {
        LOGE("%s: MQTT topic is NULL", __func__);
        return ret;
    }

    report->reported_at = time(NULL);

    pb = intf_stats_serialize_report(report);
    if (!pb)
    {
        LOGE("%s: Intf Stats report serialization failed", __func__);
        return ret;
    }

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, mqtt_topic,
                              pb->buf, pb->len, &res);

    // Free the serialized container
    intf_stats_free_packed_buffer(pb);

    return ret;
}
