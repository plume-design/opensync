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

#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "adv_data_typing.pb-c.h"
#include "fsm.h"
#include "fsm_dpi_adt.h"
#include "fsm_dpi_client_plugin.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_time.h"
#include "os_types.h"
#include "qm_conn.h"
#include "util.h"

void
dpi_adt_free_datapoint_pb(Interfaces__Adt__AdtDataPoint *data_pb)
{
    Interfaces__Adt__AdtAttrValue *one_value;
    Interfaces__Adt__AdtAttrKey *one_key;
    Interfaces__Adt__AdtKVPair *one_kv;
    size_t i;

    FREE(data_pb->device_id.data);

    FREE(data_pb->ipv4_tuple);
    if (data_pb->ipv6_tuple)
    {
        FREE(data_pb->ipv6_tuple->source_ipv6.data);
        FREE(data_pb->ipv6_tuple->destination_ipv6.data);
    }
    FREE(data_pb->ipv6_tuple);

    for (i = 0; i < data_pb->n_kv_pair; i++)
    {
        one_kv = data_pb->kv_pair[i];

        one_key = one_kv->key;
        if (one_key)
        {
            FREE(one_key->adt_key);
        }
        FREE(one_key);

        one_value = one_kv->value;
        if (one_value)
        {
            FREE(one_value->string_value);
        }
        FREE(one_value);

        FREE(one_kv);
    }
    FREE(data_pb->kv_pair);
    FREE(data_pb->network_zone);
}

void
dpi_adt_free_report_pb(Interfaces__Adt__AdtReport *report_pb)
{
    Interfaces__Adt__AdtObservationPoint *one_obs_point;
    Interfaces__Adt__AdtDataPoint *one_record;
    size_t i;

    for (i = 0; i < report_pb->n_data; i++)
    {
        one_record = report_pb->data[i];
        if (one_record)
        {
            dpi_adt_free_datapoint_pb(one_record);
            FREE(one_record);
        }
    }
    FREE(report_pb->data);

    one_obs_point = report_pb->observation_point;
    if (one_obs_point)
    {
        FREE(one_obs_point->location_id);
        FREE(one_obs_point->node_id);
        FREE(one_obs_point);
    }

    /* Do not free the report_pb itself as we don't own it */
}

void
dpi_adt_free_aggr_store_record(struct fsm_dpi_adt_data_record *rec)
{
    if (rec == NULL) return;

    FREE(rec->key);
    FREE(rec->value);
    FREE(rec->network_id);
}

void
dpi_adt_free_aggr_store(struct fsm_dpi_adt_report_aggregator *aggr)
{
    size_t i;

    for (i = 0; i < aggr->data_idx; i++)
    {
        dpi_adt_free_aggr_store_record(aggr->data[i]);
        FREE(aggr->data[i]);
    }
    aggr->data_idx = 0;
}

/**
 * @brief Add a <attr, value> pair to the list of data to report
 *
 * @remark We are not managing the size of the receiving array here
 * @remark We are not trying any sort of de-duplication at this time !
 * @remark We serialize values of type RTS_TYPE_NUMBER and RTS_TYPE_BINARY
 * @remark Parameters must be validated prior to calling the function.
 */
bool
dpi_adt_store(struct fsm_session *session,
              const char *attr,
              uint8_t type, uint16_t length,
              const void *value,
              struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_data_record *new_record;
    struct fsm_dpi_adt_session *adt_session;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_info *info;
    char value_str[1024];
    char *network_id;
    size_t sz;
    bool rc;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (client_session == NULL)
    {
        LOGD("%s: client_session is NULL", __func__);
        return false;
    }
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    if (adt_session == NULL)
    {
        LOGD("%s: adt_session is NULL", __func__);
        return false;
    }
    aggr = adt_session->adt_aggr;
    if (aggr == NULL || !aggr->initialized)
    {
        LOGD("%s: adt_aggr not initialized", __func__);
        return false;
    }

    if (aggr->data_idx == aggr->data_max)
    {
        LOGD("%s(): Too many stored records.", __func__);
        return false;
    }

    if (pkt_info == NULL) return false;

    acc = pkt_info->acc;
    if (acc == NULL) return false;

    new_record = CALLOC(1, sizeof(*new_record));
    if (new_record == NULL)
    {
        LOGT("%s: Failed to allocate memory", __func__);
        goto cleanup;
    }

    info = &new_record->info;

    rc = net_md_get_flow_info(acc, info);
    if (!rc)
    {
        LOGT("%s: No flow information", __func__);
        goto cleanup;
    }

    /* Only store valid values */
    if (info->local_mac == NULL)
    {
        LOGT("%s: No MAC", __func__);
        goto cleanup;
    }

    new_record->transport = acc->key->ipprotocol;

    new_record->key = STRDUP(attr);
    if (new_record->key == NULL) goto cleanup;

    /* Need to perform the correct conversion for the value */
    switch (type)
    {
        case RTS_TYPE_STRING:
            STRSCPY_LEN(value_str, value, length);
            new_record->value = STRDUP(value_str);
            if (new_record->value == NULL) goto cleanup;
            break;
        case RTS_TYPE_NUMBER:
            /* NUMBER is int64_t */
            sz = 3 * sizeof(int64_t) + 2; /* quick math to fetch longest possible string */
            new_record->value = CALLOC(1, sz);
            if (new_record->value == NULL) goto cleanup;
            snprintf(new_record->value, sz, "%" PRId64, *(int64_t *)value);
            break;
        case RTS_TYPE_BINARY:
            sz = 2 * length + 1;
            new_record->value = CALLOC(1, sz);
            if (new_record->value == NULL) goto cleanup;
            (void)bin2hex(value, length, new_record->value, sz);
            break;
        default:
            goto cleanup;
    }
    new_record->value_len = strlen(new_record->value);

    new_record->capture_time_ms = clock_real_ms();

    network_id = session->ops.get_network_id(session, info->local_mac);
    new_record->network_id = (network_id) ? STRDUP(network_id) : STRDUP("unknown");

    /* The new stored record is complete */
    aggr->data[aggr->data_idx] = new_record;
    aggr->data_idx++;

    return true;

cleanup:
    LOGT("%s: Cannot store record", __func__);
    if (new_record)
    {
        FREE(new_record->value);
        FREE(new_record->key);
    }
    FREE(new_record);
    return false;
}

Interfaces__Adt__AdtKVPair *
dpi_adt_populate_kv(struct fsm_dpi_adt_data_record *rec)
{
    Interfaces__Adt__AdtAttrValue *one_value_pb = NULL;
    Interfaces__Adt__AdtKVPair *one_kv_pair_pb = NULL;
    Interfaces__Adt__AdtAttrKey *one_key_pb = NULL;

    one_kv_pair_pb = MALLOC(sizeof(*one_kv_pair_pb));
    if (one_kv_pair_pb == NULL) goto cleanup;
    interfaces__adt__adt_kvpair__init(one_kv_pair_pb);

    one_key_pb = MALLOC(sizeof(*one_key_pb));
    if (one_key_pb == NULL) goto cleanup;
    interfaces__adt__adt_attr_key__init(one_key_pb);
    one_key_pb->adt_key = STRDUP(rec->key);
    if (one_key_pb->adt_key == NULL) goto cleanup;
    one_kv_pair_pb->key = one_key_pb;

    one_value_pb = MALLOC(sizeof(*one_value_pb));
    if (one_value_pb == NULL) goto cleanup;
    interfaces__adt__adt_attr_value__init(one_value_pb);
    one_value_pb->string_value = STRDUP(rec->value);
    if (one_value_pb->string_value == NULL) goto cleanup;
    one_kv_pair_pb->value = one_value_pb;

    one_kv_pair_pb->captured_at_ms = rec->capture_time_ms;

    return one_kv_pair_pb;

cleanup:
    if (one_value_pb) FREE(one_value_pb->string_value);
    FREE(one_value_pb);
    if (one_key_pb) FREE(one_key_pb->adt_key);
    FREE(one_key_pb);
    FREE(one_kv_pair_pb);
    return NULL;
}

Interfaces__Adt__AdtIpv4Tuple *
dpi_adt_populate_ipv4(struct fsm_dpi_adt_data_record *rec)
{
    Interfaces__Adt__AdtIpv4Tuple *ipv4_pb;
    struct net_md_flow_info *info;

    info = &rec->info;

    ipv4_pb = MALLOC(sizeof(*ipv4_pb));
    if (ipv4_pb == NULL) return NULL;
    interfaces__adt__adt_ipv4_tuple__init(ipv4_pb);

    ipv4_pb->transport = rec->transport;

    /* Cast messes up byte order */
    ipv4_pb->source_ipv4 = htonl(*((uint32_t *)info->local_ip));
    ipv4_pb->source_port = info->local_port;

    ipv4_pb->destination_ipv4 = htonl(*((uint32_t *)info->remote_ip));
    ipv4_pb->destination_port = info->remote_port;

    return ipv4_pb;
}

Interfaces__Adt__AdtIpv6Tuple *
dpi_adt_populate_ipv6(struct fsm_dpi_adt_data_record *rec)
{
    Interfaces__Adt__AdtIpv6Tuple *ipv6_pb;
    struct net_md_flow_info *info;

    info = &rec->info;

    ipv6_pb = MALLOC(sizeof(*ipv6_pb));
    if (ipv6_pb == NULL) return NULL;
    interfaces__adt__adt_ipv6_tuple__init(ipv6_pb);

    ipv6_pb->transport = rec->transport;

    ipv6_pb->source_ipv6.len = sizeof(struct in6_addr);
    ipv6_pb->source_ipv6.data = CALLOC(ipv6_pb->source_ipv6.len, sizeof(uint8_t));
    if (ipv6_pb->source_ipv6.data == NULL) goto cleanup;
    memcpy(ipv6_pb->source_ipv6.data, info->local_ip, ipv6_pb->source_ipv6.len);
    ipv6_pb->source_port = info->local_port;

    ipv6_pb->destination_ipv6.len = sizeof(struct in6_addr);
    ipv6_pb->destination_ipv6.data = CALLOC(ipv6_pb->destination_ipv6.len, sizeof(uint8_t));
    if (ipv6_pb->destination_ipv6.data == NULL) goto cleanup;
    memcpy(ipv6_pb->destination_ipv6.data, info->remote_ip, ipv6_pb->destination_ipv6.len);
    ipv6_pb->destination_port = info->remote_port;

    return ipv6_pb;

cleanup:
    FREE(ipv6_pb->destination_ipv6.data);
    FREE(ipv6_pb->source_ipv6.data);
    FREE(ipv6_pb);
    return NULL;
}


/**
 * @brief Build one record protobuf from the stored record data.
 *
 * @remark By construction we have ensured the records are always valid,
 * so we don't check again.
 */
bool
dpi_adt_populate_record(Interfaces__Adt__AdtDataPoint *record_pb,
                        struct fsm_dpi_adt_data_record *rec)
{
    Interfaces__Adt__AdtKVPair *one_kv_pair_pb;

    record_pb->device_id.data = CALLOC(sizeof(*rec->info.local_mac), sizeof(uint8_t));
    if (record_pb->device_id.data == NULL) goto cleanup;
    record_pb->device_id.len = sizeof(*rec->info.local_mac);
    memcpy(record_pb->device_id.data, rec->info.local_mac, record_pb->device_id.len);

    if (rec->info.ip_version == 4)
    {
        record_pb->ethertype = ETH_P_IP;
        record_pb->ipv4_tuple = dpi_adt_populate_ipv4(rec);
        if (record_pb->ipv4_tuple == NULL) goto cleanup;
    }
    else if (rec->info.ip_version == 6)
    {
        record_pb->ethertype = ETH_P_IPV6;
        record_pb->ipv6_tuple = dpi_adt_populate_ipv6(rec);
        if (record_pb->ipv6_tuple == NULL) goto cleanup;
    }
    else
    {
        LOGD("%s: Wrong ethertype", __func__);
        goto cleanup;
    }

    one_kv_pair_pb = dpi_adt_populate_kv(rec);
    if (one_kv_pair_pb == NULL) goto cleanup;

    record_pb->kv_pair[record_pb->n_kv_pair] = one_kv_pair_pb;
    record_pb->n_kv_pair++;

    record_pb->network_zone = STRDUP(rec->network_id);
    if (record_pb->network_zone == NULL) goto cleanup;

    return true;

cleanup:
    if (rec->info.ip_version == 4)
    {
        FREE(record_pb->ipv4_tuple);
    }
    else if (rec->info.ip_version == 6)
    {
        if (record_pb->ipv6_tuple)
        {
            FREE(record_pb->ipv6_tuple->source_ipv6.data);
            FREE(record_pb->ipv6_tuple->destination_ipv6.data);
        }
        FREE(record_pb->ipv6_tuple);
    }
    FREE(record_pb->device_id.data);

    return false;
}

bool
dpi_adt_store2proto(struct fsm_session *session,
                    Interfaces__Adt__AdtReport *report_pb)
{
    Interfaces__Adt__AdtObservationPoint *observation_point;
    Interfaces__Adt__AdtDataPoint *one_record_pb = NULL;
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    size_t i;
    bool rc;

    /* Make sure we have a session */
    if (session == NULL) return false;

    /* Make sure we have a destination */
    if (report_pb == NULL) return false;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (client_session == NULL) return false;
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    if (adt_session == NULL) return false;
    aggr = adt_session->adt_aggr;
    if (!aggr->initialized) return false;

    /* Nothing to report */
    if (aggr->data_idx == 0) return false;

    report_pb->data = CALLOC(aggr->data_idx, sizeof(*report_pb->data));
    if (report_pb->data == NULL) goto cleanup;

    /* Iterate thru each of the stored entries, and create the PB
     * as we go along.
     */
    for (i = 0; i < aggr->data_idx; i++)
    {
        one_record_pb = MALLOC(sizeof(*one_record_pb));
        if (one_record_pb == NULL) goto cleanup;
        interfaces__adt__adt_data_point__init(one_record_pb);
        one_record_pb->kv_pair = CALLOC(aggr->data_idx, sizeof(*one_record_pb->kv_pair));
        if (one_record_pb->kv_pair == NULL) goto cleanup;

        rc = dpi_adt_populate_record(one_record_pb, aggr->data[i]);
        if (!rc) goto cleanup;

        report_pb->data[report_pb->n_data] = one_record_pb;
        report_pb->n_data++;
    }

    /* Add the observation point */
    observation_point = MALLOC(sizeof(*observation_point));
    if (observation_point == NULL) goto cleanup;
    interfaces__adt__adt_observation_point__init(observation_point);
    observation_point->node_id = STRDUP(session->node_id);
    observation_point->location_id = STRDUP(session->location_id);
    report_pb->observation_point = observation_point;

    report_pb->reported_at_ms = clock_real_ms();

    dpi_adt_free_aggr_store(aggr);
    return true;

cleanup:
    if (one_record_pb) FREE(one_record_pb->kv_pair);
    FREE(one_record_pb);
    /* We can flush the entire aggregator at this point */
    dpi_adt_free_aggr_store(aggr);
    return false;
}

bool
dpi_adt_serialize_report(struct fsm_session *session,
                         struct fsm_dpi_adt_packed_buffer *report)
{
    Interfaces__Adt__AdtReport report_pb;
    uint8_t *buf;
    bool retval;
    size_t len;
    bool rc;

    retval = false;

    /* Initialize the AdtReport */
    interfaces__adt__adt_report__init(&report_pb);

    rc = dpi_adt_store2proto(session, &report_pb);
    if (!rc) goto cleanup_protobuf;

    /* Get serialization length */
    len = interfaces__adt__adt_report__get_packed_size(&report_pb);
    if (len == 0) goto cleanup_protobuf;

    buf = MALLOC(len * sizeof(*buf));
    if (buf == NULL) goto cleanup_protobuf;

    /* Populate serialization output structure */
    report->len = interfaces__adt__adt_report__pack(&report_pb, buf);
    report->buf = buf;

    retval = true;

cleanup_protobuf:
    dpi_adt_free_report_pb(&report_pb);
    return retval;
}

/**
 * @brief Effectively builds the report from the aggregator's content,
 *        serializes it and sends it over the MQTT channel in 'session'.
 *
 * @param session (must be validated by the caller)
 */
bool
dpi_adt_send_report(struct fsm_session *session)
{
    struct fsm_dpi_adt_packed_buffer serialized_report;
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_adt_session *adt_session;
    qm_response_t res;
    char *mqtt_topic;
    bool rc;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (client_session == NULL)
    {
        LOGD("%s: client_session is NULL", __func__);
        return false;
    }
    adt_session = (struct fsm_dpi_adt_session *)client_session->private_session;
    if (adt_session == NULL)
    {
        LOGD("%s: adt_session is NULL", __func__);
        return false;
    }
    aggr = adt_session->adt_aggr;
    if (aggr == NULL || !aggr->initialized)
    {
        LOGD("%s: adt_aggr not initialized", __func__);
        return false;
    }

    /* No data to report back */
    if (aggr->data_idx == 0)
    {
        LOGT("%s: Nothing to report", __func__);
        return true;
    }

    MEMZERO(serialized_report);

    rc = dpi_adt_serialize_report(session, &serialized_report);
    if (!rc)
    {
        LOGD("%s: Cannot serialize", __func__);
        goto cleanup_serialized_report;
    }

    mqtt_topic = session->topic;

    /* This is qm_conn_send_direct() unless overwritten in UT */
    rc = aggr->send_report(QM_REQ_COMPRESS_IF_CFG, mqtt_topic,
                           serialized_report.buf, serialized_report.len, &res);
    if (!rc)
    {
        LOGD("%s: Failed to send over qm", __func__);
    }

cleanup_serialized_report:
    FREE(serialized_report.buf);
    return rc;
}
