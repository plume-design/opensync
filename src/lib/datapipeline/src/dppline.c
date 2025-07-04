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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <protobuf-c/protobuf-c.h>
#include <unistd.h>

#include "target.h"
#include "osp_unit.h"
#include "dppline.h"
#include "ds.h"
#include "ds_dlist.h"
#include "opensync_stats.pb-c.h"
#include "memutil.h"

#include "dpp_client.h"
#include "dpp_survey.h"
#include "dpp_neighbor.h"
#include "dpp_device.h"
#include "dpp_capacity.h"
#include "dpp_bs_client.h"

#ifndef TARGET_NATIVE
#include "os_types.h"
#include "os_nif.h"
#endif

#define MODULE_ID LOG_MODULE_ID_DPP

/* Internal types   */

/* statistics type  */
typedef enum
{
    DPP_T_SURVEY    = 1,
    DPP_T_CAPACITY  = 2,
    DPP_T_NEIGHBOR  = 3,
    DPP_T_CLIENT    = 4,
    DPP_T_DEVICE    = 5,
    DPP_T_BS_CLIENT = 6,
    DPP_T_RSSI      = 7,
    DPP_T_CLIENT_AUTH_FAILS = 8,
    DPP_T_RADIUS_STATS = 9,
} DPP_STS_TYPE;

uint32_t queue_depth;
uint32_t queue_size;

typedef struct
{
    dpp_client_record_t             rec;
    dpp_client_stats_rx_t          *rx;
    int32_t                         rx_qty;
    dpp_client_stats_tx_t          *tx;
    int32_t                         tx_qty;
    dpp_client_tid_record_list_t   *tid;
    int32_t                         tid_qty;
} dppline_client_rec_t;

typedef struct
{
    radio_type_t                    radio_type;
    uint32_t                        channel;
    dppline_client_rec_t           *list;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
    char                            *uplink_type;
    bool                            uplink_changed;
} dppline_client_stats_t;

typedef struct
{
    radio_type_t                    radio_type;
    report_type_t                   report_type;
    radio_scan_type_t               scan_type;
    dpp_neighbor_record_t          *list;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
} dppline_neighbor_stats_t;

typedef struct
{
    radio_type_t                    radio_type;
    report_type_t                   report_type;
    radio_scan_type_t               survey_type;
    dpp_survey_record_t            *list;
    dpp_survey_record_avg_t        *avg;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
} dppline_survey_stats_t;

typedef struct dpp_capacity_stats
{
    radio_type_t                    radio_type;
    dpp_capacity_record_t          *list;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
} dppline_capacity_stats_t;

typedef struct dpp_device_stats
{
    dpp_device_record_t             record;
    dpp_device_temp_t              *list;
    dpp_device_thermal_record_t    *thermal_list;
    uint32_t                        qty;
    uint32_t                        thermal_qty;
    uint64_t                        timestamp_ms;
} dppline_device_stats_t;

typedef struct dpp_bs_stats
{
    radio_type_t                    radio_type;
    dpp_bs_client_record_t          *list;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
} dppline_bs_client_stats_t;

typedef struct
{
    dpp_rssi_record_t               rec;
    dpp_rssi_raw_t                 *raw;
    int32_t                         raw_qty;
} dppline_rssi_rec_t;

typedef struct dpp_rssi_stats
{
    radio_type_t                    radio_type;
    report_type_t                   report_type;
    dppline_rssi_rec_t             *list;
    uint32_t                        qty;
    uint64_t                        timestamp_ms;
} dppline_rssi_stats_t;

typedef struct dppline_client_auth_fails_client_rec
{
    mac_address_str_t                mac;
    uint32_t                         auth_fails;
    uint32_t                         invalid_psk;
} dppline_client_auth_fails_client_rec_t;

typedef struct dppline_client_auth_fails_bss_rec
{
    ifname_t                                if_name;
    dppline_client_auth_fails_client_rec_t *list;
    uint32_t                                qty;
} dppline_client_auth_fails_bss_rec_t;

typedef struct dpp_client_auth_fails_stats
{
    radio_type_t                         radio_type;
    dppline_client_auth_fails_bss_rec_t *list;
    uint32_t                             qty;
} dppline_client_auth_fails_stats_t;

/* defined in dpp_radius_stats.h */
typedef dpp_radius_stats_rec_t dppline_radius_stats_rec_t;

typedef struct dpp_radius_stats
{
    dppline_radius_stats_rec_t **list;
    uint32_t                     qty;
    uint64_t                     timestamp_ms;
} dppline_radius_stats_t;

/* DPP stats type, used as element in internal double ds */
typedef struct dpp_stats
{
    int                             type;
    int                             size;
    ds_dlist_node_t                 dnode;
    union
    {
        dppline_survey_stats_t      survey;
        dppline_neighbor_stats_t    neighbor;
        dppline_client_stats_t      client;
        dppline_capacity_stats_t    capacity;
        dppline_device_stats_t      device;
        dppline_bs_client_stats_t   bs_client;
        dppline_rssi_stats_t        rssi;
        dppline_client_auth_fails_stats_t client_auth_fails;
        dppline_radius_stats_t      radius;
    } u;
} dppline_stats_t;

/* Internal variables */
ds_dlist_t  g_dppline_list; /* double linked list used to hold stats queue */

/* private functions    */
static dppline_stats_t * dpp_alloc_stat()
{
    return CALLOC(1, sizeof(dppline_stats_t));
}

static void dppline_free_stat_bs_client(dppline_stats_t * s)
{
    dpp_bs_client_event_record_t *event;
    dpp_bs_client_band_record_t *band;
    dpp_bs_client_record_t *client = s->u.bs_client.list;
    int clients = s->u.bs_client.qty;
    int events;
    int bands;

    for (; clients; clients--, client++) {
        bands = client->num_band_records;
        band = &client->band_record[0];
        for (; bands; bands--, band++) {
            events = band->num_event_records;
            event = &band->event_record[0];
            for (; events; events--, event++) {
                FREE(event->assoc_ies);
            }
        }
    }

    FREE(s->u.bs_client.list);
}

/* free allocated memory for single stat */
static void dppline_free_stat(dppline_stats_t * s)
{
    uint32_t i;
    if (NULL != s)
    {
        switch (s->type)
        {
            case DPP_T_SURVEY:
                for (i=0; i<s->u.survey.qty; i++)
                {
                    if (s->u.survey.list[i].info.puncture_len && s->u.survey.list[i].info.puncture != NULL) {
                        FREE(s->u.survey.list[i].info.puncture);
                    }
                }
                FREE(s->u.survey.list);
                FREE(s->u.survey.avg);
                break;
            case DPP_T_NEIGHBOR:
                for (i=0; i<s->u.neighbor.qty; i++)
                {
                    FREE(s->u.neighbor.list[i].beacon_ies);
                }
                FREE(s->u.neighbor.list);
                break;
            case DPP_T_CLIENT:
                for (i=0; i<s->u.client.qty; i++)
                {
                    FREE(s->u.client.list[i].rx);
                    FREE(s->u.client.list[i].tx);
                    FREE(s->u.client.list[i].tid);
                }
                FREE(s->u.client.uplink_type);
                FREE(s->u.client.list);
                break;
            case DPP_T_DEVICE:
                FREE(s->u.device.list);
                FREE(s->u.device.thermal_list);
                break;
            case DPP_T_CAPACITY:
                FREE(s->u.capacity.list);
                break;
            case DPP_T_BS_CLIENT:
                dppline_free_stat_bs_client(s);
                break;
            case DPP_T_RSSI:
                for (i=0; i<s->u.rssi.qty; i++)
                {
                    FREE(s->u.rssi.list[i].raw);
                }
                FREE(s->u.rssi.list);
                break;
            case DPP_T_CLIENT_AUTH_FAILS:
                for (i=0; i < s->u.client_auth_fails.qty; i++)
                {
                    FREE(s->u.client_auth_fails.list[i].list);
                }
                FREE(s->u.client_auth_fails.list);
                break;
            case DPP_T_RADIUS_STATS:
                for (i=0; i < s->u.radius.qty; i++)
                {
                    s->u.radius.list[i]->cleanup(s->u.radius.list[i]);
                }
                FREE(s->u.radius.list);
                break;
            default:;
        }

        FREE(s);
    }
}

static void dppline_unshare_assoc_ies(dpp_bs_client_record_t *c)
{
    dpp_bs_client_event_record_t *event;
    dpp_bs_client_band_record_t *band;
    void *assoc_ies;
    int events;
    int bands;

    bands = c->num_band_records;
    band = &c->band_record[0];
    for (; bands; bands--, band++) {
        events = band->num_event_records;
        event = &band->event_record[0];
        for (; events; events--, event++) {
            if (event->assoc_ies) {
                assoc_ies = CALLOC(1, event->assoc_ies_len);

                if (assoc_ies)
                    memcpy(assoc_ies, event->assoc_ies, event->assoc_ies_len);
                else
                    event->assoc_ies_len = 0;

                event->assoc_ies = assoc_ies;
            }
        }
    }
}

/* copy stats to internal buffer */
static bool dppline_copysts(dppline_stats_t * dst, void * sts)
{
    int size = 0;
    switch(dst->type)
    {
        case DPP_T_SURVEY:
            {
                dpp_survey_report_data_t   *report_data = sts;
                dpp_survey_record_t        *result_entry = NULL;
                ds_dlist_iter_t             result_iter;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.survey.qty = 0;
                dst->u.survey.radio_type = report_data->radio_type;
                dst->u.survey.report_type = report_data->report_type;
                dst->u.survey.survey_type = report_data->scan_type;
                dst->u.survey.timestamp_ms = report_data->timestamp_ms;
                for (   result_entry = ds_dlist_ifirst(&result_iter, &report_data->list);
                        result_entry != NULL;
                        result_entry = ds_dlist_inext(&result_iter))
                {
                    if (REPORT_TYPE_AVERAGE == report_data->report_type) {
                        size = (dst->u.survey.qty + 1) * sizeof(dpp_survey_record_avg_t);
                        if (!dst->u.survey.qty) {
                            dst->u.survey.avg = CALLOC(1, size);
                        }
                        else {
                            dst->u.survey.avg = REALLOC(dst->u.survey.avg, size);
                            memset(&dst->u.survey.avg[dst->u.survey.qty],
                                    0,
                                    sizeof(dpp_survey_record_avg_t));
                        }
                        memcpy(&dst->u.survey.avg[dst->u.survey.qty++],
                                result_entry,
                                sizeof(dpp_survey_record_avg_t));
                        dst->u.survey.list = NULL;
                    }
                    else {
                        size = (dst->u.survey.qty + 1) * sizeof(dpp_survey_record_t);
                        if (!dst->u.survey.qty) {
                            dst->u.survey.list = CALLOC(1, size);
                        }
                        else {
                            dst->u.survey.list = REALLOC(dst->u.survey.list, size);
                            memset(&dst->u.survey.list[dst->u.survey.qty],
                                    0,
                                    sizeof(dpp_survey_record_t));
                        }
                        memcpy(&dst->u.survey.list[dst->u.survey.qty++],
                                result_entry,
                                sizeof(dpp_survey_record_t));
                        dst->u.survey.avg = NULL;
                    }
                }
            }
            break;

        case DPP_T_CAPACITY:
            {
                dpp_capacity_report_data_t *report_data = sts;
                dpp_capacity_record_list_t *result = NULL;
                dpp_capacity_record_t      *result_entry = NULL;
                ds_dlist_iter_t             result_iter;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.capacity.qty = 0;
                dst->u.capacity.radio_type = report_data->radio_type;
                dst->u.capacity.timestamp_ms = report_data->timestamp_ms;
                for (   result = ds_dlist_ifirst(&result_iter, &report_data->list);
                        result != NULL;
                        result = ds_dlist_inext(&result_iter))
                {
                    result_entry = &result->entry;

                    size = (dst->u.capacity.qty + 1) * sizeof(dpp_capacity_record_t);
                    if (!dst->u.capacity.qty) {
                        dst->u.capacity.list = CALLOC(1, size);
                    }
                    else {
                        dst->u.capacity.list = REALLOC(dst->u.capacity.list, size);
                        memset(&dst->u.capacity.list[dst->u.capacity.qty],
                               0,
                               sizeof(dpp_capacity_record_t));
                    }
                    memcpy(&dst->u.capacity.list[dst->u.capacity.qty++],
                            result_entry,
                            sizeof(dpp_capacity_record_t));
                }
            }
            break;

        case DPP_T_NEIGHBOR:
            {
                dpp_neighbor_report_data_t *report_data = sts;
                dpp_neighbor_record_list_t *result = NULL;
                dpp_neighbor_record_t      *result_entry = NULL;
                ds_dlist_iter_t             result_iter;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.neighbor.qty = 0;
                dst->u.neighbor.radio_type = report_data->radio_type;
                dst->u.neighbor.report_type = report_data->report_type;
                dst->u.neighbor.scan_type = report_data->scan_type;
                dst->u.neighbor.timestamp_ms = report_data->timestamp_ms;
                for (   result = ds_dlist_ifirst(&result_iter, &report_data->list);
                        result != NULL;
                        result = ds_dlist_inext(&result_iter))
                {
                    result_entry = &result->entry;

                    size = (dst->u.neighbor.qty + 1) * sizeof(dpp_neighbor_record_t);
                    if (!dst->u.neighbor.qty) {
                        dst->u.neighbor.list = CALLOC(1, size);
                    }
                    else {
                        dst->u.neighbor.list = REALLOC(dst->u.neighbor.list, size);
                        memset(&dst->u.neighbor.list[dst->u.neighbor.qty],
                               0,
                               sizeof(dpp_neighbor_record_t));
                    }
                    memcpy(&dst->u.neighbor.list[dst->u.neighbor.qty],
                            result_entry,
                            sizeof(dpp_neighbor_record_t));

                    if (result_entry->beacon_ies_len)
                    {
                        dst->u.neighbor.list[dst->u.neighbor.qty].beacon_ies =
                            MEMNDUP(result_entry->beacon_ies, result_entry->beacon_ies_len);
                        dst->u.neighbor.list[dst->u.neighbor.qty].beacon_ies_len = result_entry->beacon_ies_len;
                    }
                    dst->u.neighbor.qty++;
                }
            }
            break;

        case DPP_T_CLIENT:
            {
                dpp_client_report_data_t       *report_data = sts;
                dpp_client_record_t            *result_entry = NULL;
                ds_dlist_iter_t                 result_iter;

                dpp_client_stats_rx_t          *rx = NULL;
                ds_dlist_iter_t                 rx_iter;
                dpp_client_stats_tx_t          *tx = NULL;
                ds_dlist_iter_t                 tx_iter;
                dpp_client_tid_record_list_t   *tid = NULL;
                ds_dlist_iter_t                 tid_iter;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.client.qty = 0;
                dst->u.client.radio_type = report_data->radio_type;
                dst->u.client.channel = report_data->channel;
                dst->u.client.timestamp_ms = report_data->timestamp_ms;
                dst->u.client.uplink_type = strdup(report_data->uplink_type);
                dst->u.client.uplink_changed = report_data->uplink_changed;
                for (   result_entry = ds_dlist_ifirst(&result_iter, &report_data->list);
                        result_entry != NULL;
                        result_entry = ds_dlist_inext(&result_iter))
                {
                    size = (dst->u.client.qty + 1) * sizeof(dppline_client_rec_t);
                    if (!dst->u.client.qty) {
                        dst->u.client.list = CALLOC(1, size);
                    }
                    else {
                        dst->u.client.list = REALLOC(dst->u.client.list, size);
                        memset(&dst->u.client.list[dst->u.client.qty],
                               0,
                               sizeof(dppline_client_rec_t));
                    }
                    memcpy(&dst->u.client.list[dst->u.client.qty].rec,
                            result_entry,
                            sizeof(dpp_client_record_t));

                    /* Add RX stats records */
                    for (   rx = ds_dlist_ifirst(&rx_iter, &result_entry->stats_rx);
                            rx != NULL;
                            rx = ds_dlist_inext(&rx_iter))
                    {
                        size = (dst->u.client.list[dst->u.client.qty].rx_qty + 1) * sizeof(dpp_client_stats_rx_t);
                        if (!dst->u.client.list[dst->u.client.qty].rx_qty) {
                            dst->u.client.list[dst->u.client.qty].rx = CALLOC(1, size);
                        }
                        else {
                            dst->u.client.list[dst->u.client.qty].rx =
                                REALLOC(dst->u.client.list[dst->u.client.qty].rx, size);
                            memset(&dst->u.client.list[dst->u.client.qty].rx[dst->u.client.list[dst->u.client.qty].rx_qty],
                                    0,
                                    sizeof(dpp_client_stats_rx_t));
                        }
                        memcpy(&dst->u.client.list[dst->u.client.qty].rx[dst->u.client.list[dst->u.client.qty].rx_qty],
                                rx,
                                sizeof(dpp_client_stats_rx_t));

                        dst->u.client.list[dst->u.client.qty].rx_qty++;
                    }

                    /* Add TX stats records */
                    for (   tx = ds_dlist_ifirst(&tx_iter, &result_entry->stats_tx);
                            tx != NULL;
                            tx = ds_dlist_inext(&tx_iter))
                    {
                        size = (dst->u.client.list[dst->u.client.qty].tx_qty + 1) * sizeof(dpp_client_stats_tx_t);
                        if (!dst->u.client.list[dst->u.client.qty].tx_qty) {
                            dst->u.client.list[dst->u.client.qty].tx = CALLOC(1, size);
                        }
                        else {
                            dst->u.client.list[dst->u.client.qty].tx =
                                REALLOC(dst->u.client.list[dst->u.client.qty].tx, size);
                            memset(&dst->u.client.list[dst->u.client.qty].tx[dst->u.client.list[dst->u.client.qty].tx_qty],
                                    0,
                                    sizeof(dpp_client_stats_tx_t));
                        }
                        memcpy(&dst->u.client.list[dst->u.client.qty].tx[dst->u.client.list[dst->u.client.qty].tx_qty],
                                tx,
                                sizeof(dpp_client_stats_tx_t));

                        dst->u.client.list[dst->u.client.qty].tx_qty++;
                    }

                    /* Add TID records */
                    for (   tid = ds_dlist_ifirst(&tid_iter, &result_entry->tid_record_list);
                            tid != NULL;
                            tid = ds_dlist_inext(&tid_iter))
                    {
                        size = (dst->u.client.list[dst->u.client.qty].tid_qty + 1) * sizeof(dpp_client_tid_record_list_t);
                        if (!dst->u.client.list[dst->u.client.qty].tid_qty) {
                            dst->u.client.list[dst->u.client.qty].tid = CALLOC(1, size);
                        }
                        else {
                            dst->u.client.list[dst->u.client.qty].tid =
                                REALLOC(dst->u.client.list[dst->u.client.qty].tid, size);
                            memset(&dst->u.client.list[dst->u.client.qty].tid[dst->u.client.list[dst->u.client.qty].tid_qty],
                                    0,
                                    sizeof(dpp_client_tid_record_list_t));
                        }
                        memcpy(&dst->u.client.list[dst->u.client.qty].tid[dst->u.client.list[dst->u.client.qty].tid_qty],
                                tid,
                                sizeof(dpp_client_tid_record_list_t));

                        dst->u.client.list[dst->u.client.qty].tid_qty++;
                    }
                    dst->u.client.qty++;
                }
            }
            break;

        case DPP_T_DEVICE:
            {
                dpp_device_report_data_t        *report_data = sts;
                dpp_device_temp_t               *result_entry = NULL;
                dpp_device_thermal_record_t     *thermal_record = NULL;
                ds_dlist_iter_t                  result_iter;
                int                              thermal_size = 0;

                memcpy(&dst->u.device.record, &report_data->record, sizeof(dpp_device_record_t));
                dst->u.device.timestamp_ms = report_data->timestamp_ms;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.device.qty = 0;
                for (   result_entry = ds_dlist_ifirst(&result_iter, &report_data->temp);
                        result_entry != NULL;
                        result_entry = ds_dlist_inext(&result_iter))
                {
                    size = (dst->u.device.qty + 1) * sizeof(dpp_device_temp_t);
                    if (!dst->u.device.qty)
                    {
                        dst->u.device.list = CALLOC(1, size);
                    }
                    else
                    {
                        dst->u.device.list = REALLOC(dst->u.device.list, size);
                        memset(&dst->u.device.list[dst->u.device.qty],
                               0,
                               sizeof(dpp_device_temp_t));
                    }
                    memcpy(&dst->u.device.list[dst->u.device.qty++],
                            result_entry,
                            sizeof(dpp_device_temp_t));
                }

                dst->u.device.thermal_qty = 0;
                for (   thermal_record = ds_dlist_ifirst(&result_iter, &report_data->thermal_records);
                        thermal_record != NULL;
                        thermal_record = ds_dlist_inext(&result_iter))
                {
                    thermal_size += (dst->u.device.thermal_qty + 1) * sizeof(dpp_device_thermal_record_t);
                    if (!dst->u.device.thermal_qty)
                    {
                        dst->u.device.thermal_list = CALLOC(1, thermal_size);
                    }
                    else
                    {
                        dst->u.device.thermal_list = REALLOC(dst->u.device.thermal_list, thermal_size);
                        memset(&dst->u.device.thermal_list[dst->u.device.thermal_qty],
                               0,
                               sizeof(dpp_device_thermal_record_t));
                    }
                    memcpy(&dst->u.device.thermal_list[dst->u.device.thermal_qty++],
                            thermal_record,
                            sizeof(dpp_device_thermal_record_t));
                }
                size += thermal_size;
            }
            break;

        case DPP_T_BS_CLIENT:
            {
                dpp_bs_client_report_data_t   *report_data = sts;
                dpp_bs_client_record_list_t   *result = NULL;
                dst->u.bs_client.qty = 0;
                dst->u.bs_client.timestamp_ms = report_data->timestamp_ms;

                // Loop through linked list of results and copy results
                ds_dlist_foreach(&report_data->list, result) { dst->u.bs_client.qty++; }
                size = dst->u.bs_client.qty * sizeof(dpp_bs_client_record_t);
                dst->u.bs_client.list = CALLOC(1, size);
                int count = 0;
                ds_dlist_foreach(&report_data->list, result)
                {
                    assert(count < (int)dst->u.bs_client.qty);
                    memcpy(&dst->u.bs_client.list[count],
                            &result->entry,
                            sizeof(dpp_bs_client_record_t));
                    dppline_unshare_assoc_ies(&dst->u.bs_client.list[count]);
                    count++;
                }
            }
            break;

        case DPP_T_RSSI:
            {
                dpp_rssi_report_data_t         *report_data = sts;
                dpp_rssi_record_t              *result_entry = NULL;
                ds_dlist_iter_t                 result_iter;

                dpp_rssi_raw_t                 *raw = NULL;
                ds_dlist_iter_t                 raw_iter;

                /* Loop through linked list of results and copy them to dppline buffer */
                dst->u.rssi.qty = 0;
                dst->u.rssi.radio_type = report_data->radio_type;
                dst->u.rssi.report_type = report_data->report_type;
                dst->u.rssi.timestamp_ms = report_data->timestamp_ms;
                for (   result_entry = ds_dlist_ifirst(&result_iter, &report_data->list);
                        result_entry != NULL;
                        result_entry = ds_dlist_inext(&result_iter))
                {
                    size = (dst->u.rssi.qty + 1) * sizeof(dppline_rssi_rec_t);
                    if (!dst->u.rssi.qty) {
                        dst->u.rssi.list = CALLOC(1, size);
                    }
                    else {
                        dst->u.rssi.list = REALLOC(dst->u.rssi.list, size);
                        memset(&dst->u.rssi.list[dst->u.rssi.qty],
                               0,
                               sizeof(dppline_rssi_rec_t));
                    }
                    memcpy(&dst->u.rssi.list[dst->u.rssi.qty].rec,
                            result_entry,
                            sizeof(dpp_rssi_record_t));

                    if (REPORT_TYPE_RAW == report_data->report_type) {
                        for (   raw = ds_dlist_ifirst(&raw_iter, &result_entry->rssi.raw);
                                raw != NULL;
                                raw = ds_dlist_inext(&raw_iter))
                        {
                            size = (dst->u.rssi.list[dst->u.rssi.qty].raw_qty + 1) * sizeof(dpp_rssi_raw_t);
                            if (!dst->u.rssi.list[dst->u.rssi.qty].raw_qty) {
                                dst->u.rssi.list[dst->u.rssi.qty].raw = CALLOC(1, size);
                            }
                            else {
                                dst->u.rssi.list[dst->u.rssi.qty].raw =
                                    REALLOC(dst->u.rssi.list[dst->u.rssi.qty].raw, size);
                                memset(&dst->u.rssi.list[dst->u.rssi.qty].raw[dst->u.rssi.list[dst->u.rssi.qty].raw_qty],
                                        0,
                                        sizeof(dpp_rssi_raw_t));
                            }
                            memcpy(&dst->u.rssi.list[dst->u.rssi.qty].raw[dst->u.rssi.list[dst->u.rssi.qty].raw_qty],
                                    raw,
                                    sizeof(dpp_rssi_raw_t));

                            dst->u.rssi.list[dst->u.rssi.qty].raw_qty++;
                        }
                    }

                    dst->u.rssi.qty++;
                }
            }
            break;

        case DPP_T_CLIENT_AUTH_FAILS:
            {
                dpp_client_auth_fails_report_data_t *report_data = sts;
                dpp_client_auth_fails_bss_t         *bss_entry = NULL;
                ds_dlist_iter_t                      bss_iter;
                dpp_client_auth_fails_client_t      *client_entry = NULL;
                ds_dlist_iter_t                      client_iter;

                dst->u.client_auth_fails.radio_type = report_data->radio_type;
                ds_dlist_iforeach(&report_data->bsses, bss_entry, bss_iter) {
                    const size_t bss_size = (dst->u.client_auth_fails.qty + 1) * sizeof(dpp_client_auth_fails_bss_t);
                    if (!dst->u.client_auth_fails.qty) {
                        dst->u.client_auth_fails.list = CALLOC(1, bss_size);
                    }
                    else {
                        dst->u.client_auth_fails.list = REALLOC(dst->u.client_auth_fails.list, bss_size);
                        if (!dst->u.client_auth_fails.list)
                            continue;

                        memset(&dst->u.client_auth_fails.list[dst->u.client_auth_fails.qty], 0, sizeof(dpp_client_auth_fails_bss_t));
                    }

                    STRSCPY_WARN(dst->u.client_auth_fails.list[dst->u.client_auth_fails.qty].if_name, bss_entry->if_name);
                    ds_dlist_iforeach(&bss_entry->clients, client_entry, client_iter) {
                        dppline_client_auth_fails_bss_rec_t *dst_bss = &dst->u.client_auth_fails.list[dst->u.client_auth_fails.qty];
                        const size_t client_size = (dst_bss->qty + 1) * sizeof(dpp_client_auth_fails_client_t);
                        if (!dst_bss->qty) {
                            dst_bss->list = CALLOC(1, client_size);
                        }
                        else {
                            dst_bss->list = REALLOC(dst_bss->list, client_size);
                            if (!dst_bss->list)
                                continue;

                            memset(&dst_bss->list[dst_bss->qty], 0, sizeof(dpp_client_auth_fails_client_t));
                        }

                        STRSCPY_WARN(dst_bss->list[dst_bss->qty].mac, client_entry->mac);
                        dst_bss->list[dst_bss->qty].auth_fails = client_entry->auth_fails;
                        dst_bss->list[dst_bss->qty].invalid_psk = client_entry->invalid_psk;

                        size += client_size;
                        dst_bss->qty++;
                    }

                    size += bss_size;
                    dst->u.client_auth_fails.qty++;
                }
            }
            break;

        case DPP_T_RADIUS_STATS:
            {
                dpp_radius_stats_report_data_t *report = sts;
                /*
                * Types pointed to by the dst.u.radius.list and report->records should both be
                * typedefs of the same type, namely dpp_radius_stats_rec_t (dpp_radius_stats.h).
                * This module now assumes the ownership of the pointer to the list, as well as
                * the pointers in the list. The former is just a pointer, so FREE get's called.
                * The records are structs with a "cleanup" function that gets passed the object's
                * address (similar to a destructor method in OOP languages).
                */
                dst->u.radius.list = (dppline_radius_stats_rec_t **) report->records;
                dst->u.radius.timestamp_ms = report->timestamp;
                dst->u.radius.qty = report->qty;

                size = report->qty * sizeof(**dst->u.radius.list);
            }
            break;

        default:
            LOG(ERR, "Failed to copy %d stats", dst->type);
            /* do nothing */
            return false;
    }
    dst->size = size;
    return true;
}

static char * getNodeid()
{
    char * buff = NULL;

    buff = MALLOC(TARGET_ID_SZ);

    if (!osp_unit_id_get(buff, TARGET_ID_SZ))
    {
        LOG(ERR, "Error acquiring node id.");
        FREE(buff);
        return NULL;
    }

    return buff;
}


Sts__RadioBandType dppline_to_proto_radio(radio_type_t radio_type)
{
    switch (radio_type)
    {
        case RADIO_TYPE_2G:
            return STS__RADIO_BAND_TYPE__BAND2G;

        case RADIO_TYPE_5G:
            return STS__RADIO_BAND_TYPE__BAND5G;

        case RADIO_TYPE_5GL:
            return STS__RADIO_BAND_TYPE__BAND5GL;

        case RADIO_TYPE_5GU:
            return STS__RADIO_BAND_TYPE__BAND5GU;

        case RADIO_TYPE_6G:
            return STS__RADIO_BAND_TYPE__BAND6G;

        default:
            assert(0);
    }
    return 0;
}

Sts__SurveyType dppline_to_proto_survey_type(radio_scan_type_t scan_type)
{
    switch (scan_type)
    {
        case RADIO_SCAN_TYPE_ONCHAN:
            return STS__SURVEY_TYPE__ON_CHANNEL;

        case RADIO_SCAN_TYPE_OFFCHAN:
            return STS__SURVEY_TYPE__OFF_CHANNEL;

        case RADIO_SCAN_TYPE_FULL:
            return STS__SURVEY_TYPE__FULL;

        default:
            assert(0);
    }
    return 0;
}

Sts__ReportType dppline_to_proto_report_type(report_type_t report_type)
{
    switch (report_type)
    {
        case REPORT_TYPE_RAW:
            return STS__REPORT_TYPE__RAW;

        case REPORT_TYPE_AVERAGE:
            return STS__REPORT_TYPE__AVERAGE;

        case REPORT_TYPE_HISTOGRAM:
            return STS__REPORT_TYPE__HISTOGRAM;

        case REPORT_TYPE_PERCENTILE:
            return STS__REPORT_TYPE__PERCENTILE;

        case REPORT_TYPE_DIFF:
            return STS__REPORT_TYPE__DIFF;

        default:
            assert(0);
    }
    return 0;
}

static void dppline_add_stat_survey(Sts__Report *r, dppline_stats_t *s)
{
    Sts__Survey *sr = NULL;
    uint32_t i;
    dppline_survey_stats_t *survey = &s->u.survey;

    // increase the number of surveys
    r->n_survey++;

    // allocate or extend the size of surveys
    r->survey = REALLOC(r->survey,
            r->n_survey * sizeof(Sts__Survey*));

    // allocate new buffer Sts__Survey
    sr = MALLOC(sizeof(Sts__Survey));
    r->survey[r->n_survey - 1] = sr;

    sts__survey__init(sr);
    sr->band = dppline_to_proto_radio(survey->radio_type);
    sr->report_type = dppline_to_proto_report_type(survey->report_type);
    sr->has_report_type = true;
    sr->survey_type = dppline_to_proto_survey_type(survey->survey_type);
    sr->timestamp_ms = survey->timestamp_ms;
    sr->has_timestamp_ms = true;
    if (REPORT_TYPE_AVERAGE == survey->report_type) {
        sr->survey_avg = MALLOC(survey->qty * sizeof(*sr->survey_avg));
        sr->n_survey_avg = survey->qty;
        for (i = 0; i < survey->qty; i++)
        {
            dpp_survey_record_avg_t *rec = &survey->avg[i];
            Sts__Survey__SurveyAvg *dr; // dest rec
            dr = sr->survey_avg[i] = MALLOC(sizeof(**sr->survey_avg));
            sts__survey__survey_avg__init(dr);

            dr->channel = rec->info.chan;

            Sts__AvgType         *davg;
            Sts__AvgTypeSigned   *davgs;
#define CP_AVG(_name, _name1) do { \
        if (rec->_name1.avg) { \
            davg = dr->_name = MALLOC(sizeof(*dr->_name)); \
            sts__avg_type__init(davg); \
            davg->avg = rec->_name1.avg; \
            if(rec->_name1.min) { \
                davg->min = rec->_name1.min; \
                davg->has_min = true;; \
            } \
            if(rec->_name1.max) { \
                davg->max = rec->_name1.max; \
                davg->has_max = true;; \
            } \
            if(rec->_name1.num) { \
                davg->num = rec->_name1.num; \
                davg->has_num = true;; \
            } \
        } \
    } while (0)

#define CP_AVG_SIGNED(_name, _name1) do { \
        if (rec->_name1.avg) { \
            davgs = dr->_name = MALLOC(sizeof(*dr->_name)); \
            sts__avg_type_signed__init(davgs); \
            davgs->avg = rec->_name1.avg; \
            if(rec->_name1.min) { \
                davgs->min = rec->_name1.min; \
                davgs->has_min = true;; \
            } \
            if(rec->_name1.max) { \
                davgs->max = rec->_name1.max; \
                davgs->has_max = true;; \
            } \
            if(rec->_name1.num) { \
                davgs->num = rec->_name1.num; \
                davgs->has_num = true;; \
            } \
        } \
    } while (0)

            CP_AVG(busy,       chan_busy);
            CP_AVG(busy_tx,    chan_tx);
            CP_AVG(busy_self,  chan_self);
            CP_AVG(busy_rx,    chan_rx);
            CP_AVG(busy_ext,   chan_busy_ext);
            CP_AVG_SIGNED(noise_floor,chan_noise);

#undef CP_AVG
#undef CP_AVG_SIGNED
        }
        /* LOG(DEBUG, "============= %s size raw: %d proto struct: %d", __FUNCTION__,
           sizeof(s->u.survey) + s->u.survey.numrec * sizeof(dpp_survey_record_avg_t),
           sizeof(Sts__Survey*) +
           sizeof(Sts__Survey) +
           s->u.survey.numrec * sizeof(*sr->survey_avg) +
           s->u.survey.numrec * sizeof(**sr->survey_avg) +
           s->u.survey.numrec * (sizeof(Sts__AvgType)*5)); */
    } else {
        /* RAW only due to legacy (revisit once PERCENTILE AND HISTOGRAM)*/
        sr->survey_list = MALLOC(survey->qty * sizeof(*sr->survey_list));
        sr->n_survey_list = survey->qty;
        for (i = 0; i < survey->qty; i++)
        {
            dpp_survey_record_t *rec = &survey->list[i];
            Sts__Survey__SurveySample *dr; // dest rec
            dr = sr->survey_list[i] = MALLOC(sizeof(**sr->survey_list));
            sts__survey__survey_sample__init(dr);

            dr->channel = rec->info.chan;
            if (sr->survey_type == STS__SURVEY_TYPE__ON_CHANNEL && rec->info.chan_width != STS__CHAN_WIDTH__CHAN_WIDTH_UNKNOWN) {
                dr->chan_width = rec->info.chan_width;
                dr->has_chan_width = true;
            }
            if (rec->info.chan_width > STS__CHAN_WIDTH__CHAN_WIDTH_20MHZ) {
                dr->center0_freq_mhz = rec->info.center0_freq_mhz;
                dr->has_center0_freq_mhz = true;
                if (rec->info.puncture_len) {
                    dr->puncture.data = MEMNDUP(rec->info.puncture, rec->info.puncture_len);
                    dr->puncture.len = rec->info.puncture_len;
                    dr->has_puncture = true;
                }
            }
#define CP_OPT(_name, _name1) do { \
        if (rec->_name1) { \
            dr->_name = rec->_name1; \
            dr->has_##_name = true; \
        } \
    } while (0)

            CP_OPT(busy,       chan_busy);
            CP_OPT(busy_tx,    chan_tx);
            CP_OPT(busy_self,  chan_self);
            CP_OPT(busy_rx,    chan_rx);
            CP_OPT(busy_ext,   chan_busy_ext);
            CP_OPT(noise_floor,chan_noise);

#undef CP_OPT

            dr->duration_ms = rec->duration_ms;
            dr->has_duration_ms = true;
            dr->offset_ms = sr->timestamp_ms - rec->info.timestamp_ms;
            dr->has_offset_ms = true;
        }
        /* LOG(DEBUG, "============= %s size raw: %d proto struct: %d", __FUNCTION__,
           sizeof(s->u.survey) + s->u.survey.numrec * sizeof(dpp_survey_record_t),
           sizeof(Sts__Survey*) +
           sizeof(Sts__Survey) +
           s->u.survey.numrec * sizeof(*sr->survey_list) +
           s->u.survey.numrec * sizeof(**sr->survey_list)); */
    }
}

Sts__NeighborType dppline_to_proto_neighbor_scan_type(radio_scan_type_t scan_type)
{
    switch (scan_type)
    {
        case RADIO_SCAN_TYPE_FULL:
            return STS__NEIGHBOR_TYPE__FULL_SCAN;

        case RADIO_SCAN_TYPE_ONCHAN:
            return STS__NEIGHBOR_TYPE__ONCHAN_SCAN;

        case RADIO_SCAN_TYPE_OFFCHAN:
            return STS__NEIGHBOR_TYPE__OFFCHAN_SCAN;

        default:
            assert(0);
    }
    return 0;
}

static void dppline_add_stat_neighbor(Sts__Report *r, dppline_stats_t *s)
{
    Sts__Neighbor *sr = NULL;
    uint32_t i;
    int size = 0;
    dppline_neighbor_stats_t *neighbor = &s->u.neighbor;

    // increase the number of neighbors
    r->n_neighbors++;

    // allocate or extend the size of neighbors
    r->neighbors = REALLOC(r->neighbors,
            r->n_neighbors * sizeof(Sts__Neighbor*));
    size += sizeof(Sts__Neighbor*);

    // allocate new buffer Sts__Neighbor
    sr = MALLOC(sizeof(Sts__Neighbor));
    size += sizeof(Sts__Neighbor);
    r->neighbors[r->n_neighbors - 1] = sr;

    sts__neighbor__init(sr);
    sr->band = dppline_to_proto_radio(neighbor->radio_type);
    sr->scan_type = dppline_to_proto_neighbor_scan_type(neighbor->scan_type);
    sr->report_type = dppline_to_proto_report_type(neighbor->report_type);
    sr->has_report_type = true;
    sr->timestamp_ms = neighbor->timestamp_ms;
    sr->has_timestamp_ms = true;
    sr->bss_list = MALLOC(neighbor->qty * sizeof(*sr->bss_list));
    size += neighbor->qty * sizeof(*sr->bss_list);
    sr->n_bss_list = neighbor->qty;
    for (i = 0; i < neighbor->qty; i++)
    {
        dpp_neighbor_record_t *rec = &neighbor->list[i];
        Sts__Neighbor__NeighborBss *dr; // dest rec
        dr = sr->bss_list[i] = MALLOC(sizeof(**sr->bss_list));
        size += sizeof(**sr->bss_list);
        sts__neighbor__neighbor_bss__init(dr);

        dr->bssid = strdup(rec->bssid);
        size += strlen(rec->bssid) + 1;
        dr->ssid = strdup(rec->ssid);
        size += strlen(rec->ssid) + 1;
        if (rec->sig) {
            dr->rssi = rec->sig;
            dr->has_rssi = true;
        }
        if (rec->tsf) {
            dr->tsf = rec->tsf;
            dr->has_tsf = true;
        }
        dr->chan_width = (Sts__ChanWidth)rec->chanwidth;
        dr->has_chan_width = true;
        dr->channel = rec->chan;

        if (REPORT_TYPE_DIFF == neighbor->report_type) {
            if (rec->lastseen) {
                dr->status = STS__DIFF_TYPE__ADDED;
            }
            else {
                dr->status = STS__DIFF_TYPE__REMOVED;
            }
            dr->has_status = true;
        }

        if (rec->beacon_ies_len) {
            dr->beacon_ies.data = MEMNDUP(rec->beacon_ies, rec->beacon_ies_len);
            dr->beacon_ies.len = rec->beacon_ies_len;
            dr->has_beacon_ies = true;
        }
    }
    LOGT("%s: ============= size raw: %zu alloc: %d proto struct: %d", __func__,
         sizeof(s->u.neighbor), s->size, size);
}

void dpp_mac_to_str(uint8_t *mac, char *str)
{
    // slow
    //sprintf(str, MAC_ADDRESS_FORMAT, MAC_ADDRESS_PRINT(rec->mac));

    // optimized
    int i;
    uint8_t nib;
    for (i=0; i<6; i++)
    {
        nib = *mac >> 4;
        *str++ = nib < 10 ? '0' + nib : 'A' + nib - 10;
        nib = *mac & 0xF;
        *str++ = nib < 10 ? '0' + nib : 'A' + nib - 10;
        if (i < 5) *str++ = ':';
        mac++;
    }
    *str = 0;
}

char* dpp_mac_str_tmp(uint8_t *mac)
{
    static mac_address_str_t str;
    dpp_mac_to_str(mac, str);
    return str;
}

Sts__WmmAc dppline_to_proto_wmm_ac_type(radio_queue_type_t ac_type)
{
    switch (ac_type)
    {
        case RADIO_QUEUE_TYPE_VI:
            return STS__WMM_AC__WMM_AC_VI;
        case RADIO_QUEUE_TYPE_VO:
            return STS__WMM_AC__WMM_AC_VO;
        case RADIO_QUEUE_TYPE_BE:
            return STS__WMM_AC__WMM_AC_BE;
        case RADIO_QUEUE_TYPE_BK:
            return STS__WMM_AC__WMM_AC_BK;

        default:
            assert(0);
    }

    return -1;
}

static void dppline_add_stat_client(Sts__Report *r, dppline_stats_t *s)
{
    Sts__ClientReport *sr = NULL;
    Sts__Client *dr; // dest rec
    uint32_t i = 0;
    int j, j1;
    int n = 0;
    int size = 0;
    dppline_client_stats_t *client = &s->u.client;

    // increase the number of clients
    r->n_clients++;

    // allocate or extend the size of clients
    r->clients = REALLOC(r->clients,
            r->n_clients * sizeof(Sts__ClientReport*));

    // allocate new buffer
    sr = MALLOC(sizeof(Sts__ClientReport));
    size += sizeof(Sts__ClientReport);
    r->clients[r->n_clients - 1] = sr;

    sts__client_report__init(sr);
    sr->band = dppline_to_proto_radio(client->radio_type);
    sr->timestamp_ms = client->timestamp_ms;
    sr->has_timestamp_ms = true;
    sr->channel = client->channel;
    if (client->uplink_changed) {
        sr->has_uplink_changed = true;
        sr->uplink_changed = client->uplink_changed;
    }
    sr->uplink_type = strdup(client->uplink_type);

    sr->client_list = MALLOC(client->qty * sizeof(*sr->client_list));
    size += client->qty * sizeof(*sr->client_list);
    sr->n_client_list = client->qty;
    for (i = 0; i < client->qty; i++)
    {
        dpp_client_record_t *rec = &client->list[i].rec;
        int network_id_len;
        dr = sr->client_list[i] = MALLOC(sizeof(**sr->client_list));
        size += sizeof(**sr->client_list);
        sts__client__init(dr);

        dr->mac_address = MALLOC(MACADDR_STR_LEN);
        dpp_mac_to_str(rec->info.mac, dr->mac_address);
        size += MACADDR_STR_LEN;

        dr->mld_address = MALLOC(MACADDR_STR_LEN);
        dpp_mac_to_str(rec->info.mld_addr, dr->mld_address);
        size += MACADDR_STR_LEN;

        dr->ssid = strdup(rec->info.essid);
        size += strlen(rec->info.essid) + 1;

        network_id_len = strlen(rec->info.networkid);
        if (network_id_len) {
            dr->network_id = strdup(rec->info.networkid);
            size += network_id_len + 1;
        }

        dr->connected = rec->is_connected;
        dr->connect_count = rec->connected;
        dr->disconnect_count = rec->disconnected;

        if (rec->connect_ts) {
            dr->connect_offset_ms = client->timestamp_ms - rec->connect_ts;
            dr->has_connect_offset_ms = true;
        }

        if (rec->disconnect_ts) {
            dr->disconnect_offset_ms = client->timestamp_ms - rec->disconnect_ts;
            dr->has_disconnect_offset_ms = true;
        }

        if (rec->uapsd) {
            dr->uapsd = rec->uapsd;
            dr->has_uapsd = true;
        }

        dr->duration_ms = rec->duration_ms;

        dr->has_connected = true;
        dr->has_connect_count = true;
        dr->has_disconnect_count = true;
        dr->has_duration_ms = true;

        dr->stats = MALLOC(sizeof(*dr->stats));
        size += sizeof(*dr->stats);
        sts__client__stats__init(dr->stats);

        if (rec->stats.bytes_rx) {
            dr->stats->rx_bytes = rec->stats.bytes_rx;
            dr->stats->has_rx_bytes = true;
        }
        if (rec->stats.bytes_tx) {
            dr->stats->tx_bytes = rec->stats.bytes_tx;
            dr->stats->has_tx_bytes = true;
        }
        if (rec->stats.frames_rx) {
            dr->stats->rx_frames = rec->stats.frames_rx;
            dr->stats->has_rx_frames = true;
        }
        if (rec->stats.frames_tx) {
            dr->stats->tx_frames = rec->stats.frames_tx;
            dr->stats->has_tx_frames = true;
        }
        if (rec->stats.retries_rx) {
            dr->stats->rx_retries = rec->stats.retries_rx;
            dr->stats->has_rx_retries = true;
        }
        if (rec->stats.retries_rx) {
            dr->stats->tx_retries = rec->stats.retries_tx;
            dr->stats->has_tx_retries = true;
        }
        if (rec->stats.errors_rx) {
            dr->stats->rx_errors = rec->stats.errors_rx;
            dr->stats->has_rx_errors = true;
        }
        if (rec->stats.errors_tx) {
            dr->stats->tx_errors = rec->stats.errors_tx;
            dr->stats->has_tx_errors = true;
        }
        if (rec->stats.rate_rx) {
            dr->stats->rx_rate = rec->stats.rate_rx;
            dr->stats->has_rx_rate = true;
        }
        if (rec->stats.rate_tx) {
            dr->stats->tx_rate = rec->stats.rate_tx;
            dr->stats->has_tx_rate = true;
        }
        if (rec->stats.rssi) {
            dr->stats->rssi = rec->stats.rssi;
            dr->stats->has_rssi = true;
        }
        if (rec->stats.rate_rx_perceived) {
            dr->stats->rx_rate_perceived = rec->stats.rate_rx_perceived;
            dr->stats->has_rx_rate_perceived = true;
        }
        if (rec->stats.rate_tx_perceived) {
            dr->stats->tx_rate_perceived = rec->stats.rate_tx_perceived;
            dr->stats->has_tx_rate_perceived = true;
        }

        dr->rx_stats = MALLOC(client->list[i].rx_qty * sizeof(*dr->rx_stats));
        size += client->list[i].rx_qty * sizeof(*dr->rx_stats);
        dr->n_rx_stats = client->list[i].rx_qty;
        for (j = 0; j < client->list[i].rx_qty; j++)
        {
            Sts__Client__RxStats   *drx;
            dpp_client_stats_rx_t  *srx = &client->list[i].rx[j];

            drx = dr->rx_stats[j] = MALLOC(sizeof(**dr->rx_stats));
            sts__client__rx_stats__init(drx);

            drx->mcs        = srx->mcs;
            drx->nss        = srx->nss;
            drx->bw         = srx->bw;

            if (srx->bytes) {
                drx->bytes = srx->bytes;
                drx->has_bytes = true;
            }
            if (srx->msdu) {
                drx->msdus = srx->msdu;
                drx->has_msdus = true;
            }
            if (srx->mpdu) {
                drx->mpdus = srx->mpdu;
                drx->has_mpdus = true;
            }
            if (srx->ppdu) {
                drx->ppdus = srx->ppdu;
                drx->has_ppdus = true;
            }
            if (srx->retries) {
                drx->retries = srx->retries;
                drx->has_retries = true;
            }
            if (srx->errors) {
                drx->errors = srx->errors;
                drx->has_errors = true;
            }
            if (srx->rssi) {
                drx->rssi = srx->rssi;
                drx->has_rssi = true;
            }
        }

        dr->tx_stats = MALLOC(client->list[i].tx_qty * sizeof(*dr->tx_stats));
        size += client->list[i].tx_qty * sizeof(*dr->tx_stats);
        dr->n_tx_stats = client->list[i].tx_qty;
        for (j = 0; j < client->list[i].tx_qty; j++)
        {
            Sts__Client__TxStats *dtx;
            dpp_client_stats_tx_t *stx = &client->list[i].tx[j];

            dtx = dr->tx_stats[j] = MALLOC(sizeof(**dr->tx_stats));
            sts__client__tx_stats__init(dtx);

            dtx->mcs     = stx->mcs;
            dtx->nss     = stx->nss;
            dtx->bw      = stx->bw;

            if (stx->bytes) {
                dtx->bytes = stx->bytes;
                dtx->has_bytes = true;
            }
            if (stx->msdu) {
                dtx->msdus = stx->msdu;
                dtx->has_msdus = true;
            }
            if (stx->mpdu) {
                dtx->mpdus = stx->mpdu;
                dtx->has_mpdus = true;
            }
            if (stx->ppdu) {
                dtx->ppdus = stx->ppdu;
                dtx->has_ppdus = true;
            }
            if (stx->retries) {
                dtx->retries = stx->retries;
                dtx->has_retries = true;
            }
            if (stx->errors) {
                dtx->errors = stx->errors;
                dtx->has_errors = true;
            }
        }

        dr->tid_stats = MALLOC(client->list[i].tid_qty * sizeof(*dr->tid_stats));
        size += client->list[i].tid_qty * sizeof(*dr->tid_stats);
        dr->n_tid_stats = client->list[i].tid_qty;
        for (j = 0; j < client->list[i].tid_qty; j++)
        {
            Sts__Client__TidStats *dtid;
            dpp_client_tid_record_list_t *stid = &client->list[i].tid[j];
            dtid = dr->tid_stats[j] = MALLOC(sizeof(**dr->tid_stats));
            sts__client__tid_stats__init(dtid);

            dtid->offset_ms =
                sr->timestamp_ms - stid->timestamp_ms;
            dtid->has_offset_ms = true;

            dtid->sojourn = MALLOC(CLIENT_MAX_TID_RECORDS * sizeof(*dtid->sojourn));
            for (n = 0, j1 = 0; j1 < CLIENT_MAX_TID_RECORDS; j1++)
            {
                Sts__Client__TidStats__Sojourn *drr;
                dpp_client_stats_tid_t *srr = &stid->entry[n];
                if (!(srr->num_msdus)) continue;
                drr = dtid->sojourn[n] = MALLOC(sizeof(**dtid->sojourn));
                sts__client__tid_stats__sojourn__init(drr);
                drr->ac = dppline_to_proto_wmm_ac_type(srr->ac);
                drr->tid = srr->tid;

                if (srr->ewma_time_ms) {
                    drr->ewma_time_ms = srr->ewma_time_ms;
                    drr->has_ewma_time_ms = true;
                }
                if (srr->sum_time_ms) {
                    drr->sum_time_ms = srr->sum_time_ms;
                    drr->has_sum_time_ms = true;
                }
                if (srr->num_msdus) {
                    drr->num_msdus = srr->num_msdus;
                    drr->has_num_msdus = true;
                }
                n++;
            }
            dtid->n_sojourn = n;
            dtid->sojourn = REALLOC(dtid->sojourn, n * sizeof(*dtid->sojourn));
            size += n * sizeof(*dtid->sojourn);
        }
    }
    LOGT("%s: ============= size raw: %zu alloc: %d proto struct: %d", __func__,
         sizeof(s->u.device), s->size, size);
}

static void dppline_add_stat_device(Sts__Report *r, dppline_stats_t *s)
{
    Sts__Device *sr = NULL;
    uint32_t i;
    int j;
    dppline_device_stats_t *device = &s->u.device;

    // increase the number of devices
    r->n_device++;

    // allocate or extend the size of devices
    r->device = REALLOC(r->device,
            r->n_device * sizeof(Sts__Device*));

    // allocate new buffer Sts__Device
    sr = MALLOC(sizeof(Sts__Device));
    r->device[r->n_device - 1] = sr;

    sts__device__init(sr);
    sr->timestamp_ms = device->timestamp_ms;
    sr->has_timestamp_ms = true;

    if (device->record.populated) {
        sr->load = MALLOC(sizeof(*sr->load));
        sts__device__load_avg__init(sr->load);
        sr->load->one = device->record.load[DPP_DEVICE_LOAD_AVG_ONE];
        sr->load->has_one = true;
        sr->load->five = device->record.load[DPP_DEVICE_LOAD_AVG_FIVE];
        sr->load->has_five = true;
        sr->load->fifteen = device->record.load[DPP_DEVICE_LOAD_AVG_FIFTEEN];
        sr->load->has_fifteen = true;

        sr->uptime = device->record.uptime;
        sr->has_uptime = true;

        sr->mem_util = MALLOC(sizeof(*sr->mem_util));
        sts__device__mem_util__init(sr->mem_util);
        sr->mem_util->mem_total = device->record.mem_util.mem_total;
        sr->mem_util->mem_used = device->record.mem_util.mem_used;
        sr->mem_util->swap_total = device->record.mem_util.swap_total;
        sr->mem_util->has_swap_total = true;
        sr->mem_util->swap_used = device->record.mem_util.swap_used;
        sr->mem_util->has_swap_used = true;

        sr->fs_util = MALLOC(DPP_DEVICE_FS_TYPE_QTY * sizeof(*sr->fs_util));
        sr->n_fs_util = DPP_DEVICE_FS_TYPE_QTY;
        for (i = 0; i < sr->n_fs_util; i++)
        {
            sr->fs_util[i] = MALLOC(sizeof(**sr->fs_util));
            sts__device__fs_util__init(sr->fs_util[i]);

            sr->fs_util[i]->fs_total = device->record.fs_util[i].fs_total;
            sr->fs_util[i]->fs_used = device->record.fs_util[i].fs_used;
            sr->fs_util[i]->fs_type = (Sts__FsType)device->record.fs_util[i].fs_type;
        }

        sr->cpuutil = MALLOC(sizeof(*sr->cpuutil));
        sts__device__cpu_util__init(sr->cpuutil);
        sr->cpuutil->cpu_util = device->record.cpu_util.cpu_util;
        sr->cpuutil->has_cpu_util = true;

        sr->n_ps_cpu_util = 0;
        sr->n_ps_cpu_util = device->record.n_top_cpu;
        if (sr->n_ps_cpu_util > 0)
        {
            sr->ps_cpu_util = MALLOC(sr->n_ps_cpu_util * sizeof(*sr->ps_cpu_util));
            for (i = 0; i < sr->n_ps_cpu_util; i++)
            {
                sr->ps_cpu_util[i] = MALLOC(sizeof(**sr->ps_cpu_util));
                sts__device__per_process_util__init(sr->ps_cpu_util[i]);
                sr->ps_cpu_util[i]->pid = device->record.top_cpu[i].pid;
                sr->ps_cpu_util[i]->cmd = strdup(device->record.top_cpu[i].cmd);
                sr->ps_cpu_util[i]->util = device->record.top_cpu[i].util;
            }
        }

        sr->n_ps_mem_util = 0;
        sr->n_ps_mem_util = device->record.n_top_mem;
        if (sr->n_ps_mem_util > 0)
        {
            sr->ps_mem_util = MALLOC(sr->n_ps_mem_util * sizeof(*sr->ps_mem_util));
            for (i = 0; i < sr->n_ps_mem_util; i++)
            {
                sr->ps_mem_util[i] = MALLOC(sizeof(**sr->ps_mem_util));
                sts__device__per_process_util__init(sr->ps_mem_util[i]);
                sr->ps_mem_util[i]->pid = device->record.top_mem[i].pid;
                sr->ps_mem_util[i]->cmd = strdup(device->record.top_mem[i].cmd);
                sr->ps_mem_util[i]->util = device->record.top_mem[i].util;
            }
        }
        sr->powerinfo = MALLOC(sizeof(*sr->powerinfo));
        sts__device__power_info__init(sr->powerinfo);
        if (device->record.power_info.ps_type)
        {
            sr->powerinfo->ps_type = device->record.power_info.ps_type;
            sr->powerinfo->has_ps_type = true;
        }

        if (device->record.power_info.p_consumption)
        {
            sr->powerinfo->p_consumption = device->record.power_info.p_consumption;
            sr->powerinfo->has_p_consumption = true;
        }

        if (device->record.power_info.batt_level)
        {
            sr->powerinfo->batt_level = device->record.power_info.batt_level;
            sr->powerinfo->has_batt_level = true;
        }

        sr->total_file_handles = device->record.total_file_handles;
        sr->used_file_handles  = device->record.used_file_handles;
        sr->has_total_file_handles = true;
        sr->has_used_file_handles  = true;
    }

    if (device->qty > 0)
    {
        sr->radio_temp = MALLOC(device->qty * sizeof(*sr->radio_temp));
    }
    sr->n_radio_temp = device->qty;
    for (i = 0; i < device->qty; i++)
    {
        sr->radio_temp[i] = MALLOC(sizeof(**sr->radio_temp));
        sts__device__radio_temp__init(sr->radio_temp[i]);

        sr->radio_temp[i]->band = dppline_to_proto_radio(device->list[i].type);
        sr->radio_temp[i]->has_band = true;
        sr->radio_temp[i]->value = device->list[i].value;
        sr->radio_temp[i]->has_value = true;
    }

    if (device->thermal_qty > 0)
    {
        sr->thermal_stats = MALLOC(device->thermal_qty * sizeof(*sr->thermal_stats));
    }
    sr->n_thermal_stats = device->thermal_qty;
    for (i = 0; i < device->thermal_qty; i++)
    {
        Sts__Device__Thermal *dts;
        dts = sr->thermal_stats[i] = MALLOC(sizeof(**sr->thermal_stats));
        sts__device__thermal__init(sr->thermal_stats[i]);

        if(device->thermal_list[i].fan_rpm >= 0)
        {
            sr->thermal_stats[i]->fan_rpm = device->thermal_list[i].fan_rpm;
            sr->thermal_stats[i]->has_fan_rpm = true;
        }

        if(device->thermal_list[i].fan_duty_cycle >= 0)
        {
            sr->thermal_stats[i]->fan_duty_cycle = device->thermal_list[i].fan_duty_cycle;
            sr->thermal_stats[i]->has_fan_duty_cycle = true;
        }

        if(device->thermal_list[i].thermal_state >= 0)
        {
            sr->thermal_stats[i]->thermal_state = device->thermal_list[i].thermal_state;
            sr->thermal_stats[i]->has_thermal_state = true;
        }

        if(device->thermal_list[i].target_rpm >= 0)
        {
            sr->thermal_stats[i]->target_rpm = device->thermal_list[i].target_rpm;
            sr->thermal_stats[i]->has_target_rpm = true;
        }

        sr->thermal_stats[i]->led_state = MALLOC(DPP_DEVICE_LED_COUNT * sizeof(*dts->led_state));
        sr->thermal_stats[i]->n_led_state = 0;

        for (j = 0; j < DPP_DEVICE_LED_COUNT; j++)
        {
            if (device->thermal_list[i].led_states[j].value != -1)
            {
                Sts__Device__Thermal__LedState *led_state;

                led_state = sr->thermal_stats[i]->led_state[j] = MALLOC(sizeof(**sr->thermal_stats[i]->led_state));
                sts__device__thermal__led_state__init(led_state);
                led_state->position = device->thermal_list[i].led_states[j].position;
                led_state->has_position = true;
                led_state->value = device->thermal_list[i].led_states[j].value;
                led_state->has_value = true;

                sr->thermal_stats[i]->n_led_state++;
            }
        }

        sr->thermal_stats[i]->timestamp_ms = device->thermal_list[i].timestamp_ms;
        sr->thermal_stats[i]->has_timestamp_ms = true;

        sr->thermal_stats[i]->txchainmask = MALLOC(DPP_DEVICE_TX_CHAINMASK_MAX * sizeof(*dts->txchainmask));
        sr->thermal_stats[i]->n_txchainmask = 0;

        for(j = 0; j < DPP_DEVICE_TX_CHAINMASK_MAX; j++)
        {
            Sts__Device__Thermal__RadioTxChainMask  *txchainmask;
            if (device->thermal_list[i].radio_txchainmasks[j].type != RADIO_TYPE_NONE)
            {
                txchainmask = sr->thermal_stats[i]->txchainmask[j] = MALLOC(sizeof(**sr->thermal_stats[i]->txchainmask));
                sts__device__thermal__radio_tx_chain_mask__init(txchainmask);
                txchainmask->band =  dppline_to_proto_radio(device->thermal_list[i].radio_txchainmasks[j].type);
                txchainmask->has_band = true;
                txchainmask->value = device->thermal_list[i].radio_txchainmasks[j].value;
                txchainmask->has_value = true;

                sr->thermal_stats[i]->n_txchainmask++;
            }
        }
    }
}

static void dppline_add_stat_capacity(Sts__Report *r, dppline_stats_t *s)
{
    Sts__Capacity *sr = NULL;
    uint32_t i;
    dppline_capacity_stats_t *capacity = &s->u.capacity;

    // increase the number of capacities
    r->n_capacity++;

    // allocate or extend the size of capacities
    r->capacity = REALLOC(r->capacity,
            r->n_capacity * sizeof(Sts__Capacity*));

    // allocate new buffer Sts__Capacity
    sr = MALLOC(sizeof(Sts__Capacity));
    r->capacity[r->n_capacity - 1] = sr;

    sts__capacity__init(sr);
    sr->band = dppline_to_proto_radio(capacity->radio_type);
    sr->timestamp_ms = capacity->timestamp_ms;
    sr->has_timestamp_ms = true;
    sr->queue_list = MALLOC(capacity->qty * sizeof(*sr->queue_list));
    sr->n_queue_list = capacity->qty;
    for (i = 0; i < capacity->qty; i++)
    {
        dpp_capacity_record_t *rec = &capacity->list[i];

        Sts__Capacity__QueueSample *dr; // dest rec
        dr = sr->queue_list[i] = MALLOC(sizeof(**sr->queue_list));
        sts__capacity__queue_sample__init(dr);

        dr->bytes_tx = rec->bytes_tx;
        dr->busy_tx = rec->busy_tx;
        dr->sample_count = rec->samples;

        dr->has_busy_tx = true;
        dr->has_bytes_tx = true;
        dr->has_sample_count = true;

        if (rec->queue[RADIO_QUEUE_TYPE_VO])
        {
            dr->vo_count = rec->queue[RADIO_QUEUE_TYPE_VO];
            dr->has_vo_count = true;
        }
        if (rec->queue[RADIO_QUEUE_TYPE_VI])
        {
            dr->vi_count = rec->queue[RADIO_QUEUE_TYPE_VI];
            dr->has_vi_count = true;
        }
        if (rec->queue[RADIO_QUEUE_TYPE_BE])
        {
            dr->be_count = rec->queue[RADIO_QUEUE_TYPE_BE];
            dr->has_be_count = true;
        }
        if (rec->queue[RADIO_QUEUE_TYPE_BK])
        {
            dr->bk_count = rec->queue[RADIO_QUEUE_TYPE_BK];
            dr->has_bk_count = true;
        }
        if (rec->queue[RADIO_QUEUE_TYPE_CAB])
        {
            dr->cab_count = rec->queue[RADIO_QUEUE_TYPE_CAB];
            dr->has_cab_count = true;
        }
        if (rec->queue[RADIO_QUEUE_TYPE_BCN])
        {
            dr->bcn_count = rec->queue[RADIO_QUEUE_TYPE_BCN];
            dr->has_bcn_count = true;
        }
        dr->offset_ms = sr->timestamp_ms - rec->timestamp_ms;
        dr->has_offset_ms = true;
    }
    /* LOG(DEBUG, "============= %s size raw: %d proto struct: %d", __FUNCTION__,
            sizeof(s->u.capacity) + s->u.capacity.numrec * sizeof(dpp_capacity_record_t),
            sizeof(Sts__Capacity*) +
            sizeof(Sts__Capacity) +
            s->u.capacity.numrec * sizeof(*sr->queue_list) +
            s->u.capacity.numrec * sizeof(**sr->queue_list)); */
}

static void dppline_add_stat_bs_client(Sts__Report * r, dppline_stats_t * s)
{
    Sts__BSReport *sr = NULL;
    Sts__BSClient *cr;
    Sts__BSClient__BSBandReport *br;
    Sts__BSClient__BSEvent *er;

    dppline_bs_client_stats_t *bs_client = &s->u.bs_client;

    uint32_t client, band, event, band_report;

    if (0 == bs_client->qty) {
        // if stats contain no clients omit sending an empty report
        return;
    }

    // increase the number of bs reports
    r->n_bs_report++;

    // allocate or extend the size of bs_report array
    r->bs_report = REALLOC(r->bs_report,
            r->n_bs_report * sizeof(Sts__BSReport*));
    assert(r->bs_report);

    // allocate new buffer Sts__BSReport
    sr = MALLOC(sizeof(Sts__BSReport));

    // append report
    r->bs_report[r->n_bs_report - 1] = sr;

    // Initialize and copy timestamp
    sts__bsreport__init(sr);
    sr->timestamp_ms = bs_client->timestamp_ms;

    // Append clients, so client array needs to be resided
    sr->clients = REALLOC(sr->clients,
            (sr->n_clients + bs_client->qty) * sizeof(*sr->clients));
    assert(sr->clients);

    // For each client, do:
    //  1. Allocate memory for client
    //  2. Copy client specific data
    //      For each band, do:
    //      1. Allocate memory for the band record
    //      2. Copy band specific data
    //          For each event, do:
    //              1. Allocate memory for the event record
    //              2. Copy event specific data

    // For each client in the report:
    for (client = 0; client < bs_client->qty; client++)
    {
        dpp_bs_client_record_t *c_rec = &bs_client->list[client];

        // Allocate memory for the BS Client
        cr = sr->clients[sr->n_clients] = MALLOC(sizeof(**sr->clients));
        sr->n_clients++;
        sts__bsclient__init(cr);

        cr->mac_address = MALLOC(MACADDR_STR_LEN);
        dpp_mac_to_str(c_rec->mac, cr->mac_address);

        cr->mld_address = MALLOC(MACADDR_STR_LEN);
        dpp_mac_to_str(c_rec->mld_address, cr->mld_address);

        // alloc band list
        //printf("--- encode: %s\n", cr->mac_address);
        /* Calc bands we should alloc */
        cr->n_bs_band_report = 0;
        for (band = 0; band < c_rec->num_band_records; band++)
        {
            dpp_bs_client_band_record_t *b_rec = &c_rec->band_record[band];

            if (b_rec->type == RADIO_TYPE_NONE) {
                continue;
            }

            cr->n_bs_band_report++;
        }

        cr->bs_band_report = CALLOC(cr->n_bs_band_report, sizeof(*cr->bs_band_report));

        // For each band per client
        for (band = 0, band_report = 0; band < c_rec->num_band_records; band++)
        {
            dpp_bs_client_band_record_t *b_rec = &c_rec->band_record[band];

            if (b_rec->type == RADIO_TYPE_NONE) {
                continue;
            }

            // Allocate memory for the band report
            br = cr->bs_band_report[band_report] = MALLOC(sizeof(Sts__BSClient__BSBandReport));
            band_report++;
            sts__bsclient__bsband_report__init(br);

            // Copy all band specific information
            br->band            = dppline_to_proto_radio(b_rec->type);

            br->connected       = b_rec->connected;
            br->has_connected   = true;

            br->rejects         = b_rec->rejects;
            br->has_rejects     = true;

            br->connects        = b_rec->connects;
            br->has_connects    = true;

            br->disconnects     = b_rec->disconnects;
            br->has_disconnects = true;

            br->activity_changes = b_rec->activity_changes;
            br->has_activity_changes = true;

            br->steering_success_cnt = b_rec->steering_success_cnt;
            br->has_steering_success_cnt = true;

            br->steering_fail_cnt = b_rec->steering_fail_cnt;
            br->has_steering_fail_cnt = true;

            br->steering_kick_cnt = b_rec->steering_kick_cnt;
            br->has_steering_kick_cnt = true;

            br->sticky_kick_cnt = b_rec->sticky_kick_cnt;
            br->has_sticky_kick_cnt = true;

            br->probe_bcast_cnt = b_rec->probe_bcast_cnt;
            br->has_probe_bcast_cnt = true;

            br->ifname = strdup(b_rec->ifname);

            // alloc event list
            br->event_list = CALLOC(b_rec->num_event_records, sizeof(*br->event_list));
            br->n_event_list = b_rec->num_event_records;

            // copy each event
            for (event = 0; event < b_rec->num_event_records; event++)
            {
                dpp_bs_client_event_record_t *e_rec = &b_rec->event_record[event];

                // alloc event
                er = br->event_list[event] = MALLOC(sizeof(Sts__BSClient__BSEvent));
                sts__bsclient__bsevent__init(er);

                er->type = (Sts__BSEventType)e_rec->type;

                er->offset_ms = bs_client->timestamp_ms - e_rec->timestamp_ms;

                er->rssi = e_rec->rssi;
                er->has_rssi = true;

                er->probe_bcast = e_rec->probe_bcast;
                er->has_probe_bcast = true;

                er->probe_blocked = e_rec->probe_blocked;
                er->has_probe_blocked = true;

                er->disconnect_src = (Sts__DisconnectSrc)e_rec->disconnect_src;
                er->has_disconnect_src = true;

                er->disconnect_type = (Sts__DisconnectType)e_rec->disconnect_type;
                er->has_disconnect_type = true;

                er->disconnect_reason = e_rec->disconnect_reason;
                er->has_disconnect_reason = true;

                er->backoff_enabled = e_rec->backoff_enabled;
                er->has_backoff_enabled = true;

                er->backoff_period = e_rec->backoff_period;
                er->has_backoff_period = true;
                er->active = e_rec->active;
                er->has_active = true;

                er->rejected = e_rec->rejected;
                er->has_rejected = true;

                er->is_btm_supported = e_rec->is_BTM_supported;
                er->has_is_btm_supported = true;

                er->is_rrm_supported = e_rec->is_RRM_supported;
                er->has_is_rrm_supported = true;

                er->band_cap_2g = e_rec->band_cap_2G;
                er->has_band_cap_2g = true;

                er->band_cap_5g = e_rec->band_cap_5G;
                er->has_band_cap_5g = true;

                er->band_cap_6g = e_rec->band_cap_6G;
                er->has_band_cap_6g = true;

                er->max_chwidth = e_rec->max_chwidth;
                er->has_max_chwidth = true;

                er->max_streams = e_rec->max_streams;
                er->has_max_streams = true;

                er->phy_mode = e_rec->phy_mode;
                er->has_phy_mode = true;

                er->max_mcs = e_rec->max_MCS;
                er->has_max_mcs = true;

                er->max_txpower = e_rec->max_txpower;
                er->has_max_txpower = true;

                er->is_static_smps = e_rec->is_static_smps;
                er->has_is_static_smps = true;

                er->is_mu_mimo_supported = e_rec->is_mu_mimo_supported;
                er->has_is_mu_mimo_supported = true;

                er->rrm_caps_link_meas = e_rec->rrm_caps_link_meas;
                er->has_rrm_caps_link_meas = true;

                er->rrm_caps_neigh_rpt = e_rec->rrm_caps_neigh_rpt;
                er->has_rrm_caps_neigh_rpt = true;

                er->rrm_caps_bcn_rpt_passive = e_rec->rrm_caps_bcn_rpt_passive;
                er->has_rrm_caps_bcn_rpt_passive = true;

                er->rrm_caps_bcn_rpt_active = e_rec->rrm_caps_bcn_rpt_active;
                er->has_rrm_caps_bcn_rpt_active = true;

                er->rrm_caps_bcn_rpt_table = e_rec->rrm_caps_bcn_rpt_table;
                er->has_rrm_caps_bcn_rpt_table = true;

                er->rrm_caps_lci_meas = e_rec->rrm_caps_lci_meas;
                er->has_rrm_caps_lci_meas = true;

                er->rrm_caps_ftm_range_rpt = e_rec->rrm_caps_ftm_range_rpt;
                er->has_rrm_caps_ftm_range_rpt = true;

                if (e_rec->assoc_ies_len) {
                    er->assoc_ies.data = MALLOC(e_rec->assoc_ies_len);
                    if (er->assoc_ies.data) {
                        memcpy(er->assoc_ies.data, e_rec->assoc_ies, e_rec->assoc_ies_len);
                        er->assoc_ies.len = e_rec->assoc_ies_len;
                        er->has_assoc_ies = true;
                    }
                }

                er->btm_status = e_rec->btm_status;
                er->has_btm_status = true;
            }
        }
    }
}

Sts__RssiPeer__RssiSource dppline_to_proto_rssi_source(rssi_source_t rssi_source)
{
    switch (rssi_source)
    {
        case RSSI_SOURCE_CLIENT:
            return STS__RSSI_PEER__RSSI_SOURCE__CLIENT;

        case RSSI_SOURCE_PROBE:
            return STS__RSSI_PEER__RSSI_SOURCE__PROBE;

        case RSSI_SOURCE_NEIGHBOR:
            return STS__RSSI_PEER__RSSI_SOURCE__NEIGHBOR;

        default:
            assert(0);
    }
    return 0;
}

static void dppline_add_stat_rssi(Sts__Report *r, dppline_stats_t *s)
{
    Sts__RssiReport *sr = NULL;
    Sts__RssiPeer *dr; // dest rec
    uint32_t i = 0;
    int j;
    dppline_rssi_stats_t *rssi = &s->u.rssi;

    // increase the number of rssi_report
    r->n_rssi_report++;

    // allocate or extend the size of rssi_report
    r->rssi_report = REALLOC(r->rssi_report,
            r->n_rssi_report * sizeof(Sts__RssiReport*));

    // allocate new buffer
    sr = MALLOC(sizeof(Sts__RssiReport));
    r->rssi_report[r->n_rssi_report - 1] = sr;

    sts__rssi_report__init(sr);
    sr->band = dppline_to_proto_radio(rssi->radio_type);
    sr->report_type = dppline_to_proto_report_type(rssi->report_type);
    sr->timestamp_ms = rssi->timestamp_ms;
    sr->has_timestamp_ms = true;
    sr->peer_list = MALLOC(rssi->qty * sizeof(*sr->peer_list));
    sr->n_peer_list = rssi->qty;
    for (i = 0; i < rssi->qty; i++)
    {
        dpp_rssi_record_t *rec = &rssi->list[i].rec;
        dr = sr->peer_list[i] = MALLOC(sizeof(**sr->peer_list));
        sts__rssi_peer__init(dr);

        dr->mac_address = MALLOC(MACADDR_STR_LEN);
        dpp_mac_to_str(rec->mac, dr->mac_address);

        if (rec->source) {
            dr->rssi_source = dppline_to_proto_rssi_source(rec->source);
            dr->has_rssi_source = true;
        }

        if (REPORT_TYPE_RAW == rssi->report_type) {
            dr->rssi_list = MALLOC(rssi->list[i].raw_qty * sizeof(*dr->rssi_list));
            dr->n_rssi_list = rssi->list[i].raw_qty;
            for (j = 0; j < rssi->list[i].raw_qty; j++)
            {
                Sts__RssiPeer__RssiSample   *draw;
                dpp_rssi_raw_t  *sraw = &rssi->list[i].raw[j];

                draw = dr->rssi_list[j] = MALLOC(sizeof(**dr->rssi_list));
                sts__rssi_peer__rssi_sample__init(draw);

                draw->rssi = sraw->rssi;
                if (sraw->timestamp_ms) {
                    draw->offset_ms = sr->timestamp_ms - sraw->timestamp_ms;
                    draw->has_offset_ms = true;
                }
            }
        }
        else {
            Sts__AvgType   *davg;
            dpp_avg_t      *savg = &rssi->list[i].rec.rssi.avg;

            davg = dr->rssi_avg = MALLOC(sizeof(*dr->rssi_avg));
            sts__avg_type__init(davg);

            if (savg->avg) {
                davg->avg = savg->avg;
                if(savg->min) {
                    davg->min = savg->min;
                    davg->has_min = true;;
                }
                if(savg->max) {
                    davg->max = savg->max;
                    davg->has_max = true;;
                }
                if(savg->num) {
                    davg->num = savg->num;
                    davg->has_num = true;;
                }
            }
        }

        if (rec->rx_ppdus) {
            dr->rx_ppdus = rec->rx_ppdus;
            dr->has_rx_ppdus = true;
        }

        if (rec->tx_ppdus) {
            dr->tx_ppdus = rec->tx_ppdus;
            dr->has_tx_ppdus = true;
        }
    }
}

static void dppline_add_stat_client_auth_fails(Sts__Report *r, dppline_stats_t *s)
{
    dppline_client_auth_fails_stats_t *client_auth_fails = &s->u.client_auth_fails;
    Sts__ClientAuthFailsReport *sr = NULL;
    size_t i;
    size_t j;

    // increase the number of client_auth_fails_report
    r->n_client_auth_fails_report++;

    // allocate or extend the size of client_auth_fails_report
    r->client_auth_fails_report = REALLOC(r->client_auth_fails_report, r->n_client_auth_fails_report * sizeof(Sts__ClientAuthFailsReport*));

    // allocate new buffer
    sr = MALLOC(sizeof(Sts__ClientAuthFailsReport));
    r->client_auth_fails_report[r->n_client_auth_fails_report - 1] = sr;

    sts__client_auth_fails_report__init(sr);
    sr->band = dppline_to_proto_radio(client_auth_fails->radio_type);
    sr->bss_list = MALLOC(client_auth_fails->qty * sizeof(*sr->bss_list));
    sr->n_bss_list = client_auth_fails->qty;
    for (i = 0; i < client_auth_fails->qty; i++)
    {
        dppline_client_auth_fails_bss_rec_t *bss;
        Sts__ClientAuthFailsReport__BSS *br;

        bss = &client_auth_fails->list[i];
        br = sr->bss_list[i] = MALLOC(sizeof(Sts__ClientAuthFailsReport__BSS));

        sts__client_auth_fails_report__bss__init(br);
        br->ifname = strdup(bss->if_name);

        br->client_list = MALLOC(bss->qty * sizeof(*br->client_list));
        br->n_client_list = bss->qty;

        for (j = 0; j < bss->qty; j++)
        {
            dppline_client_auth_fails_client_rec_t *client;
            Sts__ClientAuthFailsReport__BSS__Client *cr;

            client = &bss->list[j];
            cr = br->client_list[j] = MALLOC(sizeof(Sts__ClientAuthFailsReport__BSS__Client));

            sts__client_auth_fails_report__bss__client__init(cr);
            cr->mac_address = strdup(client->mac);
            cr->auth_fails = client->auth_fails;
            cr->invalid_psk = client->invalid_psk;
        }
    }
}

static void dppline_add_stat_radius(Sts__Report *r, dppline_stats_t *s)
{
    Sts__RadiusReport *sr = NULL;
    dppline_radius_stats_t *dpp_radius = &s->u.radius;
    unsigned int list_index;

    /* Allocate or extend the size of the radius_report and increment counter. */
    r->n_radius_report++;
    r->radius_report = REALLOC(r->radius_report,
            r->n_radius_report * sizeof(Sts__RadiusReport*));

    /* Allocate new buffer for RadiusReport protobuf object */
    sr = MALLOC(sizeof(Sts__RadiusReport));

    sts__radius_report__init(sr);
    sr->radius_list = MALLOC(dpp_radius->qty * sizeof(*sr->radius_list));
    sr->timestamp_ms = dpp_radius->timestamp_ms;
    sr->has_timestamp_ms = true;
    sr->n_radius_list = dpp_radius->qty;

    r->radius_report[r->n_radius_report - 1] = sr;

    for (list_index = 0; list_index < dpp_radius->qty; list_index++)
    {
        dppline_radius_stats_rec_t *record;
        Sts__RadiusReport__RadiusRecord *rr;

        record = dpp_radius->list[list_index];
        rr = sr->radius_list[list_index] = MALLOC(sizeof(*rr));
        sts__radius_report__radius_record__init(rr);

        rr->vif_name      = strdup(record->vif_name);
        rr->vif_role      = strdup(record->vif_role);
        rr->serveraddress = strdup(record->radiusAuthServerAddress);

        rr->serverindex                    = record->radiusAuthServerIndex;
        rr->clientserverportnumber         = record->radiusAuthClientServerPortNumber;
        rr->clientroundtriptime            = record->radiusAuthClientRoundTripTime;
        rr->clientaccessrequests           = record->radiusAuthClientAccessRequests;
        rr->clientaccessretransmissions    = record->radiusAuthClientAccessRetransmissions;
        rr->clientaccessaccepts            = record->radiusAuthClientAccessAccepts;
        rr->clientaccessrejects            = record->radiusAuthClientAccessRejects;
        rr->clientaccesschallenges         = record->radiusAuthClientAccessChallenges;
        rr->clientmalformedaccessresponses = record->radiusAuthClientMalformedAccessResponses;
        rr->clientbadauthenticators        = record->radiusAuthClientBadAuthenticators;
        rr->clientpendingrequests          = record->radiusAuthClientPendingRequests;
        rr->clienttimeouts                 = record->radiusAuthClientTimeouts;
        rr->clientunknowntypes             = record->radiusAuthClientUnknownTypes;
        rr->clientpacketsdropped           = record->radiusAuthClientPacketsDropped;
    }
}

static void dppline_add_stat(Sts__Report * r, dppline_stats_t * s)
{
    switch(s->type)
    {
        case DPP_T_SURVEY:
            dppline_add_stat_survey(r, s);
            break;

        case DPP_T_CAPACITY:
            dppline_add_stat_capacity(r, s);
            break;

        case DPP_T_NEIGHBOR:
            dppline_add_stat_neighbor(r, s);
            break;

        case DPP_T_CLIENT:
            dppline_add_stat_client(r, s);
            break;

        case DPP_T_DEVICE:
            dppline_add_stat_device(r, s);
            break;

        case DPP_T_BS_CLIENT:
            dppline_add_stat_bs_client(r, s);
            break;

        case DPP_T_RSSI:
            dppline_add_stat_rssi(r, s);
            break;

        case DPP_T_CLIENT_AUTH_FAILS:
            dppline_add_stat_client_auth_fails(r, s);
            break;

        case DPP_T_RADIUS_STATS:
            dppline_add_stat_radius(r, s);
            break;

        default:
            LOG(ERR, "Failed to add %d to stats report", s->type);
            /* do nothing       */
            break;
    }

}


/*
 * Genetic function for removing a single stat from queue head
 */
bool dppline_remove_head()
{
    dppline_stats_t * s = NULL;

    /* get head if now queue node given     */
    s = ds_dlist_head(&g_dppline_list);

    /* remove head element                  */
    ds_dlist_remove_head(&g_dppline_list);

    /* reduce queue depth                   */
    queue_depth--;
    queue_size -= s->size;

    /* free allocated memory                */
    dppline_free_stat(s);

    return true;
}

void dppline_log_queue()
{
    LOGT( "Q len: %d size: %d\n", queue_depth, queue_size );
}

/*
 * Genetic function for adding new stats to internal queue
 */
static bool dppline_put(DPP_STS_TYPE type, void * rpt)
{
    dppline_stats_t *s = NULL;

    /* allocate buffer          */
    s = dpp_alloc_stat();
    if (!s)
    {
        LOG(ERR, "Failed add %d to stats queue", type);
        return false;
    }

    /* set stats buffer type    */
    s->type = type;

    /* copy stats               */
    if (!dppline_copysts(s, rpt))
    {
        dppline_free_stat(s);
        return false;
    }

    /* insert new element into stats queue  */
    ds_dlist_insert_tail(&g_dppline_list, s);

    // update counters
    queue_depth++;
    queue_size += s->size;

    // drop old entries if queue too long
    if (queue_depth > DPP_MAX_QUEUE_DEPTH
            || queue_size > DPP_MAX_QUEUE_SIZE_BYTES)
    {
        LOG(WARN, "Queue size exceeded %d > %d || %d > %d",
                queue_depth, DPP_MAX_QUEUE_DEPTH,
                queue_size, DPP_MAX_QUEUE_SIZE_BYTES);
        dppline_remove_head();
    }

    dppline_log_queue();

    return true;
}

/* Initialize library     */
bool dpp_init()
{
    LOG(INFO,
        "Initializing DPP library.\n");

    ds_dlist_init(&g_dppline_list, struct dpp_stats, dnode);

    /* reset the queue depth counter    */
    queue_depth = 0;

    return true;
}

/*
 * Put survey stats to internal queue
 */
bool dpp_put_survey(dpp_survey_report_data_t *rpt)
{
    return dppline_put(DPP_T_SURVEY, rpt);
}

/*
 * Put capacity stats to internal queue
 */
bool dpp_put_capacity(dpp_capacity_report_data_t* rpt)
{
    return dppline_put(DPP_T_CAPACITY, rpt);
}

/*
 * Put neighbor stats to internal queue
 */
bool dpp_put_neighbor(dpp_neighbor_report_data_t *rpt)
{
    return dppline_put(DPP_T_NEIGHBOR, rpt);
}

/*
 * Put client stats to internal queue
 */
bool dpp_put_client(dpp_client_report_data_t *rpt)
{
    return dppline_put(DPP_T_CLIENT, rpt);
}

/*
 * Put client stats to internal queue
 */
bool dpp_put_device(dpp_device_report_data_t * rpt)
{
    return dppline_put(DPP_T_DEVICE, rpt);
}

bool dpp_put_bs_client(dpp_bs_client_report_data_t *rpt)
{
    if (ds_dlist_is_empty(&rpt->list)) {
        // ignore empty reports
        return true;
    }
    return dppline_put(DPP_T_BS_CLIENT, rpt);
}

/*
 * Put client stats to internal queue
 */
bool dpp_put_rssi(dpp_rssi_report_data_t * rpt)
{
    return dppline_put(DPP_T_RSSI, rpt);
}

bool dpp_put_client_auth_fails(dpp_client_auth_fails_report_data_t *rpt)
{
    return dppline_put(DPP_T_CLIENT_AUTH_FAILS, rpt);
}

bool dpp_put_radius_stats(dpp_radius_stats_report_data_t *rpt)
{
    return dppline_put(DPP_T_RADIUS_STATS, rpt);
}

/*
 * Create the protobuf buff and copy it to given buffer
 */
#ifndef DPP_FAST_PACK
bool dpp_get_report(uint8_t * buff, size_t sz, uint32_t * packed_sz)
{
    ds_dlist_iter_t iter;
    dppline_stats_t *s;
    bool ret = false;
    size_t tmp_packed_size; /* packed size of current report */

    /* prevent sending empty reports */
    if (dpp_get_queue_elements() == 0)
    {
        LOG(DEBUG, "get_report: queue depth is zero");
        return false;
    }

    /* stop any further actions in case improper buffer submitted */
    if (NULL == buff || sz == 0)
    {
        LOG(DEBUG, "get_report: invalid buffer or size");
        return false;
    }

    /* initialize report structure. Note - it has to be on heap,
     * otherwise __free_unpacked function fails
     */
    Sts__Report * report = MALLOC(sizeof(Sts__Report));
    sts__report__init(report);
    report->nodeid = getNodeid();

    for (s = ds_dlist_ifirst(&iter, &g_dppline_list); s != NULL; s = ds_dlist_inext(&iter))
    {
        /* try to add new stats data to protobuf report */
        dppline_add_stat(report, s);

        tmp_packed_size = sts__report__get_packed_size(report);

        /* check the size, if size too small break the process */
        if (sz < tmp_packed_size)
        {
            LOG(WARNING, "Packed size: %5zd, buffer size: %5zd ",
                tmp_packed_size,
                sz);

            /* break if size exceeded */
            break; /* for loop   */;
        }
        else
        {
            /* pack current report to return buffer */
            *packed_sz = sts__report__pack(report, buff);

            /* remove item from the list and free memory */
            s = ds_dlist_iremove(&iter);

            /* decrease queue depth */
            if (0 == queue_depth)
            {
                LOG(ERR, "Queue depth zero but dpp is keep adding");
                break;
            }

            queue_size -= s->size;
            queue_depth--;

            /* at least one stat report is in protobuf, good
             * reason to announce success
             */
            ret = true;

            /* free internal stats structure */
            dppline_free_stat(s);
        }
    }

    /* in any case,
     * free memory used for report using system allocator
     */
    sts__report__free_unpacked(report, NULL);
    dppline_log_queue();

    return ret;
}
#else
bool dpp_get_report2(uint8_t **pbuff, size_t suggest_sz, uint32_t *packed_sz)
{
    ds_dlist_iter_t iter;
    dppline_stats_t *s;
    bool ret = false;
    size_t packed_size; // packed size of current report
    size_t unpacked_size = 0; // unpacked size of current report
    uint8_t *buff;

    // prevent sending empty reports
    if (dpp_get_queue_elements() == 0)
    {
        LOG(DEBUG, "get_report: queue depth is zero");
        return false;
    }

    // verify suggested size
    if (suggest_sz == 0)
    {
        LOG(DEBUG, "get_report: suggest_sz is zero");
        return false;
    }

    buff = MALLOC(suggest_sz);

    /* initialize report structure. Note - it has to be on heap,
     * otherwise __free_unpacked function fails
     */
    Sts__Report * report = MALLOC(sizeof(Sts__Report));
    sts__report__init(report);
    report->nodeid = getNodeid();

    for (s = ds_dlist_ifirst(&iter, &g_dppline_list); s != NULL; s = ds_dlist_inext(&iter))
    {
        // add new stats data to protobuf report
        dppline_add_stat(report, s);

        // at least one stat report is in protobuf, mark success
        ret = true;

        // remove item from the list and free memory
        s = ds_dlist_iremove(&iter);

        // decrease queue depth and size
        if (0 == queue_depth)
        {
            LOG(ERR, "Queue depth zero but dpp list not empty");
        }
        else
        {
            queue_depth--;
        }
        queue_size -= s->size;

        // calc approx unpacked size
        unpacked_size += s->size + sizeof(s->u);

        // free internal stats structure
        dppline_free_stat(s);

        // unpacked compare is faster, as long as unpacked is smaller than
        // suggested we don't need to calculate packed size
        if (unpacked_size > suggest_sz)
        {
            // compute packed size
            packed_size = sts__report__get_packed_size(report);
            if (packed_size > suggest_sz)
            {
                // don't keep adding, stop here
                goto L_resize;
            }
        }
    }

    // compute packed size
    packed_size = sts__report__get_packed_size(report);

    // if buff size too small increase buff
    if (packed_size > suggest_sz)
    {
        L_resize:
        LOG(DEBUG, "increasing buffer size %d to packed size: %5d",
                (int)suggest_sz, (int)packed_size);
        buff = REALLOC(buff, packed_size);
    }

    *pbuff = buff;

    // pack current report to return buffer
    *packed_sz = sts__report__pack(report, buff);

    // free memory used for report using system allocator
    sts__report__free_unpacked(report, NULL);

    // debug
    dppline_log_queue();

    return ret;
}
#endif


/*
 * Count the number of stats in queue
 */
int dpp_get_queue_elements()
{
    dppline_stats_t * s = NULL;
    ds_dlist_iter_t iter;
    uint32_t queue = 0;

    /* iterate the queue and count the number of elements */
    for (s = ds_dlist_ifirst(&iter, &g_dppline_list); s != NULL; s = ds_dlist_inext(&iter))
    {
        queue++;
    }

    if (queue != queue_depth)
    {
        LOG(ERR, "Queue depth mismatch %d != %d", queue, queue_depth);
    }

    return queue;
}

// alloc and init a dpp_client_record_t
dpp_client_record_t* dpp_client_record_alloc()
{
    dpp_client_record_t *record = NULL;

    record = MALLOC(sizeof(dpp_client_record_t));
    memset(record, 0, sizeof(dpp_client_record_t));

    // init stats_rx dlist
    ds_dlist_init(&record->stats_rx, dpp_client_stats_rx_t, node);

    // init stats_tx dlist
    ds_dlist_init(&record->stats_tx, dpp_client_stats_tx_t, node);

    // init tid_record_list dlist
    ds_dlist_init(&record->tid_record_list, dpp_client_tid_record_list_t, node);

    return record;
}
