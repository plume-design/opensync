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
#include "lte_info.h"
#include "memutil.h"

#include "lte_info.pb-c.h"

/* ================================================================================
 * Define various mappings between PB enums
 * ================================================================================*/
static Interfaces__LteInfo__LteBandwidth cell_bandwidth_map(enum cell_bandwidth bw)
{
    switch (bw)
    {
        case CELL_BANDWIDTH_UNSPECIFIED:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_UNSPECIFIED;

        case CELL_BANDWIDTH_1P4_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_1P4_MHZ;

        case CELL_BANDWIDTH_3_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_3_MHZ;

        case CELL_BANDWIDTH_5_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_5_MHZ;

        case CELL_BANDWIDTH_10_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_10_MHZ;

        case CELL_BANDWIDTH_15_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_15_MHZ;

        case CELL_BANDWIDTH_20_MHZ:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_20_MHZ;

        default:
            return INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_UNSPECIFIED;
    }
}

static Interfaces__LteInfo__LteCellMode cell_mode_index[] = {
    [CELL_MODE_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_UNSPECIFIED,
    [CELL_MODE_LTE] = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_LTE,
    [CELL_MODE_WCDMA] = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_WCDMA,
};

static Interfaces__LteInfo__LteCellMode cell_mode_map(enum cell_mode mode)
{
    if (mode < ARRAY_LEN(cell_mode_index))
    {
        return cell_mode_index[mode];
    }

    return INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_UNSPECIFIED;
}

static Interfaces__LteInfo__CellularMode cellular_mode_index[] = {
    [CELL_MODE_UNSPECIFIED] = INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_UNSPECIFIED,
    [CELL_MODE_NR5G_SA] = INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_NR5G_SA,
    [CELL_MODE_NR5G_ENDC] = INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_NR5G_ENDC,
    [CELL_MODE_LTE] = INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_LTE,
    [CELL_MODE_WCDMA] = INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_WCDMA,
};

static Interfaces__LteInfo__CellularMode cellular_mode_map(enum cell_cellular_mode mode)
{
    if (mode < ARRAY_LEN(cellular_mode_index))
    {
        return cellular_mode_index[mode];
    }

    return INTERFACES__LTE_INFO__CELLULAR_MODE__CELLULAR_MODE_UNSPECIFIED;
}

static Interfaces__LteInfo__LteFddTddMode fdd_tdd_mode_index[] = {
    [FDD_TDD_MODE_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_UNSPECIFIED,
    [FDD_TDD_MODE_FDD] = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_FDD,
    [FDD_TDD_MODE_TDD] = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_TDD,
};

static Interfaces__LteInfo__LteFddTddMode fdd_tdd_mode_map(enum fdd_tdd_mode mode)
{
    if (mode < ARRAY_LEN(fdd_tdd_mode_index))
    {
        return fdd_tdd_mode_index[mode];
    }

    return INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_UNSPECIFIED;
}

static Interfaces__LteInfo__NrDlBandwidth nr_dl_bandwidth_index[] = {
    [NR_DL_BANDWIDTH_UNSPECIFIED] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_UNSPECIFIED,
    [NR_DL_BANDWIDTH_5_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_5_MHZ,
    [NR_DL_BANDWIDTH_10_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_10_MHZ,
    [NR_DL_BANDWIDTH_15_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_15_MHZ,
    [NR_DL_BANDWIDTH_20_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_20_MHZ,
    [NR_DL_BANDWIDTH_25_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_25_MHZ,
    [NR_DL_BANDWIDTH_30_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_30_MHZ,
    [NR_DL_BANDWIDTH_40_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_40_MHZ,
    [NR_DL_BANDWIDTH_50_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_50_MHZ,
    [NR_DL_BANDWIDTH_60_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_60_MHZ,
    [NR_DL_BANDWIDTH_80_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_80_MHZ,
    [NR_DL_BANDWIDTH_90_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_90_MHZ,
    [NR_DL_BANDWIDTH_100_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_100_MHZ,
    [NR_DL_BANDWIDTH_400_MHZ] = INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_400_MHZ,
};

static Interfaces__LteInfo__NrDlBandwidth nr_dl_bandwidth_map(enum nr_dl_bandwidth bw)
{
    if (bw < ARRAY_LEN(nr_dl_bandwidth_index))
    {
        return nr_dl_bandwidth_index[bw];
    }

    return INTERFACES__LTE_INFO__NR_DL_BANDWIDTH__BW_UNSPECIFIED;
}

static Interfaces__LteInfo__NrScs nr_scs_index[] = {
    [NR_SCS_UNSPECIFIED] = INTERFACES__LTE_INFO__NR_SCS__SCS_UNSPECIFIED,
    [NR_SCS_15_KHZ] = INTERFACES__LTE_INFO__NR_SCS__SCS_15_KHZ,
    [NR_SCS_30_KHZ] = INTERFACES__LTE_INFO__NR_SCS__SCS_30_KHZ,
    [NR_SCS_60_KHZ] = INTERFACES__LTE_INFO__NR_SCS__SCS_60_KHZ,
    [NR_SCS_120_KHZ] = INTERFACES__LTE_INFO__NR_SCS__SCS_120_KHZ,
    [NR_SCS_240_KHZ] = INTERFACES__LTE_INFO__NR_SCS__SCS_240_KHZ,
};

static Interfaces__LteInfo__NrScs nr_scs_map(enum nr_scs nr_scs)
{
    if (nr_scs < ARRAY_LEN(nr_scs_index))
    {
        return nr_scs_index[nr_scs];
    }

    return INTERFACES__LTE_INFO__NR_SCS__SCS_UNSPECIFIED;
}

/**
 * NeighborCell freq mode
 */
static Interfaces__LteInfo__LteNeighborFreqMode cell_neighbor_freq_mode_index[] = {
    [CELL_FREQ_MODE_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_UNSPECIFIED,
    [CELL_FREQ_MODE_INTRA] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTRA,
    [CELL_FREQ_MODE_INTER] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTER,
    [CELL_FREQ_MODE_WCDMA] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA,
    [CELL_FREQ_MODE_WCDMA_LTE] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA_LTE,
};

static Interfaces__LteInfo__LteNeighborFreqMode cell_neighbor_freq_mode_map(enum cell_neighbor_freq_mode mode)
{
    if (mode < ARRAY_LEN(cell_neighbor_freq_mode_index))
    {
        return cell_neighbor_freq_mode_index[mode];
    }

    return INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_UNSPECIFIED;
}

static Interfaces__LteInfo__LteCarrierComponent cell_carrier_component_index[] = {
    [CELL_CC_UNAVAILABLE] = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_CC_UNAVAILABLE,
    [CELL_PCC] = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_PCC,
    [CELL_SCC] = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_SCC,
};

static Interfaces__LteInfo__LteCarrierComponent cell_carrier_component_map(enum cell_carrier_component cc)
{
    if (cc < ARRAY_LEN(cell_carrier_component_index))
    {
        return cell_carrier_component_index[cc];
    }

    return INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_CC_UNAVAILABLE;
}

static Interfaces__LteInfo__LtePcellState cell_pcell_state_index[] = {
    [CELL_NO_SERVING] = INTERFACES__LTE_INFO__LTE_PCELL_STATE__LTE_NO_SERVING,
    [CELL_REGISTERED] = INTERFACES__LTE_INFO__LTE_PCELL_STATE__LTE_REGISTERED,
};

static Interfaces__LteInfo__LtePcellState cell_pcell_state_map(enum cell_pcell_state state)
{
    if (state < ARRAY_LEN(cell_pcell_state_index))
    {
        return cell_pcell_state_index[state];
    }

    return INTERFACES__LTE_INFO__LTE_PCELL_STATE__LTE_NO_SERVING;
}

static Interfaces__LteInfo__LteScellState cell_scell_state_index[] = {
    [CELL_DECONFIGURED] = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_DECONFIGURED,
    [CELL_CONFIGURED_DEACTIVATED] = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_CONFIGURED_DEACTIVATED,
    [CELL_CONFIGURERD_ACTIVATED] = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_CONFIGURERD_ACTIVATED,
};

static Interfaces__LteInfo__LteScellState cell_scell_state_map(enum cell_scell_state state)
{
    if (state < ARRAY_LEN(cell_scell_state_index))
    {
        return cell_scell_state_index[state];
    }

    return INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_DECONFIGURED;
}

static Interfaces__LteInfo__LteNetRegStatus cell_net_reg_status_index[] = {
    [CELL_NET_REG_STAT_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_UNSPECIFIED,
    [CELL_NET_REG_STAT_NOTREG] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_NOTREG,
    [CELL_NET_REG_STAT_REG] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_REG,
    [CELL_NET_REG_STAT_SEARCH] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_SEARCH,
    [CELL_NET_REG_STAT_DENIED] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_DENIED,
    [CELL_NET_REG_STAT_UNKNOWN] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_UNKNOWN,
    [CELL_NET_REG_STAT_ROAMING] = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_ROAMING,
};

static Interfaces__LteInfo__LteNetRegStatus cell_neg_reg_status_map(enum cell_net_reg_status status)
{
    if (status < ARRAY_LEN(cell_net_reg_status_index))
    {
        return cell_net_reg_status_index[status];
    }

    return INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_UNSPECIFIED;
}

static Interfaces__LteInfo__LteSimType cell_sim_type_index[] = {
    [CELL_SIM_TYPE_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_UNSPECIFIED,
    [CELL_SIM_TYPE_ESIM] = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_ESIM,
    [CELL_SIM_TYPE_PSIM] = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_PSIM,
};

static Interfaces__LteInfo__LteSimType cell_sim_type_map(enum cell_sim_type type)
{
    if (type < ARRAY_LEN(cell_sim_type_index))
    {
        return cell_sim_type_index[type];
    }

    return INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_UNSPECIFIED;
}

static Interfaces__LteInfo__LteSimStatus cell_sim_status_index[] = {
    [CELL_SIM_STATUS_UNSPECIFIED] = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_UNSPECIFIED,
    [CELL_SIM_STATUS_INSERTED] = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_INSERTED,
    [CELL_SIM_STATUS_REMOVED] = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_REMOVED,
    [CELL_SIM_STATUS_BAD] = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_BAD,
};

static Interfaces__LteInfo__LteSimStatus cell_sim_status_map(enum cell_sim_status status)
{
    if (status < ARRAY_LEN(cell_sim_status_index))
    {
        return cell_sim_status_index[status];
    }

    return INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_UNSPECIFIED;
}

static Interfaces__LteInfo__LteNeighborCellSet cell_neighbor_cell_set_index[] = {
    [CELL_NEIGHBOR_CELL_SET_UNSPECIFIED] =
            INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_UNSPECIFIED,
    [CELL_NEIGHBOR_CELL_SET_ACTIVE_SET] = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_ACTIVE_SET,
    [CELL_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR] =
            INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR,
    [CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR] =
            INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
};

static Interfaces__LteInfo__LteNeighborCellSet cell_neighbor_cell_set_map(enum cell_neighbor_cell_set set)
{
    if (set < ARRAY_LEN(cell_neighbor_cell_set_index))
    {
        return cell_neighbor_cell_set_index[set];
    }

    return INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_UNSPECIFIED;
}

/**
 * @brief add the lte net info to a report
 *
 * @param source the lte net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool lte_info_set_net_info(struct lte_net_info *source, struct lte_info_report *report)
{
    struct lte_net_info *lte_net_info;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the lte info structure was already allocated */
    if (report->lte_net_info != NULL) return false;

    report->lte_net_info = CALLOC(1, sizeof(*lte_net_info));

    lte_net_info = report->lte_net_info;
    lte_net_info->net_status = source->net_status;
    lte_net_info->mcc = source->mcc;
    lte_net_info->mnc = source->mnc;
    lte_net_info->tac = source->tac;
    ret = lte_info_set_string(source->service_provider, &lte_net_info->service_provider);
    if (!ret) goto error;
    lte_net_info->sim_type = source->sim_type;
    lte_net_info->sim_status = source->sim_status;
    lte_net_info->active_sim_slot = source->active_sim_slot;
    lte_net_info->rssi = source->rssi;
    lte_net_info->ber = source->ber;
    lte_net_info->rsrp = source->rsrp;
    lte_net_info->sinr = source->sinr;
    lte_net_info->last_healthcheck_success = source->last_healthcheck_success;
    lte_net_info->healthcheck_failures = source->healthcheck_failures;

    return true;

error:
    lte_info_free_report(report);
    return false;
}

/**
 * @brief free the lte_net_info field contained in a report
 *
 * @param report the report
 */
void lte_info_free_net_info(struct lte_info_report *report)
{
    struct lte_net_info *lte_net_info;

    if (report == NULL) return;

    lte_net_info = report->lte_net_info;
    if (lte_net_info == NULL) return;

    FREE(lte_net_info->service_provider);
    FREE(lte_net_info);
    report->lte_net_info = NULL;
}

/**
 * @brief Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct lte_info_report *lte_info_allocate_report(size_t n_neighbors)
{
    struct lte_info_report *report;

    report = CALLOC(1, sizeof(*report));

    report->lte_neigh_cell_info = CALLOC(n_neighbors, sizeof(*report->lte_neigh_cell_info));

    report->n_neigh_cells = n_neighbors;

    return report;
}

/**
 * @brief: free a report structure
 *
 * @param report the report pointer to free
 */
void lte_info_free_report(struct lte_info_report *report)
{
    struct lte_net_neighbor_cell_info *cell;
    size_t i;

    if (report == NULL) return;

    lte_info_free_common_header(report);
    lte_info_free_net_info(report);
    lte_info_free_serving_cell(report);
    lte_info_free_data_usage(report);

    for (i = 0; i < report->n_neigh_cells; i++)
    {
        cell = report->lte_neigh_cell_info[i];
        lte_info_free_neigh_cell_info(cell);
    }

    lte_info_free_pca_info(report);
    lte_info_free_sca_info(report);

    FREE(report->lte_neigh_cell_info);

    lte_info_free_pdp_ctx_info(report);

    cellular_info_free_nr5g_sa_serving_cell(report);

    cellular_info_free_nr5g_nsa_serving_cell(report);

    FREE(report);
}

/**
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool lte_info_set_common_header(struct lte_common_header *source, struct lte_info_report *report)
{
    struct lte_common_header *header;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->header != NULL) return false;

    report->header = CALLOC(1, sizeof(*header));

    header = report->header;
    header->request_id = source->request_id;

    ret = lte_info_set_string(source->if_name, &header->if_name);
    if (!ret) goto error;

    ret = lte_info_set_string(source->node_id, &header->node_id);
    if (!ret) goto error;

    ret = lte_info_set_string(source->location_id, &header->location_id);
    if (!ret) goto error;

    ret = lte_info_set_string(source->imei, &header->imei);
    if (!ret) goto error;

    ret = lte_info_set_string(source->imsi, &header->imsi);
    if (!ret) goto error;

    ret = lte_info_set_string(source->iccid, &header->iccid);
    if (!ret) goto error;

    header->reported_at = time(NULL);
    LOGD("%s: request_id[%d], if_name[%s], node_id[%s], location_id[%s], imei[%s] imsi[%s], iccid[%s], reported_at[%d]",
         __func__,
         header->request_id,
         header->if_name,
         header->node_id,
         header->location_id,
         header->imei,
         header->imsi,
         header->iccid,
         (int)header->reported_at);
    return true;

error:
    lte_info_free_report(report);
    return false;
}

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void lte_info_free_common_header(struct lte_info_report *report)
{
    struct lte_common_header *header;

    if (report == NULL) return;

    header = report->header;
    if (header == NULL) return;

    FREE(header->if_name);
    FREE(header->node_id);
    FREE(header->location_id);
    FREE(header->imei);
    FREE(header->imsi);
    FREE(header->iccid);
    FREE(header);
    report->header = NULL;
}

/**
 * @brief set the lte data usage of a report
 *
 * @param source the lte data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool lte_info_set_data_usage(struct lte_data_usage *source, struct lte_info_report *report)
{
    struct lte_data_usage *data_usage;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->lte_data_usage != NULL) return false;

    report->lte_data_usage = CALLOC(1, sizeof(*data_usage));

    data_usage = report->lte_data_usage;
    data_usage->rx_bytes = source->rx_bytes;
    data_usage->tx_bytes = source->tx_bytes;
    data_usage->failover_start = source->failover_start;
    data_usage->failover_end = source->failover_end;
    data_usage->failover_count = source->failover_count;

    return true;
}

/**
 * @brief free the lte data usage contained in a report
 *
 * @param report the report
 */
void lte_info_free_data_usage(struct lte_info_report *report)
{
    struct lte_data_usage *lte_data_usage;

    if (report == NULL) return;

    lte_data_usage = report->lte_data_usage;
    if (lte_data_usage == NULL) return;

    FREE(lte_data_usage);
    report->lte_data_usage = NULL;
}

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool lte_info_add_neigh_cell_info(struct lte_net_neighbor_cell_info *cell_info, struct lte_info_report *report)
{
    struct lte_net_neighbor_cell_info *cell;
    size_t idx;
    bool ret;

    if (report == NULL) return false;
    if (cell_info == NULL) return false;

    /* Bail if we are already at capacity */
    idx = report->cur_neigh_cell_idx;
    if (idx == report->n_neigh_cells) return false;

    cell = CALLOC(1, sizeof(*cell));

    ret = lte_info_set_neigh_cell_info(cell_info, cell);
    if (!ret) goto error;

    report->lte_neigh_cell_info[idx] = cell;
    report->cur_neigh_cell_idx++;

    return true;

error:
    lte_info_free_report(report);
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
bool lte_info_set_neigh_cell_info(struct lte_net_neighbor_cell_info *source, struct lte_net_neighbor_cell_info *dest)
{
    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->freq_mode = source->freq_mode;
    dest->mode = source->mode;
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
void lte_info_free_neigh_cell_info(struct lte_net_neighbor_cell_info *cell)
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
bool lte_info_set_serving_cell(struct lte_net_serving_cell_info *source, struct lte_info_report *report)
{
    struct lte_net_serving_cell_info *cell;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the signal quality structure was already allocated */
    if (report->lte_srv_cell != NULL) return false;

    report->lte_srv_cell = CALLOC(1, sizeof(*cell));

    cell = report->lte_srv_cell;

    cell->state = source->state;
    cell->mode = source->mode;
    cell->fdd_tdd_mode = source->fdd_tdd_mode;
    cell->cellid = source->cellid;
    cell->pcid = source->pcid;
    cell->uarfcn = source->uarfcn;
    cell->earfcn = source->earfcn;
    cell->freq_band = source->freq_band;
    cell->ul_bandwidth = source->ul_bandwidth;
    cell->dl_bandwidth = source->dl_bandwidth;
    cell->tac = source->tac;
    cell->rsrp = source->rsrp;
    cell->rsrq = source->rsrq;
    cell->rssi = source->rssi;
    cell->sinr = source->sinr;
    cell->srxlev = source->srxlev;

    return true;
}

/**
 * @brief free a serving cell info
 *
 * @param report the report
 */
void lte_info_free_serving_cell(struct lte_info_report *report)
{
    struct lte_net_serving_cell_info *srv_cell;

    if (report == NULL) return;

    srv_cell = report->lte_srv_cell;
    if (srv_cell == NULL) return;

    FREE(srv_cell);
    report->lte_srv_cell = NULL;
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
bool lte_info_set_primary_carrier_agg(struct lte_net_pca_info *source, struct lte_info_report *report)
{
    struct lte_net_pca_info *pca;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the pca structure was already allocated */
    if (report->lte_pca_info != NULL) return false;

    report->lte_pca_info = CALLOC(1, sizeof(*pca));

    pca = report->lte_pca_info;

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
 * @brief free pca info
 *
 * @param report the report
 */
void lte_info_free_pca_info(struct lte_info_report *report)
{
    struct lte_net_pca_info *pca;

    if (report == NULL) return;

    pca = report->lte_pca_info;
    if (pca == NULL) return;

    FREE(pca);
    report->lte_pca_info = NULL;
}

/**
 * @brief set secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool lte_info_set_secondary_carrier_agg(struct lte_net_sca_info *source, struct lte_info_report *report)
{
    struct lte_net_sca_info *sca;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the pca structure was already allocated */
    if (report->lte_sca_info != NULL) return false;

    report->lte_sca_info = CALLOC(1, sizeof(*sca));

    sca = report->lte_sca_info;

    sca->lcc = source->lcc;
    sca->freq = source->freq;
    sca->bandwidth = source->bandwidth;
    sca->scell_state = source->scell_state;
    sca->pcid = source->pcid;
    sca->rsrp = source->rsrp;
    sca->rsrq = source->rsrq;
    sca->rssi = source->rssi;
    sca->sinr = source->sinr;

    return true;
}

/**
 * @brief free sca info
 *
 * @param report the report
 */
void lte_info_free_sca_info(struct lte_info_report *report)
{
    struct lte_net_sca_info *sca;

    if (report == NULL) return;

    sca = report->lte_sca_info;
    if (sca == NULL) return;

    FREE(sca);
    report->lte_sca_info = NULL;
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
bool lte_info_set_pdp_ctx_dynamic_params(struct lte_pdp_ctx_dynamic_params_info *source, struct lte_info_report *report)
{
    struct lte_pdp_ctx_dynamic_params_info *pdp_ctx_params;
    int ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the pca structure was already allocated */
    if (report->lte_pdp_ctx_info != NULL) return false;

    report->lte_pdp_ctx_info = CALLOC(1, sizeof(*pdp_ctx_params));

    pdp_ctx_params = report->lte_pdp_ctx_info;

    pdp_ctx_params->cid = source->cid;
    pdp_ctx_params->bearer_id = source->bearer_id;

    // expected info, bail out when it is not available
    ret = lte_info_set_string(source->apn, &pdp_ctx_params->apn);
    if (!ret) goto error;
    ret = lte_info_set_string(source->local_addr, &pdp_ctx_params->local_addr);
    if (!ret) goto error;

    // some of the mobile operators may not implement always all the following field
    lte_info_set_string(source->subnetmask, &pdp_ctx_params->subnetmask);
    lte_info_set_string(source->gw_addr, &pdp_ctx_params->gw_addr);
    lte_info_set_string(source->dns_prim_addr, &pdp_ctx_params->dns_prim_addr);
    lte_info_set_string(source->dns_sec_addr, &pdp_ctx_params->dns_sec_addr);
    lte_info_set_string(source->p_cscf_prim_addr, &pdp_ctx_params->p_cscf_prim_addr);
    lte_info_set_string(source->p_cscf_sec_addr, &pdp_ctx_params->p_cscf_sec_addr);
    pdp_ctx_params->im_cn_signalling_flag = source->im_cn_signalling_flag;
    pdp_ctx_params->lipaindication = source->lipaindication;

    return true;

error:
    lte_info_free_report(report);
    return false;
}

/**
 * @brief free cgcontrdp info
 *
 * @param report the report
 */
void lte_info_free_pdp_ctx_info(struct lte_info_report *report)
{
    struct lte_pdp_ctx_dynamic_params_info *pdp_ctx;

    if (report == NULL) return;

    pdp_ctx = report->lte_pdp_ctx_info;
    if (pdp_ctx == NULL) return;

    FREE(pdp_ctx->apn);
    FREE(pdp_ctx->local_addr);
    FREE(pdp_ctx->subnetmask);
    FREE(pdp_ctx->gw_addr);
    FREE(pdp_ctx->dns_prim_addr);
    FREE(pdp_ctx->dns_sec_addr);
    FREE(pdp_ctx->p_cscf_prim_addr);
    FREE(pdp_ctx->p_cscf_sec_addr);

    FREE(pdp_ctx);
    report->lte_pdp_ctx_info = NULL;
}

/**
 * @brief set nr5g sa info
 *
 * @param report the report
 */
bool cellular_info_set_nr5g_sa_srv_cell(
        struct cellular_nr5g_sa_net_serving_cell_info *source,
        struct lte_info_report *report)
{
    struct cellular_nr5g_sa_net_serving_cell_info *srv_cell_info;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* return if cellular structure is already allocated */
    if (report->nr5g_sa_srv_cell != NULL) return false;

    report->nr5g_sa_srv_cell = CALLOC(1, sizeof(*srv_cell_info));

    srv_cell_info = report->nr5g_sa_srv_cell;
    srv_cell_info->state = source->state;
    srv_cell_info->mode = source->mode;
    srv_cell_info->fdd_tdd_mode = source->fdd_tdd_mode;
    srv_cell_info->mcc = source->mcc;
    srv_cell_info->mnc = source->mnc;
    srv_cell_info->cellid = source->cellid;
    srv_cell_info->pcid = source->pcid;
    srv_cell_info->tac = source->tac;
    srv_cell_info->arfcn = source->arfcn;
    srv_cell_info->band = source->band;
    srv_cell_info->bandwidth = source->bandwidth;
    srv_cell_info->rsrp = source->rsrp;
    srv_cell_info->rsrq = source->rsrq;
    srv_cell_info->sinr = source->sinr;
    srv_cell_info->scs = source->scs;
    srv_cell_info->srxlev = source->srxlev;

    return true;
}

/**
 * @brief free nr5g sa serving cell info
 *
 * @param report the report
 */
void cellular_info_free_nr5g_sa_serving_cell(struct lte_info_report *report)
{
    struct cellular_nr5g_sa_net_serving_cell_info *srv_cell;

    if (report == NULL) return;

    srv_cell = report->nr5g_sa_srv_cell;
    if (srv_cell == NULL) return;

    FREE(srv_cell);
    report->nr5g_sa_srv_cell = NULL;
}

/**
 * @brief set nr5g sa info
 *
 * @param report the report
 */
bool cellular_info_set_nr5g_nsa_srv_cell(
        struct cellular_nr5g_nsa_net_serving_cell_info *source,
        struct lte_info_report *report)
{
    struct cellular_nr5g_nsa_net_serving_cell_info *srv_cell_info;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* return if cellular structure is already allocated */
    if (report->nr5g_nsa_srv_cell != NULL) return false;

    report->nr5g_nsa_srv_cell = CALLOC(1, sizeof(*srv_cell_info));

    srv_cell_info = report->nr5g_nsa_srv_cell;
    srv_cell_info->lte_state = source->lte_state;
    srv_cell_info->lte_mode = source->lte_mode;
    srv_cell_info->lte_fdd_tdd_mode = source->lte_fdd_tdd_mode;
    srv_cell_info->lte_mcc = source->lte_mcc;
    srv_cell_info->lte_mnc = source->lte_mnc;
    srv_cell_info->lte_cellid = source->lte_cellid;
    srv_cell_info->lte_pcid = source->lte_pcid;
    srv_cell_info->lte_tac = source->lte_tac;
    srv_cell_info->lte_earfcn = source->lte_earfcn;
    srv_cell_info->lte_freq_band_ind = source->lte_freq_band_ind;
    srv_cell_info->lte_ul_bandwidth = source->lte_ul_bandwidth;
    srv_cell_info->lte_tac = source->lte_tac;
    srv_cell_info->lte_rsrp = source->lte_rsrp;
    srv_cell_info->lte_rsrq = source->lte_rsrq;
    srv_cell_info->lte_rssi = source->lte_rssi;
    srv_cell_info->lte_sinr = source->lte_sinr;
    srv_cell_info->lte_cqi = source->lte_cqi;
    srv_cell_info->lte_tx_power = source->lte_tx_power;
    srv_cell_info->lte_srxlev = source->lte_srxlev;

    srv_cell_info->nr5g_nsa_mode = source->nr5g_nsa_mode;
    srv_cell_info->nr5g_nsa_mcc = source->nr5g_nsa_mcc;
    srv_cell_info->nr5g_nsa_mnc = source->nr5g_nsa_mnc;
    srv_cell_info->nr5g_nsa_pcid = source->nr5g_nsa_pcid;
    srv_cell_info->nr5g_nsa_rsrp = source->nr5g_nsa_rsrp;
    srv_cell_info->nr5g_nsa_sinr = source->nr5g_nsa_sinr;
    srv_cell_info->nr5g_nsa_rsrq = source->nr5g_nsa_rsrq;
    srv_cell_info->nr5g_nsa_arfcn = source->nr5g_nsa_arfcn;
    srv_cell_info->nr5g_nsa_band = source->nr5g_nsa_band;
    srv_cell_info->nr5g_nsa_dl_bandwidth = source->nr5g_nsa_dl_bandwidth;
    srv_cell_info->nr5g_nsa_scs = source->nr5g_nsa_scs;

    return true;
}

/**
 * @brief free nr5g sa serving cell info
 *
 * @param report the report
 */
void cellular_info_free_nr5g_nsa_serving_cell(struct lte_info_report *report)
{
    struct cellular_nr5g_nsa_net_serving_cell_info *srv_cell;

    if (report == NULL) return;

    srv_cell = report->nr5g_nsa_srv_cell;
    if (srv_cell == NULL) return;

    FREE(srv_cell);
    report->nr5g_nsa_srv_cell = NULL;
}

/**
 * @brief set the common header of a report
 *
 * @param report the report to update
 * @return LteCommonHeader
 *
 * Note: the destination is freed on error
 */
static Interfaces__LteInfo__LteCommonHeader *lte_info_set_pb_common_header(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteCommonHeader *pb;
    struct lte_common_header *header;

    if (report == NULL) return NULL;

    header = report->header;
    if (header == NULL) return NULL;

    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the protobuf structure */
    interfaces__lte_info__lte_common_header__init(pb);

    pb->request_id = header->request_id;
    pb->if_name = header->if_name;
    pb->node_id = header->node_id;
    pb->location_id = header->location_id;
    pb->imei = header->imei;
    pb->imsi = header->imsi;
    pb->iccid = header->iccid;
    pb->reported_at = header->reported_at;
    LOGD("%s: request_id[%d], if_name[%s], node_id[%s], location_id[%s], imei[%s] imsi[%s], iccid[%s], reported_at[%d]",
         __func__,
         pb->request_id,
         pb->if_name,
         pb->node_id,
         pb->location_id,
         pb->imei,
         pb->imsi,
         pb->iccid,
         (int)pb->reported_at);

    return pb;
}

static void lte_info_free_pb_common_header(Interfaces__LteInfo__LteCommonHeader *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetInfo *lte_info_set_lte_net_info(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetInfo *pb;
    struct lte_net_info *lte_net_info;

    if (report == NULL) return NULL;

    lte_net_info = report->lte_net_info;
    if (lte_net_info == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_net_info__init(pb);

    /* Assign the message fields */
    pb->net_status = cell_neg_reg_status_map(lte_net_info->net_status);
    pb->mcc = lte_net_info->mcc;
    pb->mnc = lte_net_info->mnc;
    pb->tac = lte_net_info->tac;
    pb->service_provider = lte_net_info->service_provider;
    pb->rssi = lte_net_info->rssi;
    pb->ber = lte_net_info->ber;
    pb->sim_type = cell_sim_type_map(lte_net_info->sim_type);
    pb->sim_status = cell_sim_status_map(lte_net_info->sim_status);
    pb->last_healthcheck_success = lte_net_info->last_healthcheck_success;
    pb->healthcheck_failures = lte_net_info->healthcheck_failures;

    return pb;
}

static void lte_info_free_pb_lte_net_info(Interfaces__LteInfo__LteNetInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteDataUsage *lte_info_set_lte_data_usage(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteDataUsage *pb;
    struct lte_data_usage *lte_data_usage;

    if (report == NULL) return NULL;

    lte_data_usage = report->lte_data_usage;
    if (lte_data_usage == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_data_usage__init(pb);

    /* Assign the message fields */
    pb->rx_bytes = lte_data_usage->rx_bytes;
    pb->tx_bytes = lte_data_usage->tx_bytes;
    pb->failover_start = lte_data_usage->failover_start;
    pb->failover_end = lte_data_usage->failover_end;
    pb->failover_count = lte_data_usage->failover_count;

    return pb;
}

static void lte_info_free_pb_lte_data_usage(Interfaces__LteInfo__LteDataUsage *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetServingCellInfo *lte_info_set_srv_cell(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetServingCellInfo *pb;
    struct lte_net_serving_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->lte_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_net_serving_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__LteInfo__LteServingCellState)cell->state;
    pb->mode = cell_mode_map(cell->mode);
    pb->fdd_tdd_mode = fdd_tdd_mode_map(cell->fdd_tdd_mode);
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->uarfcn = cell->uarfcn;
    pb->earfcn = cell->earfcn;
    pb->freq_band = cell->freq_band;
    pb->ul_bandwidth = cell_bandwidth_map(cell->ul_bandwidth);
    pb->dl_bandwidth = cell_bandwidth_map(cell->dl_bandwidth);
    pb->tac = cell->tac;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->rssi = cell->rssi;
    pb->sinr = cell->sinr;
    pb->srxlev = cell->srxlev;

    LOGD("%s: state[%d], mode[%d], fdd_tdd_mode[%d], cellid[0x%x], pcid[%d], uarfcn[%d], earfcn[%d], freq_band[%d] "
         "ul_bandwidth[%d] dl_bandwidth[%d], "
         "tac[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d], srxlev[%d]",
         __func__,
         pb->state,
         pb->mode,
         pb->fdd_tdd_mode,
         pb->cellid,
         pb->pcid,
         pb->uarfcn,
         pb->earfcn,
         pb->freq_band,
         pb->ul_bandwidth,
         pb->dl_bandwidth,
         pb->tac,
         pb->rsrp,
         pb->rsrq,
         pb->rssi,
         pb->sinr,
         pb->srxlev);
    return pb;
}

static void lte_info_free_pb_srv_cell(Interfaces__LteInfo__LteNetServingCellInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetNeighborCellInfo *lte_info_set_pb_neighbor(
        struct lte_net_neighbor_cell_info *neighbor)
{
    Interfaces__LteInfo__LteNetNeighborCellInfo *pb;

    /* Allocate the message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_net_neighbor_cell_info__init(pb);

    /* Set the message fieldss */
    pb->mode = cell_mode_map(neighbor->mode);
    pb->freq_mode = cell_neighbor_freq_mode_map(neighbor->freq_mode);
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
    pb->cell_set = cell_neighbor_cell_set_map(neighbor->cell_set);
    pb->rank = neighbor->rank;
    pb->cellid = neighbor->cellid;
    pb->inter_freq_srxlev = neighbor->inter_freq_srxlev;

    return pb;
}

static void free_pb_neighbor(Interfaces__LteInfo__LteNetNeighborCellInfo *pb)
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
Interfaces__LteInfo__LteNetNeighborCellInfo **lte_info_set_pb_neighbors(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetNeighborCellInfo **neighbors_pb_tbl;
    Interfaces__LteInfo__LteNetNeighborCellInfo **neighbors_pb;
    struct lte_net_neighbor_cell_info **neighbors;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_neigh_cells == 0) return NULL;

    neighbors_pb_tbl = CALLOC(report->n_neigh_cells, sizeof(*neighbors_pb_tbl));

    neighbors = report->lte_neigh_cell_info;
    neighbors_pb = neighbors_pb_tbl;
    allocated = 0;
    /* Set each of the window protobuf */
    for (i = 0; i < report->n_neigh_cells; i++)
    {
        *neighbors_pb = lte_info_set_pb_neighbor(*neighbors);
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

static Interfaces__LteInfo__LteNetCarrierAggInfo *lte_info_set_pb_primary_carrier_agg(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetCarrierAggInfo *pb;
    struct lte_net_pca_info *pca;

    if (report == NULL) return NULL;

    pca = report->lte_pca_info;
    if (pca == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_net_carrier_agg_info__init(pb);

    /* Assign the message fields */
    pb->lte_carrier_component = cell_carrier_component_map(pca->lcc);
    pb->freq = pca->freq;
    pb->bandwidth = cell_bandwidth_map(pca->bandwidth);
    pb->pcell_state = cell_pcell_state_map(pca->pcell_state);
    pb->pcid = pca->pcid;
    pb->rsrp = pca->rsrp;
    pb->rsrq = pca->rsrq;
    pb->rssi = pca->rssi;
    pb->sinr = pca->sinr;

    return pb;
}

static void lte_info_free_pb_primary_carrier_agg(Interfaces__LteInfo__LteNetCarrierAggInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetCarrierAggInfo *lte_info_set_pb_secondary_carrier_agg(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetCarrierAggInfo *pb;
    struct lte_net_sca_info *sca;

    if (report == NULL) return NULL;

    sca = report->lte_sca_info;
    if (sca == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_net_carrier_agg_info__init(pb);

    /* Assign the message fields */
    pb->lte_carrier_component = cell_carrier_component_map(sca->lcc);
    pb->freq = sca->freq;
    pb->bandwidth = cell_bandwidth_map(sca->bandwidth);
    pb->scell_state = cell_scell_state_map(sca->scell_state);
    pb->pcid = sca->pcid;
    pb->rsrp = sca->rsrp;
    pb->rsrq = sca->rsrq;
    pb->rssi = sca->rssi;
    pb->sinr = sca->sinr;

    return pb;
}

static void lte_info_free_pb_secondary_carrier_agg(Interfaces__LteInfo__LteNetCarrierAggInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LtePDPContextInfo *lte_info_set_pb_pdp_ctx_param(struct lte_info_report *report)
{
    Interfaces__LteInfo__LtePDPContextInfo *pb;
    struct lte_pdp_ctx_dynamic_params_info *pdp_ctx;

    if (report == NULL) return NULL;

    pdp_ctx = report->lte_pdp_ctx_info;
    if (pdp_ctx == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__lte_pdpcontext_info__init(pb);

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
    pb->p_cscf_sec_addr = pdp_ctx->p_cscf_sec_addr;
    ;
    pb->im_cn_signalling_flag = pdp_ctx->im_cn_signalling_flag;
    ;
    pb->lipaindication = pdp_ctx->lipaindication;

    return pb;
}

static void lte_info_free_pb_pdp_ctx_param(Interfaces__LteInfo__LtePDPContextInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__CellularNr5gSaServingCellInfo *cellular_info_set_pb_nr5g_sa_srv_cell(
        struct lte_info_report *report)
{
    Interfaces__LteInfo__CellularNr5gSaServingCellInfo *pb;
    struct cellular_nr5g_sa_net_serving_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->nr5g_sa_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__cellular_nr5g_sa_serving_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__LteInfo__CellularServingCellState)cell->state;
    pb->mode = cellular_mode_map(cell->mode);
    pb->fdd_tdd_mode = (Interfaces__LteInfo__CellularFddTddMode)cell->fdd_tdd_mode;
    pb->mcc = cell->mcc;
    pb->mnc = cell->mcc;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->tac = cell->tac;
    pb->arfcn = cell->arfcn;
    pb->band = cell->band;
    pb->bw = nr_dl_bandwidth_map(cell->bandwidth);
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->sinr = cell->sinr;
    pb->scs = nr_scs_map(cell->scs);
    pb->srxlev = cell->srxlev;

    LOGD("%s: state[%d], mode[%d], fdd_tdd_mode[%d], mcc[%d], mnc[%d], cellid[0x%d], pcid[%d], arfcn[%d], band[%d] "
         "bandwidth[%d], "
         "tac[%d], rsrp[%d], rsrq[%d], sinr[%d], scs[%d], srxlev[%d]",
         __func__,
         pb->state,
         pb->mode,
         pb->fdd_tdd_mode,
         pb->mcc,
         pb->mnc,
         pb->cellid,
         pb->pcid,
         pb->arfcn,
         pb->band,
         pb->bw,
         pb->tac,
         pb->rsrp,
         pb->rsrq,
         pb->sinr,
         pb->scs,
         pb->srxlev);
    return pb;
}

static void lte_info_free_pb_cellular_nr5g_sa_net_serving_cell_info(
        Interfaces__LteInfo__CellularNr5gSaServingCellInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__CellularNr5gNsaServingCellInfo *cellular_info_set_pb_nr5g_nsa_srv_cell(
        struct lte_info_report *report)
{
    Interfaces__LteInfo__CellularNr5gNsaServingCellInfo *pb;
    struct cellular_nr5g_nsa_net_serving_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->nr5g_nsa_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality message */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the message */
    interfaces__lte_info__cellular_nr5g_nsa_serving_cell_info__init(pb);

    /* Assign the message fields */
    pb->lte_state = (Interfaces__LteInfo__CellularServingCellState)cell->lte_state;
    pb->lte_mode = cellular_mode_map(cell->lte_mode);
    pb->lte_fdd_tdd_mode = (Interfaces__LteInfo__CellularFddTddMode)cell->lte_fdd_tdd_mode;
    pb->lte_mode = cellular_mode_map(cell->lte_mode);
    pb->lte_mcc = cell->lte_mcc;
    pb->lte_mnc = cell->lte_mcc;
    pb->lte_cellid = cell->lte_cellid;
    pb->lte_pcid = cell->lte_pcid;
    pb->lte_earfcn = cell->lte_earfcn;
    pb->lte_freq_band_ind = cell->lte_freq_band_ind;
    pb->ul_bandwidth = cell_bandwidth_map(cell->lte_ul_bandwidth);
    pb->dl_bandwidth = cell_bandwidth_map(cell->lte_dl_bandwidth);
    pb->lte_tac = cell->lte_tac;
    pb->lte_rsrp = cell->lte_rsrp;
    pb->lte_rsrq = cell->lte_rsrq;
    pb->lte_rssi = cell->lte_rssi;
    pb->lte_sinr = cell->lte_sinr;
    pb->lte_cqi = cell->lte_cqi;
    pb->lte_tx_power = cell->lte_tx_power;
    pb->lte_srxlev = cell->lte_srxlev;

    pb->nr5g_nsa_mode = cell->nr5g_nsa_mode;
    pb->nr5g_nsa_mcc = cell->nr5g_nsa_mcc;
    pb->nr5g_nsa_mnc = cell->nr5g_nsa_mnc;
    pb->nr5g_nsa_pcid = cell->nr5g_nsa_pcid;
    pb->nr5g_nsa_rsrp = cell->nr5g_nsa_rsrp;
    pb->nr5g_nsa_sinr = cell->nr5g_nsa_sinr;
    pb->nr5g_nsa_rsrq = cell->nr5g_nsa_rsrq;
    pb->nr5g_nsa_arfcn = cell->nr5g_nsa_arfcn;
    pb->nr5g_nsa_band = cell->nr5g_nsa_band;
    pb->nr5g_nsa_dl_bandwidth = nr_dl_bandwidth_map(cell->nr5g_nsa_dl_bandwidth);
    pb->nr5g_nsa_scs = nr_scs_map(cell->nr5g_nsa_scs);

    LOGD("%s: lte_state[%d], lte_mode[%d], lte_fdd_tdd_mode[%d], lte_mcc[%d], lte_mnc[%d], lte_cellid[0x%d], "
         "lte_pcid[%d], lte_earfcn[%d], lte_freq_band_ind[%d] ul_bandwidth[%d],"
         " dl_bandwidth[%d], lte_tac[%d], lte_rsrp[%d], lte_rsrq[%d], lte_rssi[%d], lte_sinr[%d], lte_cqi[%d], "
         "lte_tx_power[%d], lte_srxlev[%d], nr5g_nsa_mode[%d], nr5g_nsa_mcc[%d], nr5g_nsa_mnc[%d],"
         " nr5g_nsa_pcid[%d], nr5g_nsa_rsrp[%d], nr5g_nsa_sinr[%d], nr5g_nsa_rsrq[%d], nr5g_nsa_arfcn[%d], "
         "nr5g_nsa_band[%d], nr5g_nsa_dl_bandwidth[%d], nr5g_nsa_scs[%d]",
         __func__,
         pb->lte_state,
         pb->lte_mode,
         pb->lte_fdd_tdd_mode,
         pb->lte_mcc,
         pb->lte_mnc,
         pb->lte_cellid,
         pb->lte_pcid,
         pb->lte_earfcn,
         pb->lte_freq_band_ind,
         pb->ul_bandwidth,
         pb->dl_bandwidth,
         pb->lte_tac,
         pb->lte_rsrp,
         pb->lte_rsrq,
         pb->lte_rssi,
         pb->lte_sinr,
         pb->lte_cqi,
         pb->lte_tx_power,
         pb->lte_srxlev,
         pb->nr5g_nsa_mode,
         pb->nr5g_nsa_mcc,
         pb->nr5g_nsa_mnc,
         pb->nr5g_nsa_pcid,
         pb->nr5g_nsa_rsrp,
         pb->nr5g_nsa_sinr,
         pb->nr5g_nsa_rsrq,
         pb->nr5g_nsa_arfcn,
         pb->nr5g_nsa_band,
         pb->nr5g_nsa_dl_bandwidth,
         pb->nr5g_nsa_scs);

    return pb;
}

static void lte_info_free_pb_cellular_nr5g_nsa_net_serving_cell_info(
        Interfaces__LteInfo__CellularNr5gNsaServingCellInfo *pb)
{
    FREE(pb);
}

/**
 * @brief Free a lte info report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void lte_info_free_pb_report(Interfaces__LteInfo__LteInfoReport *pb)
{
    size_t i;

    /* Free the common header */
    lte_info_free_pb_common_header(pb->header);

    /* Free the lte net info */
    lte_info_free_pb_lte_net_info(pb->lte_net_info);

    /* Free the lte data usage */
    lte_info_free_pb_lte_data_usage(pb->lte_data_usage);

    /* Free the serving cell info */
    lte_info_free_pb_srv_cell(pb->lte_srv_cell);

    /* Free the neighbors cells info */
    for (i = 0; i < pb->n_lte_neigh_cell_info; i++)
    {
        free_pb_neighbor(pb->lte_neigh_cell_info[i]);
    }

    FREE(pb->lte_neigh_cell_info);

    /* Free the primary carrier aggregation info */
    lte_info_free_pb_primary_carrier_agg(pb->lte_primary_carrier_agg_info);

    /* Free the secondary carrier aggregation info */
    lte_info_free_pb_secondary_carrier_agg(pb->lte_secondary_carrier_agg_info);

    /* Free the pdp context info */
    lte_info_free_pb_pdp_ctx_param(pb->lte_pdp_context);

    /* Free the nr5g sa serving cell info */
    lte_info_free_pb_cellular_nr5g_sa_net_serving_cell_info(pb->cell_nr5g_sa_srv_cell);

    lte_info_free_pb_cellular_nr5g_nsa_net_serving_cell_info(pb->cell_nr5g_nsa_srv_cell);

    FREE(pb);
}

static Interfaces__LteInfo__LteInfoReport *lte_info_set_pb_report(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteInfoReport *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));

    /* Initialize the protobuf structure */
    interfaces__lte_info__lte_info_report__init(pb);

    /* Add the common header */
    pb->header = lte_info_set_pb_common_header(report);

    /* Add the lte net info */
    pb->lte_net_info = lte_info_set_lte_net_info(report);

    /* Add the lte data usage */
    pb->lte_data_usage = lte_info_set_lte_data_usage(report);

    /* Add the service cell info */
    pb->lte_srv_cell = lte_info_set_srv_cell(report);

    /* Serialize the neighbor cells */
    if (report->n_neigh_cells != 0)
    {
        pb->lte_neigh_cell_info = lte_info_set_pb_neighbors(report);
        if (pb->lte_neigh_cell_info == NULL) goto error;
    }
    pb->n_lte_neigh_cell_info = report->n_neigh_cells;

    /* Add the primary carrier component info */
    pb->lte_primary_carrier_agg_info = lte_info_set_pb_primary_carrier_agg(report);

    /* Add the secondary carrier component info */
    pb->lte_secondary_carrier_agg_info = lte_info_set_pb_secondary_carrier_agg(report);

    /* Add pdp context dynamic parameters */
    pb->lte_pdp_context = lte_info_set_pb_pdp_ctx_param(report);

    /* Add nr5g sa info */
    pb->cell_nr5g_sa_srv_cell = cellular_info_set_pb_nr5g_sa_srv_cell(report);

    /* Add nr5g sa info */
    pb->cell_nr5g_nsa_srv_cell = cellular_info_set_pb_nr5g_nsa_srv_cell(report);
    return pb;

error:
    lte_info_free_pb_report(pb);
    return NULL;
}

/**
 * @brief Generates a lte info serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see lte_info_free_packed_buffer() for this purpose.
 *
 * @param lte info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct lte_info_packed_buffer *serialize_lte_info(struct lte_info_report *report)
{
    struct lte_info_packed_buffer *serialized;
    Interfaces__LteInfo__LteInfoReport *pb;
    size_t len;
    void *buf;

    if (report == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));

    pb = lte_info_set_pb_report(report);
    if (pb == NULL) goto error;

    /* Get serialization length */
    len = interfaces__lte_info__lte_info_report__get_packed_size(pb);
    if (len == 0) goto error;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = interfaces__lte_info__lte_info_report__pack(pb, buf);
    serialized->buf = buf;

    /* Free the protobuf structure */
    lte_info_free_pb_report(pb);

    return serialized;

error:
    lte_info_free_packed_buffer(serialized);
    lte_info_free_pb_report(pb);

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
void lte_info_free_packed_buffer(struct lte_info_packed_buffer *pb)
{
    if (pb == NULL) return;

    FREE(pb->buf);
    FREE(pb);
}
