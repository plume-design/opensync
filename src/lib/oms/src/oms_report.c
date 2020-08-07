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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "log.h"
#include "qm_conn.h"
#include "oms.h"
#include "oms_report.h"
#include "object_manager.pb-c.h"

#define MAX_STRLEN 256

/**
 * @brief Duplicates a string and returns true if successful
 *
 * wrapper around string duplication when the source string might be
 * a null pointer.
 *
 * @param src source string to duplicate. Might be NULL.
 * @param dst destination string pointer
 * @return true if duplicated, false otherwise
 */
static bool
oms_report_str_duplicate(char *src, char **dst)
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


/**
 * @brief Allocates and sets an observation point protobuf.
 *
 * Uses the node info to fill a dynamically allocated
 * observation point protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see oms_free_pb_op() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to a observation point protobuf structure
 */
static ObjectManager__Status__ObservationPoint *
oms_report_set_node_info(void)
{
    ObjectManager__Status__ObservationPoint *pb;
    struct oms_mgr *mgr;
    bool ret;

    mgr = oms_get_mgr();

    /* Allocate the protobuf structure */
    pb = calloc(1, sizeof(*pb));
    if (pb == NULL)
    {
        LOGE("%s: ObservationPoint protobuf struct allocation"
             " failed", __func__);
        return NULL;
    }

    /* Initialize the protobuf structure */
    object_manager__status__observation_point__init(pb);

    /* Set the protobuf fields */
    ret = oms_report_str_duplicate(mgr->node_id, &pb->nodeid);
    LOGI("%s: nodeid set to %s", __func__, pb->nodeid);
    if (!ret) goto err_free_pb;

    ret = oms_report_str_duplicate(mgr->location_id, &pb->locationid);
    if (!ret) goto err_free_node_id;

    return pb;

err_free_node_id:
    free(pb->nodeid);

err_free_pb:
    free(pb);

    return NULL;
}


/**
 * @brief Frees an observation point protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb observation point structure to free
 * @return none
 */
static void
oms_report_free_pb_op(ObjectManager__Status__ObservationPoint *pb)
{
    if (pb == NULL) return;

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
struct packed_buffer *
oms_report_serialize_node_info(void)
{
    ObjectManager__Status__ObservationPoint *pb;
    struct packed_buffer *serialized;
    size_t len;
    void *buf;

    /* Allocate serialization output container */
    serialized = calloc(1, sizeof(*serialized));
    if (!serialized) return NULL;

    /* Allocate and set observation point protobuf */
    pb = oms_report_set_node_info();
    if (pb == NULL) goto err_free_serialized;

    /* Get serialization length */
    len = object_manager__status__observation_point__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (buf == NULL) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = object_manager__status__observation_point__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    oms_report_free_pb_op(pb);

    return serialized;

err_free_pb:
    oms_report_free_pb_op(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets an object status report protobuf.
 *
 * Uses the state info to fill a dynamically allocated protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see oms_report_free_pb_report_status() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a object status protobuf structure
 */
static ObjectManager__Status__ObjectStatus *
oms_report_set_report_status(struct oms_state_entry *state)
{
    ObjectManager__Status__ObjectStatus *pb;
    struct oms_mgr *mgr;
    bool report;
    bool ret;

    mgr = oms_get_mgr();

    /* Check if this entry is to be reported */
    report = true;
    if (mgr->report_cb) report = mgr->report_cb(state);
    if (!report) return NULL;

    /* Allocate the protobuf structure */
    pb = calloc(1, sizeof(*pb));
    if (pb == NULL)
    {
        LOGE("%s: memory allocation failure", __func__);
        return NULL;
    }

    /* Initialize the protobuf structure */
    object_manager__status__object_status__init(pb);

    ret = oms_report_str_duplicate(state->object, &pb->objectname);
    if (!ret) goto err_free_pb;

    ret = oms_report_str_duplicate(state->state, &pb->status);
    if (!ret) goto err_free_object;

    ret = oms_report_str_duplicate(state->version, &pb->version);
    if (!ret) goto err_free_state;
    return pb;

err_free_state:
    free(pb->status);

err_free_object:
    free(pb->objectname);

err_free_pb:
    free(pb);

    return NULL;
}


/**
 * @brief Free a object status protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow stats structure to free
 * @return none
 */
void
oms_report_free_pb_report_status(ObjectManager__Status__ObjectStatus *pb)
{
    if (pb == NULL) return;

    free(pb->objectname);
    free(pb->status);
    free(pb->version);

    free(pb);
}


/**
 * @brief Generate an object status serialized protobuf.
 *
 * Uses the information pointed by the state parameter to generate
 * a serialized object status report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see oms_report_free_packed_buffer() for this purpose.
 *
 * @param state info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
oms_report_serialize_status(struct oms_state_entry *state)
{
    ObjectManager__Status__ObjectStatus *pb;
    struct packed_buffer *serialized;
    size_t len;
    void *buf;

    if (state == NULL) return NULL;

    /* Allocate serialization output container */
    serialized = calloc(1, sizeof(*serialized));
    if (!serialized) return NULL;

    /* Allocate and set the i container */
    pb = oms_report_set_report_status(state);
    if (pb == NULL) goto err_free_serialized;

    /* get serialization length */
    len = object_manager__status__object_status__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (!buf) goto err_free_pb;

    /* Serialize protobuf */
    serialized->len = object_manager__status__object_status__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    oms_report_free_pb_report_status(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    oms_report_free_pb_report_status(pb);

err_free_serialized:
    free(serialized);

    return NULL;
}


/**
 * @brief Allocates and sets table of object status protobufs
 *
 * Uses the window info to fill a dynamically allocated
 * table of object status protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @return a flow stats protobuf pointers table
 */
ObjectManager__Status__ObjectStatus **
oms_report_set_object_status(void)
{
    ObjectManager__Status__ObjectStatus **status_pb_tbl;
    ObjectManager__Status__ObjectStatus **status_pb;
    struct oms_state_entry *state;
    struct oms_mgr *mgr;
    size_t allocated;
    ds_tree_t *tree;
    size_t n_nodes;
    size_t i;

    mgr = oms_get_mgr();
    n_nodes = mgr->num_states;
    if (n_nodes == 0) return NULL;
    tree = &mgr->state;

    /* Allocate the array of interfaces */
    status_pb_tbl = calloc(mgr->num_states, sizeof(*status_pb_tbl));
    if (status_pb_tbl == NULL)
    {
        LOGE("%s: allocation failure", __func__);
        return NULL;
    }

    allocated = 0;
    state = ds_tree_head(tree);
    status_pb = status_pb_tbl;
    while (state && allocated < n_nodes)
    {
        *status_pb =  oms_report_set_report_status(state);
        if (*status_pb != NULL)
        {
            status_pb++;
            allocated++;
        }
        state = ds_tree_next(tree, state);
    }

    if (allocated != 0)
    {
        mgr->num_reports = allocated;
        return status_pb_tbl;
    }

err_free_pb_status_tbl:
    status_pb = status_pb_tbl;
    for (i = 0; i < allocated; i++)
    {
        oms_report_free_pb_report_status(*status_pb);
        status_pb++;
    }

    free(status_pb_tbl);

    return NULL;
}


/**
 * @brief Allocates and sets a object manager status report protobuf.
 *
 * The caller is responsible for freeing the returned pointer,
 * @see oms_report_free_pb_report() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static ObjectManager__Status__ObjectStatusReport *
oms_report_set_pb_report(void)
{
    ObjectManager__Status__ObjectStatusReport *pb;
    struct oms_mgr *mgr;

    mgr = oms_get_mgr();

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL)
    {
        LOGE("%s: allocation failure", __func__);
        return NULL;
    }

    /* Initialize protobuf */
    object_manager__status__object_status_report__init(pb);

    /* Set protobuf fields */
    pb->reportedat = time(NULL);
    pb->has_reportedat = true;

    pb->observationpoint = oms_report_set_node_info();
    if (!pb->observationpoint) goto err_free_pb_report;

    pb->objectstatus = oms_report_set_object_status();
    if (!pb->objectstatus) goto err_free_pb_os;
    pb->n_objectstatus = mgr->num_reports;

    return pb;

err_free_pb_os:
    oms_report_free_pb_op(pb->observationpoint);

err_free_pb_report:
    free(pb);

    return NULL;
}


/**
 * @brief Free an object status report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void
oms_report_free_pb_report(ObjectManager__Status__ObjectStatusReport *pb)
{
    size_t i;

    if (pb == NULL) return;

    oms_report_free_pb_op(pb->observationpoint);

    for (i = 0; i < pb->n_objectstatus; i++)
    {
        oms_report_free_pb_report_status(pb->objectstatus[i]);
    }

    free(pb->objectstatus);
    free(pb);

    return;
}


/**
 * @brief Generates a flow report serialized protobuf
 *
 * Uses the information pointed by the report parameter to generate
 * a serialized flow report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see oms_report_free_packed_buffer() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct packed_buffer *
oms_report_serialize_report(void)
{
    ObjectManager__Status__ObjectStatusReport *pb;
    struct packed_buffer *serialized;
    struct oms_mgr *mgr;
    size_t len;
    void *buf;

    mgr = oms_get_mgr();
    mgr->num_reports = 0;

    /* Allocate serialization output structure */
    serialized = calloc(1,sizeof(*serialized));
    if (serialized == NULL)
    {
        LOGE("%s: packed_buffer memory allocation failed", __func__);
        return NULL;
    }

    /* Allocate and set the object status report protobuf */
    pb = oms_report_set_pb_report();
    if (pb == NULL) goto err_free_serialized;

    /* Get serialized length */
    len = object_manager__status__object_status_report__get_packed_size(pb);
    if (len == 0)
    {
        LOGE("%s: Failed to get serialized report len", __func__);
        goto err_free_pb;
    }

    /* Allocate space for the serialized buffer */
    buf = malloc(len);
    if (buf == NULL)
    {
        LOGE("%s: failed to allocate serialized buf", __func__);
        goto err_free_pb;
    }

    len = object_manager__status__object_status_report__pack(pb, buf);
    serialized->len = len;
    serialized->buf = buf;

    /* Free the protobuf structure */
    oms_report_free_pb_report(pb);

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
oms_report_free_packed_buffer(struct packed_buffer *pb)
{
    if (pb == NULL) return;

    free(pb->buf);
    free(pb);
}


/**
 * @brief Prepares the serialized object status report and sends it over mqtt
 *
 * Converts the object status report information into serialized content, and
 * sends it over MQTT.
 *
 * @param mqtt_topic a pointer to the mqtt topic
 * @return result of mqtt send
 */
bool
oms_report_send_report(char *mqtt_topic)
{
    struct packed_buffer *pb;
    qm_response_t res;
    bool ret;

    if (mqtt_topic == NULL)
    {
        LOGE("%s: MQTT topic is NULL", __func__);
        return false;
    }

    pb = oms_report_serialize_report();
    if (pb == NULL)
    {
        LOGE("%s: report serialization failed", __func__);
        return false;
    }

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, mqtt_topic,
                              pb->buf, pb->len, &res);

    /* Free the serialized container */
    oms_report_free_packed_buffer(pb);

    return ret;
}
