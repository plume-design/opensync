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

#ifndef LTE_INFO_H_INCLUDED
#define LTE_INFO_H_INCLUDED

#include <stdlib.h>
#include <stdbool.h>

#include "memutil.h"

/**
 * LteInfo representation
 */
struct lte_info
{
    char *prod_id_info;
    char *chip_serial;
    char *imei;
    char *imsi;
    char *iccid;
    char *sim_status;
    char *net_reg_status;
    char *service_provider_name;
    char *sim_slot;
};


/**
 * LteSigQual representation
 */
struct lte_sig_qual
{
  char *rssi;
  char *ber;
};


struct lte_net_serving_cell_info
{
    char *cell_type;
    char *state;
    char *is_tdd;
    char *mcc;
    char *mnc;
    char *cellid;
    char *pcid;
    char *uarfcn;
    char *earfcn;
    char *freq_band;
    char *ul_bandwidth;
    char *dl_bandwidth;
    char *tac;
    char *rsrp;
    char *rsrq;
    char *rssi;
    char *sinr;
    char *srxlev;
};


/**
 * NeighborCell freq mode
 */
enum lte_neighbor_cell_mode
{
    LTE_CELL_MODE_UNSPECIFIED,
    LTE_CELL_MODE_LTE,
    LTE_CELL_MODE_WCDMA,
};


/**
 * NeighborCell freq mode
 */
enum lte_neighbor_freq_mode
{
    LTE_FREQ_MODE_UNSPECIFIED,
    LTE_FREQ_MODE_INTRA,
    LTE_FREQ_MODE_INTER,
    LTE_FREQ_MODE_WCDMA,
    LTE_FREQ_MODE_WCDMA_LTE,
};


struct lte_net_neighbor_cell_info
{
    enum lte_neighbor_cell_mode mode;
    enum lte_neighbor_freq_mode freq_mode;
    char *earfcn;
    char *uarfcn;
    char *pcid;
    char *rsrq;
    char *rsrp;
    char *rssi;
    char *sinr;
    char *srxlev_base_station;
    char *cell_resel_priority;
    char *s_non_intra_search;
    char *thresh_serving_low;
    char *s_intra_search;
    char *thresh_x_low;
    char *thresh_x_high;
    char *psc;
    char *rscp;
    char *ecno;
    char *set;
    char *rank;
    char *cellid;
    char *srxlev_inter_freq;
};


/**
 * LTE info report
 */
struct lte_info_report
{
    char *if_name;
    struct lte_info *lte_info;
    struct lte_sig_qual *lte_sig_qual;
    struct lte_net_serving_cell_info *lte_srv_cell;
    size_t n_neigh_cells;
    size_t cur_neigh_cell_idx;
    struct lte_net_neighbor_cell_info **lte_neigh_cell_info;
};


/**
 * @brief Copy a string, returning success or failure
 *
 * @param source the source to copy
 * @param dest the copy destination
 * @return true if no source or the copy succeeded, false otherwise
 */
static inline bool
lte_info_set_string(char *source, char **dest)
{
    if (dest == NULL) return false;

    /* Bail if the destination is alreay allocated */
    if (*dest != NULL) return false;

    if (source == NULL) return true;

    *dest = STRDUP(source);
    return (*dest != NULL);
}


/**
 * @brief add the lte info to a report
 *
 * @param source the lte info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
lte_info_set_info(struct lte_info *source, struct lte_info_report *report);


/**
 * @brief free the lte_info field contained in a report
 *
 * @param report the report
 */
void
lte_info_free_info(struct lte_info_report *report);


/**
 * @brief: Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct lte_info_report *
lte_info_allocate_report(size_t n_neighbors);

/**
 * @brief free a report structure
 *
 * @param report the report pointer to free
 */
void
lte_info_free_report(struct lte_info_report *report);

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
lte_info_add_neigh_cell_info(struct lte_net_neighbor_cell_info *cell_info,
                             struct lte_info_report *report);


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
                             struct lte_net_neighbor_cell_info *dest);


/**
 * @brief free a neighbor cell info
 *
 * @param cell the structure to free
 */
void
lte_info_free_neigh_cell_info(struct lte_net_neighbor_cell_info *cell);


/**
 * @brief set the signal quality info of a report
 *
 * @param source the signal quality info to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
lte_info_set_lte_sig_qual(struct lte_sig_qual *source,
                          struct lte_info_report *report);


/**
 * @brief free the lte_sig_qal field contained in a report
 *
 * @param report the report
 */
void
lte_info_free_lte_sig_qual(struct lte_info_report *report);


/**
 * @brief set a serving cell info
 *
 * @param source the serving cell info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
lte_info_set_serving_cell(struct lte_net_serving_cell_info *source,
                          struct lte_info_report *report);


/**
 * @brief free a serving cell info
 *
 * @param sig_qual the structure to free
 */
void
lte_info_free_serving_cell(struct lte_info_report *report);


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
                     struct lte_info_report *report);


/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct lte_info_packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};


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
serialize_lte_info(struct lte_info_report *report);


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
lte_info_free_packed_buffer(struct lte_info_packed_buffer *pb);
#endif /* LTE_INFO_H_INCLUDED */
