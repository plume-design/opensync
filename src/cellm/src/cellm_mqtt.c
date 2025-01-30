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
#include "cellm_mgr.h"

struct lte_info_packed_buffer *lte_serialized = NULL;
struct lte_info_report *lte_report = NULL;

char lte_mqtt_topic[256];

static uint32_t lte_request_id = 0;

static int cellm_lte_send_report(char *topic, struct lte_info_packed_buffer *pb)
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

    LOGD("%s: msg len: %zu, topic: %s", __func__, pb->len, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic, pb->buf, pb->len, &res);
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
int cellm_lte_set_common_header(struct lte_info_report *lte_report)
{
    bool ret;
    struct lte_common_header common_header;
    cellm_mgr_t *mgr = cellm_get_mgr();

    if (!lte_report) return -1;

    MEMZERO(common_header);
    common_header.request_id = lte_request_id++;
    common_header.if_name = mgr->cellm_config_info->if_name;
    common_header.node_id = mgr->node_id;
    common_header.location_id = mgr->location_id;
    common_header.imei = mgr->modem_info->imei;
    common_header.imsi = mgr->modem_info->imsi;
    common_header.iccid = mgr->modem_info->iccid;
    // reported_at is set in lte_info_set_common_header
    ret = lte_info_set_common_header(&common_header, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set the lte_net_info in the report
 */
int cellm_lte_set_net_info(struct lte_info_report *lte_report)
{
    bool ret;
    struct lte_net_info net_info;
    cellm_mgr_t *mgr = cellm_get_mgr();

    net_info.net_status = mgr->modem_info->reg_status;
    net_info.rssi = mgr->modem_info->rssi;
    net_info.ber = mgr->modem_info->ber;
    net_info.mcc = mgr->modem_info->srv_cell.mcc;
    net_info.mnc = mgr->modem_info->srv_cell.mnc;
    net_info.tac = mgr->modem_info->srv_cell.tac;
    net_info.service_provider = mgr->modem_info->operator;
    net_info.sim_type = mgr->modem_info->sim_type;
    net_info.sim_status = mgr->modem_info->sim_status;
    net_info.active_sim_slot = mgr->modem_info->active_simcard_slot;
    net_info.last_healthcheck_success = mgr->modem_info->last_healthcheck_success;
    net_info.healthcheck_failures = mgr->modem_info->healthcheck_failures;

    ret = lte_info_set_net_info(&net_info, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set lte_data_usage in the report
 */
int cellm_lte_set_data_usage(struct lte_info_report *lte_report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct lte_data_usage data_usage;

    data_usage.rx_bytes = mgr->modem_info->rx_bytes;
    data_usage.tx_bytes = mgr->modem_info->tx_bytes;
    data_usage.failover_start = mgr->cellm_state_info->cellm_failover_start;
    data_usage.failover_end = mgr->cellm_state_info->cellm_failover_end;
    data_usage.failover_count = mgr->cellm_state_info->cellm_failover_count;
    ret = lte_info_set_data_usage(&data_usage, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set serving cell lte info
 */
void cellm_lte_set_serving_cell_lte(struct lte_net_serving_cell_info *srv_cell_info, cell_serving_cell_info_t *srv_cell)
{
    srv_cell_info->fdd_tdd_mode = srv_cell->fdd_tdd_mode;
    srv_cell_info->earfcn = srv_cell->earfcn;
    srv_cell_info->freq_band = srv_cell->freq_band;
    srv_cell_info->ul_bandwidth = srv_cell->ul_bandwidth;
    srv_cell_info->dl_bandwidth = srv_cell->dl_bandwidth;
    srv_cell_info->tac = srv_cell->tac;
    srv_cell_info->rsrp = srv_cell->rsrp;
    srv_cell_info->rsrq = srv_cell->rsrq;
    srv_cell_info->rssi = srv_cell->rssi;
    srv_cell_info->sinr = srv_cell->sinr;
    srv_cell_info->srxlev = srv_cell->srxlev;
}

/**
 * @brief Set serving cell info
 */
int cellm_lte_set_serving_cell(struct lte_info_report *lte_report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct lte_net_serving_cell_info srv_cell_info;
    cell_serving_cell_info_t *srv_cell;

    MEMZERO(srv_cell_info);
    srv_cell = &mgr->modem_info->srv_cell;

    srv_cell_info.state = srv_cell->state;
    if (srv_cell->state == SERVING_CELL_SEARCH) /* No service */
    {
        LOGI("%s: state=LTE_SERVING_CELL_SEARCH", __func__);
    }

    if (srv_cell->state == SERVING_CELL_LIMSERV) /* No data connection, camping on a cell */
    {
        LOGI("%s: state=LTE_SERVING_CELL_LIMSERV", __func__);
    }

    switch (srv_cell->mode)
    {
        case CELL_MODE_NR5G_SA:
        case CELL_MODE_NR5G_ENDC:
        case CELL_MODE_NR5G_NSA:
        case CELL_MODE_NR5G_NSA_5G_RRC_IDLE:
            srv_cell_info.mode = CELL_MODE_5G;
            break;

        case CELL_MODE_LTE:
        case CELL_MODE_WCDMA:
            srv_cell_info.mode = CELL_MODE_4G;
            break;

        default:
            srv_cell_info.mode = CELL_MODE_AUTO;
            break;
    }

    if (srv_cell->state == SERVING_CELL_NOCONN && srv_cell->mode == CELL_MODE_WCDMA) /* No PDP context, WCDMA mode */
    {
        LOGI("%s: state=LTE_SERVING_CELL_NOCONN, srv_cell->mode=LTE_CELL_MODE_WCDMA", __func__);
    }

    srv_cell_info.cellid = srv_cell->cellid;
    srv_cell_info.pcid = srv_cell->pcid;
    if (srv_cell->mode == CELL_MODE_LTE)
    {
        cellm_lte_set_serving_cell_lte(&srv_cell_info, srv_cell);
    }
    ret = lte_info_set_serving_cell(&srv_cell_info, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set intra neighbor cell info in the report
 */
int cellm_lte_set_neigh_cell_info(struct lte_info_report *lte_report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct lte_net_neighbor_cell_info neigh_cell_info;
    cell_neighbor_cell_info_t *neigh_cell;
    int i;

    for (i = 0; i < MAX_NEIGH_CELL_COUNT; i++)
    {
        MEMZERO(neigh_cell_info);
        neigh_cell = &mgr->modem_info->neigh_cell[i];

        neigh_cell_info.freq_mode = neigh_cell->freq_mode;
        neigh_cell_info.mode = neigh_cell->mode;
        neigh_cell_info.earfcn = neigh_cell->earfcn;
        neigh_cell_info.pcid = neigh_cell->pcid;
        neigh_cell_info.rsrq = neigh_cell->rsrq;
        neigh_cell_info.rsrp = neigh_cell->rsrp;
        neigh_cell_info.rssi = neigh_cell->rssi;
        neigh_cell_info.sinr = neigh_cell->sinr;
        neigh_cell_info.srxlev = neigh_cell->srxlev;
        neigh_cell_info.cell_resel_priority = neigh_cell->cell_resel_priority;

        ret = lte_info_add_neigh_cell_info(&neigh_cell_info, lte_report);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief Set carrier aggregation info to the report
 */
int cellm_lte_set_carrier_agg_info(struct lte_info_report *lte_report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    cell_pca_info_t *lte_pca;
    cell_sca_info_t *lte_sca;

    struct lte_net_pca_info pca_info;
    struct lte_net_sca_info sca_info;

    MEMZERO(pca_info);
    MEMZERO(sca_info);
    lte_pca = &mgr->modem_info->pca_info;
    lte_sca = &mgr->modem_info->sca_info;

    pca_info.lcc = lte_pca->lcc;
    pca_info.freq = lte_pca->freq;
    pca_info.bandwidth = lte_pca->bandwidth;
    pca_info.pcell_state = lte_pca->pcell_state;
    pca_info.pcid = lte_pca->pcid;
    pca_info.rsrp = lte_pca->rsrp;
    pca_info.rsrq = lte_pca->rsrq;
    pca_info.rssi = lte_pca->rssi;
    pca_info.sinr = lte_pca->sinr;
    sca_info.lcc = lte_sca->lcc;
    sca_info.freq = lte_sca->freq;
    sca_info.bandwidth = lte_sca->bandwidth;
    sca_info.scell_state = lte_sca->scell_state;
    sca_info.pcid = lte_sca->pcid;
    sca_info.rsrp = lte_sca->rsrp;
    sca_info.rsrq = lte_sca->rsrq;
    sca_info.rssi = lte_sca->rssi;
    sca_info.sinr = lte_sca->sinr;

    ret = lte_info_set_primary_carrier_agg(&pca_info, lte_report);
    if (!ret) return -1;
    ret = lte_info_set_secondary_carrier_agg(&sca_info, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set dynamic pdp context parameters info
 */
int cellm_lte_set_pdp_context_dynamic_info(struct lte_info_report *lte_report)
{
    cellm_mgr_t *mgr = cellm_get_mgr();
    cell_pdp_ctx_dynamic_param_info_t *pdp_modem_source;

    int ret;
    struct lte_pdp_ctx_dynamic_params_info pdp_ctx_report;

    MEMZERO(pdp_ctx_report);
    pdp_modem_source = &mgr->modem_info->pdp_ctx_info;
    pdp_ctx_report.cid = pdp_modem_source->cid;
    pdp_ctx_report.bearer_id = pdp_modem_source->bearer_id;
    pdp_ctx_report.apn = pdp_modem_source->apn;
    pdp_ctx_report.local_addr = pdp_modem_source->local_addr;
    pdp_ctx_report.subnetmask = pdp_modem_source->subnetmask;
    pdp_ctx_report.gw_addr = pdp_modem_source->gw_addr;
    pdp_ctx_report.dns_prim_addr = pdp_modem_source->dns_prim_addr;
    pdp_ctx_report.dns_sec_addr = pdp_modem_source->dns_sec_addr;
    pdp_ctx_report.p_cscf_prim_addr = pdp_modem_source->p_cscf_prim_addr;
    pdp_ctx_report.im_cn_signalling_flag = pdp_modem_source->im_cn_signalling_flag;
    pdp_ctx_report.lipaindication = pdp_modem_source->lipaindication;

    ret = lte_info_set_pdp_ctx_dynamic_params(&pdp_ctx_report, lte_report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Check the modem status
 */

int cellm_check_modem_status(cellm_mgr_t *mgr)
{
    cellm_config_info_t *cellm_config_info;
    osn_cell_modem_info_t *modem_info;
    int res;

    cellm_config_info = mgr->cellm_config_info;
    if (!cellm_config_info) return -1;

    modem_info = mgr->modem_info;
    if (!modem_info) return -1;

    if (!cellm_config_info->modem_enable) return -1;

    if (!modem_info->modem_present) return -1;

    if (!modem_info->sim_inserted) return -1;

    res = strncmp(modem_info->iccid, "0", strlen(modem_info->iccid));
    if (!res) return -1;

    return 0;
}

/**
 * @brief set a full report
 */
int cellm_lte_set_report(void)
{
    int res;
    cellm_mgr_t *mgr = cellm_get_mgr();

    if (cellm_check_modem_status(mgr)) return -1;

    lte_report = lte_info_allocate_report(MAX_NEIGH_CELL_COUNT);
    if (!lte_report)
    {
        LOGE("lte_report calloc failed");
        return -1;
    }

    /* Set the common header */
    res = cellm_lte_set_common_header(lte_report);
    if (res)
    {
        LOGE("Failed to set common header");
        return res;
    }

    /* Add the lte net info */
    res = cellm_lte_set_net_info(lte_report);
    if (res)
    {
        LOGE("Failed to set net info");
        return res;
    }

    /* Add the lte data usage */
    res = cellm_lte_set_data_usage(lte_report);
    if (res)
    {
        LOGE("Failed to set data usage");
        return res;
    }

    /* Add the serving cell info */
    res = cellm_lte_set_serving_cell(lte_report);
    if (res)
    {
        LOGE("Failed to set serving cell");
        return res;
    }

    /* Add neighbor cell info */
    res = cellm_lte_set_neigh_cell_info(lte_report);
    if (res) LOGE("Failed to set neighbor cell");

    /* Add carrier aggregation info */
    res = cellm_lte_set_carrier_agg_info(lte_report);
    if (res)
    {
        LOGE("Failed to set carrier aggregation info");
        return res;
    }
    /* Add pdp context dynamic parameters info */
    res = cellm_lte_set_pdp_context_dynamic_info(lte_report);
    if (res) LOGE("Failed to set carrier aggregation info");
    return res;
}

void cellm_mqtt_cleanup(void)
{
    lte_info_free_report(lte_report);
    lte_info_free_packed_buffer(lte_serialized);
    return;
}

/**
 * @brief serialize a full report
 */
int cellm_serialize_report(void)
{
    int res;
    cellm_mgr_t *mgr = cellm_get_mgr();

    res = cellm_lte_set_report();
    if (res) return res;

    lte_serialized = serialize_lte_info(lte_report);
    if (!lte_serialized) return -1;

    if (mgr->topic[0])
    {
        res = cellm_lte_send_report(mgr->topic, lte_serialized);
        LOGD("%s: AWLAN topic[%s]", __func__, mgr->topic);
    }
    else
    {
        LOGE("%s: AWLAN topic: not set, mqtt report not sent", __func__);
        return -1;
    }
    return res;
}

int cellm_build_mqtt_report(time_t now)
{
    int res;
    cellm_mgr_t *mgr = cellm_get_mgr();

    res = osn_cell_read_modem();
    if (res < 0)
    {
        LOGW("%s: osn_cell_read_modem() failed", __func__);
    }

    /*
     * We have to catch the case where something has changed the the SIM slot.
     */
    if (mgr->modem_info->active_simcard_slot != mgr->cellm_config_info->active_simcard_slot)
    {
        osn_cell_set_sim_slot(mgr->cellm_config_info->active_simcard_slot);
    }

    res = cellm_serialize_report();
    if (res)
    {
        LOGW("%s: lte_serialize_report: failed", __func__);
    }

    cellm_mqtt_cleanup();

    return res;
}
