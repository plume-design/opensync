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
#include <inttypes.h>

#include "cell_info.h"
#include "log.h"
#include "cellm_mgr.h"
#include "qm_conn.h"

struct cell_info_packed_buffer *serialized_report = NULL;
struct cell_info_report *report = NULL;

static uint32_t cellm_info_request_id = 0;

static int cellm_info_send_report(char *topic, struct cell_info_packed_buffer *pb)
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

    LOGI("%s: msg len: %zu, topic: %s", __func__, pb->len, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic, pb->buf, pb->len, &res);
    if (!ret)
    {
        LOGE("error sending mqtt with topic %s", topic);
        return -1;
    }
#else
    /* Silence include warnings on X86 */
    (void)qm_conn_send_direct;
#endif

    return 0;
}

/**
 * @brief Set the common header
 */
int cellm_info_mqtt_set_common_header(struct cell_info_report *report)
{
    bool ret;
    struct cell_common_header common_header;
    cellm_mgr_t *mgr = cellm_get_mgr();

    if (!report) return -1;

    LOGI("%s: if_name[%s], node_id[%s], location_id[%s], imei[%s], imsi[%s], iccid[%s], modem_info[%s]",
         __func__,
         mgr->cellm_config_info->if_name,
         mgr->node_id,
         mgr->location_id,
         mgr->modem_info->imei,
         mgr->modem_info->imsi,
         mgr->modem_info->iccid,
         "");
    MEMZERO(common_header);
    common_header.request_id = cellm_info_request_id++;
    STRSCPY(common_header.if_name, mgr->cellm_config_info->if_name);
    STRSCPY(common_header.node_id, mgr->node_id);
    STRSCPY(common_header.location_id, mgr->location_id);
    STRSCPY(common_header.imei, mgr->modem_info->imei);
    STRSCPY(common_header.imsi, mgr->modem_info->imsi);
    STRSCPY(common_header.iccid, mgr->modem_info->iccid);
    common_header.ue_band_info = &mgr->modem_info->ue_band_info;
    /* TODO: This field is nowhere to be found */
#if 0
    STRSCPY(common_header.modem_info, mgr->modem_info);
#else
    STRSCPY(common_header.modem_info, "");
#endif
    // reported_at is set in cell_info_set_common_header
    ret = cellm_info_set_common_header(&common_header, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set the cell_net_info in the report
 */
int cellm_info_mqtt_set_net_info(struct cell_info_report *report)
{
    bool ret;
    struct cell_net_info net_info;
    cellm_mgr_t *mgr = cellm_get_mgr();

    net_info.net_status = mgr->modem_info->cell_net_info.net_status;
    net_info.mcc = mgr->modem_info->cell_net_info.mcc;
    net_info.mnc = mgr->modem_info->cell_net_info.mnc;
    net_info.tac = mgr->modem_info->cell_net_info.tac;
    STRSCPY(net_info.service_provider, mgr->modem_info->cell_net_info.service_provider);
    net_info.sim_type = mgr->modem_info->cell_net_info.sim_type;
    net_info.sim_status = mgr->modem_info->cell_net_info.sim_status;
    net_info.active_sim_slot = mgr->modem_info->cell_net_info.active_sim_slot;
    net_info.rssi = mgr->modem_info->cell_net_info.rssi;
    net_info.ber = mgr->modem_info->cell_net_info.ber;
    net_info.endc = mgr->modem_info->cell_net_info.endc;
    net_info.mode = mgr->modem_info->cell_net_info.mode;

    ret = cellm_info_set_net_info(&net_info, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set cell_data_usage in the report
 */
int cellm_info_mqtt_set_data_usage(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_data_usage data_usage;

    LOGI("%s: rx_bytes[%" PRIu64 "], tx_bytes[%" PRIu64 "]",
         __func__,
         (uint64_t)mgr->modem_info->rx_bytes,
         (uint64_t)mgr->modem_info->tx_bytes);

    data_usage.rx_bytes = mgr->modem_info->rx_bytes;
    data_usage.tx_bytes = mgr->modem_info->tx_bytes;
    ret = cellm_info_set_data_usage(&data_usage, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set serving cell info
 */
int cellm_info_mqtt_set_serving_cell(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_serving_cell_info *srv_cell;

    srv_cell = &mgr->modem_info->cell_srv_cell;

    LOGI("%s: state[%d], fdd_tdd_mode[%d], cellid[%d] pcid[%d], uarfcn[%d], "
         "earfcn[%d], band[%d], ul_bandwidth[%d] dl_bandwidth[%d], tac[%d], rsrp[%d], "
         "rsrq[%d], rssi[%d], sinr[%d], srxlev[%d] endc[%d]",
         __func__,
         srv_cell->state,
         srv_cell->fdd_tdd_mode,
         srv_cell->cellid,
         srv_cell->pcid,
         srv_cell->uarfcn,
         srv_cell->earfcn,
         srv_cell->band,
         srv_cell->ul_bandwidth,
         srv_cell->dl_bandwidth,
         srv_cell->tac,
         srv_cell->rsrp,
         srv_cell->rsrq,
         srv_cell->rssi,
         srv_cell->sinr,
         srv_cell->srxlev,
         srv_cell->endc);

    ret = cellm_info_set_serving_cell(srv_cell, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set intra neighbor cell info in the report
 */
int cellm_info_set_neigh_cell_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_net_neighbor_cell_info *neigh_cell;

    for (size_t i = 0; i < report->n_neigh_cells; i++)
    {
        if (i == MAX_CELL_COUNT)
        {
            LOGE("%s: Number of neighbor cells [%zu] exceeds maximum cell count[%d]",
                 __func__,
                 report->n_neigh_cells,
                 MAX_CELL_COUNT);
            break;
        }

        neigh_cell = &mgr->modem_info->cell_neigh_cell_info[i];
        ret = cellm_info_add_neigh_cell(neigh_cell, report, i);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief Set full scan neighbor cell info in the report
 */
int cellm_info_set_full_scan_neigh_cell_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_full_scan_neighbor_cell_info *full_scan_neigh_cell;

    for (size_t i = 0; i < report->n_full_scan_neigh_cells; i++)
    {
        if (i == MAX_FULL_SCAN_CELL_COUNT)
        {
            LOGE("%s: Number of full scan neighbor cells [%zu] exceeds maximum cell count[%d]",
                 __func__,
                 report->n_full_scan_neigh_cells,
                 MAX_FULL_SCAN_CELL_COUNT);
            break;
        }

        full_scan_neigh_cell = &mgr->modem_info->cell_full_scan_neigh_cell_info[i];
        ret = cellm_info_add_full_scan_neigh_cell(full_scan_neigh_cell, report, i);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief Set carrier aggregation info to the report
 */
int cellm_info_set_pca_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_net_pca_info *pca_info;

    pca_info = &mgr->modem_info->cell_pca_info;
    LOGI("%s: lcc[%d], freq[%d], bandwidth[%d], pcell_state[%d], pcid[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d]",
         __func__,
         pca_info->lcc,
         pca_info->freq,
         pca_info->bandwidth,
         pca_info->pcell_state,
         pca_info->pcid,
         pca_info->rsrp,
         pca_info->rsrq,
         pca_info->rssi,
         pca_info->sinr);
    ret = cellm_info_set_pca(pca_info, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set LTE secondary aggregation info
 */
int cellm_info_set_lte_sca_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_net_lte_sca_info *lte_sca_info;
    size_t i;

    for (i = 0; i < mgr->modem_info->n_lte_sca_cells; i++)
    {
        lte_sca_info = &mgr->modem_info->cell_lte_sca_info[i];
        if (lte_sca_info == NULL) return -1;
        LOGI("%s: lcc[%d], freq[%d], bandwidth[%d], scell_state[%d], pcid[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d]",
             __func__,
             lte_sca_info->lcc,
             lte_sca_info->freq,
             lte_sca_info->bandwidth,
             lte_sca_info->scell_state,
             lte_sca_info->pcid,
             lte_sca_info->rsrp,
             lte_sca_info->rsrq,
             lte_sca_info->rssi,
             lte_sca_info->sinr);
        ret = cellm_info_add_lte_sca(lte_sca_info, report);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief Set dynamic pdp context parameters info
 */
int cellm_info_set_pdp_ctx_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_pdp_ctx_dynamic_params_info *pdp_ctx;
    size_t i;

    for (i = 0; i < mgr->modem_info->n_pdp_cells; i++)
    {
        pdp_ctx = &mgr->modem_info->cell_pdp_ctx_info[i];
        if (pdp_ctx == NULL) return -1;
        ret = cellm_info_add_pdp_ctx(pdp_ctx, report);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief Set nr5g sa serving cell
 */
int cellm_info_mqtt_set_nr5g_sa_serving_cell(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_nr5g_cell_info *srv_cell_info;

    srv_cell_info = &mgr->modem_info->nr5g_sa_srv_cell;
    LOGI("%s: state[%d], fdd_tdd_mode[%d], mcc[%d], mnc[%d], cellid[%d], pcid[%d], tac[%d], arfcn[%d], band[%d], "
         "ul_bandwidth[%d], dl_bandwidth[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d], layers[%d], mcs[%d], "
         "modulation[%d]",
         __func__,
         srv_cell_info->state,
         srv_cell_info->fdd_tdd_mode,
         srv_cell_info->mcc,
         srv_cell_info->mnc,
         srv_cell_info->cellid,
         srv_cell_info->pcid,
         srv_cell_info->tac,
         srv_cell_info->arfcn,
         srv_cell_info->band,
         srv_cell_info->ul_bandwidth,
         srv_cell_info->dl_bandwidth,
         srv_cell_info->rsrp,
         srv_cell_info->rsrq,
         srv_cell_info->sinr,
         srv_cell_info->scs,
         srv_cell_info->srxlev,
         srv_cell_info->layers,
         srv_cell_info->mcs,
         srv_cell_info->modulation);
    ret = cellm_info_set_nr5g_sa_serving_cell(srv_cell_info, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set nr5g nsa serving cell
 */
int cellm_info_mqtt_set_nr5g_nsa_serving_cell(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_nr5g_cell_info *srv_cell_info;

    srv_cell_info = &mgr->modem_info->nr5g_nsa_srv_cell;
    LOGI("%s: state[%d], fdd_tdd_mode[%d], mcc[%d], mnc[%d], cellid[%d], pcid[%d], tac[%d], arfcn[%d], band[%d], "
         "ul_bandwidth[%d], dl_bandwidth[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d], layers[%d], mcs[%d], "
         "modulation[%d]",
         __func__,
         srv_cell_info->state,
         srv_cell_info->fdd_tdd_mode,
         srv_cell_info->mcc,
         srv_cell_info->mnc,
         srv_cell_info->cellid,
         srv_cell_info->pcid,
         srv_cell_info->tac,
         srv_cell_info->arfcn,
         srv_cell_info->band,
         srv_cell_info->ul_bandwidth,
         srv_cell_info->dl_bandwidth,
         srv_cell_info->rsrp,
         srv_cell_info->rsrq,
         srv_cell_info->sinr,
         srv_cell_info->scs,
         srv_cell_info->srxlev,
         srv_cell_info->layers,
         srv_cell_info->mcs,
         srv_cell_info->modulation);
    ret = cellm_info_set_nr5g_nsa_serving_cell(srv_cell_info, report);
    if (!ret) return -1;

    return 0;
}

/**
 * @brief Set nrg secondary aggregation info
 */
int cellm_info_set_nrg_sca_info(struct cell_info_report *report)
{
    bool ret;
    cellm_mgr_t *mgr = cellm_get_mgr();
    struct cell_nr5g_cell_info *nrg_sca_info;
    size_t i;

    for (i = 0; i < mgr->modem_info->n_nrg_sca_cells; i++)
    {
        nrg_sca_info = &mgr->modem_info->cell_nrg_sca_info[i];
        if (nrg_sca_info == NULL) return -1;
        ret = cellm_info_add_nrg_sca(nrg_sca_info, report);
        if (!ret) return -1;
    }

    return 0;
}

/**
 * @brief set a full report
 */
int cellm_info_set_report(void)
{
    int res;
    cellm_mgr_t *mgr = cellm_get_mgr();

    report = cellm_info_allocate_report(
            mgr->modem_info->n_neigh_cells,
            mgr->modem_info->n_full_scan_neigh_cells,
            mgr->modem_info->n_lte_sca_cells,
            mgr->modem_info->n_pdp_cells,
            mgr->modem_info->n_nrg_sca_cells);
    if (!report)
    {
        LOGE("report calloc failed");
        return -1;
    }

    /* Set the common header */
    res = cellm_info_mqtt_set_common_header(report);
    if (res)
    {
        LOGE("Failed to set common header");
        return res;
    }

    /* Add the cell net info */
    res = cellm_info_mqtt_set_net_info(report);
    if (res)
    {
        LOGE("Failed to set net info");
        return res;
    }

    /* Add the cell data usage */
    res = cellm_info_mqtt_set_data_usage(report);
    if (res)
    {
        LOGE("Failed to set data usage");
        return res;
    }

    /* Add the serving cell info */
    res = cellm_info_mqtt_set_serving_cell(report);
    if (res)
    {
        LOGE("Failed to set serving cell");
        return res;
    }

    /* Add neighbor cell info */
    res = cellm_info_set_neigh_cell_info(report);
    if (res)
    {
        LOGE("Failed to set neighbor cell");
        return res;
    }

    /* Add full scan neighbor cell info */
    res = cellm_info_set_full_scan_neigh_cell_info(report);
    if (res)
    {
        LOGE("Failed to set full scan neighbor cell info");
        return res;
    }

    /* Add primary carrier aggregation info */
    res = cellm_info_set_pca_info(report);
    if (res)
    {
        LOGE("Failed to set carrier aggregation info");
        return res;
    }

    /* Add lte secondary aggregation info */
    res = cellm_info_set_lte_sca_info(report);
    if (res)
    {
        LOGE("Failed to set lte sca info");
        return res;
    }

    /* Add pdp context dynamic parameters info */
    res = cellm_info_set_pdp_ctx_info(report);
    if (res)
    {
        LOGE("Failed to set pdp context info");
        return res;
    }

    /* Add nr5g_sa serving cell info */
    res = cellm_info_mqtt_set_nr5g_sa_serving_cell(report);
    if (res)
    {
        LOGE("Failed to set nr5g_sa srv cell info");
        return res;
    }

    /* Add nr5g_nsa serving cell info */
    res = cellm_info_mqtt_set_nr5g_nsa_serving_cell(report);
    if (res)
    {
        LOGE("Failed to set nr5g_nsa srv cell info");
        return res;
    }

    /* Add nrg secondary aggregation info */
    res = cellm_info_set_nrg_sca_info(report);
    if (res)
    {
        LOGE("Failed to set nrg sca info");
        return res;
    }

    return 0;
}

void cellm_info_mqtt_cleanup(void)
{
    cellm_info_free_report(report);
    cellm_info_free_packed_buffer(serialized_report);
    return;
}

/**
 * @brief serialize a full report
 */
int cellm_info_serialize_report(void)
{
    int res;
    cellm_mgr_t *mgr = cellm_get_mgr();

    res = cellm_info_set_report();
    if (res) return res;

    serialized_report = serialize_cell_info(report);
    if (!serialized_report) return -1;

    if (mgr->topic[0])
    {
        res = cellm_info_send_report(mgr->topic, serialized_report);
        LOGD("%s: AWLAN topic[%s]", __func__, mgr->topic);
    }
    else
    {
        LOGE("%s: AWLAN topic: not set, mqtt report not sent", __func__);
        return -1;
    }
    return res;
}

int cellm_info_build_mqtt_report(time_t now)
{
    int res;

    LOGI("%s()", __func__);
    res = osn_cell_read_modem();
    if (res < 0)
    {
        LOGW("%s: osn_cell_read_modem() failed", __func__);
    }

    res = cellm_info_serialize_report();
    if (res)
    {
        LOGW("%s: cell_serialize_report: failed", __func__);
    }

    cellm_info_mqtt_cleanup();

    return res;
}
