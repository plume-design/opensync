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
#include "osn_cell_modem.h"

/**
 * LteCommonHeader representation
 */
struct lte_common_header
{
    uint32_t request_id;
    char *if_name;
    char *node_id;
    char *location_id;
    char *imei;
    char *imsi;
    char *iccid;
    uint64_t reported_at;  // Unix time in seconds
};

/**
 * LteNetInfo representation
 */
struct lte_net_info
{
    enum cell_net_reg_status net_status;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t tac;
    char *service_provider;
    enum cell_sim_type sim_type;
    enum cell_sim_status sim_status;
    uint32_t active_sim_slot;
    int32_t rssi;
    int32_t ber;
    int32_t rsrp;
    int32_t sinr;
    uint64_t last_healthcheck_success;
    uint64_t healthcheck_failures;
};

/**
 * LteDataUsage
 */
struct lte_data_usage
{
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t failover_start;  // Unix time in seconds
    uint64_t failover_end;    // Unix time in seconds
    uint32_t failover_count;
};

struct lte_net_serving_cell_info
{
    enum serving_cell_state state;
    enum cell_mode mode;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t uarfcn;
    uint32_t earfcn;
    uint32_t freq_band;
    enum cell_bandwidth ul_bandwidth;
    enum cell_bandwidth dl_bandwidth;
    uint32_t tac;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
};

struct cellular_nr5g_sa_net_serving_cell_info
{
    enum serving_cell_state state;
    enum cell_cellular_mode mode;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t tac;
    uint32_t arfcn;
    uint32_t band;
    enum nr_dl_bandwidth bandwidth;
    int32_t rsrp;
    int32_t rsrq;
    uint32_t sinr;
    enum nr_scs scs;
    uint32_t srxlev;
};

struct cellular_nr5g_nsa_net_serving_cell_info
{
    enum serving_cell_state lte_state;
    enum cell_cellular_mode lte_mode;
    enum fdd_tdd_mode lte_fdd_tdd_mode;
    uint32_t lte_mcc;
    uint32_t lte_mnc;
    uint32_t lte_cellid;
    uint32_t lte_pcid;
    uint32_t lte_earfcn;
    uint32_t lte_freq_band_ind;
    enum cell_bandwidth lte_ul_bandwidth;
    enum cell_bandwidth lte_dl_bandwidth;
    uint32_t lte_tac;
    int32_t lte_rsrp;
    int32_t lte_rsrq;
    int32_t lte_rssi;
    uint32_t lte_sinr;
    uint32_t lte_cqi;
    uint32_t lte_tx_power;
    uint32_t lte_srxlev;
    /* 5GNR-NSA `*/
    enum cell_cellular_mode nr5g_nsa_mode;
    uint32_t nr5g_nsa_mcc;
    uint32_t nr5g_nsa_mnc;
    uint32_t nr5g_nsa_pcid;
    int32_t nr5g_nsa_rsrp;
    int32_t nr5g_nsa_sinr;
    int32_t nr5g_nsa_rsrq;
    uint32_t nr5g_nsa_arfcn;
    uint32_t nr5g_nsa_band;
    enum nr_dl_bandwidth nr5g_nsa_dl_bandwidth;
    enum nr_scs nr5g_nsa_scs;
};

struct lte_net_neighbor_cell_info
{
    enum cell_mode mode;
    enum cell_neighbor_freq_mode freq_mode;
    uint32_t earfcn;
    uint32_t pcid;
    int32_t rsrq;
    int32_t rsrp;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
    uint32_t cell_resel_priority;
    uint32_t s_non_intra_search;
    uint32_t thresh_serving_low;
    uint32_t s_intra_search;
    uint32_t thresh_x_low;
    uint32_t thresh_x_high;
    uint32_t psc;
    int32_t rscp;
    int32_t ecno;
    enum cell_neighbor_cell_set cell_set;
    int32_t rank;
    uint32_t cellid;
    int32_t inter_freq_srxlev;
};

struct lte_net_pca_info
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum cell_bandwidth bandwidth;
    enum cell_pcell_state pcell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
};

struct lte_net_sca_info
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum cell_bandwidth bandwidth;
    enum cell_scell_state scell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
};

struct lte_pdp_ctx_dynamic_params_info
{
    uint32_t cid;
    uint32_t bearer_id;
    char *apn;
    char *local_addr;
    char *subnetmask;
    char *gw_addr;
    char *dns_prim_addr;
    char *dns_sec_addr;
    char *p_cscf_prim_addr;
    char *p_cscf_sec_addr;
    uint32_t im_cn_signalling_flag;
    uint32_t lipaindication;
};

/**
 * LTE info report
 */
struct lte_info_report
{
    struct lte_common_header *header;
    struct lte_net_info *lte_net_info;
    struct lte_data_usage *lte_data_usage;
    struct lte_net_serving_cell_info *lte_srv_cell;
    size_t n_neigh_cells;
    size_t cur_neigh_cell_idx;
    struct lte_net_neighbor_cell_info **lte_neigh_cell_info;
    struct lte_net_pca_info *lte_pca_info;
    struct lte_net_sca_info *lte_sca_info;
    struct lte_pdp_ctx_dynamic_params_info *lte_pdp_ctx_info;
    struct cellular_nr5g_sa_net_serving_cell_info *nr5g_sa_srv_cell;
    struct cellular_nr5g_nsa_net_serving_cell_info *nr5g_nsa_srv_cell;
};

/**
 * @brief Copy a string, returning success or failure
 *
 * @param source the source to copy
 * @param dest the copy destination
 * @return true if no source or the copy succeeded, false otherwise
 */
static inline bool lte_info_set_string(char *source, char **dest)
{
    if (dest == NULL) return false;

    /* Bail if the destination is already allocated */
    if (*dest != NULL) return false;

    if (source == NULL) return true;

    *dest = STRDUP(source);
    return (*dest != NULL);
}

/**
 * @brief add the lte net info to a report
 *
 * @param source the lte net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool lte_info_set_net_info(struct lte_net_info *source, struct lte_info_report *report);

/**
 * @brief free the lte_net_info field contained in a report
 *
 * @param report the report
 */
void lte_info_free_net_info(struct lte_info_report *report);

/**
 * @brief set the lte data usage of a report
 *
 * @param source the lte data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool lte_info_set_data_usage(struct lte_data_usage *source, struct lte_info_report *report);

/**
 * @brief free the lte data usage contained in a report
 *
 * @param report the report
 */
void lte_info_free_data_usage(struct lte_info_report *report);

/**
 * @brief: Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct lte_info_report *lte_info_allocate_report(size_t n_neighbors);

/**
 * @brief free a report structure
 *
 * @param report the report pointer to free
 */
void lte_info_free_report(struct lte_info_report *report);

/**
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the header was set, false otherwise
 */
bool lte_info_set_common_header(struct lte_common_header *source, struct lte_info_report *report);

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void lte_info_free_common_header(struct lte_info_report *report);

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool lte_info_add_neigh_cell_info(struct lte_net_neighbor_cell_info *cell_info, struct lte_info_report *report);

/**
 * @brief copy a neighbor cell info
 *
 * @param source the cell info to copy
 * @param dest the copy destination
 * @return true if the cell info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool lte_info_set_neigh_cell_info(struct lte_net_neighbor_cell_info *source, struct lte_net_neighbor_cell_info *dest);

/**
 * @brief free a neighbor cell info
 *
 * @param cell the structure to free
 */
void lte_info_free_neigh_cell_info(struct lte_net_neighbor_cell_info *cell);

/**
 * @brief set a serving cell info
 *
 * @param source the serving cell info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool lte_info_set_serving_cell(struct lte_net_serving_cell_info *source, struct lte_info_report *report);

/**
 * @brief free a serving cell info
 *
 * @param sig_qual the structure to free
 */
void lte_info_free_serving_cell(struct lte_info_report *report);

/**
 * @brief set primary carrier aggregation info
 *
 * @param pca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool lte_info_set_primary_carrier_agg(struct lte_net_pca_info *source, struct lte_info_report *report);

/**
 * @brief free pca info
 *
 * @param report the report
 */
void lte_info_free_pca_info(struct lte_info_report *report);

/**
 * @brief set secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool lte_info_set_secondary_carrier_agg(struct lte_net_sca_info *source, struct lte_info_report *report);

/**
 * @brief free sca info
 *
 * @param report the report
 */
void lte_info_free_sca_info(struct lte_info_report *report);

/**
 * @brief  set dynamic pdp ctx info
 *
 * @param report the report to update
 */
int cellm_lte_set_pdp_context_dynamic_info(struct lte_info_report *lte_report);

/**
 * @brief  sets to the report dynamic pdp ctx info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool lte_info_set_pdp_ctx_dynamic_params(
        struct lte_pdp_ctx_dynamic_params_info *source,
        struct lte_info_report *report);

/**
 * @brief free dynamic pdp ctx info
 *
 * @param report the report
 */
void lte_info_free_pdp_ctx_info(struct lte_info_report *report);

/**
 * @brief  sets to the report nr5g sa info
 *
 * @param source nr5g sa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool cellular_info_set_nr5g_sa_srv_cell(
        struct cellular_nr5g_sa_net_serving_cell_info *source,
        struct lte_info_report *report);

/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void cellular_info_free_nr5g_sa_serving_cell(struct lte_info_report *report);

/**
 * @brief  sets to the report nr5g nsa info
 *
 * @param source nr5g nsa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool cellular_info_set_nr5g_nsa_srv_cell(
        struct cellular_nr5g_nsa_net_serving_cell_info *source,
        struct lte_info_report *report);

/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void cellular_info_free_nr5g_nsa_serving_cell(struct lte_info_report *report);

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
 * a serialized observation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see lte_info_free_packed_buffer() for this purpose.
 *
 * @param lte info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct lte_info_packed_buffer *serialize_lte_info(struct lte_info_report *report);

/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb)
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void lte_info_free_packed_buffer(struct lte_info_packed_buffer *pb);
#endif /* LTE_INFO_H_INCLUDED */
