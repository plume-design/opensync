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
 * @brief add the lte info to a report
 *
 * @param source the lte info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
lte_info_set_info(struct lte_info *source, struct lte_info_report *report)
{
    struct lte_info *lte_info;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the lte info structure was already allocated */
    if (report->lte_info != NULL) return false;

    report->lte_info = CALLOC(1, sizeof(*lte_info));
    if (report->lte_info == NULL) return false;

    lte_info = report->lte_info;

    ret = lte_info_set_string(source->prod_id_info, &lte_info->prod_id_info);
    if (!ret) goto error;

    ret = lte_info_set_string(source->chip_serial, &lte_info->chip_serial);
    if (!ret) goto error;

    ret = lte_info_set_string(source->imei, &lte_info->imei);
    if (!ret) goto error;

    ret = lte_info_set_string(source->imsi, &lte_info->imsi);
    if (!ret) goto error;

    ret = lte_info_set_string(source->iccid, &lte_info->iccid);
    if (!ret) goto error;

    ret = lte_info_set_string(source->sim_status, &lte_info->sim_status);
    if (!ret) goto error;

    ret = lte_info_set_string(source->net_reg_status, &lte_info->net_reg_status);
    if (!ret) goto error;

    ret = lte_info_set_string(source->service_provider_name, &lte_info->service_provider_name);
    if (!ret) goto error;

    ret = lte_info_set_string(source->sim_slot, &lte_info->sim_slot);
    if (!ret) goto error;

    return true;

error:
    lte_info_free_info(report);
    return false;
}


/**
 * @brief free the lte_info field contained in a report
 *
 * @param report the report
 */
void
lte_info_free_info(struct lte_info_report *report)
{
    struct lte_info *lte_info;

    if (report == NULL) return;

    lte_info = report->lte_info;
    if (lte_info == NULL) return;

    FREE(lte_info->prod_id_info);
    FREE(lte_info->chip_serial);
    FREE(lte_info->imei);
    FREE(lte_info->imsi);
    FREE(lte_info->iccid);
    FREE(lte_info->sim_status);
    FREE(lte_info->net_reg_status);
    FREE(lte_info->service_provider_name);
    FREE(lte_info->sim_slot);

    FREE(lte_info);
    report->lte_info = NULL;
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

    FREE(report->if_name);
    lte_info_free_info(report);
    lte_info_free_lte_sig_qual(report);
    lte_info_free_serving_cell(report);

    for (i = 0; i < report->n_neigh_cells; i++)
    {
        cell = report->lte_neigh_cell_info[i];
        lte_info_free_neigh_cell_info(cell);
    }

    FREE(report->lte_neigh_cell_info);

    FREE(report);
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
    if (!ret) return false; /* the cell is freed on error */

    report->lte_neigh_cell_info[idx] = cell;
    report->cur_neigh_cell_idx++;

    return true;
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
    bool ret;

    if (source == NULL) return false;
    if (dest == NULL) return false;

    dest->mode = source->mode;
    dest->freq_mode = source->freq_mode;

    ret = lte_info_set_string(source->earfcn, &dest->earfcn);
    if (!ret) goto error;

    ret = lte_info_set_string(source->uarfcn, &dest->uarfcn);
    if (!ret) goto error;

    ret = lte_info_set_string(source->pcid, &dest->pcid);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rsrq, &dest->rsrq);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rsrp, &dest->rsrp);
    if (!ret) goto error;

    ret = lte_info_set_string(source->sinr, &dest->sinr);
    if (!ret) goto error;

    ret = lte_info_set_string(source->srxlev_base_station, &dest->srxlev_base_station);
    if (!ret) goto error;

    ret = lte_info_set_string(source->cell_resel_priority, &dest->cell_resel_priority);
    if (!ret) goto error;

    ret = lte_info_set_string(source->s_non_intra_search, &dest->s_non_intra_search);
    if (!ret) goto error;

    ret = lte_info_set_string(source->thresh_serving_low, &dest->thresh_serving_low);
    if (!ret) goto error;

    ret = lte_info_set_string(source->s_intra_search, &dest->s_intra_search);
    if (!ret) goto error;

    ret = lte_info_set_string(source->thresh_x_low, &dest->thresh_x_low);
    if (!ret) goto error;

    ret = lte_info_set_string(source->thresh_x_high, &dest->thresh_x_high);
    if (!ret) goto error;

    ret = lte_info_set_string(source->psc, &dest->psc);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rscp, &dest->rscp);
    if (!ret) goto error;

    ret = lte_info_set_string(source->ecno, &dest->ecno);
    if (!ret) goto error;

    ret = lte_info_set_string(source->set, &dest->set);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rank, &dest->rank);
    if (!ret) goto error;

    ret = lte_info_set_string(source->cellid, &dest->cellid);
    if (!ret) goto error;

    ret = lte_info_set_string(source->srxlev_inter_freq, &dest->srxlev_inter_freq);
    if (!ret) goto error;
    return true;

error:
    lte_info_free_neigh_cell_info(dest);
    return false;
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

    FREE(cell->earfcn);
    FREE(cell->uarfcn);
    FREE(cell->pcid);
    FREE(cell->rsrq);
    FREE(cell->rsrp);
    FREE(cell->sinr);
    FREE(cell->srxlev_base_station);
    FREE(cell->cell_resel_priority);
    FREE(cell->s_non_intra_search);
    FREE(cell->thresh_serving_low);
    FREE(cell->s_intra_search);
    FREE(cell->thresh_x_low);
    FREE(cell->thresh_x_high);
    FREE(cell->psc);
    FREE(cell->rscp);
    FREE(cell->ecno);
    FREE(cell->set);
    FREE(cell->rank);
    FREE(cell->cellid);
    FREE(cell->srxlev_inter_freq);

    FREE(cell);
    return;
}


/**
 * @brief set the signal quality info of a report
 *
 * @param source the signal quality info to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
lte_info_set_lte_sig_qual(struct lte_sig_qual *source,
                          struct lte_info_report *report)
{
    struct lte_sig_qual *sig_qual;
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the signal quality structure was already allocated */
    if (report->lte_sig_qual != NULL) return false;

    report->lte_sig_qual = CALLOC(1, sizeof(*sig_qual));
    if (report->lte_sig_qual == NULL) return false;

    sig_qual = report->lte_sig_qual;

    ret = lte_info_set_string(source->rssi, &sig_qual->rssi);
    if (!ret) goto error;

    ret = lte_info_set_string(source->ber, &sig_qual->ber);
    if (!ret) goto error;

    return true;

error:
    lte_info_free_lte_sig_qual(report);
    return false;
}


/**
 * @brief free the lte_sig_qal field contained in a report
 *
 * @param report the report
 */
void
lte_info_free_lte_sig_qual(struct lte_info_report *report)
{
    struct lte_sig_qual *sig_qual;

    if (report == NULL) return;

    sig_qual = report->lte_sig_qual;
    if (sig_qual == NULL) return;

    FREE(sig_qual->rssi);
    FREE(sig_qual->ber);
    FREE(sig_qual);
    report->lte_sig_qual = NULL;
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
    bool ret;

    if (source == NULL) return false;
    if (report == NULL) return false;

    /* Bail if the signal quality structure was already allocated */
    if (report->lte_srv_cell != NULL) return false;

    report->lte_srv_cell = CALLOC(1, sizeof(*cell));
    if (report->lte_srv_cell == NULL) return false;

    cell = report->lte_srv_cell;

    ret = lte_info_set_string(source->cell_type, &cell->cell_type);
    if (!ret) goto error;

    ret = lte_info_set_string(source->state, &cell->state);
    if (!ret) goto error;

    ret = lte_info_set_string(source->is_tdd, &cell->is_tdd);
    if (!ret) goto error;

    ret = lte_info_set_string(source->mcc, &cell->mcc);
    if (!ret) goto error;

    ret = lte_info_set_string(source->mnc, &cell->mnc);
    if (!ret) goto error;

    ret = lte_info_set_string(source->cellid, &cell->cellid);
    if (!ret) goto error;

    ret = lte_info_set_string(source->pcid, &cell->pcid);
    if (!ret) goto error;

    ret = lte_info_set_string(source->uarfcn, &cell->uarfcn);
    if (!ret) goto error;

    ret = lte_info_set_string(source->earfcn, &cell->earfcn);
    if (!ret) goto error;

    ret = lte_info_set_string(source->freq_band, &cell->freq_band);
    if (!ret) goto error;

    ret = lte_info_set_string(source->ul_bandwidth, &cell->ul_bandwidth);
    if (!ret) goto error;

    ret = lte_info_set_string(source->tac, &cell->tac);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rsrp, &cell->rsrp);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rsrp, &cell->rsrq);
    if (!ret) goto error;

    ret = lte_info_set_string(source->rssi, &cell->rssi);
    if (!ret) goto error;

    ret = lte_info_set_string(source->sinr, &cell->sinr);
    if (!ret) goto error;

    ret = lte_info_set_string(source->srxlev, &cell->srxlev);
    if (!ret) goto error;

    return true;

error:
    lte_info_free_serving_cell(report);
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

    FREE(srv_cell->cell_type);
    FREE(srv_cell->state);
    FREE(srv_cell->is_tdd);
    FREE(srv_cell->mcc);
    FREE(srv_cell->mnc);
    FREE(srv_cell->cellid);
    FREE(srv_cell->pcid);
    FREE(srv_cell->uarfcn);
    FREE(srv_cell->earfcn);
    FREE(srv_cell->freq_band);
    FREE(srv_cell->ul_bandwidth);
    FREE(srv_cell->dl_bandwidth);
    FREE(srv_cell->tac);
    FREE(srv_cell->rsrp);
    FREE(srv_cell->rsrq);
    FREE(srv_cell->rssi);
    FREE(srv_cell->sinr);
    FREE(srv_cell->srxlev);

    FREE(srv_cell);
    report->lte_srv_cell = NULL;
}


/**
 * @brief set the interface name of a report
 *
 * @param if_name the interface name to set
 * @param report the report to update
 * @return true if the interface name was set, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
lte_info_set_if_name(char *if_name,
                     struct lte_info_report *report)
{
    bool ret;

    ret = lte_info_set_string(if_name, &report->if_name);
    return ret;
}


static Interfaces__LteInfo__LteInfo *
lte_info_set_lte_info(struct lte_info_report *report)
{
    Interfaces__LteInfo__LteInfo *pb;
    struct lte_info *lte_info;

   if (report == NULL) return NULL;

    lte_info = report->lte_info;
    if (lte_info == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the message */
    interfaces__lte_info__lte_info__init(pb);

    /* Assign the message fields */
    pb->prod_id_info = lte_info->prod_id_info;
    pb->chip_serial = lte_info->chip_serial;
    pb->imei = lte_info->imei;
    pb->imsi = lte_info->imsi;
    pb->iccid = lte_info->iccid;
    pb->sim_status = lte_info->sim_status;
    pb->net_reg_status = lte_info->net_reg_status;
    pb->service_provider_name = lte_info->service_provider_name;
    pb->sim_slot = lte_info->sim_slot;

    return pb;
}


static void
lte_info_free_pb_lte_info(Interfaces__LteInfo__LteInfo *pb)
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
    pb->cell_type = cell->cell_type;
    pb->state = cell->state;
    pb->is_tdd = cell->is_tdd;
    pb->mcc = cell->mcc;
    pb->mnc = cell->mnc;
    pb->cellid = pb->cellid;
    pb->pcid = cell->pcid;
    pb->uarfcn = cell->uarfcn;
    pb->earfcn = cell->earfcn;
    pb->freq_band = cell->freq_band;
    pb->ul_bandwidth = cell->ul_bandwidth;
    pb->dl_bandwidth = cell->dl_bandwidth;
    pb->tac = cell->tac;
    pb->rsrp = cell->rsrp;
    pb->rsrq = cell->rsrq;
    pb->rssi = cell->rssi;
    pb->sinr = cell->sinr;
    pb->srxlev = cell->srxlev;

    return pb;
}


static void
lte_info_free_pb_srv_cell(Interfaces__LteInfo__LteNetServingCellInfo *pb)
{
    FREE(pb);
}


static Interfaces__LteInfo__LteSigQual *
lte_info_set_pb_sig_qual(struct lte_info_report *report)
{
    static Interfaces__LteInfo__LteSigQual *pb;
    struct lte_sig_qual *sig_qual;

    if (report == NULL) return NULL;

    sig_qual = report->lte_sig_qual;
    if (sig_qual == NULL) return NULL;

    /* Allocate the signal quality mesage */
    pb = CALLOC(1, sizeof(*pb));
    if (pb == NULL) return NULL;

    /* Initialize the message */
    interfaces__lte_info__lte_sig_qual__init(pb);

    /* Assign the message fields */
    pb->rssi = sig_qual->rssi;
    pb->ber = sig_qual->ber;

    return pb;
}


static void
lte_info_free_pb_sig_qual(Interfaces__LteInfo__LteSigQual *pb)
{
    FREE(pb);
}


/**
 * brief map a neighbor mode value to the corresponding message value
 *
 * @param mode the neighbor mode
 * @return the message mode value
 */
static Interfaces__LteInfo__LteNeighborCellMode
lte_info_set_neighbor_mode(enum lte_neighbor_cell_mode mode)
{
    Interfaces__LteInfo__LteNeighborCellMode ret;

    switch(mode)
    {
        case LTE_CELL_MODE_LTE:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_MODE__LTE_CELL_MODE_LTE;
            break;

        case LTE_CELL_MODE_WCDMA:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_MODE__LTE_CELL_MODE_WCDMA;
            break;

        default:
            ret = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_MODE__LTE_CELL_MODE_UNSPECIFIED;
            break;
    }

    return ret;
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
    pb->mode = lte_info_set_neighbor_mode(neighbor->mode);
    pb->freq_mode = lte_info_set_neighbor_freq_mode(neighbor->freq_mode);
    pb->earfcn = neighbor->earfcn;
    pb->uarfcn = neighbor->uarfcn;
    pb->pcid = neighbor->pcid;
    pb->rsrp = neighbor->rsrp;
    pb->rssi = neighbor->rssi;
    pb->sinr = neighbor->sinr;
    pb->srxlev_base_station = neighbor->srxlev_base_station;
    pb->cell_resel_priority = neighbor->cell_resel_priority;
    pb->s_non_intra_search = neighbor->s_non_intra_search;
    pb->thresh_serving_low = neighbor->thresh_serving_low;
    pb->s_intra_search = neighbor->s_intra_search;
    pb->thresh_x_low = neighbor->thresh_x_low;
    pb->thresh_x_high = neighbor->thresh_x_high;
    pb->psc = neighbor->psc;
    pb->rscp = neighbor->rscp;
    pb->ecno = neighbor->ecno;
    pb->set = neighbor->set;
    pb->rank = neighbor->rank;
    pb->cellid = neighbor->cellid;
    pb->srxlev_inter_freq = neighbor->srxlev_inter_freq;

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

    /* Fre the lte info */
    lte_info_free_pb_lte_info(pb->lte_info);

    /* Free the signal quality info */
    lte_info_free_pb_sig_qual(pb->lte_sig_qual);

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

    /* Add the interface name */
    pb->if_name = report->if_name;

    /* Add the lte info */
    pb->lte_info = lte_info_set_lte_info(report);

    /* Add the signal quality info */
    pb->lte_sig_qual = lte_info_set_pb_sig_qual(report);

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
    buf = malloc(len);
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
