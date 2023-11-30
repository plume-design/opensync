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
#include "os_types.h"

#include "cell_info.pb-c.h"

#define C_AT_CMD_RESP 16
#define C_AT_CMD_LONG_RESP 32
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
    uint64_t reported_at; // Unix time in seconds
};

/**
 * Network Registration Status
 */
enum cell_net_reg_status
{
    CELL_NET_REG_STAT_UNSPECIFIED = INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_UNSPECIFIED,
    CELL_NET_REG_STAT_NOTREG =      INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_NOTREG,
    CELL_NET_REG_STAT_REG =         INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_REG,
    CELL_NET_REG_STAT_SEARCH =      INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_SEARCH,
    CELL_NET_REG_STAT_DENIED =      INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_DENIED,
    CELL_NET_REG_STAT_UNKNOWN =     INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_UNKNOWN,
    CELL_NET_REG_STAT_ROAMING =     INTERFACES__CELL_INFO__CELLULAR_NET_REG_STATUS__NET_REG_STAT_ROAMING,
};

enum cell_sim_type
{
    CELL_SIM_TYPE_UNSPECIFIED = INTERFACES__CELL_INFO__CELLULAR_SIM_TYPE__CELLULAR_SIM_TYPE_UNSPECIFIED,
    CELL_SIM_TYPE_ESIM =        INTERFACES__CELL_INFO__CELLULAR_SIM_TYPE__CELLULAR_SIM_TYPE_ESIM,
    CELL_SIM_TYPE_PSIM =        INTERFACES__CELL_INFO__CELLULAR_SIM_TYPE__CELLULAR_SIM_TYPE_PSIM,
};

enum cell_sim_status
{
    CELL_SIM_STATUS_UNSPECIFIED = INTERFACES__CELL_INFO__CELLULAR_SIM_STATUS__CELLULAR_SIM_STATUS_UNSPECIFIED,
    CELL_SIM_STATUS_INSERTED =    INTERFACES__CELL_INFO__CELLULAR_SIM_STATUS__CELLULAR_SIM_STATUS_INSERTED,
    CELL_SIM_STATUS_REMOVED =     INTERFACES__CELL_INFO__CELLULAR_SIM_STATUS__CELLULAR_SIM_STATUS_REMOVED,
    CELL_SIM_STATUS_BAD =         INTERFACES__CELL_INFO__CELLULAR_SIM_STATUS__CELLULAR_SIM_STATUS_BAD,
};

enum cell_endc
{
    CELL_ENDC_UNDEFINED = INTERFACES__CELL_INFO__ENDC__ENDC_UNDEFINED,
    CELL_ENDC_NOT_SUPPORTED = INTERFACES__CELL_INFO__ENDC__ENDC_NOT_SUPPORTED,
    CELL_ENDC_SUPPORTED = INTERFACES__CELL_INFO__ENDC__ENDC_SUPPORTED,
};

/**
 * Cell mode
 */
enum cellular_mode {
    CELL_MODE_UNSPECIFIED = INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_UNSPECIFIED,
    CELL_MODE_NR5G_SA =     INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_NR5G_SA,
    CELL_MODE_NR5G_NSA =     INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_NR5G_NSA,
    CELL_MODE_NR5G_NSA_5G_RRC_IDLE =   INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_NR5G_NSA_5G_RRC_IDLE,
    CELL_MODE_LTE =         INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_LTE,
    CELL_MODE_WCDMA =       INTERFACES__CELL_INFO__CELLULAR_MODE__CELLULAR_MODE_WCDMA,

};

/**
 * CellNetInfo representation
 */
struct cell_net_info
{
    enum cell_net_reg_status net_status;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t tac;
    char service_provider[C_FQDN_LEN];
    enum cell_sim_type sim_type;
    enum cell_sim_status sim_status;
    uint32_t active_sim_slot;
    int32_t rssi;
    int32_t ber;
    int32_t rsrp;
    int32_t sinr;
    uint64_t last_healthcheck_success;
    uint64_t healthcheck_failures;
    enum cell_endc endc;
    enum cellular_mode mode;
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
 * Serving Cell State
 */
enum serving_cell_state {
    SERVING_CELL_UNSPECIFIED = INTERFACES__CELL_INFO__CELLULAR_SERVING_CELL_STATE__CELLULAR_SERVING_CELL_UNSPECIFIED,
    SERVING_CELL_SEARCH =      INTERFACES__CELL_INFO__CELLULAR_SERVING_CELL_STATE__CELLULAR_SERVING_CELL_SEARCH,
    SERVING_CELL_LIMSERV =     INTERFACES__CELL_INFO__CELLULAR_SERVING_CELL_STATE__CELLULAR_SERVING_CELL_LIMSERV,
    SERVING_CELL_NOCONN =      INTERFACES__CELL_INFO__CELLULAR_SERVING_CELL_STATE__CELLULAR_SERVING_CELL_NOCONN,
    SERVING_CELL_CONNECT =     INTERFACES__CELL_INFO__CELLULAR_SERVING_CELL_STATE__CELLULAR_SERVING_CELL_CONNECT,
};

/**
 * fdd_tdd_mode
 */
enum fdd_tdd_mode {
    FDD_TDD_MODE_UNSPECIFIED = INTERFACES__CELL_INFO__FDD_TDD_MODE__CELLULAR_DUPLEX_UNSPECIFIED,
    FDD_TDD_MODE_FDD =         INTERFACES__CELL_INFO__FDD_TDD_MODE__CELLULAR_DUPLEX_FDD,
    FDD_TDD_MODE_TDD =         INTERFACES__CELL_INFO__FDD_TDD_MODE__CELLULAR_DUPLEX_TDD,
};

/**
 * Uplink/Downlink Bandwidth in MHz
 */
enum bandwidth {
    CELL_BANDWIDTH_UNSPECIFIED = INTERFACES__CELL_INFO__BANDWIDTH__BW_UNSPECIFIED,
    CELL_BANDWIDTH_1P4_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_1P4_MHZ,
    CELL_BANDWIDTH_3_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_3_MHZ,
    CELL_BANDWIDTH_5_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_5_MHZ,
    CELL_BANDWIDTH_10_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_10_MHZ,
    CELL_BANDWIDTH_15_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_15_MHZ,
    CELL_BANDWIDTH_20_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_20_MHZ,
    CELL_BANDWIDTH_25_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_25_MHZ,
    CELL_BANDWIDTH_30_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_30_MHZ,
    CELL_BANDWIDTH_35_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_35_MHZ,
    CELL_BANDWIDTH_40_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_40_MHZ,
    CELL_BANDWIDTH_45_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_45_MHZ,
    CELL_BANDWIDTH_50_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_50_MHZ,
    CELL_BANDWIDTH_60_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_60_MHZ,
    CELL_BANDWIDTH_70_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_70_MHZ,
    CELL_BANDWIDTH_80_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_80_MHZ,
    CELL_BANDWIDTH_90_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_90_MHZ,
    CELL_BANDWIDTH_100_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_100_MHZ,
    CELL_BANDWIDTH_200_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_200_MHZ,
    CELL_BANDWIDTH_400_MHZ = INTERFACES__CELL_INFO__BANDWIDTH__BW_400_MHZ,
};

/**
 * NR sub-carrier space
 */
enum nr_scs {
    NR_SCS_UNSPECIFIED = INTERFACES__CELL_INFO__NR_SCS__SCS_UNSPECIFIED,
    NR_SCS_15_KHZ =      INTERFACES__CELL_INFO__NR_SCS__SCS_15_KHZ,
    NR_SCS_30_KHZ =      INTERFACES__CELL_INFO__NR_SCS__SCS_30_KHZ,
    NR_SCS_60_KHZ =      INTERFACES__CELL_INFO__NR_SCS__SCS_60_KHZ,
    NR_SCS_120_KHZ =     INTERFACES__CELL_INFO__NR_SCS__SCS_120_KHZ,
    NR_SCS_240_KHZ =     INTERFACES__CELL_INFO__NR_SCS__SCS_240_KHZ,
};

/**
 * NR5G Layers
 */
enum nr5g_layers {
    NR5G_LAYERS_UNDEFINED = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_UNDEFINED,
    NR5G_LAYERS_0 = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_0,
    NR5G_LAYERS_1 = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_1,
    NR5G_LAYERS_2 = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_2,
    NR5G_LAYERS_3 = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_3,
    NR5G_LAYERS_4 = INTERFACES__CELL_INFO__NR5G_LAYERS__LAYERS_4,
};

/**
 * NR5G mcs
 */
enum nr5g_mcs {
    NR5G_MCS_UNDEFINED = INTERFACES__CELL_INFO__NR5G_MCS__MCS_UNDEFINED,
    NR5G_MCS_0 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_0,
    NR5G_MCS_1 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_1,
    NR5G_MCS_2 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_3,
    NR5G_MCS_3 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_3,
    NR5G_MCS_4 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_4,
    NR5G_MCS_5 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_5,
    NR5G_MCS_6 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_6,
    NR5G_MCS_7 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_7,
    NR5G_MCS_8 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_8,
    NR5G_MCS_9 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_9,
    NR5G_MCS_10 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_10,
    NR5G_MCS_11 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_11,
    NR5G_MCS_12 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_12,
    NR5G_MCS_13 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_13,
    NR5G_MCS_14 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_14,
    NR5G_MCS_15 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_15,
    NR5G_MCS_16 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_16,
    NR5G_MCS_17 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_17,
    NR5G_MCS_18 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_18,
    NR5G_MCS_19 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_19,
    NR5G_MCS_20 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_20,
    NR5G_MCS_21 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_21,
    NR5G_MCS_22 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_22,
    NR5G_MCS_23 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_23,
    NR5G_MCS_24 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_24,
    NR5G_MCS_25 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_25,
    NR5G_MCS_26 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_26,
    NR5G_MCS_27 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_27,
    NR5G_MCS_28 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_28,
    NR5G_MCS_29 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_29,
    NR5G_MCS_30 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_30,
    NR5G_MCS_31 = INTERFACES__CELL_INFO__NR5G_MCS__MCS_31,
};

/**
 * NR5G modulation
 */
enum nr5g_modulation {
    NR5G_MODULATION_UNDEFINED = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_UNDEFINED,
    NR5G_MODULATION_QPSK = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_QSPK,
    NR5G_MODULATION_16QAM = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_16QAM,
    NR5G_MODULATION_64QAM = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_64QAM,
    NR5G_MODULATION_256QAM = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_256QAM,
    NR5G_MODULATION_1024QAM = INTERFACES__CELL_INFO__NR5G_MODULATION__MOD_1024QAM,
};

struct lte_serving_cell_info
{
    enum serving_cell_state state;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t uarfcn;
    uint32_t earfcn;
    uint32_t band;
    enum bandwidth ul_bandwidth;
    enum bandwidth dl_bandwidth;
    uint32_t tac;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
    enum cell_endc endc;
};

struct cell_nr5g_cell_info
{
    enum serving_cell_state state;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t arfcn;
    uint32_t band;
    enum bandwidth ul_bandwidth;
    enum bandwidth dl_bandwidth;
    enum nr_scs scs;
    uint32_t tac;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    uint32_t sinr;
    uint32_t cqi;
    uint32_t tx_power;
    uint32_t srxlev;
    enum nr5g_layers layers;
    enum nr5g_mcs mcs;
    enum nr5g_modulation modulation;
};

/**
 * NeighborCell freq mode
 */
enum cell_neighbor_freq_mode
{
    CELL_FREQ_MODE_UNSPECIFIED = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_UNSPECIFIED,
    CELL_FREQ_MODE_INTRA = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_INTRA,
    CELL_FREQ_MODE_INTER = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_INTER,
    CELL_FREQ_MODE_WCDMA = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_WCDMA,
    CELL_FREQ_MODE_WCDMA_LTE = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_WCDMA_LTE,
    CELL_FREQ_MODE_5G = INTERFACES__CELL_INFO__NEIGHBOR_FREQ_MODE__FREQ_MODE_5G,
};

/**
 * Cell Set
 */
enum cell_neighbor_cell_set {
    CELL_NEIGHBOR_CELL_SET_UNSPECIFIED = INTERFACES__CELL_INFO__NEIGHBOR_CELL_SET__NEIGHBOR_CELL_SET_UNSPECIFIED,
    CELL_NEIGHBOR_CELL_SET_ACTIVE_SET = INTERFACES__CELL_INFO__NEIGHBOR_CELL_SET__NEIGHBOR_CELL_SET_ACTIVE_SET,
    CELL_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR = INTERFACES__CELL_INFO__NEIGHBOR_CELL_SET__NEIGHBOR_CELL_SET_SYNC_NEIGHBOR,
    CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR = INTERFACES__CELL_INFO__NEIGHBOR_CELL_SET__NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
};

struct cell_net_neighbor_cell_info
{
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

enum cell_carrier_component {
    CELL_CC_UNAVAILABLE = INTERFACES__CELL_INFO__CARRIER_COMPONENT__CC_UNAVAILABLE,
    CELL_PCC = INTERFACES__CELL_INFO__CARRIER_COMPONENT__PCC,
    CELL_SCC = INTERFACES__CELL_INFO__CARRIER_COMPONENT__SCC,
};

enum cell_pcell_state {
    CELL_NO_SERVING = INTERFACES__CELL_INFO__PCELL_STATE__NO_SERVING,
    CELL_REGISTERED = INTERFACES__CELL_INFO__PCELL_STATE__REGISTERED,
};

enum cell_scell_state {
    CELL_DECONFIGURED = INTERFACES__CELL_INFO__SCELL_STATE__DECONFIGURED,
    CELL_CONFIGURED_DEACTIVATED = INTERFACES__CELL_INFO__SCELL_STATE__CONFIGURED_DEACTIVATED,
    CELL_CONFIGURERD_ACTIVATED = INTERFACES__CELL_INFO__SCELL_STATE__CONFIGURERD_ACTIVATED,
};

struct cell_net_pca_info
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum bandwidth bandwidth;
    enum cell_pcell_state pcell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
};

struct cell_net_lte_sca_info
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum bandwidth bandwidth;
    enum cell_scell_state scell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
};

struct cell_pdp_ctx_dynamic_params_info
{
    uint32_t cid;
    uint32_t bearer_id;
    char    apn[C_AT_CMD_LONG_RESP];
    char    local_addr[C_AT_CMD_LONG_RESP];
    char    subnetmask[C_AT_CMD_LONGEST_RESP];
    char    gw_addr[C_AT_CMD_LONG_RESP];
    char    dns_prim_addr[C_AT_CMD_LONGEST_RESP];
    char    dns_sec_addr[C_AT_CMD_LONG_RESP];
    char    p_cscf_prim_addr[C_AT_CMD_LONG_RESP];
    char    p_cscf_sec_addr[C_AT_CMD_LONG_RESP];
    uint32_t im_cn_signalling_flag;
    uint32_t lipaindication;
};

/**
 * CELL info report
 */
struct cell_info_report
{
    struct cell_common_header *header;
    struct cell_net_info *cell_net_info;
    struct cell_data_usage *cell_data_usage;
    struct lte_serving_cell_info *cell_srv_cell;
    size_t n_neigh_cells;
    size_t cur_neigh_cell_idx;
    struct cell_net_neighbor_cell_info **cell_neigh_cell_info;
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
static inline bool
cell_info_set_string(char *source, char **dest)
{
    if (dest == NULL) return false;

    /* Bail if the destination is alreay allocated */
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
bool
cell_info_set_net_info(struct cell_net_info *source, struct cell_info_report *report);


/**
 * @brief free the cell_net_info field contained in a report
 *
 * @param report the report
 */
void
cell_info_free_net_info(struct cell_info_report *report);


/**
 * @brief set the cell data usage of a report
 *
 * @param source the cell data usage to copy
 * @param report the report to update
 * @return true if the info was set, false otherwise
 */
bool
cell_info_set_data_usage(struct cell_data_usage *source, struct cell_info_report *report);

/**
 * @brief free the cell data usage contained in a report
 *
 * @param report the report
 */
void
cell_info_free_data_usage(struct cell_info_report *report);

/**
 * @brief: Allocate a report structure
 *
 * @param n_neighbors the number of neighbor cells to report
 */
struct cell_info_report *
cell_info_allocate_report(size_t n_neighbors, size_t n_lte_sca_cells,
                          size_t n_pdp_cells, size_t n_nrg_sca_cells);

/**
 * @brief free a report structure
 *
 * @param report the report pointer to free
 */
void
cell_info_free_report(struct cell_info_report *report);

/**
 * @brief set the common header of a report
 *
 * @param source the common header to copy
 * @param report the report to update
 * @return true if the header was set, false otherwise
 */
bool
cell_info_set_common_header(struct cell_common_header *source,
                           struct cell_info_report *report);

/**
 * @brief free the common header contained in a report
 *
 * @param report the report
 */
void
cell_info_free_common_header(struct cell_info_report *report);

/**
 * @brief add a neighbor cell info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_neigh_cell(struct cell_net_neighbor_cell_info *cell_info,
                         struct cell_info_report *report);


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
                         struct cell_net_neighbor_cell_info *dest);


/**
 * @brief free a neighbor cell info
 *
 * @param cell the structure to free
 */
void
cell_info_free_neigh_cell(struct cell_net_neighbor_cell_info *cell);


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
cell_info_set_serving_cell(struct lte_serving_cell_info *source,
                           struct cell_info_report *report);


/**
 * @brief free a serving cell info
 *
 * @param sig_qual the structure to free
 */
void
cell_info_free_serving_cell(struct cell_info_report *report);

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
                  struct cell_info_report *report);

/**
 * @brief free pca info
 *
 * @param report the report
 */
void
cell_info_free_pca(struct cell_info_report *report);

/**
 * @brief add lte secondary aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_lte_sca(struct cell_net_lte_sca_info *cell_info,
                      struct cell_info_report *report);


/**
 * @brief set lte secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_lte_sca(struct cell_net_lte_sca_info *source,
                      struct cell_net_lte_sca_info *dest);

/**
 * @brief free lte sca info
 *
 * @param report the report
 */
void
cell_info_free_lte_sca(struct cell_net_lte_sca_info *sca);


/**
 * @brief add dynamic pdp ctx info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *cell_info,
                      struct cell_info_report *report);

/**
 * @brief  sets dynamic pdp ctx info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool
cell_info_set_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *source,
                      struct cell_pdp_ctx_dynamic_params_info *dest);

/**
 * @brief free dynamic pdp ctx info
 *
 * @param report the report
 */
void
cell_info_free_pdp_ctx(struct cell_pdp_ctx_dynamic_params_info *pdp_ctx);

/**
 * @brief  sets to the report nr5g sa info
 *
 * @param source nr5g sa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool
cell_info_set_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *source,
                                       struct cell_info_report *report);


/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void
cell_info_free_nr5g_sa_serving_cell(struct cell_nr5g_cell_info *cell);

/**
 * @brief  sets to the report nr5g nsa info
 *
 * @param source nr5g nsa info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 */
bool
cell_info_set_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *source,
                                    struct cell_info_report *report);

/**
 * @brief free cellular nr 5g info
 *
 * @param report the report
 */
void
cell_info_free_nr5g_nsa_serving_cell(struct cell_nr5g_cell_info *cell);

/**
 * @brief add nrg secondary aggregation info to a report
 *
 * @param cell_info the cell info to add
 * @param report the report to update
 * @return true if the cell info was added, false otherwise
 */
bool
cell_info_add_nrg_sca(struct cell_nr5g_cell_info *cell_info,
                      struct cell_info_report *report);


/**
 * @brief set nrg secondary carrier aggregation info
 *
 * @param sca_info the carrier aggregation info to copy
 * @param report the report to update
 * @return true if the info was copied, false otherwise
 *
 * Note: the destination is freed on error
 */
bool
cell_info_set_nrg_sca(struct cell_nr5g_cell_info *source,
                      struct cell_nr5g_cell_info *dest);

/**
 * @brief free nrg sca info
 *
 * @param report the report
 */
void
cell_info_free_nrg_sca(struct cell_nr5g_cell_info *sca);

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
 * a serialized obervation point buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see cell_info_free_packed_buffer() for this purpose.
 *
 * @param cell info used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
struct cell_info_packed_buffer *
serialize_cell_info(struct cell_info_report *report);


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
cell_info_free_packed_buffer(struct cell_info_packed_buffer *pb);
#endif /* CELL_INFO_H_INCLUDED */
