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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ds_dlist.h"
#include "ds_tree.h"
#include "fsm.h"
#include "log.h"
#include "mdns_records.h"
#include "memutil.h"

/*****************************************************************************
 * Observation Point(Node Info)
 ****************************************************************************/

/**
 * @brief Allocates and sets an observation point protobuf.
 *
 * Uses the node info to fill a dynamically allocated
 * observation point protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see mdns_records_free_pb_op() for this purpose.
 *
 * @param node info used to fill up the protobuf.
 * @return a pointer to a observation point protobuf structure
 */
static Interfaces__MdnsRecordsTelemetry__ObservationPoint *
mdns_records_set_node_info(node_info_t *node)
{
    Interfaces__MdnsRecordsTelemetry__ObservationPoint *pb = NULL;
    bool                                                ret;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(Interfaces__MdnsRecordsTelemetry__ObservationPoint));

    /* Initialize the protobuf structure */
    interfaces__mdns_records_telemetry__observation_point__init(pb);

    /* Set the protobuf fields */
    ret = mdns_records_str_duplicate(node->node_id, &pb->node_id);
    if (!ret) goto err_free_pb;

    ret = mdns_records_str_duplicate(node->location_id, &pb->location_id);
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
mdns_records_free_pb_op(Interfaces__MdnsRecordsTelemetry__ObservationPoint *pb)
{
    if (!pb) return;

    FREE(pb->node_id);
    FREE(pb->location_id);

    FREE(pb);
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
mdns_records_serialize_node_info(node_info_t *node)
{
    Interfaces__MdnsRecordsTelemetry__ObservationPoint *pb;
    packed_buffer_t                                    *serialized;
    void                                               *buf;
    size_t                                              len;

    if (!node) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set observation point protobuf */
    pb = mdns_records_set_node_info(node);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = interfaces__mdns_records_telemetry__observation_point__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__mdns_records_telemetry__observation_point__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    mdns_records_free_pb_op(pb);

    return serialized;

err_free_pb:
    mdns_records_free_pb_op(pb);

err_free_serialized:
    FREE(serialized);

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
 * @see mdns_records_free_pb_window() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a observation point protobuf structure
 */
static Interfaces__MdnsRecordsTelemetry__ObservationWindow *
mdns_records_set_pb_window(observation_window_t *window)
{
    Interfaces__MdnsRecordsTelemetry__ObservationWindow *pb = NULL;

    if (!window) return NULL;

    /* Don't accept empty observation window */
    if ((window->started_at == 0) || (window->ended_at == 0)) return NULL;

    /* Allocate protobuf */
    pb = CALLOC(1, sizeof(Interfaces__MdnsRecordsTelemetry__ObservationWindow));

    /* Initialize the observation window */
    interfaces__mdns_records_telemetry__observation_window__init(pb);

    /* set observation window fields */
    pb->started_at     = window->started_at;
    pb->ended_at       = window->ended_at;

    pb->has_started_at = true;
    pb->has_ended_at   = true;

    return pb;
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
mdns_records_free_pb_window(Interfaces__MdnsRecordsTelemetry__ObservationWindow *pb)
{
    if (!pb) return;

    FREE(pb);
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
mdns_records_serialize_window(observation_window_t *window)
{
    Interfaces__MdnsRecordsTelemetry__ObservationWindow *pb;
    packed_buffer_t                                     *serialized;
    void                                                *buf;
    size_t                                               len;

    if (!window) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the protobuf */
    pb = mdns_records_set_pb_window(window);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = interfaces__mdns_records_telemetry__observation_window__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialized protobuf */
    serialized->len = interfaces__mdns_records_telemetry__observation_window__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    mdns_records_free_pb_window(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    mdns_records_free_pb_window(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/*****************************************************************************
 * Mdns Records
 ****************************************************************************/

/**
 * @brief Allocates and sets a mdns record protobuf.
 *
 * Uses the mdns_record_t info to fill a dynamically allocated
 * mdns record protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see mdns_record_free_pb_mdns_record() for this purpose.
 *
 * @param mdns record info used to fill up the protobuf
 * @return a pointer to a mdns record protobuf structure
 */
static Interfaces__MdnsRecordsTelemetry__MdnsRecord *
mdns_records_set_pb_record(mdns_records_t *rec)
{
    Interfaces__MdnsRecordsTelemetry__MdnsRecord *pb  = NULL;
    struct resource                              *res = NULL;
    bool                                          ret = false;

    if (!rec) return NULL;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(Interfaces__MdnsRecordsTelemetry__MdnsRecord));

    /* Initialize the protobuf structure */
    interfaces__mdns_records_telemetry__mdns_record__init(pb);

    res = &rec->resource;

    /* Set the protobuf fields */
    ret = mdns_records_str_duplicate(res->name, &pb->owner_name);
    if (!ret) goto err_free_pb;

    switch (res->type)
    {
        case QTYPE_A:
        {
            ret = mdns_records_str_duplicate(res->known.a.name, &pb->domain_name);
            if (!ret) goto err_free_owner_name;

            /* The IP stored in res->known.a.ip is in host-byte order. Convert it to
               network-byte order so that it is represented properly on all platforms */
            res->known.a.ip.s_addr = htonl(res->known.a.ip.s_addr);

            ret = mdns_records_str_duplicate(inet_ntoa(res->known.a.ip), &pb->ip);
            if (!ret) goto err_free_domain_name;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_A;
            pb->has_type = true;

            break;
        }

        case QTYPE_NS:
        {
            ret = mdns_records_str_duplicate(res->known.ns.name, &pb->domain_name);
            if (!ret) goto err_free_owner_name;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_NS;
            pb->has_type = true;

            break;
        }

        case QTYPE_CNAME:
        {
            ret = mdns_records_str_duplicate(res->known.cname.name, &pb->domain_name);
            if (!ret) goto err_free_owner_name;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_CNAME;
            pb->has_type = true;

            break;
        }

        case QTYPE_PTR:
        {
            ret = mdns_records_str_duplicate(res->known.ptr.name, &pb->domain_name);
            if (!ret) goto err_free_owner_name;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_PTR;
            pb->has_type = true;

            break;
        }

        case QTYPE_TXT:
        {
            if (!res->rdlength)     break;

            pb->res_desc.data = MALLOC(res->rdlength);

            memcpy(pb->res_desc.data, res->rdata, res->rdlength);
            pb->res_desc.len = res->rdlength;
            pb->has_res_desc = true;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_TXT;
            pb->has_type = true;

            break;
        }

        case QTYPE_SRV:
        {
            ret = mdns_records_str_duplicate(res->known.srv.name, &pb->domain_name);
            if (!ret) goto err_free_owner_name;

            pb->type     = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_SRV;
            pb->has_type = true;

            pb->priority     = res->known.srv.priority;
            pb->has_priority = true;

            pb->weight       = res->known.srv.weight;
            pb->has_weight   = true;

            pb->port         = res->known.srv.port;
            pb->has_port     = true;

            break;
        }

        default:
        {
            LOGE("%s: unspecified resource record of type '%d'", __func__, res->type);
            goto err_free_owner_name;

            break;
        }

    }

    return pb;

err_free_domain_name:
    FREE(pb->domain_name);

err_free_owner_name:
    FREE(pb->owner_name);

err_free_pb:
    FREE(pb);

    return NULL;
}

/**
 * @brief Free a mdns record protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb mdns record structure to free
 * @return none
 */
void
mdns_records_free_pb_record(Interfaces__MdnsRecordsTelemetry__MdnsRecord *pb)
{
    if (!pb) return;

    FREE(pb->owner_name);

    if (pb->domain_name)    FREE(pb->domain_name);
    if (pb->res_desc.data)  FREE(pb->res_desc.data);
    if (pb->ip)             FREE(pb->ip);

    FREE(pb);
}

/**
 * @brief Generates a mdns record serialized protobuf.
 *
 * Uses the information pointed by the record parameter to generate
 * a serialized mdsn record buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see intf_stats_free_packed_buffer() for this purpose.
 *
 * @param record info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
mdns_records_serialize_record(mdns_records_t *rec)
{
    Interfaces__MdnsRecordsTelemetry__MdnsRecord *pb;
    packed_buffer_t                              *serialized;
    void                                         *buf;
    size_t                                        len;

    if (!rec) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the mdns record container */
    pb = mdns_records_set_pb_record(rec);
    if (!pb) goto err_free_serialized;

    /* get serialization length */
    len = interfaces__mdns_records_telemetry__mdns_record__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__mdns_records_telemetry__mdns_record__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    mdns_records_free_pb_record(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    mdns_records_free_pb_record(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets table of mdns record protobufs
 *
 * Uses the client info to fill a dynamically allocated
 * table of mdns records protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param client info used to fill up the protobuf table
 * @return a mdns records protobuf pointers table
 */
Interfaces__MdnsRecordsTelemetry__MdnsRecord **
mdns_records_set_pb_mdns_records(mdns_client_t *client)
{
    Interfaces__MdnsRecordsTelemetry__MdnsRecord **records_pb_tbl = NULL;
    mdns_records_list_t *records_list = &client->records_list;
    mdns_records_t *rec = NULL;
    ds_dlist_iter_t rec_iter;
    size_t allocated = 0;
    size_t i;

    if (client->num_records == 0) return NULL;

    /* Allocate the mdns records table */
    records_pb_tbl = CALLOC(client->num_records, sizeof(Interfaces__MdnsRecordsTelemetry__MdnsRecord *));

    for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
          rec != NULL;
          rec = ds_dlist_inext(&rec_iter))
    {
        records_pb_tbl[allocated] = mdns_records_set_pb_record(rec);
        if (!records_pb_tbl[allocated]) goto err_free_pb_records;

        allocated++;
    }

    return records_pb_tbl;

err_free_pb_records:
    for (i = 0; i < allocated; i++)
    {
        mdns_records_free_pb_record(records_pb_tbl[i]);
    }

    FREE(records_pb_tbl);

    return NULL;
}


/*****************************************************************************
 * Mdns Client
 ****************************************************************************/

/**
 * @brief Allocates and sets an client protobuf.
 *
 * Uses the client info to fill a dynamically allocated
 * client protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see mdns_records_free_pb_client() for this purpose.
 *
 * @param mdns_client_t used to fill up the protobuf
 * @return a pointer to a client protobuf structure
 */
static Interfaces__MdnsRecordsTelemetry__MdnsClient *
mdns_records_set_pb_client(mdns_client_t *client)
{
    Interfaces__MdnsRecordsTelemetry__MdnsClient *pb = NULL;
    bool                                         ret = false;

    if (!client) return NULL;

    /* Allocate protobuf */
    pb = CALLOC(1, sizeof(Interfaces__MdnsRecordsTelemetry__MdnsClient));

    /* Initialize the client */
    interfaces__mdns_records_telemetry__mdns_client__init(pb);

    /* Set client fields */
    ret = mdns_records_str_duplicate(client->mac_str, &pb->mac);
    if (!ret) goto err_free_pb;

    ret = mdns_records_str_duplicate(client->ip_str, &pb->ip);
    if (!ret) goto err_free_mac;

    pb->n_mdns_records = client->num_records;

    if (client->num_records == 0) return pb;

    /* Allocate mdns records container */
    pb->mdns_records = mdns_records_set_pb_mdns_records(client);
    if (!pb->mdns_records) goto err_free_ip;

    return pb;

err_free_ip:
    FREE(pb->ip);

err_free_mac:
    FREE(pb->mac);

err_free_pb:
    FREE(pb);

    return NULL;
}

/**
 * @brief Free a client protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb client structure to free
 * @return none
 */
void
mdns_records_free_pb_client(Interfaces__MdnsRecordsTelemetry__MdnsClient *pb)
{
    size_t  i;

    if (!pb) return;

    for (i = 0; i < pb->n_mdns_records; i++)
    {
        mdns_records_free_pb_record(pb->mdns_records[i]);
    }

    FREE(pb->mdns_records);
    FREE(pb->mac);
    FREE(pb->ip);

    FREE(pb);
}

/**
 * @brief Generates a client serialized protobuf
 *
 * Uses the information pointed by the client parameter to generate
 * a serialized client buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see free_packed_buffer() for this purpose.
 *
 * @param mdns client info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
mdns_records_serialize_client(mdns_client_t *client)
{
    Interfaces__MdnsRecordsTelemetry__MdnsClient *pb;
    packed_buffer_t                              *serialized;
    void                                         *buf;
    size_t                                        len;

    if (!client) return NULL;

    /* Allocate serialization output container */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the client protobuf */
    pb = mdns_records_set_pb_client(client);
    if (!pb) goto err_free_serialized;

    /* Get serialization length */
    len = interfaces__mdns_records_telemetry__mdns_client__get_packed_size(pb);
    if (len == 0) goto err_free_pb;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    /* Serialize protobuf */
    serialized->len = interfaces__mdns_records_telemetry__mdns_client__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    mdns_records_free_pb_client(pb);

    /* Return the serialized content */
    return serialized;

err_free_pb:
    mdns_records_free_pb_client(pb);

err_free_serialized:
    FREE(serialized);

    return NULL;
}

/**
 * @brief Allocates and sets table of mdns clients protobufs
 *
 * Uses the report info to fill a dynamically allocated
 * table of clients protobufs.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a clients protobuf pointers table
 */
Interfaces__MdnsRecordsTelemetry__MdnsClient **
mdns_records_set_pb_clients(mdns_records_report_data_t *report)
{
    Interfaces__MdnsRecordsTelemetry__MdnsClient **clients_pb_tbl;
    ds_tree_t *clients = &report->staged_clients;
    mdns_client_t *client = NULL;
    size_t allocated = 0;
    size_t num_clients;
    size_t i;

    if (!report) return NULL;

    num_clients = mdns_records_get_num_clients(clients);
    if (num_clients == 0) return NULL;

    clients_pb_tbl = CALLOC(num_clients, sizeof(Interfaces__MdnsRecordsTelemetry__MdnsClient *));

    ds_tree_foreach(clients, client)
    {
        clients_pb_tbl[allocated] = mdns_records_set_pb_client(client);
        if (!clients_pb_tbl[allocated]) goto err_free_pb_clients;

        allocated++;
    }

    return clients_pb_tbl;

err_free_pb_clients:
    for (i = 0; i < allocated; i++)
    {
        mdns_records_free_pb_client(clients_pb_tbl[i]);
    }

    FREE(clients_pb_tbl);

    return NULL;
}

/*****************************************************************************
 * Mdns Records Report
 ****************************************************************************/

/**
 * @brief Allocates and sets a mdns records report protobuf.
 *
 * Uses the clients tree info to fill a dynamically allocated
 * mdns records report protobuf.
 * The caller is responsible for freeing the returned pointer,
 * @see mdns_records_free_pb_report() for this purpose.
 *
 * @param node info used to fill up the protobuf
 * @return a pointer to a MdnsRecordReport protobuf structure
 */
static Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *
mdns_records_set_pb_report(mdns_records_report_data_t *report)
{
    Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *pb = NULL;

    pb = CALLOC(1, sizeof(Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport));

    /* Initialize protobuf */
    interfaces__mdns_records_telemetry__mdns_records_report__init(pb);

    /* Set observation point */
    pb->observation_point = mdns_records_set_node_info(&report->node_info);
    if (!pb->observation_point)
    {
        LOGE("%s: set observation_point failed", __func__);
        goto err_free_pb_report;
    }

    /* Set observation window */
    pb->observation_window = mdns_records_set_pb_window(&report->obs_w);
    if (!pb->observation_window)
    {
        LOGE("%s: set observation_window failed", __func__);
        goto err_free_pb_op;
    }

    /* Allocate the clients container */
    pb->clients = mdns_records_set_pb_clients(report);
    if (!pb->clients)
    {
        LOGE("%s: clients container allocation failed", __func__);
        goto err_free_pb_ow;
    }

    pb->n_clients = mdns_records_get_num_clients(&report->staged_clients);

    return pb;

err_free_pb_ow:
    mdns_records_free_pb_window(pb->observation_window);

err_free_pb_op:
    mdns_records_free_pb_op(pb->observation_point);

err_free_pb_report:
    FREE(pb);

    return NULL;
}

/**
 * @brief Free a mdns record report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb mdns record report structure to free
 * @return none
 */
static void
mdns_records_free_pb_report(Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *pb)
{
    size_t  i;

    if (!pb) return;

    mdns_records_free_pb_op(pb->observation_point);
    mdns_records_free_pb_window(pb->observation_window);

    for (i = 0; i < pb->n_clients; i++)
    {
        mdns_records_free_pb_client(pb->clients[i]);
    }

    FREE(pb->clients);
    FREE(pb);
}

/*****************************************************************************
 * Report serialization
 ****************************************************************************/

/**
 * @brief Generates a mdns records report serialized protobuf
 *
 * Uses the information pointed by the clients parameter to generate
 * a serialized mdns records report buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see mdns_records_free_packed_buffer() for this purpose.
 *
 * @param report data used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
packed_buffer_t *
mdns_records_serialize_report(mdns_records_report_data_t *report)
{
    Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *pb          = NULL;
    packed_buffer_t                                     *serialized  = NULL;
    void                                                *buf;
    size_t                                               len;

    if (!report)
    {
        LOGE("%s: Mdns records report is NULL", __func__);
        return NULL;
    }

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(packed_buffer_t));

    /* Allocate and set the MdnsRecordsReport protobuf */
    pb = mdns_records_set_pb_report(report);
    if (!pb)
    {
        LOGE("%s: set_pb_report failed", __func__);
        goto err_free_serialized;
    }

    /* Get serialized length */
    len = interfaces__mdns_records_telemetry__mdns_records_report__get_packed_size(pb);
    if (len == 0)
    {
        LOGE("%s: failed to get serialized report len", __func__);
        goto err_free_pb;
    }

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = interfaces__mdns_records_telemetry__mdns_records_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    mdns_records_free_pb_report(pb);

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
mdns_records_free_packed_buffer(packed_buffer_t *pb)
{
    if (!pb) return;

    if (pb->buf) FREE(pb->buf);

    FREE(pb);
}

/**
 * @brief Prepares the serialized mdns records report and sends it over mqtt
 *
 * Converts the mdns records report information into serialized content, and
 * sends it over MQTT.
 *
 * @param  report, a pointer to the mdns records report structure
           mqtt_topic a pointer to the mqtt topic
 * @return result of mqtt send
 */
bool
mdns_records_send_report(mdns_records_report_data_t *report, struct fsm_session *session)
{
    packed_buffer_t *pb;

    if (!report)
    {
        LOGE("%s: mDNS records report is NULL", __func__);
        return false;
    }

    if (!session->topic)
    {
        LOGE("%s: MQTT topic is NULL", __func__);
        return false;
    }

    pb = mdns_records_serialize_report(report);
    if (!pb)
    {
        LOGE("%s: mDNS records report serialization failed", __func__);
        return false;
    }

    session->ops.send_pb_report(session, session->topic, pb->buf, pb->len);

    mdns_records_free_packed_buffer(pb);

    return true;
}
