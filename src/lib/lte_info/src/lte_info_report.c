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

/**
 * @brief add the lte net info to a report
 *
 * @param source the lte net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
lte_info_set_net_info(struct lte_net_info *source, struct lte_info_report *report)
{
    struct lte_net_info *lte_net_info;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the lte info structure was already allocated */
    if (report->lte_net_info != NULL) return false;

    report->lte_net_info = CALLOC(1, sizeof(*lte_net_info));
    if (report->lte_net_info == NULL) goto error;

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
void
lte_info_free_net_info(struct lte_info_report *report)
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
struct lte_info_report *
lte_info_allocate_report(size_t n_neighbors)
{
    struct lte_info_report *report;

    report = CALLOC(1, sizeof(*report));
    if (report == NULL) return NULL;

    report->lte_neigh_cell_info = CALLOC(n_neighbors,
                                         sizeof(*report->lte_neigh_cell_info));

    if (report->lte_neigh_cell_info == NULL) goto error;
    report->n_neigh_cells = n_neighbors;

    return report;

error:
    lte_info_free_report(report);
    return NULL;
}


/**
 * @brief: free a report structure
 *
 * @param report the report pointer to free
 */
void
lte_info_free_report(struct lte_info_report *report)
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

    FREE(report->lte_neigh_cell_info);

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
lte_info_set_common_header(struct lte_common_header *source,
                          struct lte_info_report *report)
{
    struct lte_common_header *header;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->header != NULL) return false;

    report->header = CALLOC(1, sizeof(*header));
    if (report->header == NULL) return false;

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
         __func__, header->request_id, header->if_name, header->node_id, header->location_id, header->imei, header->imsi,
         header->iccid, (int)header->reported_at);
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
void
lte_info_free_common_header(struct lte_info_report *report)
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
bool
lte_info_set_data_usage(struct lte_data_usage *source,
                        struct lte_info_report *report)
{
    struct lte_data_usage *data_usage;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the header was already set */
    if (report->lte_data_usage != NULL) return false;

    report->lte_data_usage = CALLOC(1, sizeof(*data_usage));
    if (report->lte_data_usage == NULL) goto error;

    data_usage = report->lte_data_usage;
    data_usage->rx_bytes = source->rx_bytes;
    data_usage->tx_bytes = source->tx_bytes;
    data_usage->failover_start = source->failover_start;
    data_usage->failover_end = source->failover_end;
    data_usage->failover_count = source->failover_count;

    return true;

error:
    lte_info_free_report(report);
    return false;
}

/**
 * @brief free the lte data usage contained in a report
 *
 * @param report the report
 */
void
lte_info_free_data_usage(struct lte_info_report *report)
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
bool
lte_info_add_neigh_cell_info(struct lte_net_neighbor_cell_info *cell_info,
                             struct lte_info_report *report)
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
    if (cell == NULL) return false;

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
bool
lte_info_set_neigh_cell_info(struct lte_net_neighbor_cell_info *source,
                             struct lte_net_neighbor_cell_info *dest)
{

    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->mode = source->mode;
    dest->freq_mode = source->freq_mode;
    dest->earfcn = source->earfcn;
    dest->uarfcn = source->uarfcn;
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
lte_info_free_neigh_cell_info(struct lte_net_neighbor_cell_info *cell)
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
lte_info_set_serving_cell(struct lte_net_serving_cell_info *source,
                          struct lte_info_report *report)
{
    struct lte_net_serving_cell_info *cell;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the signal quality structure was already allocated */
    if (report->lte_srv_cell != NULL) return false;

    report->lte_srv_cell = CALLOC(1, sizeof(*cell));
    if (report->lte_srv_cell == NULL) goto error;

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

error:
    lte_info_free_report(report);
    return false;
}


/**
 * @brief free a serving cell info
 *
 * @param report the report
 */
void
lte_info_free_serving_cell(struct lte_info_report *report)
{
    struct lte_net_serving_cell_info *srv_cell;

    if (report == NULL) return;

    srv_cell = report->lte_srv_cell;
    if (srv_cell == NULL) return;

    FREE(srv_cell);
    report->lte_srv_cell = NULL;
}

/**
 * @brief set the common header of a report
 *
 * @param report the report to update
 * @return LteCommonHeader
 *
 * Note: the destination is freed on error
 */
static Interfaces__LteInfo__LteCommonHeader *
lte_info_set_pb_common_header(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteCommonHeader *pb;
    struct lte_common_header *header;

    if (report == NULL) return NULL;

    header = report->header;
    if (header == NULL) return NULL;

    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

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
         __func__, pb->request_id, pb->if_name, pb->node_id, pb->location_id, pb->imei, pb->imsi,
         pb->iccid, (int)pb->reported_at);

    return pb;
}

static void
lte_info_free_pb_common_header(Interfaces__LteInfo__LteCommonHeader *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetInfo *
lte_info_set_lte_net_info(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetInfo *pb;
    struct lte_net_info *lte_net_info;

   if (report == NULL) return NULL;

    lte_net_info = report->lte_net_info;
    if (lte_net_info == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the message */
    interfaces__lte_info__lte_net_info__init(pb);

    /* Assign the message fields */
    pb->net_status = (Interfaces__LteInfo__LteNetRegStatus)lte_net_info->net_status;
    pb->mcc = lte_net_info->mcc;
    pb->mnc = lte_net_info->mnc;
    pb->tac = lte_net_info->tac;
    pb->service_provider = lte_net_info->service_provider;
    pb->rssi = lte_net_info->rssi;
    pb->ber = lte_net_info->ber;
    pb->sim_type = (Interfaces__LteInfo__LteSimType)lte_net_info->sim_type;
    pb->sim_status = (Interfaces__LteInfo__LteSimStatus)lte_net_info->sim_status;

    return pb;
}

static void
lte_info_free_pb_lte_net_info(Interfaces__LteInfo__LteNetInfo *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteDataUsage *
lte_info_set_lte_data_usage(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteDataUsage *pb;
    struct lte_data_usage *lte_data_usage;

   if (report == NULL) return NULL;

    lte_data_usage = report->lte_data_usage;
    if (lte_data_usage == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

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

static void
lte_info_free_pb_lte_data_usage(Interfaces__LteInfo__LteDataUsage *pb)
{
    FREE(pb);
}

static Interfaces__LteInfo__LteNetServingCellInfo *
lte_info_set_srv_cell(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetServingCellInfo *pb;
    struct lte_net_serving_cell_info *cell;

    if (report == NULL) return NULL;

    cell = report->lte_srv_cell;
    if (cell == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the message */
    interfaces__lte_info__lte_net_serving_cell_info__init(pb);

    /* Assign the message fields */
    pb->state = (Interfaces__LteInfo__LteServingCellState)cell->state;
    pb->mode = (Interfaces__LteInfo__LteCellMode)cell->mode;
    pb->fdd_tdd_mode = (Interfaces__LteInfo__LteFddTddMode)cell->fdd_tdd_mode;
    pb->cellid = cell->cellid;
    pb->pcid = cell->pcid;
    pb->uarfcn = cell->uarfcn;
    pb->earfcn = cell->earfcn;
    pb->freq_band = cell->freq_band;
    pb->ul_bandwidth = (Interfaces__LteInfo__LteBandwidth)cell->ul_bandwidth;
    pb->dl_bandwidth = (Interfaces__LteInfo__LteBandwidth)cell->dl_bandwidth;
    pb->tac = cell->tac;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->rssi = cell->rssi;
    pb->sinr = cell->sinr;
    pb->srxlev = cell->srxlev;

    LOGD("%s: state[%d], mode[%d], fdd_tdd_mode[%d], cellid[0x%x], pcid[%d], uarfcn[%d], earfcn[%d], freq_band[%d] ul_bandwidth[%d] dl_bandwidth[%d], "
         "tac[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d], srxlev[%d]",
         __func__, pb->state, pb->mode, pb->fdd_tdd_mode, pb->cellid, pb->pcid, pb->uarfcn, pb->earfcn, pb->freq_band, pb->ul_bandwidth,
         pb->dl_bandwidth, pb->tac, pb->rsrp, pb->rsrq, pb->rssi, pb->sinr, pb->srxlev);
    return pb;
}


static void
lte_info_free_pb_srv_cell(Interfaces__LteInfo__LteNetServingCellInfo *pb)
{
    FREE(pb);
}

/**
 * brief map a neighbor frequency mode value to the corresponding message value
 *
 * @param mode the neighbor grequency mode
 * @return the message mode value
 */
static Interfaces__LteInfo__LteNeighborFreqMode
lte_info_set_neighbor_freq_mode(enum lte_neighbor_freq_mode mode)
{
    Interfaces__LteInfo__LteNeighborFreqMode ret;

    switch(mode)
    {
        case LTE_FREQ_MODE_INTRA:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTRA;
            break;

        case LTE_FREQ_MODE_INTER:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTER;
            break;

        case LTE_FREQ_MODE_WCDMA:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA;
            break;

        case LTE_FREQ_MODE_WCDMA_LTE:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA_LTE;
            break;

        default:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_UNSPECIFIED;
            break;
    }

    return ret;
}


static Interfaces__LteInfo__LteNetNeighborCellInfo *
lte_info_set_pb_neighbor(struct lte_net_neighbor_cell_info *neighbor)
{
    Interfaces__LteInfo__LteNetNeighborCellInfo *pb;

    /* Allocate the message */
    pb  = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the message */
    interfaces__lte_info__lte_net_neighbor_cell_info__init(pb);

    /* Set the message fieldss */
    pb->mode = (Interfaces__LteInfo__LteCellMode)neighbor->mode;
    pb->freq_mode = lte_info_set_neighbor_freq_mode(neighbor->freq_mode);
    pb->earfcn = neighbor->earfcn;
    pb->uarfcn = neighbor->uarfcn;
    pb->pcid = neighbor->pcid;
    pb->rsrp = neighbor->rsrp;
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
    pb->cell_set = (Interfaces__LteInfo__LteNeighborCellSet)neighbor->cell_set;
    pb->rank = neighbor->rank;
    pb->cellid = neighbor->cellid;
    pb->inter_freq_srxlev = neighbor->inter_freq_srxlev;

    return pb;
}


static void
free_pb_neighbor(Interfaces__LteInfo__LteNetNeighborCellInfo *pb)
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
Interfaces__LteInfo__LteNetNeighborCellInfo **
lte_info_set_pb_neighbors(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteNetNeighborCellInfo **neighbors_pb_tbl;
    Interfaces__LteInfo__LteNetNeighborCellInfo **neighbors_pb;
    struct lte_net_neighbor_cell_info **neighbors;
    size_t i, allocated;

    if (report == NULL) return NULL;

    if (report->n_neigh_cells == 0) return NULL;

    neighbors_pb_tbl = CALLOC(report->n_neigh_cells,
                              sizeof(*neighbors_pb_tbl));
    if (neighbors_pb_tbl == NULL) return NULL;

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


/**
 * @brief Free a lte info report protobuf structure.
 *
 * Free dynamically allocated fields and the protobuf structure.
 *
 * @param pb flow report structure to free
 * @return none
 */
static void
lte_info_free_pb_report(Interfaces__LteInfo__LteInfoReport *pb)
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
    FREE(pb);
}

static Interfaces__LteInfo__LteInfoReport *
lte_info_set_pb_report(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteInfoReport *pb;

    /* Allocate the protobuf structure */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

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

    return pb;

error:
    lte_info_free_pb_report(pb);
    return NULL;
}


/**
 * @brief Generates a lte info serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see lte_info_free_packed_buffer() for this purpose.
 *
 * @param lte info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct lte_info_packed_buffer *
serialize_lte_info(struct lte_info_report *report)
{
    struct lte_info_packed_buffer *serialized;
    Interfaces__LteInfo__LteInfoReport *pb;
    size_t len;
    void *buf;

    if (report == NULL) return NULL;

    /* Allocate serialization output structure */
    serialized = CALLOC(1, sizeof(*serialized));
    if (serialized == NULL) return NULL;

    pb = lte_info_set_pb_report(report);
    if (pb == NULL) goto error;

    /* Get serialization length */
    len = interfaces__lte_info__lte_info_report__get_packed_size(pb);
    if (len == 0) goto error;

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);
    if (buf == NULL) goto error;

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
void
lte_info_free_packed_buffer(struct lte_info_packed_buffer *pb)
{
    if (pb == NULL) return;

    FREE(pb->buf);
    FREE(pb);
}
