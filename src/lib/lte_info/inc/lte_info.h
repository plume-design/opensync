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
#include "os_types.h"

#include "lte_info.pb-c.h"

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
    uint64_t reported_at; // Unix time in seconds
};

/**
 * Network Registration Status
 */
enum lte_net_reg_status
{
    LTE_NET_REG_STAT_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_UNSPECIFIED,
    LTE_NET_REG_STAT_NOTREG = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_NOTREG,
    LTE_NET_REG_STAT_REG = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_REG,
    LTE_NET_REG_STAT_SEARCH = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_SEARCH,
    LTE_NET_REG_STAT_DENIED = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_DENIED,
    LTE_NET_REG_STAT_UNKNOWN = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_UNKNOWN,
    LTE_NET_REG_STAT_ROAMING = INTERFACES__LTE_INFO__LTE_NET_REG_STATUS__LTE_NET_REG_STAT_ROAMING,
};

enum lte_sim_type
{
    LTE_SIM_TYPE_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_UNSPECIFIED,
    LTE_SIM_TYPE_ESIM = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_ESIM,
    LTE_SIM_TYPE_PSIM = INTERFACES__LTE_INFO__LTE_SIM_TYPE__LTE_SIM_TYPE_PSIM,
};

enum lte_sim_status
{
    LTE_SIM_STATUS_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_UNSPECIFIED,
    LTE_SIM_STATUS_INSERTED = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_INSERTED,
    LTE_SIM_STATUS_REMOVED = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_REMOVED,
    LTE_SIM_STATUS_BAD = INTERFACES__LTE_INFO__LTE_SIM_STATUS__LTE_SIM_STATUS_BAD,
};

/**
 * LteNetInfo representation
 */
struct lte_net_info
{
    enum lte_net_reg_status net_status;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t tac;
    char *service_provider;
    enum lte_sim_type sim_type;
    enum lte_sim_status sim_status;
    uint32_t active_sim_slot;
    int32_t rssi;
    int32_t ber;
};

/**
 * LteDataUsage
 */
struct lte_data_usage
{
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t failover_start; // Unix time in seconds
    uint64_t failover_end; // Unix time in seconds
    uint32_t failover_count;
};

/**
 * Serving Cell State
 */
enum lte_serving_cell_state {
    LTE_SERVING_CELL_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_SERVING_CELL_STATE__LTE_SERVING_CELL_UNSPECIFIED,
    LTE_SERVING_CELL_SEARCH = INTERFACES__LTE_INFO__LTE_SERVING_CELL_STATE__LTE_SERVING_CELL_SEARCH,
    LTE_SERVING_CELL_LIMSERV = INTERFACES__LTE_INFO__LTE_SERVING_CELL_STATE__LTE_SERVING_CELL_LIMSERV,
    LTE_SERVING_CELL_NOCONN = INTERFACES__LTE_INFO__LTE_SERVING_CELL_STATE__LTE_SERVING_CELL_NOCONN,
    LTE_SERVING_CELL_CONNECT = INTERFACES__LTE_INFO__LTE_SERVING_CELL_STATE__LTE_SERVING_CELL_CONNECT,
};

/**
 * Cell mode
 */
enum lte_cell_mode {
    LTE_CELL_MODE_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_UNSPECIFIED,
    LTE_CELL_MODE_LTE = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_LTE,
    LTE_CELL_MODE_WCDMA = INTERFACES__LTE_INFO__LTE_CELL_MODE__LTE_CELL_MODE_WCDMA,
};

/**
 * fdd_tdd_mode
 */
enum lte_fdd_tdd_mode {
    LTE_MODE_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_UNSPECIFIED,
    LTE_MODE_FDD = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_FDD,
    LTE_MODE_TDD = INTERFACES__LTE_INFO__LTE_FDD_TDD_MODE__LTE_MODE_TDD,
};

/**
 * Uplink/Downlink Bandwidth in MHz
 */
enum lte_bandwidth {
    LTE_BANDWIDTH_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_UNSPECIFIED,
    LTE_BANDWIDTH_1P4_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_1P4_MHZ,
    LTE_BANDWIDTH_3_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_3_MHZ,
    LTE_BANDWIDTH_5_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_5_MHZ,
    LTE_BANDWIDTH_10_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_10_MHZ,
    LTE_BANDWIDTH_15_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_15_MHZ,
    LTE_BANDWIDTH_20_MHZ = INTERFACES__LTE_INFO__LTE_BANDWIDTH__LTE_BANDWIDTH_20_MHZ,
};

struct lte_net_serving_cell_info
{
    enum lte_serving_cell_state state;
    enum lte_cell_mode mode;
    enum lte_fdd_tdd_mode fdd_tdd_mode;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t uarfcn;
    uint32_t earfcn;
    uint32_t freq_band;
    enum lte_bandwidth ul_bandwidth;
    enum lte_bandwidth dl_bandwidth;
    uint32_t tac;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
};


/**
 * NeighborCell freq mode
 */
enum lte_neighbor_freq_mode
{
    LTE_FREQ_MODE_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_UNSPECIFIED,
    LTE_FREQ_MODE_INTRA = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTRA,
    LTE_FREQ_MODE_INTER = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_INTER,
    LTE_FREQ_MODE_WCDMA = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA,
    LTE_FREQ_MODE_WCDMA_LTE = INTERFACES__LTE_INFO__LTE_NEIGHBOR_FREQ_MODE__LTE_FREQ_MODE_WCDMA_LTE,
};

/**
 * Cell Set
 */
enum lte_neighbor_cell_set {
    LTE_NEIGHBOR_CELL_SET_UNSPECIFIED = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_UNSPECIFIED,
    LTE_NEIGHBOR_CELL_SET_ACTIVE_SET = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_ACTIVE_SET,
    LTE_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR,
    LTE_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR = INTERFACES__LTE_INFO__LTE_NEIGHBOR_CELL_SET__LTE_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
};

struct lte_net_neighbor_cell_info
{
    enum lte_cell_mode mode;
    enum lte_neighbor_freq_mode freq_mode;
    uint32_t earfcn;
    uint32_t uarfcn;
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
    enum lte_neighbor_cell_set cell_set;
    int32_t rank;
    uint32_t cellid;
    int32_t inter_freq_srxlev;
};

enum lte_carrier_component {
    LTE_CC_UNAVAILABLE = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_CC_UNAVAILABLE,
    LTE_PCC = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_PCC,
    LTE_SCC = INTERFACES__LTE_INFO__LTE_CARRIER_COMPONENT__LTE_SCC,
};

enum lte_pcell_state {
    LTE_NO_SERVING = INTERFACES__LTE_INFO__LTE_PCELL_STATE__LTE_NO_SERVING,
    LTE_REGISTERED = INTERFACES__LTE_INFO__LTE_PCELL_STATE__LTE_REGISTERED,
};

enum lte_scell_state {
    LTE_DECONFIGURED = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_DECONFIGURED,
    LTE_CONFIGURED_DEACTIVATED = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_CONFIGURED_DEACTIVATED,
    LTE_CONFIGURERD_ACTIVATED = INTERFACES__LTE_INFO__LTE_SCELL_STATE__LTE_CONFIGURERD_ACTIVATED,
};

struct lte_net_pca_info
{
    enum lte_carrier_component lcc;
    uint32_t freq;
    enum lte_bandwidth bandwidth;
    enum lte_pcell_state pcell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
};

struct lte_net_sca_info
{
    enum lte_carrier_component lcc;
    uint32_t freq;
    enum lte_bandwidth bandwidth;
    enum lte_scell_state scell_state;
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
    char    *apn;
    char    *local_addr;
    char    *subnetmask;
    char    *gw_addr;
    char    *dns_prim_addr;
    char    *dns_sec_addr;
    char    *p_cscf_prim_addr;
    char    *p_cscf_sec_addr;
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
 * @brief add the lte net info to a report
 *
 * @param source the lte net info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
lte_info_set_net_info(struct lte_net_info *source, struct lte_info_report *report);


/**
 * @brief free the lte_net_info field contained in a report
 *
 * @param report the report
 */
void
lte_info_free_net_info(struct lte_info_report *report);


/**
 * @brief set the lte data usage of a report
 *
 * @param source the lte data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
lte_info_set_data_usage(struct lte_data_usage *source, struct lte_info_report *report);

/**
 * @brief free the lte data usage contained in a report
 *
 * @param report the report
 */
void
lte_info_free_data_usage(struct lte_info_report *report);

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
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the header was set, false otherwise
 */
bool
lte_info_set_common_header(struct lte_common_header *source,
                           struct lte_info_report *report);

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void
lte_info_free_common_header(struct lte_info_report *report);

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
 * @brief set primary carrier aggregation info
 *
 * @param pca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
lte_info_set_primary_carrier_agg(struct lte_net_pca_info *source,
                                 struct lte_info_report *report);

/**
 * @brief free pca info
 *
 * @param report the report
 */
void
lte_info_free_pca_info(struct lte_info_report *report);

/**
 * @brief set secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
lte_info_set_secondary_carrier_agg(struct lte_net_sca_info *source,
                                   struct lte_info_report *report);

/**
 * @brief free sca info
 *
 * @param report the report
 */
void
lte_info_free_sca_info(struct lte_info_report *report);


/**
 * @brief  set dynamic pdp ctx info
 *
 * @param report the report to update
 */
int
lte_set_pdp_context_dynamic_info(struct lte_info_report *lte_report);


/**
 * @brief  sets to the report dynamic pdp ctx info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool
lte_info_set_pdp_ctx_dynamic_params(struct lte_pdp_ctx_dynamic_params_info *source,
                                        struct lte_info_report *report);

/**
 * @brief free dynamic pdp ctx info
 *
 * @param report the report
 */
void
lte_info_free_pdp_ctx_info(struct lte_info_report *report);


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
