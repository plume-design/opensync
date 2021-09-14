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
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "lte_info.h"
#include "memutil.h"
#include "log.h"
#include "target.h"
#include "qm_conn.h"
#include "lte_info.h"
#include "ltem_mgr.h"

struct lte_info_packed_buffer *lte_serialized = NULL;
struct lte_info_report *lte_report = NULL;

char *lte_deployment = "ci";
char lte_mqtt_topic[256];

static uint32_t lte_request_id = 0;

static int
lte_send_report(char *topic, struct lte_info_packed_buffer *pb)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    if (!topic)
    {
        LOGE("%s: topic NULL", __func__);
        return -1;
    }

    if (!pb)
    {
        LOGE("%s: pb NULL", __func__);
        return -1;
    }

    if (!pb->buf)
    {
        LOGE("%s: pb->buf NULL", __func__);
        return -1;
    }

    LOGI("%s: msg len: %zu, topic: %s",
         __func__, pb->len, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
                              pb->buf, pb->len, &res);
    if (!ret)
    {
        LOGE("error sending mqtt with topic %s", topic);
        return -1;
    }
#endif

    return 0;
}

/**
 * @brief Set the common header
 */
int
lte_set_common_header(struct lte_info_report *lte_report)
{
    bool ret;
    struct lte_common_header common_header;
    ltem_mgr_t *mgr = ltem_get_mgr();

    if (!lte_report) return -1;

    MEMZERO(common_header);
    common_header.request_id = lte_request_id++;
    common_header.if_name = mgr->lte_config_info->if_name;
    common_header.node_id = mgr->node_id;
    common_header.location_id = mgr->location_id;
    common_header.imei = mgr->modem_info.imei;
    common_header.imsi = mgr->modem_info.imsi;
    common_header.iccid = mgr->modem_info.iccid;
    // reported_at is set in lte_info_set_common_header
    ret = lte_info_set_common_header(&common_header, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set the lte_net_info in the report
 */
int
lte_set_net_info(struct lte_info_report *lte_report)
{
    bool ret;
    struct lte_net_info net_info;
    ltem_mgr_t *mgr = ltem_get_mgr();

    net_info.net_status = mgr->modem_info.reg_status;
    net_info.rssi = mgr->modem_info.rssi;
    net_info.ber = mgr->modem_info.ber;
    ret = lte_info_set_net_info(&net_info, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set lte_data_usage in the report
 */
int
lte_set_data_usage(struct lte_info_report *lte_report)
{
    bool ret;
    ltem_mgr_t *mgr = ltem_get_mgr();
    struct lte_data_usage data_usage;

    data_usage.rx_bytes = mgr->modem_info.rx_bytes;
    data_usage.tx_bytes = mgr->modem_info.tx_bytes;
    data_usage.failover_start = mgr->lte_state_info->lte_failover_start;
    data_usage.failover_end = mgr->lte_state_info->lte_failover_end;
    data_usage.failover_count = mgr->lte_state_info->lte_failover_count;
    ret = lte_info_set_data_usage(&data_usage, lte_report);
    if (!ret) return -1;

    return 0;

}

/**
 * @brief test setting a serving cell info
 */
int
lte_set_serving_cell(struct lte_info_report *lte_report)
{
    bool ret;
    ltem_mgr_t *mgr = ltem_get_mgr();
    struct lte_net_serving_cell_info srv_cell_info;
    lte_serving_cell_info_t *srv_cell;

    MEMZERO(srv_cell_info);
    srv_cell = &mgr->modem_info.srv_cell;

    srv_cell_info.state = srv_cell->state;
    srv_cell_info.mode = srv_cell->mode;
    srv_cell_info.fdd_tdd_mode = srv_cell->fdd_tdd_mode;
    srv_cell_info.cellid = srv_cell->cellid;
    srv_cell_info.pcid = srv_cell->pcid;
    srv_cell_info.earfcn = srv_cell->earfcn;
    srv_cell_info.freq_band = srv_cell->freq_band;
    srv_cell_info.ul_bandwidth = srv_cell->ul_bandwidth;
    srv_cell_info.dl_bandwidth = srv_cell->dl_bandwidth;
    srv_cell_info.tac = srv_cell->tac;
    srv_cell_info.rsrp = srv_cell->rsrp;
    srv_cell_info.rsrq = srv_cell->rsrq;
    srv_cell_info.rssi = srv_cell->rssi;
    srv_cell_info.sinr = srv_cell->sinr;
    srv_cell_info.srxlev = srv_cell->srxlev;

    ret = lte_info_set_serving_cell(&srv_cell_info, lte_report);
    if (!ret) return -1;

    return 0;
}


/**
 * @brief Set intra neighbor cell info in the report
 */
int
lte_set_neigh_cell_intra_info(struct lte_info_report *lte_report)
{
    bool ret;
    ltem_mgr_t *mgr = ltem_get_mgr();
    struct lte_net_neighbor_cell_info neigh_cell_info;
    lte_neighbor_cell_intra_info_t *neigh_cell;

    MEMZERO(neigh_cell_info);
    neigh_cell = &mgr->modem_info.neigh_cell_intra;

    neigh_cell_info.mode = neigh_cell->mode;
    neigh_cell_info.freq_mode = neigh_cell->freq_mode;
    neigh_cell_info.earfcn = neigh_cell->earfcn;
    neigh_cell_info.pcid = neigh_cell->pcid;
    neigh_cell_info.rsrq = neigh_cell->rsrq;
    neigh_cell_info.rsrp = neigh_cell->rsrp;
    neigh_cell_info.rssi = neigh_cell->rssi;
    neigh_cell_info.sinr = neigh_cell->sinr;
    neigh_cell_info.srxlev = neigh_cell->srxlev;
    neigh_cell_info.cell_resel_priority = neigh_cell->cell_resel_priority;
    neigh_cell_info.s_non_intra_search = neigh_cell->s_non_intra_search;
    neigh_cell_info.thresh_serving_low = neigh_cell->thresh_serving_low;
    neigh_cell_info.s_intra_search = neigh_cell->s_intra_search;

    ret = lte_info_add_neigh_cell_info(&neigh_cell_info, lte_report);
    if (!ret) return -1;

    return 0;
}


/**
 * @brief set a full report
 */
int
lte_set_report(void)
{
    int res;

    lte_report = lte_info_allocate_report(1);
    if (!lte_report)
    {
        LOGE("lte_report calloc failed");
        return -1;
    }

    /* Set the common header */
    res = lte_set_common_header(lte_report);
    if (res)
    {
        LOGE("Failed to set common header");
        return res;
    }

    /* Add the lte net info */
    res = lte_set_net_info(lte_report);
    if (res)
    {
        LOGE("Failed to set net info");
        return res;
    }

    /* Add the lte data usage */
    res = lte_set_data_usage(lte_report);
    if (res)
    {
        LOGE("Failed to set data usage");
        return res;
    }

    /* Add the serving cell info */
    res = lte_set_serving_cell(lte_report);
    if (res)
    {
        LOGE("Failed to set serving cell");
        return res;
    }

    /* Add neighbor cell info */
    res = lte_set_neigh_cell_intra_info(lte_report);
    if (res) LOGE("Failed to set neighbor cell");

    return res;
}

void
lte_mqtt_cleanup(void)
{
    lte_info_free_report(lte_report);
    lte_info_free_packed_buffer(lte_serialized);
    return;
}

/**
 * @brief serialize a full report
 */
int
lte_serialize_report(void)
{
    int res;
    ltem_mgr_t *mgr = ltem_get_mgr();

    res = lte_set_report();
    if (res) return res;

    lte_serialized = serialize_lte_info(lte_report);
    if (!lte_serialized) return -1;


    if (mgr->topic[0])
    {
        res = lte_send_report(mgr->topic, lte_serialized);
        LOGI("%s: AWLAN topic[%s]", __func__, mgr->topic);
    }
    else
    {
        return -1;
    }
    return res;
}



int
ltem_build_mqtt_report(time_t now)
{
    int res;
    ltem_mgr_t *mgr = ltem_get_mgr();

    res = ltem_get_modem_info();
    if (res < 0)
    {
        LOGI("%s: ltem_get_modem_info: Failed", __func__);
    }

    if (mgr->modem_info.model[0])
    {
        mgr->lte_state_info->modem_present = true;
    }
    else
    {
        mgr->lte_state_info->modem_present = false;
    }

    res = lte_serialize_report();

    lte_mqtt_cleanup();

    return res;
}
