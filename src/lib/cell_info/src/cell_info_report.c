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
#include <inttypes.h>

#include "log.h"
#include "cell_info.h"
#include "memutil.h"
#include "cell_info.pb-c.h"
#include "util.h"

/**
 * @brief add the cell net info to a report
 *
 * @param source the cell net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_set_net_info(struct cell_net_info *source, struct cell_info_report *report)
{
    struct cell_net_info *cell_net_info;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the cell info structure was already allocated */
    if (report->cell_net_info != NULL) return false;

    report->cell_net_info = CALLOC(1, sizeof(*cell_net_info));

    cell_net_info = report->cell_net_info;
    cell_net_info->net_status = source->net_status;
    cell_net_info->mcc = source->mcc;
    cell_net_info->mnc = source->mnc;
    cell_net_info->tac = source->tac;
    STRSCPY(cell_net_info->service_provider, source->service_provider);
    cell_net_info->sim_type = source->sim_type;
    cell_net_info->sim_status = source->sim_status;
    cell_net_info->active_sim_slot = source->active_sim_slot;
    cell_net_info->rssi = source->rssi;
    cell_net_info->ber = source->ber;
    cell_net_info->rsrp = source->rsrp;
    cell_net_info->sinr = source->sinr;
    cell_net_info->last_healthcheck_success = source->last_healthcheck_success;
    cell_net_info->healthcheck_failures = source->healthcheck_failures;
    cell_net_info->endc = source->endc;
    cell_net_info->mode = source->mode;
    return true;

error:
    cell_info_free_report(report);
    return false;
}


/**
 * @brief free the cell_net_info field contained in a report
 *
 * @param report the report
 */
void
cell_info_free_net_info(struct cell_info_report *report)
{
    struct cell_net_info *cell_net_info;

    if (report == NULL) return;

    cell_net_info = report->cell_net_info;
    if (cell_net_info == NULL) return;

    FREE(cell_net_info);
    report->cell_net_info = NULL;
}


/**
 * @brief Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct cell_info_report *
cell_info_allocate_report(size_t n_neighbors, size_t n_full_scan_neighbors, size_t n_lte_sca_cells,
                          size_t n_pdp_cells, size_t n_nrg_sca_cells)
{
    struct cell_info_report *report;

    report = CALLOC(1, sizeof(*report));

    report->cell_neigh_cell_info = CALLOC(n_neighbors, sizeof(*report->cell_neigh_cell_info));
    report->n_neigh_cells = n_neighbors;

    report->cell_full_scan_neigh_cell_info = CALLOC(n_full_scan_neighbors, sizeof(*report->cell_full_scan_neigh_cell_info));
    report->n_full_scan_neigh_cells = n_full_scan_neighbors;

    report->cell_lte_sca_info = CALLOC(n_lte_sca_cells, sizeof(*report->cell_lte_sca_info));
    report->n_lte_sca_cells = n_lte_sca_cells;

    report->cell_pdp_ctx_info = CALLOC(n_pdp_cells, sizeof(*report->cell_pdp_ctx_info));
    report->n_pdp_cells = n_pdp_cells;

    report->cell_nrg_sca_info = CALLOC(n_nrg_sca_cells, sizeof(*report->cell_nrg_sca_info));
    report->n_nrg_sca_cells = n_nrg_sca_cells;

    return report;
}

/**
 * @brief: free a report structure
 *
 * @param report the report pointer to free
 */
void
cell_info_free_report(struct cell_info_report *report)
{
    struct cell_net_neighbor_cell_info *neigh_cell;
    struct cell_full_scan_neighbor_cell_info *neigh_full_scan_cell;
    struct cell_pdp_ctx_dynamic_params_info *cell_pdp_ctx_info;
    struct cell_net_lte_sca_info *cell_lte_sca_info;
    struct cell_nr5g_cell_info *cell_nrg_sca_info;
    size_t i;

    if (report == NULL) return;

    cell_info_free_common_header(report);
    cell_info_free_net_info(report);
    cell_info_free_serving_cell(report);
    cell_info_free_data_usage(report);

    for (i = 0; i < report->n_neigh_cells; i++)
    {
        neigh_cell = report->cell_neigh_cell_info[i];
        cell_info_free_neigh_cell(neigh_cell);
    }
    FREE(report->cell_neigh_cell_info);

    for (i = 0; i < report->n_full_scan_neigh_cells; i++)
    {
        neigh_full_scan_cell = report->cell_full_scan_neigh_cell_info[i];
        cell_info_free_full_scan_neigh_cell(neigh_full_scan_cell);
    }
    FREE(report->cell_full_scan_neigh_cell_info);

    cell_info_free_pca(report);

    for (i = 0; i < report->n_lte_sca_cells; i++)
    {
        cell_lte_sca_info = report->cell_lte_sca_info[i];
        cell_info_free_lte_sca(cell_lte_sca_info);
    }
    FREE(report->cell_lte_sca_info);

    for (i = 0; i < report->n_pdp_cells; i++)
    {
        cell_pdp_ctx_info = report->cell_pdp_ctx_info[i];
        cell_info_free_pdp_ctx(cell_pdp_ctx_info);
    }
    FREE(report->cell_pdp_ctx_info);

    FREE(report->nr5g_sa_srv_cell);

    FREE(report->nr5g_nsa_srv_cell);

    for (i = 0; i < report->n_nrg_sca_cells; i++)
    {
        cell_nrg_sca_info = report->cell_nrg_sca_info[i];
        cell_info_free_nrg_sca(cell_nrg_sca_info);
    }
    FREE(report->cell_nrg_sca_info);


    FREE(report);
}

/**
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
cell_info_set_common_header(struct cell_common_header *source,
                          struct cell_info_report *report)
{
    struct cell_common_header *header;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->header != NULL) return false;

    report->header = CALLOC(1, sizeof(*header));

    header = report->header;
    header->request_id = source->request_id;

    STRSCPY(header->if_name, source->if_name);
    STRSCPY(header->node_id, source->node_id);
    STRSCPY(header->location_id, source->location_id);
    STRSCPY(header->imei, source->imei);
    STRSCPY(header->imsi, source->imsi);
    STRSCPY(header->iccid, source->iccid);
    STRSCPY(header->modem_info, source->modem_info);

    header->reported_at = time(NULL);
    LOGI("%s: request_id[%d], if_name[%s], node_id[%s], location_id[%s], imei[%s] imsi[%s], iccid[%s], reported_at[%d]",
         __func__, header->request_id, header->if_name, header->node_id, header->location_id, header->imei, header->imsi,
         header->iccid, (int)header->reported_at);
    return true;

}

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void
cell_info_free_common_header(struct cell_info_report *report)
{
    struct cell_common_header *header;

    if (report == NULL) return;

    header = report->header;
    if (header == NULL) return;

    FREE(header);
    report->header = NULL;
}

/**
 * @brief set the cell data usage of a report
 *
 * @param source the cell data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
cell_info_set_data_usage(struct cell_data_usage *source,
                        struct cell_info_report *report)
{
    struct cell_data_usage *data_usage;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->cell_data_usage != NULL) return false;

    report->cell_data_usage = CALLOC(1, sizeof(*data_usage));

    data_usage = report->cell_data_usage;
    data_usage->rx_bytes = source->rx_bytes;
    data_usage->tx_bytes = source->tx_bytes;

    return true;
}

/**
 * @brief free the cell data usage contained in a report
 *
 * @param report the report
 */
void
cell_info_free_data_usage(struct cell_info_report *report)
{
    struct cell_data_usage *cell_data_usage;

    if (report == NULL) return;

    cell_data_usage = report->cell_data_usage;
    if (cell_data_usage == NULL) return;

    FREE(cell_data_usage);
    report->cell_data_usage = NULL;
}

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @param idx the index into which we are adding cell info in the report
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_neigh_cell(struct cell_net_neighbor_cell_info *cell_info,
                         struct cell_info_report *report,
                         size_t idx)
{
    struct cell_net_neighbor_cell_info *cell;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    LOGI("%s: idx[%zu], n_neigh_cells[%zu]", __func__, idx, report->n_neigh_cells);

    cell = CALLOC(1, sizeof(*cell));
    ret = cell_info_set_neigh_cell(cell_info, cell);
    if (!ret) goto error;

    report->cell_neigh_cell_info[idx] = cell;

    return true;

error:
    cell_info_free_report(report);
    return false;
}


/**
 * @brief copy a neighbor cell info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_neigh_cell(struct cell_net_neighbor_cell_info *source,
                         struct cell_net_neighbor_cell_info *dest)
{

    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->freq_mode = source->freq_mode;
    dest->earfcn = source->earfcn;
    dest->pcid = source->pcid;
    dest->rsrq = source->rsrq;
    dest->rsrp = source->rsrp;
    dest->rssi = source->rssi;
    dest->sinr = source->sinr;
    dest->srxlev = source->srxlev;
    dest->cell_resel_priority = source->cell_resel_priority;
    dest->s_non_intra_search = source->s_non_intra_search;
    dest->thresh_serving_low = source->thresh_serving_low;
    dest->s_intra_search = source->s_intra_search;
    dest->thresh_x_low = source->thresh_x_low;
    dest->thresh_x_high = source->thresh_x_high;
    dest->psc = source->psc;
    dest->rscp = source->rscp;
    dest->ecno = source->ecno;
    dest->cell_set = source->cell_set;
    dest->rank = source->rank;
    dest->cellid = source->cellid;
    dest->inter_freq_srxlev = source->inter_freq_srxlev;

    return true;

}


/**
 * @brief free a neighbor cell info
 *
 * @param cell the structure to free
 */
void
cell_info_free_neigh_cell(struct cell_net_neighbor_cell_info *cell)
{
    if (cell == NULL) return;

    FREE(cell);
    return;
}

/**
 * @brief add a full scan neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @param idx the index into which we are adding cell info in the report
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_full_scan_neigh_cell(struct cell_full_scan_neighbor_cell_info *cell_info,
                                   struct cell_info_report *report,
                                   size_t idx)
{
    struct cell_full_scan_neighbor_cell_info *cell;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    LOGI("%s: idx[%zu], n_full_scan_neigh_cells[%zu]", __func__, idx, report->n_full_scan_neigh_cells);

    cell = CALLOC(1, sizeof(*cell));
    ret = cell_info_set_full_scan_neigh_cell(cell_info, cell);
    if (!ret) goto error;

    report->cell_full_scan_neigh_cell_info[idx] = cell;

    return true;

error:
    cell_info_free_report(report);
    return false;
}

/**
 * @brief copy full scan neighbor cell info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_full_scan_neigh_cell(struct cell_full_scan_neighbor_cell_info *source,
                                   struct cell_full_scan_neighbor_cell_info *dest)
{

    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->rat = source->rat;
    dest->mcc = source->mcc;
    dest->mnc = source->mnc;
    dest->freq = source->freq;
    dest->pcid = source->pcid;
    dest->rsrp = source->rsrp;
    dest->rsrq = source->rsrq;
    dest->srxlev = source->srxlev;
    dest->scs = source->scs;
    dest->squal = source->squal;
    dest->cellid = source->cellid;
    dest->tac = source->tac;
    dest->bandwidth = source->bandwidth;
    dest->band = source->band;

    return true;
}

/**
 * @brief free full scan neighbor cell info
 *
 * @param cell the structure to free
 */
void
cell_info_free_full_scan_neigh_cell(struct cell_full_scan_neighbor_cell_info *cell)
{
    if (cell == NULL) return;

    FREE(cell);
    return;
}

/**
 * @brief set primary carrier aggregation info
 *
 * @param pca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_pca(struct cell_net_pca_info *source,
                  struct cell_info_report *report)
{
    struct cell_net_pca_info *pca;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the pca structure was already allocated */
    if (report->cell_pca_info != NULL) return false;

    report->cell_pca_info = CALLOC(1, sizeof(*pca));

    pca = report->cell_pca_info;

    pca->lcc = source->lcc;
    pca->freq = source->freq;
    pca->bandwidth = source->bandwidth;
    pca->pcell_state = source->pcell_state;
    pca->pcid = source->pcid;
    pca->rsrp = source->rsrp;
    pca->rsrq = source->rsrq;
    pca->rssi = source->rssi;
    pca->sinr = source->sinr;

    return true;
}

/**
 * @brief set lte secondary carrier aggregation info
 *
 * @param pca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_lte_sca(struct cell_net_lte_sca_info *source,
                      struct cell_net_lte_sca_info *dest)
{
    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->lcc = source->lcc;
    dest->freq = source->freq;
    dest->bandwidth = source->bandwidth;
    dest->scell_state = source->scell_state;
    dest->pcid = source->pcid;
    dest->rsrp = source->rsrp;
    dest->rsrq = source->rsrq;
    dest->rssi = source->rssi;
    dest->sinr = source->sinr;

    return true;
}

/**
 * @brief add lte secondary aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_lte_sca(struct cell_net_lte_sca_info *cell_info,
                      struct cell_info_report *report)
{
    struct cell_net_lte_sca_info *cell;
    size_t idx;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    /* Bail if we are already at capacity */
    idx = report->cur_lte_sca_cell_idx;
    if (idx == report->n_lte_sca_cells) return true;

    cell = CALLOC(1, sizeof(*cell));

    ret = cell_info_set_lte_sca(cell_info, cell);
    if (!ret) goto error;

    report->cell_lte_sca_info[idx] = cell;
    report->cur_lte_sca_cell_idx++;

    return true;

error:
    cell_info_free_report(report);
    return false;
}

/**
 * @brief add nrg secondary carrier aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_nrg_sca(struct cell_nr5g_cell_info *cell_info,
                      struct cell_info_report *report)
{
    struct cell_nr5g_cell_info *cell;
    size_t idx;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    /* Bail if we are already at capacity */
    idx = report->cur_nrg_sca_cell_idx;
    if (idx == report->n_nrg_sca_cells) return false;

    cell = CALLOC(1, sizeof(*cell));

    ret = cell_info_set_nrg_sca(cell_info, cell);
    if (!ret) goto error;

    report->cell_nrg_sca_info[idx] = cell;
    report->cur_nrg_sca_cell_idx++;

    return true;

error:
    cell_info_free_report(report);
    return false;
}


/**
 * @brief copy secondary aggregation info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_nrg_sca(struct cell_nr5g_cell_info *source,
                      struct cell_nr5g_cell_info *dest)
{

    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->state = source->state;
    dest->fdd_tdd_mode = source->fdd_tdd_mode;
    dest->mcc = source->mcc;
    dest->mnc = source->mnc;
    dest->cellid = source->cellid;
    dest->pcid = source->pcid;
    dest->tac = source->tac;
    dest->arfcn = source->arfcn;
    dest->band = source->band;
    dest->tac = source->tac;
    dest->rsrp = source->rsrp;
    dest->rsrq = source->rsrq;
    dest->rssi = source->rssi;
    dest->sinr = source->sinr;
    dest->cqi = source->cqi;
    dest->tx_power = source->tx_power;
    dest->srxlev = source->srxlev;
    dest->ul_bandwidth = source->ul_bandwidth;
    dest->dl_bandwidth = source->dl_bandwidth;
    dest->scs = source->scs;
    dest->layers = source->layers;
    dest->mcs = source->mcs;
    dest->modulation = source->modulation;

    return true;

}

/**
 * @brief free a nr5g_sa cell info
 *
 * @param cell the structure to free
 */
void
cell_info_free_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *cell)
{
    if (cell == NULL) return;

    FREE(cell);
    return;
}

/**
 * @brief free a nr5g_nsa cell info
 *
 * @param cell the structure to free
 */
void
cell_info_free_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *cell)
{
    if (cell == NULL) return;

    FREE(cell);
    return;
}

/**
 * @brief set a serving cell info
 *
 * @param srv_cell the serving cell info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_serving_cell(struct lte_serving_cell_info *source,
                           struct cell_info_report *report)
{
    struct lte_serving_cell_info *cell;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the signal quality structure was already allocated */
    if (report->cell_srv_cell != NULL) return false;

    report->cell_srv_cell = CALLOC(1, sizeof(*cell));

    cell = report->cell_srv_cell;

    cell->state = source->state;
    cell->fdd_tdd_mode = source->fdd_tdd_mode;
    cell->cellid = source->cellid;
    cell->pcid = source->pcid;
    cell->uarfcn = source->uarfcn;
    cell->earfcn = source->earfcn;
    cell->band = source->band;
    cell->ul_bandwidth = source->ul_bandwidth;
    cell->dl_bandwidth = source->dl_bandwidth;
    cell->tac = source->tac;
    cell->rsrp = source->rsrp;
    cell->rsrq = source->rsrq;
    cell->rssi = source->rssi;
    cell->sinr = source->sinr;
    cell->srxlev = source->srxlev;
    cell->endc = source->endc;

    return true;
}


/**
 * @brief free a serving cell info
 *
 * @param report the report
 */
void
cell_info_free_serving_cell(struct cell_info_report *report)
{
    struct lte_serving_cell_info *srv_cell;

    if (report == NULL) return;

    srv_cell = report->cell_srv_cell;
    if (srv_cell == NULL) return;

    FREE(srv_cell);
    report->cell_srv_cell = NULL;
}

/**
 * @brief free pca info
 *
 * @param report the report
 */
void
cell_info_free_pca(struct cell_info_report *report)
{
    struct cell_net_pca_info *pca;

    if (report == NULL) return;

    pca = report->cell_pca_info;
    if (pca == NULL) return;

    FREE(pca);
    report->cell_pca_info = NULL;
}

/**
 * @brief free lte sca info
 *
 * @param report the report
 */
void
cell_info_free_lte_sca(struct cell_net_lte_sca_info *sca)
{
    if (sca == NULL) return;

    FREE(sca);
}

/**
 * @brief add dynamic pdp ctx info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *cell_info,
                      struct cell_info_report *report)
{
    struct cell_pdp_ctx_dynamic_params_info *cell;
    size_t idx;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    /* Bail if we are already at capacity */
    idx = report->cur_pdp_idx;
    if (idx == report->n_pdp_cells) return false;

    cell = CALLOC(1, sizeof(*cell));

    ret = cell_info_set_pdp_ctx(cell_info, cell);
    if (!ret) goto error;

    report->cell_pdp_ctx_info[idx] = cell;
    report->cur_pdp_idx++;

    return true;

error:
    cell_info_free_report(report);
    return false;
}

/**
 * @brief set pdp context dynamic parameter info
 *
 * @param ppdc_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *source,
                      struct cell_pdp_ctx_dynamic_params_info *dest)
{
    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->cid = source->cid;
    dest->bearer_id = source->bearer_id;

    // expected info, bail out when it is not available
    STRSCPY(dest->apn, source->apn);
    STRSCPY(dest->local_addr, source->local_addr);

    // some of the mobile operators may not implement always all the following field
    STRSCPY(dest->subnetmask, source->subnetmask);
    STRSCPY(dest->gw_addr, source->gw_addr);
    STRSCPY(dest->dns_prim_addr, source->dns_prim_addr);
    STRSCPY(dest->dns_sec_addr, source->dns_sec_addr);
    STRSCPY(dest->p_cscf_prim_addr, source->p_cscf_prim_addr);
    STRSCPY(dest->p_cscf_sec_addr, source->p_cscf_sec_addr);
    dest->im_cn_signalling_flag = source->im_cn_signalling_flag;
    dest->lipaindication = source->lipaindication;

    return true;

error:
    return false;
}

/**
 * @brief free pdp context info
 *
 * @param report the report
 */
void
cell_info_free_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *pdp_ctx)
{
    if (pdp_ctx == NULL) return;

    FREE(pdp_ctx);
}


/**
 * @brief set nr5g sa info
 *
 * @param report the report
 */
bool
cell_info_set_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *source,
                                   struct cell_info_report *report)
{
    struct cell_nr5g_cell_info *srv_cell_info;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* return if cellular structure is already allocated */
    if (report->nr5g_sa_srv_cell != NULL) return false;

    report->nr5g_sa_srv_cell = CALLOC(1, sizeof(*srv_cell_info));

    srv_cell_info = report->nr5g_sa_srv_cell;
    srv_cell_info->state = source->state;
    srv_cell_info->fdd_tdd_mode = source->fdd_tdd_mode;
    srv_cell_info->cellid = source->cellid;
    srv_cell_info->pcid = source->pcid;
    srv_cell_info->tac = source->tac;
    srv_cell_info->arfcn = source->arfcn;
    srv_cell_info->band = source->band;
    srv_cell_info->ul_bandwidth = source->ul_bandwidth;
    srv_cell_info->dl_bandwidth = source->dl_bandwidth;
    srv_cell_info->rsrp = source->rsrp;
    srv_cell_info->rsrq = source->rsrq;
    srv_cell_info->sinr = source->sinr;
    srv_cell_info->scs = source->scs;
    srv_cell_info->srxlev = source->srxlev;
    srv_cell_info->layers = source->layers;
    srv_cell_info->mcs = source->mcs;
    srv_cell_info->modulation = source->modulation;

    return true;
}

/**
 * @brief set nr5g sa info
 *
 * @param report the report
 */
bool
cell_info_set_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *source,
                                    struct cell_info_report *report)
{
    struct cell_nr5g_cell_info *srv_cell_info;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* return if cellular structure is already allocated */
    if (report->nr5g_nsa_srv_cell != NULL) return false;

    report->nr5g_nsa_srv_cell = CALLOC(1, sizeof(*srv_cell_info));

    srv_cell_info = report->nr5g_nsa_srv_cell;
    srv_cell_info->state = source->state;
    srv_cell_info->fdd_tdd_mode = source->fdd_tdd_mode;
    srv_cell_info->cellid = source->cellid;
    srv_cell_info->pcid = source->pcid;
    srv_cell_info->tac = source->tac;
    srv_cell_info->arfcn = source->arfcn;
    srv_cell_info->band = source->band;
    srv_cell_info->tac = source->tac;
    srv_cell_info->rsrp = source->rsrp;
    srv_cell_info->rsrq = source->rsrq;
    srv_cell_info->rssi = source->rssi;
    srv_cell_info->sinr = source->sinr;
    srv_cell_info->cqi = source->cqi;
    srv_cell_info->tx_power = source->tx_power;
    srv_cell_info->srxlev = source->srxlev;
    srv_cell_info->ul_bandwidth = source->ul_bandwidth;
    srv_cell_info->dl_bandwidth = source->dl_bandwidth;
    srv_cell_info->scs = source->scs;
    srv_cell_info->layers = source->layers;
    srv_cell_info->mcs = source->mcs;
    srv_cell_info->modulation = source->modulation;

    return true;
}

/**
 * @brief free nrg sca info
 *
 * @param report the report
 */
void
cell_info_free_nrg_sca(struct cell_nr5g_cell_info *sca)
{
    if (sca == NULL) return;

    FREE(sca);
}

/**
 * @brief set the common header of a report
 *
 * @param report the report to update
 * @return CellCommonHeader
 *
 * Note: the destination is freed on error
 */
static Interfaces__CellInfo__CellularCommonHeader *
cell_info_set_pb_common_header(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularCommonHeader *pb;
    struct cell_common_header *header;

    if (report == NULL) return NULL;

    header = report->header;
    if (header == NULL) return NULL;

    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the protobuf structure */
    interfaces__cell_info__cellular_common_header__init(pb);

    pb->request_id = header->request_id;
    pb->if_name = header->if_name;
    pb->node_id = header->node_id;
    pb->location_id = header->location_id;
    pb->imei = header->imei;
    pb->imsi = header->imsi;
    pb->iccid = header->iccid;
    pb->modem_info = header->modem_info;
    LOGI("%s: request_id[%d], if_name[%s], node_id[%s], location_id[%s], imei[%s] imsi[%s], iccid[%s]",
         __func__, pb->request_id, pb->if_name, pb->node_id, pb->location_id, pb->imei, pb->imsi,
         pb->iccid);

    return pb;
}

static void
cell_info_free_pb_common_header(Interfaces__CellInfo__CellularCommonHeader *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__CellularNetInfo *
cell_info_set_cell_net_info(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularNetInfo *pb;
    struct cell_net_info *cell_net_info;

    if (report == NULL) return NULL;

    cell_net_info = report->cell_net_info;
    if (cell_net_info == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__cellular_net_info__init(pb);

    /* Assign the message fields */
    pb->net_status = (Interfaces__CellInfo__CellularNetRegStatus)cell_net_info->net_status;
    pb->mcc = cell_net_info->mcc;
    pb->mnc = cell_net_info->mnc;
    pb->tac = cell_net_info->tac;
    pb->service_provider = cell_net_info->service_provider;
    pb->sim_type = (Interfaces__CellInfo__CellularSimType)cell_net_info->sim_type;
    pb->sim_status = (Interfaces__CellInfo__CellularSimStatus)cell_net_info->sim_status;
    pb->active_sim_slot = cell_net_info->active_sim_slot;
    pb->endc = (Interfaces__CellInfo__Endc)cell_net_info->endc;
    pb->mode = (Interfaces__CellInfo__CellularMode)cell_net_info->mode;

    LOGI("%s: net_status[%d], mcc[%d], mnc[%d], tac[%d], provider[%s], sim_type[%d], sim_status[%d], sim_slot[%d], "
         "endc[%d], mode[%d]",
         __func__, pb->net_status, pb->mcc, pb->mnc, pb->tac, pb->service_provider, pb->sim_type, pb->sim_status,
         pb->active_sim_slot, pb->endc, pb->mode);

    return pb;
}

static void
cell_info_free_pb_cell_net_info(Interfaces__CellInfo__CellularNetInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__CellularDataUsage *
cell_info_set_cell_data_usage(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularDataUsage *pb;
    struct cell_data_usage *cell_data_usage;

    if (report == NULL) return NULL;

    cell_data_usage = report->cell_data_usage;
    if (cell_data_usage == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__cellular_data_usage__init(pb);

    /* Assign the message fields */
    pb->rx_bytes = cell_data_usage->rx_bytes;
    pb->tx_bytes = cell_data_usage->tx_bytes;

    LOGI("%s: rx_bytes[%"PRIu64"], tx_bytes[%"PRIu64"]", __func__,
         (uint64_t)pb->rx_bytes,
         (uint64_t)pb->tx_bytes);

    return pb;
}

static void
cell_info_free_pb_cell_data_usage(Interfaces__CellInfo__CellularDataUsage *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__LteNetServingCellInfo *
cell_info_set_srv_cell(struct cell_info_report *report)
{
    Interfaces__CellInfo__LteNetServingCellInfo *pb;
    struct lte_serving_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->cell_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__lte_net_serving_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__CellInfo__CellularServingCellState)cell->state;
    pb->fdd_tdd_mode = (Interfaces__CellInfo__FddTddMode)cell->fdd_tdd_mode;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->uarfcn = cell->uarfcn;
    pb->earfcn = cell->earfcn;
    pb->band = cell->band;
    pb->ul_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->ul_bandwidth;
    pb->dl_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->dl_bandwidth;
    pb->tac = cell->tac;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->rssi = cell->rssi;
    pb->sinr = cell->sinr;
    pb->srxlev = cell->srxlev;
    pb->endc = (Interfaces__CellInfo__Endc)cell->endc;

    LOGI("%s: state[%d], fdd_tdd_mode[%d], cellid[0x%x], pcid[%d], uarfcn[%d], earfcn[%d], "
         "band[%d] ul_bandwidth[%d] dl_bandwidth[%d], tac[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d], srxlev[%d]",
         __func__, pb->state, pb->fdd_tdd_mode, pb->cellid, pb->pcid, pb->uarfcn, pb->earfcn,
         pb->band, pb->ul_bandwidth, pb->dl_bandwidth, pb->tac, pb->rsrp, pb->rsrq, pb->rssi, pb->sinr, pb->srxlev);

    return pb;
}


static void
cell_info_free_pb_srv_cell(Interfaces__CellInfo__LteNetServingCellInfo *pb)
{
    FREE(pb);
}

/**
 * brief map a neighbor frequency mode value to the corresponding message value
 *
 * @param mode the neighbor frequency mode
 * @return the message mode value
 */
static Interfaces__CellInfo__NeighborFreqMode
cell_info_set_neighbor_freq_mode(enum cell_neighbor_freq_mode mode)
{
    Interfaces__CellInfo__NeighborFreqMode ret;

    switch(mode)
    {
        case CELL_FREQ_MODE_INTRA:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_INTRA;
            break;

        case CELL_FREQ_MODE_INTER:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_INTER;
            break;

        case CELL_FREQ_MODE_WCDMA:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_WCDMA;
            break;

        case CELL_FREQ_MODE_WCDMA_LTE:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_WCDMA_LTE;
            break;

        case CELL_FREQ_MODE_5G:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_5G;
            break;

        default:
            ret = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_UNSPECIFIED;
            break;
    }

    return ret;
}


static Interfaces__CellInfo__NetNeighborCellInfo *
cell_info_set_pb_neighbor(struct cell_net_neighbor_cell_info *neighbor)
{
    Interfaces__CellInfo__NetNeighborCellInfo *pb;

    /* Allocate the message */
    pb  = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__net_neighbor_cell_info__init(pb);

    /* Set the message fieldss */
    pb->freq_mode = cell_info_set_neighbor_freq_mode(neighbor->freq_mode);
    pb->earfcn = neighbor->earfcn;
    pb->pcid = neighbor->pcid;
    pb->rsrp = neighbor->rsrp;
    pb->rsrq = neighbor->rsrq;
    pb->rssi = neighbor->rssi;
    pb->sinr = neighbor->sinr;
    pb->srxlev = neighbor->srxlev;
    pb->cell_resel_priority = neighbor->cell_resel_priority;
    pb->s_non_intra_search = neighbor->s_non_intra_search;
    pb->thresh_serving_low = neighbor->thresh_serving_low;
    pb->s_intra_search = neighbor->s_intra_search;
    pb->thresh_x_low = neighbor->thresh_x_low;
    pb->thresh_x_high = neighbor->thresh_x_high;
    pb->psc = neighbor->psc;
    pb->rscp = neighbor->rscp;
    pb->ecno = neighbor->ecno;
    pb->cell_set = (Interfaces__CellInfo__NeighborCellSet)neighbor->cell_set;
    pb->rank = neighbor->rank;
    pb->cellid = neighbor->cellid;
    pb->inter_freq_srxlev = neighbor->inter_freq_srxlev;

    LOGI("%s: freq_mode[%d], earfcn[%d], pcid[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d], srxlev[%d], "
         "resel_priority[%d], s_non_intra_search[%d], thresh_serving_low[%d], s_intra_search[%d], thresh_x_low[%d], "
         "thresh_x_high[%d], psc[%d], rscp[%d], ecno[%d], cell_set[%d], rank[%d], cellid[%d], inter_freq_srxlev[%d]",
         __func__, pb->freq_mode, pb->earfcn, pb->pcid, pb->rsrp, pb->rsrq, pb->rssi, pb->sinr, pb->srxlev,
         pb->cell_resel_priority, pb->s_non_intra_search, pb->thresh_serving_low, pb->s_intra_search,
         pb->thresh_x_low, pb->thresh_x_high, pb->psc, pb->rscp, pb->ecno, pb->cell_set, pb->rank, pb->cellid,
         pb->inter_freq_srxlev);

    return pb;
}


static void
free_pb_neighbor(Interfaces__CellInfo__NetNeighborCellInfo *pb)
{
    FREE(pb);
}


/**
 * @brief Allocates and sets a table of neighbor cells messages
 *
 * Uses the report info to fill a dynamically allocated
 * table of neighbor cell messages.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Interfaces__CellInfo__NetNeighborCellInfo **
cell_info_set_pb_neighbors(struct cell_info_report *report)
{
    Interfaces__CellInfo__NetNeighborCellInfo **neighbors_pb_tbl;
    Interfaces__CellInfo__NetNeighborCellInfo **neighbors_pb;
    struct cell_net_neighbor_cell_info **neighbors;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_neigh_cells == 0) return NULL;

    neighbors_pb_tbl = CALLOC(report->n_neigh_cells,
                              sizeof(*neighbors_pb_tbl));

    neighbors = report->cell_neigh_cell_info;
    neighbors_pb = neighbors_pb_tbl;
    allocated = 0;
    /* Set each of the window protobuf */
    for (i = 0; i < report->n_neigh_cells; i++)
    {
        *neighbors_pb = cell_info_set_pb_neighbor(*neighbors);
        if (*neighbors_pb == NULL) goto error;

        allocated++;
        neighbors++;
        neighbors_pb++;
    }
    return neighbors_pb_tbl;

error:
    for (i = 0; i < allocated; i++)
    {
        free_pb_neighbor(neighbors_pb_tbl[i]);
    }
    FREE(neighbors_pb_tbl);

    return NULL;
}


static Interfaces__CellInfo__NetCarrierAggInfo *
cell_info_set_pb_primary_carrier_agg(struct cell_info_report *report)
{
    Interfaces__CellInfo__NetCarrierAggInfo *pb;
    struct cell_net_pca_info *pca;

    if (report == NULL) return NULL;

    pca = report->cell_pca_info;
    if (pca == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__net_carrier_agg_info__init(pb);

    /* Assign the message fields */
    pb->carrier_component = (Interfaces__CellInfo__CarrierComponent)pca->lcc;
    pb->freq = pca->freq;
    pb->bandwidth = (Interfaces__CellInfo__Bandwidth)pca->bandwidth;
    pb->pcell_state = (Interfaces__CellInfo__PcellState)pca->pcell_state;
    pb->pcid = pca->pcid;
    pb->rsrp = pca->rsrp;
    pb->rsrq = pca->rsrq;
    pb->rssi = pca->rssi;
    pb->sinr = pca->sinr;

    LOGI("%s: lcc[%d], freq[%d], bandwidth[%d], pcell_state[%d], pcid[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d]",
         __func__, pb->carrier_component, pb->freq, pb->bandwidth, pb->pcell_state, pb->pcid, pb->rsrp, pb->rsrq, pb->rssi,
         pb->sinr);

    return pb;
}


static void
cell_info_free_pb_primary_carrier_agg(Interfaces__CellInfo__NetCarrierAggInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__NetCarrierAggInfo *
cell_info_set_pb_lte_secondary_carrier_agg(struct cell_net_lte_sca_info *sca)
{
    Interfaces__CellInfo__NetCarrierAggInfo *pb;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__net_carrier_agg_info__init(pb);

    /* Assign the message fields */
    pb->carrier_component = (Interfaces__CellInfo__CarrierComponent)sca->lcc;
    pb->freq = sca->freq;
    pb->bandwidth = (Interfaces__CellInfo__Bandwidth)sca->bandwidth;
    pb->scell_state = (Interfaces__CellInfo__ScellState)sca->scell_state;
    pb->pcid = sca->pcid;
    pb->rsrp = sca->rsrp;
    pb->rsrq = sca->rsrq;
    pb->rssi = sca->rssi;
    pb->sinr = sca->sinr;

    LOGI("%s: lcc[%d], freq[%d], bandwidth[%d], scell_state[%d], pcid[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d]",
         __func__, pb->carrier_component, pb->freq, pb->bandwidth, pb->scell_state, pb->pcid, pb->rsrp, pb->rsrq,
         pb->rssi, pb->sinr);

    return pb;
}

static void
cell_info_free_pb_lte_secondary_carrier_agg(Interfaces__CellInfo__NetCarrierAggInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__NetCarrierAggInfo **
cell_info_set_pb_lte_secondary_carrier_aggs(struct cell_info_report *report)
{
    Interfaces__CellInfo__NetCarrierAggInfo **sca_pb_tbl;
    Interfaces__CellInfo__NetCarrierAggInfo **sca_pb;
    struct cell_net_lte_sca_info **sca;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_lte_sca_cells == 0) return NULL;

    sca_pb_tbl = CALLOC(report->n_lte_sca_cells, sizeof(*sca_pb_tbl));

    sca = report->cell_lte_sca_info;
    sca_pb = sca_pb_tbl;
    allocated = 0;

    for (i = 0; i < report->n_lte_sca_cells; i++)
    {
        *sca_pb = cell_info_set_pb_lte_secondary_carrier_agg(*sca);
        if (sca_pb == NULL) goto error;

        allocated++;
        sca++;
        sca_pb++;
    }
    return sca_pb_tbl;

error:
    for (i = 0; i < allocated; i++)
    {
        cell_info_free_pb_lte_secondary_carrier_agg(sca_pb_tbl[i]);
    }

    FREE(sca_pb_tbl);
    return NULL;
}

static Interfaces__CellInfo__PdpContextInfo *
cell_info_set_pb_pdp_ctx_param(struct cell_pdp_ctx_dynamic_params_info *pdp_ctx)
{
    Interfaces__CellInfo__PdpContextInfo *pb;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__pdp_context_info__init(pb);

    /* Assign the message fields */
    pb->cid = pdp_ctx->cid;
    pb->bearer_id = pdp_ctx->bearer_id;
    pb->apn = pdp_ctx->apn;
    pb->local_addr = pdp_ctx->local_addr;
    pb->subnetmask = pdp_ctx->subnetmask;
    pb->gw_addr = pdp_ctx->gw_addr;
    pb->dns_prim_addr = pdp_ctx->dns_prim_addr;
    pb->dns_sec_addr = pdp_ctx->dns_sec_addr;
    pb->p_cscf_prim_addr = pdp_ctx->p_cscf_prim_addr;
    pb->p_cscf_sec_addr = pdp_ctx->p_cscf_sec_addr;;
    pb->im_cn_signalling_flag = pdp_ctx->im_cn_signalling_flag;;
    pb->lipaindication = pdp_ctx->lipaindication;

    return pb;
}

static void
cell_info_free_pb_pdp_ctx_param(Interfaces__CellInfo__PdpContextInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__PdpContextInfo **
cell_info_set_pb_pdp_ctx_params(struct cell_info_report *report)
{
    Interfaces__CellInfo__PdpContextInfo **pdp_pb_tbl;
    Interfaces__CellInfo__PdpContextInfo **pdp_pb;
    struct cell_pdp_ctx_dynamic_params_info **pdp;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_pdp_cells == 0) return NULL;

    pdp_pb_tbl = CALLOC(report->n_pdp_cells, sizeof(*pdp_pb_tbl));

    pdp = report->cell_pdp_ctx_info;
    pdp_pb = pdp_pb_tbl;
    allocated = 0;

    for (i = 0; i < report->n_pdp_cells; i++)
    {
        *pdp_pb = cell_info_set_pb_pdp_ctx_param(*pdp);
        if (pdp_pb == NULL) goto error;

        allocated++;
        pdp++;
        pdp_pb++;
    }

    return pdp_pb_tbl;

error:
    for (i = 0; i < allocated; i++)
    {
        cell_info_free_pb_pdp_ctx_param(pdp_pb_tbl[i]);
    }

    FREE(pdp_pb_tbl);
    return NULL;
}

static Interfaces__CellInfo__CellularNr5gCellInfo *
cellular_info_set_pb_nr5g_sa_srv_cell(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularNr5gCellInfo *pb;
    struct cell_nr5g_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->nr5g_sa_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__cellular_nr5g_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__CellInfo__CellularServingCellState)cell->state;
    pb->fdd_tdd_mode = (Interfaces__CellInfo__FddTddMode)cell->fdd_tdd_mode;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->arfcn = cell->arfcn;
    pb->band = cell->band;
    pb->ul_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->ul_bandwidth;
    pb->dl_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->dl_bandwidth;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->sinr = cell->sinr;
    pb->scs = (Interfaces__CellInfo__NrScs)cell->scs;
    pb->srxlev = cell->srxlev;
    pb->layers = (Interfaces__CellInfo__Nr5gLayers)cell->layers;
    pb->mcs = (Interfaces__CellInfo__Nr5gMcs)cell->mcs;
    pb->modulation = (Interfaces__CellInfo__Nr5gModulation)cell->modulation;

    LOGI("%s: state[%d], fdd_tdd_mode[%d], cellid[%d], pcid[%d], arfcn[%d], band[%d], "
         "ul_bandwidth[%d], dl_bandwidth[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d], layers[%d], mcs[%d], "
         "modulation[%d]", __func__,
         pb->state, pb->fdd_tdd_mode, pb->cellid, pb->pcid, pb->arfcn, pb->band, pb->ul_bandwidth,
         pb->dl_bandwidth, pb->rsrp, pb->rsrq, pb->sinr, pb->scs, pb->srxlev, pb->layers, pb->mcs, pb->modulation);

    return pb;
}

static void
cell_info_free_pb_cellular_nr5g_sa_net_serving_cell_info(Interfaces__CellInfo__CellularNr5gCellInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__CellularNr5gCellInfo *
cellular_info_set_pb_nr5g_nsa_srv_cell(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularNr5gCellInfo *pb;
    struct cell_nr5g_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->nr5g_nsa_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__cellular_nr5g_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__CellInfo__CellularServingCellState)cell->state;
    pb->fdd_tdd_mode = (Interfaces__CellInfo__FddTddMode)cell->fdd_tdd_mode;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->arfcn = cell->arfcn;
    pb->band = cell->band;
    pb->ul_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->ul_bandwidth;
    pb->dl_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->dl_bandwidth;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->sinr = cell->sinr;
    pb->scs = (Interfaces__CellInfo__NrScs)cell->scs;
    pb->srxlev = cell->srxlev;
    pb->layers = (Interfaces__CellInfo__Nr5gLayers)cell->layers;
    pb->mcs = (Interfaces__CellInfo__Nr5gMcs)cell->mcs;
    pb->modulation = (Interfaces__CellInfo__Nr5gModulation)cell->modulation;

    LOGI("%s: state[%d], fdd_tdd_mode[%d], cellid[%d], pcid[%d], arfcn[%d], band[%d], "
         "ul_bandwidth[%d], dl_bandwidth[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d], layers[%d], mcs[%d], "
         "modulation[%d]", __func__,
         pb->state, pb->fdd_tdd_mode, pb->cellid, pb->pcid, pb->arfcn, pb->band, pb->ul_bandwidth,
         pb->dl_bandwidth, pb->rsrp, pb->rsrq, pb->sinr, pb->scs, pb->srxlev, pb->layers, pb->mcs, pb->modulation);

    return pb;
}

static void
cell_info_free_pb_cellular_nr5g_nsa_net_serving_cell_info(Interfaces__CellInfo__CellularNr5gCellInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__CellularNr5gCellInfo *
cell_info_set_pb_nrg_secondary_carrier_agg(struct cell_nr5g_cell_info *cell)
{
    Interfaces__CellInfo__CellularNr5gCellInfo *pb;

    if (cell == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));

    /* Assign the message fields */
    interfaces__cell_info__cellular_nr5g_cell_info__init(pb);
    pb->state = (Interfaces__CellInfo__CellularServingCellState)cell->state;
    pb->fdd_tdd_mode = (Interfaces__CellInfo__FddTddMode)cell->fdd_tdd_mode;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->arfcn = cell->arfcn;
    pb->band = cell->band;
    pb->ul_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->ul_bandwidth;
    pb->dl_bandwidth = (Interfaces__CellInfo__Bandwidth)cell->dl_bandwidth;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->sinr = cell->sinr;
    pb->scs = (Interfaces__CellInfo__NrScs)cell->scs;
    pb->srxlev = cell->srxlev;
    pb->layers = (Interfaces__CellInfo__Nr5gLayers)cell->layers;
    pb->mcs = (Interfaces__CellInfo__Nr5gMcs)cell->mcs;
    pb->modulation = (Interfaces__CellInfo__Nr5gModulation)cell->modulation;

    LOGI("%s: state[%d], fdd_tdd_mode[%d], cellid[%d], pcid[%d], arfcn[%d], band[%d], "
         "ul_bandwidth[%d], dl_bandwidth[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d], layers[%d], mcs[%d], "
         "modulation[%d]", __func__,
         pb->state, pb->fdd_tdd_mode, pb->cellid, pb->pcid, pb->arfcn, pb->band, pb->ul_bandwidth,
         pb->dl_bandwidth, pb->rsrp, pb->rsrq, pb->sinr, pb->scs, pb->srxlev, pb->layers, pb->mcs, pb->modulation);

    return pb;
}

static void
cell_info_free_pb_nrg_secondary_carrier_agg(Interfaces__CellInfo__CellularNr5gCellInfo *pb)
{
    FREE(pb);
}

static Interfaces__CellInfo__CellularNr5gCellInfo **
cell_info_set_pb_nrg_secondary_carrier_aggs(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularNr5gCellInfo **sca_pb_tbl;
    Interfaces__CellInfo__CellularNr5gCellInfo **sca_pb;
    struct cell_nr5g_cell_info **sca;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_nrg_sca_cells == 0) return NULL;

    sca_pb_tbl = CALLOC(report->n_nrg_sca_cells, sizeof(*sca_pb_tbl));

    sca = report->cell_nrg_sca_info;
    sca_pb = sca_pb_tbl;
    allocated = 0;

    for (i = 0; i < report->n_nrg_sca_cells; i++)
    {
        *sca_pb = cell_info_set_pb_nrg_secondary_carrier_agg(*sca);
        if (sca_pb == NULL) goto error;

        allocated++;
        sca++;
        sca_pb++;
    }
    return sca_pb_tbl;

error:
    for (i = 0; i < allocated; i++)
    {
        cell_info_free_pb_nrg_secondary_carrier_agg(sca_pb_tbl[i]);
    }

    FREE(sca_pb_tbl);
    return NULL;
}

static Interfaces__CellInfo__CellFullScanNeighborCell *
cell_info_set_pb_full_scan_neighbor(struct cell_full_scan_neighbor_cell_info *neighbor)
{
    Interfaces__CellInfo__CellFullScanNeighborCell *pb;

    /* Allocate the message */
    pb  = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__cell_info__cell_full_scan_neighbor_cell__init(pb);

    /* Set the message fields */
    pb->rat = (Interfaces__CellInfo__RadioAccessTechnology)neighbor->rat;
    pb->mcc = neighbor->mcc;
    pb->mnc = neighbor->mnc;
    pb->freq = neighbor->freq;
    pb->pcid = neighbor->pcid;
    pb->rsrp = neighbor->rsrp;
    pb->rsrq = neighbor->rsrq;
    pb->srxlev = neighbor->srxlev;
    pb->scs = (Interfaces__CellInfo__NrScs)neighbor->scs;
    pb->squal = neighbor->squal;
    pb->cellid = neighbor->cellid;
    pb->tac = neighbor->tac;
    pb->bandwidth = (Interfaces__CellInfo__Bandwidth)neighbor->bandwidth;
    pb->band = neighbor->band;

    LOGI("%s: rat[%d], mcc[%d], mnc[%d], freq[%d], pcid[%d], rsrp[%d], rsrq[%d], srxlev[%d], "
         "scs[%d], squal[%d], cellid[%d], tac[%d], bandwidth[%d], band[%d]",
         __func__, neighbor->rat, neighbor->mcc, neighbor->mnc, neighbor->freq, neighbor->pcid,
         neighbor->rsrp, neighbor->rsrq, neighbor->srxlev, neighbor->scs, neighbor->squal,
         neighbor->cellid, neighbor->tac, neighbor->bandwidth, neighbor->band);

    return pb;
}


static void
free_pb_full_scan_neighbor(Interfaces__CellInfo__CellFullScanNeighborCell *pb)
{
    FREE(pb);
}

/**
 * @brief Allocates and sets a table of full scan neighbor cell messages
 *
 * Uses the report info to fill a dynamically allocated
 * table of full scan neighbor cell messages.
 * The caller is responsible for freeing the returned pointer
 *
 * @param window info used to fill up the protobuf table
 * @return a flow stats protobuf pointers table
 */
Interfaces__CellInfo__CellFullScanNeighborCell **
cell_info_set_pb_full_scan_neighbors(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellFullScanNeighborCell **neighbors_pb_fs_tbl;
    Interfaces__CellInfo__CellFullScanNeighborCell **neighbors_pb_fs;
    struct cell_full_scan_neighbor_cell_info **neighbors;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_full_scan_neigh_cells == 0) return NULL;

    neighbors_pb_fs_tbl = CALLOC(report->n_full_scan_neigh_cells,
                              sizeof(*neighbors_pb_fs_tbl));

    neighbors = report->cell_full_scan_neigh_cell_info;
    neighbors_pb_fs = neighbors_pb_fs_tbl;
    allocated = 0;
    /* Set each of the window protobuf */
    for (i = 0; i < report->n_full_scan_neigh_cells; i++)
    {
        *neighbors_pb_fs = cell_info_set_pb_full_scan_neighbor(*neighbors);
        if (*neighbors_pb_fs == NULL) goto error;

        allocated++;
        neighbors++;
        neighbors_pb_fs++;
    }
    return neighbors_pb_fs_tbl;

error:
    for (i = 0; i < allocated; i++)
    {
        free_pb_full_scan_neighbor(neighbors_pb_fs_tbl[i]);
    }
    FREE(neighbors_pb_fs_tbl);

    return NULL;
}

/**
 * @brief Free a cell info report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void
cell_info_free_pb_report(Interfaces__CellInfo__CellularInfoReport *pb)
{
    size_t i;

    /* Free the common header */
    cell_info_free_pb_common_header(pb->header);

    /* Free the cell net info */
    cell_info_free_pb_cell_net_info(pb->cellular_net_info);

    /* Free the cell data usage */
    cell_info_free_pb_cell_data_usage(pb->lte_data_usage);

    /* Free the serving cell info */
    cell_info_free_pb_srv_cell(pb->lte_srv_cell);

    /* Free the neighbors cells info */
    for (i = 0; i < pb->n_lte_neigh_cell_info; i++)
    {
        free_pb_neighbor(pb->lte_neigh_cell_info[i]);
    }

    FREE(pb->lte_neigh_cell_info);

    /* Free the primary carrier aggregation info */
    cell_info_free_pb_primary_carrier_agg(pb->lte_primary_carrier_agg_info);

    /* Free the secondary carrier aggregation info */
    for (i = 0; i < pb->n_lte_secondary_carrier_agg_info; i++)
    {
        cell_info_free_pb_lte_secondary_carrier_agg(pb->lte_secondary_carrier_agg_info[i]);
    }

    /* Free the pdp context info */
    for (i = 0; i < pb->n_pdp_context; i++)
    {
        cell_info_free_pb_pdp_ctx_param(pb->pdp_context[i]);
    }

    /* Free the nr5g sa serving cell info */
    cell_info_free_pb_cellular_nr5g_sa_net_serving_cell_info(pb->cell_nr5g_sa_srv_cell);

    cell_info_free_pb_cellular_nr5g_nsa_net_serving_cell_info(pb->cell_nr5g_nsa_srv_cell);

    /* Free the secondary carrier aggregation info */
    for (i = 0; i < pb->n_nrg_secondary_carrier_agg_info; i++)
    {
        cell_info_free_pb_nrg_secondary_carrier_agg(pb->nrg_secondary_carrier_agg_info[i]);
    }

    FREE(pb);
}

static Interfaces__CellInfo__CellularInfoReport *
cell_info_set_pb_report(struct cell_info_report *report)
{
    Interfaces__CellInfo__CellularInfoReport *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the protobuf structure */
    interfaces__cell_info__cellular_info_report__init(pb);

    /* Add the common header */
    pb->header = cell_info_set_pb_common_header(report);

    /* Add the cell net info */
    pb->cellular_net_info = cell_info_set_cell_net_info(report);

    /* Add the cell data usage */
    pb->lte_data_usage = cell_info_set_cell_data_usage(report);

    /* Add the service cell info */
    pb->lte_srv_cell = cell_info_set_srv_cell(report);

    /* Serialize the neighbor cells */
    if (report->n_neigh_cells != 0)
    {
        pb->lte_neigh_cell_info = cell_info_set_pb_neighbors(report);
        if (pb->lte_neigh_cell_info == NULL) goto error;
    }
    pb->n_lte_neigh_cell_info = report->n_neigh_cells;

    /* Serialize the full scan neighbor cells */
    if (report->n_full_scan_neigh_cells != 0)
    {
        pb->full_scan_neigh_cell_info = cell_info_set_pb_full_scan_neighbors(report);
        if (pb->full_scan_neigh_cell_info == NULL) goto error;
    }
    pb->n_full_scan_neigh_cell_info = report->n_full_scan_neigh_cells;

    /* Add the primary carrier component info */
    pb->lte_primary_carrier_agg_info = cell_info_set_pb_primary_carrier_agg(report);

    /* Add the secondary carrier component info */
    if (report->n_lte_sca_cells != 0)
    {
        pb->lte_secondary_carrier_agg_info = cell_info_set_pb_lte_secondary_carrier_aggs(report);
        if (pb->lte_secondary_carrier_agg_info == NULL) goto error;
    }
    pb->n_lte_secondary_carrier_agg_info = report->n_lte_sca_cells;

    /* Add pdp context dynamic parameters */
    if (report->n_pdp_cells != 0)
    {
        pb->pdp_context = cell_info_set_pb_pdp_ctx_params(report);
        if (pb->pdp_context == NULL) goto error;
    }
    pb->n_pdp_context = report->n_pdp_cells;

    /* Add nr5g sa info */
    pb->cell_nr5g_sa_srv_cell = cellular_info_set_pb_nr5g_sa_srv_cell(report);

    /* Add nr5g nsa info */
    pb->cell_nr5g_nsa_srv_cell = cellular_info_set_pb_nr5g_nsa_srv_cell(report);

    /* Add the secondary carrier component info */
    if (report->n_nrg_sca_cells != 0)
    {
        pb->nrg_secondary_carrier_agg_info = cell_info_set_pb_nrg_secondary_carrier_aggs(report);
        if (pb->nrg_secondary_carrier_agg_info == NULL) goto error;
    }
    pb->n_nrg_secondary_carrier_agg_info = report->n_nrg_sca_cells;

    return pb;


error:
    cell_info_free_pb_report(pb);
    return NULL;
}


/**
 * @brief Generates a cell info serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see cell_info_free_packed_buffer() for this purpose.
 *
 * @param cell info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct cell_info_packed_buffer *
serialize_cell_info(struct cell_info_report *report)
{
    struct cell_info_packed_buffer *serialized;
    Interfaces__CellInfo__CellularInfoReport *pb;
    size_t len;
    void *buf;

    if (report == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));

    pb = cell_info_set_pb_report(report);
    if (pb == NULL) goto error;

    /* Get serialization length */
    len = interfaces__cell_info__cellular_info_report__get_packed_size(pb);
    if (len == 0) goto error;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = interfaces__cell_info__cellular_info_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    cell_info_free_pb_report(pb);

    return serialized;

error:
    cell_info_free_packed_buffer(serialized);
    cell_info_free_pb_report(pb);

    return NULL;
}


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
cell_info_free_packed_buffer(struct cell_info_packed_buffer *pb)
{
    if (pb == NULL) return;

    FREE(pb->buf);
    FREE(pb);
}
