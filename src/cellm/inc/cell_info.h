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

#ifndef CELL_INFO_H_INCLUDED
#define CELL_INFO_H_INCLUDED

#include <stdlib.h>
#include <stdbool.h>

#include "memutil.h"
#include "osn_cell_modem.h"

#define C_AT_CMD_RESP         16
#define C_AT_CMD_LONG_RESP    32
#define C_AT_CMD_LONGEST_RESP 64

/**
 * CellCommonHeader representation
 */
struct cell_common_header
{
    uint32_t request_id;
    char if_name[C_IFNAME_LEN];
    char node_id[C_HOSTNAME_LEN];
    char location_id[C_HOSTNAME_LEN];
    char imei[C_HOSTNAME_LEN];
    char imsi[C_HOSTNAME_LEN];
    char iccid[C_HOSTNAME_LEN];
    char modem_info[C_FQDN_LEN];
    struct osn_cell_ue_band_capability *ue_band_info;
    uint64_t reported_at;  // Unix time in seconds
};

/**
 * CellDataUsage
 */
struct cell_data_usage
{
    uint64_t rx_bytes;
    uint64_t tx_bytes;
};

/**
 * CELL info report
 */
struct cell_info_report
{
    struct cell_common_header *header;
    struct cell_net_info *cell_net_info;
    struct cell_data_usage *cell_data_usage;
    struct cell_serving_cell_info *cell_srv_cell;
    size_t n_neigh_cells;
    struct cell_net_neighbor_cell_info **cell_neigh_cell_info;
    size_t n_full_scan_neigh_cells;
    struct cell_full_scan_neighbor_cell_info **cell_full_scan_neigh_cell_info;
    struct cell_net_pca_info *cell_pca_info;
    size_t n_lte_sca_cells;
    size_t cur_lte_sca_cell_idx;
    struct cell_net_lte_sca_info **cell_lte_sca_info;
    size_t n_pdp_cells;
    size_t cur_pdp_idx;
    struct cell_pdp_ctx_dynamic_params_info **cell_pdp_ctx_info;
    struct cell_nr5g_cell_info *nr5g_sa_srv_cell;
    struct cell_nr5g_cell_info *nr5g_nsa_srv_cell;
    size_t n_nrg_sca_cells;
    size_t cur_nrg_sca_cell_idx;
    struct cell_nr5g_cell_info **cell_nrg_sca_info;
};

/**
 * @brief Copy a string, returning success or failure
 *
 * @param source the source to copy
 * @param dest the copy destination
 * @return true if no source or the copy succeeded, false otherwise
 */
static inline bool cell_info_set_string(char *source, char **dest)
{
    if (dest == NULL) return false;

    /* Bail if the destination is already allocated */
    if (*dest != NULL) return false;

    if (source == NULL) return true;

    *dest = STRDUP(source);
    return (*dest != NULL);
}

/**
 * @brief add the cell net info to a report
 *
 * @param source the cell net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_set_net_info(struct cell_net_info *source, struct cell_info_report *report);

/**
 * @brief free the cell_net_info field contained in a report
 *
 * @param report the report
 */
void cellm_info_free_net_info(struct cell_info_report *report);

/**
 * @brief set the cell data usage of a report
 *
 * @param source the cell data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool cellm_info_set_data_usage(struct cell_data_usage *source, struct cell_info_report *report);

/**
 * @brief free the cell data usage contained in a report
 *
 * @param report the report
 */
void cellm_info_free_data_usage(struct cell_info_report *report);

/**
 * @brief: Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct cell_info_report *cellm_info_allocate_report(
        size_t n_neighbors,
        size_t n_full_scan_neighbors,
        size_t n_lte_sca_cells,
        size_t n_pdp_cells,
        size_t n_nrg_sca_cells);

/**
 * @brief free a report structure
 *
 * @param report the report pointer to free
 */
void cellm_info_free_report(struct cell_info_report *report);

/**
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the header was set, false otherwise
 */
bool cellm_info_set_common_header(struct cell_common_header *source, struct cell_info_report *report);

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void cellm_info_free_common_header(struct cell_info_report *report);

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @param idx the index into which we are adding cell info in the report
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_add_neigh_cell(
        struct cell_net_neighbor_cell_info *cell_info,
        struct cell_info_report *report,
        size_t idx);

/**
 * @brief copy a neighbor cell info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_neigh_cell(struct cell_net_neighbor_cell_info *source, struct cell_net_neighbor_cell_info *dest);

/**
 * @brief free a neighbor cell info
 *
 * @param cell the structure to free
 */
void cellm_info_free_neigh_cell(struct cell_net_neighbor_cell_info *cell);

/**
 * @brief add full scan neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @param idx the index into which we are adding cell info in the report
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_add_full_scan_neigh_cell(
        struct cell_full_scan_neighbor_cell_info *cell_info,
        struct cell_info_report *report,
        size_t idx);

/**
 * @brief copy full scan neighbor cell info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_full_scan_neigh_cell(
        struct cell_full_scan_neighbor_cell_info *source,
        struct cell_full_scan_neighbor_cell_info *dest);

/**
 * @brief free full scan neighbor cell info
 *
 * @param cell the structure to free
 */
void cellm_info_free_full_scan_neigh_cell(struct cell_full_scan_neighbor_cell_info *cell);

/**
 * @brief set a serving cell info
 *
 * @param source the serving cell info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_serving_cell(struct cell_serving_cell_info *source, struct cell_info_report *report);

/**
 * @brief free a serving cell info
 *
 * @param sig_qual the structure to free
 */
void cellm_info_free_serving_cell(struct cell_info_report *report);

/**
 * @brief set primary carrier aggregation info
 *
 * @param pca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_pca(struct cell_net_pca_info *source, struct cell_info_report *report);

/**
 * @brief free pca info
 *
 * @param report the report
 */
void cellm_info_free_pca(struct cell_info_report *report);

/**
 * @brief add lte secondary aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_add_lte_sca(struct cell_net_lte_sca_info *cell_info, struct cell_info_report *report);

/**
 * @brief set lte secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_lte_sca(struct cell_net_lte_sca_info *source, struct cell_net_lte_sca_info *dest);

/**
 * @brief free lte sca info
 *
 * @param report the report
 */
void cellm_info_free_lte_sca(struct cell_net_lte_sca_info *sca);

/**
 * @brief add dynamic pdp ctx info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_add_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *cell_info, struct cell_info_report *report);

/**
 * @brief  sets dynamic pdp ctx info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool cellm_info_set_pdp_ctx(
        struct cell_pdp_ctx_dynamic_params_info *source,
        struct cell_pdp_ctx_dynamic_params_info *dest);

/**
 * @brief free dynamic pdp ctx info
 *
 * @param report the report
 */
void cellm_info_free_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *pdp_ctx);

/**
 * @brief  sets to the report nr5g sa info
 *
 * @param source nr5g sa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool cellm_info_set_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *source, struct cell_info_report *report);

/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void cellm_info_free_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *cell);

/**
 * @brief  sets to the report nr5g nsa info
 *
 * @param source nr5g nsa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool cellm_info_set_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *source, struct cell_info_report *report);

/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void cellm_info_free_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *cell);

/**
 * @brief add nrg secondary aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool cellm_info_add_nrg_sca(struct cell_nr5g_cell_info *cell_info, struct cell_info_report *report);

/**
 * @brief set nrg secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool cellm_info_set_nrg_sca(struct cell_nr5g_cell_info *source, struct cell_nr5g_cell_info *dest);

/**
 * @brief free nrg sca info
 *
 * @param report the report
 */
void cellm_info_free_nrg_sca(struct cell_nr5g_cell_info *sca);

/**
 * @brief Container of protobuf serialization output
 *
 * Contains the information related to a serialized protobuf
 */
struct cell_info_packed_buffer
{
    size_t len; /*<! length of the serialized protobuf */
    void *buf;  /*<! dynamically allocated pointer to serialized data */
};

/**
 * @brief Generates a cell info serialized protobuf
 *
 * Uses the information pointed by the info parameter to generate
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see cell_info_free_packed_buffer() for this purpose.
 *
 * @param cell info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct cell_info_packed_buffer *serialize_cell_info(struct cell_info_report *report);

/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb)
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void cellm_info_free_packed_buffer(struct cell_info_packed_buffer *pb);
#endif /* CELL_INFO_H_INCLUDED */
