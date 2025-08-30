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
#include "memutil.h"

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
intf_stats_str_duplicate(char *src, char **dst, size_t max)
{
    if (src == NULL)
    {
        *dst = NULL;
        return true;
    }

    *dst = strndup(src, max);
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
static Interfaces__IntfStats__ObservationPoint *
intf_stats_set_node_info(node_info_t *node)
{
    Interfaces__IntfStats__ObservationPoint *pb = NULL;
    bool ret;

    // Allocate the protobuf structure
    pb = CALLOC(1, sizeof(Interfaces__IntfStats__ObservationPoint));

    /* Initialize the protobuf structure */
    interfaces__intf_stats__observation_point__init(pb);

    /* Set the protobuf fields */
    ret = intf_stats_str_duplicate(node->node_id, &pb->node_id, MAX_STRLEN);
    if (!ret) goto err_free_pb;

    ret = intf_stats_str_duplicate(node->location_id, &pb->location_id, MAX_STRLEN);
    if (!ret) goto err_free_node_id;

    return pb;

err_free_node_id:
    FREE(pb->node_id);

err_free_pb:
    FREE(pb);

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
intf_stats_free_pb_op(Interfaces__IntfStats__ObservationPoint *pb)
{
    if (!pb) return;

    FREE(pb->node_id);
    FREE(pb->location_id);

    FREE(pb);

    return;
}

/**
 * @brief Generates an observation point serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_node_info(node_info_t *node)
{
    Interfaces__IntfStats__ObservationPoint *pb;
    packed_buffer_t                         *serialized;
    void                                    *buf;
    size_t                                   len;

    if (!node) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set observation point protobuf */
    pb = intf_stats_set_node_info(node);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = interfaces__intf_stats__observation_point__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__intf_stats__observation_point__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_op(pb);

    return serialized;

err_free_pb:
    intf_stats_free_pb_op(pb);

err_free_serialized:
    FREE(serialized);

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
static Interfaces__IntfStats__IntfStats *
intf_stats_set_intf_stats(intf_stats_t *intf)
{
    Interfaces__IntfStats__IntfStats *pb = NULL;
    bool ret = false;

    // Allocate the protobuf structure
    pb = CALLOC(1, sizeof(Interfaces__IntfStats__IntfStats));

    // Initialize the protobuf structure
    interfaces__intf_stats__intf_stats__init(pb);

    // Set the protobuf fields
    ret = intf_stats_str_duplicate(intf->ifname, &pb->if_name, sizeof(intf->ifname));
    if (!ret) goto err_free_pb;

    ret = intf_stats_str_duplicate(intf->role, &pb->role, sizeof(intf->role));
    if (!ret) goto err_free_ifname;

    pb->tx_bytes = intf->tx_bytes;
    pb->has_tx_bytes = true;

    pb->rx_bytes = intf->rx_bytes;
    pb->has_rx_bytes = true;

    pb->tx_packets = intf->tx_packets;
    pb->has_tx_packets = true;

    pb->rx_packets = intf->rx_packets;
    pb->has_rx_packets = true;

    return pb;

err_free_ifname:
    FREE(pb->if_name);

err_free_pb:
    FREE(pb);

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
intf_stats_free_pb_intf_stats(Interfaces__IntfStats__IntfStats *pb)
{
    if (!pb) return;

    FREE(pb->if_name);
    FREE(pb->role);
    FREE(pb);
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
    Interfaces__IntfStats__IntfStats *pb;
    packed_buffer_t        *serialized;
    void                   *buf;
    size_t                  len;

    if (!intf) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the intf stats container */
    pb = intf_stats_set_intf_stats(intf);
    if (!pb) goto err_free_serialized;

    /* get serialization length */
    len = interfaces__intf_stats__intf_stats__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__intf_stats__intf_stats__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_intf_stats(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    intf_stats_free_pb_intf_stats(pb);

err_free_serialized:
    FREE(serialized);

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
Interfaces__IntfStats__IntfStats  **
intf_stats_set_pb_intf_stats(intf_stats_window_t *window)
{
    Interfaces__IntfStats__IntfStats **intfs_pb_tbl = NULL;
    size_t i, allocated = 0;

    ds_dlist_t          *window_intf_list = &window->intf_list;
    intf_stats_t        *intf = NULL;
    ds_dlist_iter_t     intf_iter;

    if (!window) return NULL;

    if (window->num_intfs == 0) return NULL;

    // Allocate the array of interfaces
    intfs_pb_tbl = CALLOC(window->num_intfs, sizeof(Interfaces__IntfStats__IntfStats *));

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

    /* if no of monitor interfaces & allocated are not same */
    if (allocated != window->num_intfs)
    {
        LOGE("%s: window_intf_list is empty", __func__);
        goto err_free_pb_intfs;
    }

    return intfs_pb_tbl;

err_free_pb_intfs:
    for (i = 0; i < allocated; i++)
    {
        intf_stats_free_pb_intf_stats(intfs_pb_tbl[i]);
    }

    FREE(intfs_pb_tbl);

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
static Interfaces__IntfStats__ObservationWindow *
intf_stats_set_pb_window(intf_stats_window_t *window)
{
    Interfaces__IntfStats__ObservationWindow *pb = NULL;

    // Allocate protobuf
    pb = CALLOC(1, sizeof(Interfaces__IntfStats__ObservationWindow));

    interfaces__intf_stats__observation_window__init(pb);

    // Set protobuf fields
    pb->started_at     = window->started_at;
    pb->has_started_at = true;

    pb->ended_at       = window->ended_at;
    pb->has_ended_at   = true;

    // Accept window with no interfaces
    if (window->num_intfs == 0) return pb;

    // Allocate interface stats container
    pb->intf_stats = intf_stats_set_pb_intf_stats(window);
    if (!pb->intf_stats) goto err_free_pb_window;

    pb->n_intf_stats = window->num_intfs;

    return pb;

err_free_pb_window:
    FREE(pb);

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
intf_stats_free_pb_window(Interfaces__IntfStats__ObservationWindow *pb)
{
    size_t i;

    if (!pb) return;

    for (i = 0; i < pb->n_intf_stats; i++)
    {
        intf_stats_free_pb_intf_stats(pb->intf_stats[i]);
    }

    FREE(pb->intf_stats);
    FREE(pb);

    return;
}

/**
 * @brief Generates an observation window serialized protobuf
 *
 * Uses the information pointed by the window parameter to generate
 * a serialized observation window buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param window info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
intf_stats_serialize_window(intf_stats_window_t *window)
{
    Interfaces__IntfStats__ObservationWindow *pb;
    packed_buffer_t                *serialized;
    void                           *buf;
    size_t                          len;

    if (!window) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the intf stats protobuf */
    pb = intf_stats_set_pb_window(window);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = interfaces__intf_stats__observation_window__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__intf_stats__observation_window__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    intf_stats_free_pb_window(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    intf_stats_free_pb_window(pb);

err_free_serialized:
    FREE(serialized);

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
Interfaces__IntfStats__ObservationWindow **
intf_stats_set_pb_windows(intf_stats_report_data_t *report)
{
    Interfaces__IntfStats__ObservationWindow **windows_pb_tbl;
    size_t  i, allocated = 0;

    intf_stats_list_t           *window_list = &report->window_list;
    intf_stats_window_list_t    *window = NULL;
    intf_stats_window_t         *window_entry = NULL;

    ds_dlist_iter_t             win_iter;

    if (!report) return NULL;

    if (report->num_windows == 0) return NULL;


    windows_pb_tbl = CALLOC(report->num_windows,
                            sizeof(Interfaces__IntfStats__ObservationWindow *));

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

    FREE(windows_pb_tbl);

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
static Interfaces__IntfStats__IntfReport *
intf_stats_set_pb_report(intf_stats_report_data_t *report)
{
    Interfaces__IntfStats__IntfReport *pb = NULL;

    pb = CALLOC(1, sizeof(Interfaces__IntfStats__IntfReport));

    // Initialize protobuf
    interfaces__intf_stats__intf_report__init(pb);

    // Set protobuf fields
    pb->reported_at     = report->reported_at;
    pb->has_reported_at = true;

    pb->observation_point = intf_stats_set_node_info(&report->node_info);
    if (!pb->observation_point)
    {
        LOGE("%s: set observation_point failed", __func__);
        goto err_free_pb_report;
    }

    // Accept report with no windows
    if (report->num_windows == 0) return pb;

    // Allocate observation windows container
    pb->observation_windows = intf_stats_set_pb_windows(report);
    if (!pb->observation_windows)
    {
        LOGE("%s: observation windows container allocation failed", __func__);
        goto err_free_pb_op;
    }

    pb->n_observation_windows = report->num_windows;

    return pb;

err_free_pb_op:
    intf_stats_free_pb_op(pb->observation_point);

err_free_pb_report:
    FREE(pb);

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
intf_stats_free_pb_report(Interfaces__IntfStats__IntfReport *pb)
{
    size_t i;

    if (!pb) return;

    intf_stats_free_pb_op(pb->observation_point);

    for (i = 0; i < pb->n_observation_windows; i++)
    {
        intf_stats_free_pb_window(pb->observation_windows[i]);
    }

    FREE(pb->observation_windows);
    FREE(pb);

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
    Interfaces__IntfStats__IntfReport *pb = NULL;
    packed_buffer_t                   *serialized = NULL;
    void                              *buf;
    size_t                             len;

    if (!report)
    {
        LOGE("%s: Intf Stats report is NULL", __func__);
        return NULL;
    }

    // Allocate serialization output structure
    serialized = CALLOC(1,sizeof(packed_buffer_t));

    // Allocate and set the IntfReport protobuf
    pb = intf_stats_set_pb_report(report);
    if (!pb)
    {
        LOGE("%s: set_pb_report failed", __func__);
        goto err_free_serialized;
    }

    // Get serialized length
    len = interfaces__intf_stats__intf_report__get_packed_size(pb);
    if (len == 0)
    {
        LOGE("%s: Failed to get serialized report len", __func__);
        goto err_free_pb;
    }

    // Allocate space for the serialized buffer
    buf = MALLOC(len);

    serialized->len = interfaces__intf_stats__intf_report__pack(pb, buf);
    serialized->buf = buf;

    // Free the protobuf structure
    intf_stats_free_pb_report(pb);

    return serialized;

err_free_pb:
    FREE(pb);

err_free_serialized:
    FREE(serialized);

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

    if (pb->buf) FREE(pb->buf);

    FREE(pb);
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
