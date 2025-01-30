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

#ifndef OSN_CELL_MODEM_H_INCLUDED
#define OSN_CELL_MODEM_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "const.h"

#define C_AT_CMD_RESP         16
#define C_AT_CMD_LONG_RESP    32
#define C_AT_CMD_LONGEST_RESP 64

#define MAX_CELL_COUNT           6
#define MAX_FULL_SCAN_CELL_COUNT 20

#define MAX_NEIGH_CELL_COUNT 6

/**
 * apn_proto_type: ipv4, ipv6, ipv4v6
 */
enum cell_apn_proto_type
{
    CELL_APN_PROTO_TYPE_UNDEFINED,
    CELL_APN_PROTO_TYPE_IPV4,
    CELL_APN_PROTO_TYPE_IPV6,
    CELL_APN_PROTO_TYPE_IPV4V6,
};

/**
 * apn_auth_proto: CHAP, PAP
 */
enum cell_apn_auth_proto
{
    CELL_APN_AUTH_PROTO_CHAP,
    CELL_APN_AUTH_PROTO_PAP,
};

/**
 * cell_mode: Auto, 4G, 5G
 */
enum cell_mode
{
    CELL_MODE_AUTO,
    CELL_MODE_4G,
    CELL_MODE_5G,
};

enum cell_access_tech
{
    CELL_ACT_UNSPECIFIED = 0,
    CELL_ACT_UTRAN = 2,
    CELL_ACT_UTRAN_HSDPA = 4,
    CELL_ACT_UTRAN_HSUPA = 5,
    CELL_ACT_UTRAN_HSDPA_HSUPA = 6,
    CELL_ACT_EUTRAN = 7,
};

enum cell_sim_slot
{
    CELL_SLOT_0,
    CELL_SLOT_1,
};

/**
 * Uplink/Downlink Bandwidth in MHz
 */
enum cell_bandwidth
{
    CELL_BANDWIDTH_UNSPECIFIED,
    CELL_BANDWIDTH_1P4_MHZ,
    CELL_BANDWIDTH_3_MHZ,
    CELL_BANDWIDTH_5_MHZ,
    CELL_BANDWIDTH_10_MHZ,
    CELL_BANDWIDTH_15_MHZ,
    CELL_BANDWIDTH_20_MHZ,
    CELL_BANDWIDTH_25_MHZ,
    CELL_BANDWIDTH_30_MHZ,
    CELL_BANDWIDTH_35_MHZ,
    CELL_BANDWIDTH_40_MHZ,
    CELL_BANDWIDTH_45_MHZ,
    CELL_BANDWIDTH_50_MHZ,
    CELL_BANDWIDTH_60_MHZ,
    CELL_BANDWIDTH_70_MHZ,
    CELL_BANDWIDTH_80_MHZ,
    CELL_BANDWIDTH_90_MHZ,
    CELL_BANDWIDTH_100_MHZ,
    CELL_BANDWIDTH_200_MHZ,
    CELL_BANDWIDTH_400_MHZ
};

/**
 * Serving Cell State
 */
enum serving_cell_state
{
    SERVING_CELL_UNSPECIFIED,
    SERVING_CELL_SEARCH,
    SERVING_CELL_LIMSERV,
    SERVING_CELL_NOCONN,
    SERVING_CELL_CONNECT
};

enum cell_cellular_mode
{
    CELL_MODE_UNSPECIFIED,
    CELL_MODE_NR5G_SA,
    CELL_MODE_NR5G_ENDC,
    CELL_MODE_NR5G_NSA,
    CELL_MODE_NR5G_NSA_5G_RRC_IDLE,
    CELL_MODE_LTE,
    CELL_MODE_WCDMA
};

/**
 * fdd_tdd_mode
 */
enum fdd_tdd_mode
{
    FDD_TDD_MODE_UNSPECIFIED,
    FDD_TDD_MODE_FDD,
    FDD_TDD_MODE_TDD
};

enum nr_dl_bandwidth
{
    NR_DL_BANDWIDTH_UNSPECIFIED,
    NR_DL_BANDWIDTH_5_MHZ,
    NR_DL_BANDWIDTH_10_MHZ,
    NR_DL_BANDWIDTH_15_MHZ,
    NR_DL_BANDWIDTH_20_MHZ,
    NR_DL_BANDWIDTH_25_MHZ,
    NR_DL_BANDWIDTH_30_MHZ,
    NR_DL_BANDWIDTH_40_MHZ,
    NR_DL_BANDWIDTH_50_MHZ,
    NR_DL_BANDWIDTH_60_MHZ,
    NR_DL_BANDWIDTH_80_MHZ,
    NR_DL_BANDWIDTH_90_MHZ,
    NR_DL_BANDWIDTH_100_MHZ,
    NR_DL_BANDWIDTH_400_MHZ
};

/**
 * NR sub-carrier space
 */
enum nr_scs
{
    NR_SCS_UNSPECIFIED,
    NR_SCS_15_KHZ,
    NR_SCS_30_KHZ,
    NR_SCS_60_KHZ,
    NR_SCS_120_KHZ,
    NR_SCS_240_KHZ
};

enum cell_neighbor_freq_mode
{
    CELL_FREQ_MODE_UNSPECIFIED,
    CELL_FREQ_MODE_INTRA,
    CELL_FREQ_MODE_INTER,
    CELL_FREQ_MODE_WCDMA,
    CELL_FREQ_MODE_WCDMA_LTE,
    CELL_FREQ_MODE_5G
};

enum cell_carrier_component
{
    CELL_CC_UNAVAILABLE,
    CELL_PCC,
    CELL_SCC
};

enum cell_pcell_state
{
    CELL_NO_SERVING,
    CELL_REGISTERED
};

enum cell_scell_state
{
    CELL_DECONFIGURED,
    CELL_CONFIGURED_DEACTIVATED,
    CELL_CONFIGURERD_ACTIVATED
};

/**
 * Network Registration Status
 */
enum cell_net_reg_status
{
    CELL_NET_REG_STAT_UNSPECIFIED,
    CELL_NET_REG_STAT_NOTREG,
    CELL_NET_REG_STAT_REG,
    CELL_NET_REG_STAT_SEARCH,
    CELL_NET_REG_STAT_DENIED,
    CELL_NET_REG_STAT_UNKNOWN,
    CELL_NET_REG_STAT_ROAMING
};

enum cell_sim_type
{
    CELL_SIM_TYPE_UNSPECIFIED,
    CELL_SIM_TYPE_ESIM,
    CELL_SIM_TYPE_PSIM
};

enum cell_sim_status
{
    CELL_SIM_STATUS_UNSPECIFIED,
    CELL_SIM_STATUS_INSERTED,
    CELL_SIM_STATUS_REMOVED,
    CELL_SIM_STATUS_BAD
};

enum cell_endc
{
    CELL_ENDC_UNDEFINED,
    CELL_ENDC_NOT_SUPPORTED,
    CELL_ENDC_SUPPORTED
};

/**
 * Radio access technology
 */
enum rat
{
    RAT_LTE,
    RAT_NR5G
};

/**
 * NR5G Layers
 */
enum nr5g_layers
{
    NR5G_LAYERS_UNDEFINED,
    NR5G_LAYERS_0,
    NR5G_LAYERS_1,
    NR5G_LAYERS_2,
    NR5G_LAYERS_3,
    NR5G_LAYERS_4
};

/**
 * NR5G mcs
 */
enum nr5g_mcs
{
    NR5G_MCS_UNDEFINED,
    NR5G_MCS_0,
    NR5G_MCS_1,
    NR5G_MCS_2,
    NR5G_MCS_3,
    NR5G_MCS_4,
    NR5G_MCS_5,
    NR5G_MCS_6,
    NR5G_MCS_7,
    NR5G_MCS_8,
    NR5G_MCS_9,
    NR5G_MCS_10,
    NR5G_MCS_11,
    NR5G_MCS_12,
    NR5G_MCS_13,
    NR5G_MCS_14,
    NR5G_MCS_15,
    NR5G_MCS_16,
    NR5G_MCS_17,
    NR5G_MCS_18,
    NR5G_MCS_19,
    NR5G_MCS_20,
    NR5G_MCS_21,
    NR5G_MCS_22,
    NR5G_MCS_23,
    NR5G_MCS_24,
    NR5G_MCS_25,
    NR5G_MCS_26,
    NR5G_MCS_27,
    NR5G_MCS_28,
    NR5G_MCS_29,
    NR5G_MCS_30,
    NR5G_MCS_31
};

/**
 * NR5G modulation
 */
enum nr5g_modulation
{
    NR5G_MODULATION_UNDEFINED,
    NR5G_MODULATION_QPSK,
    NR5G_MODULATION_16QAM,
    NR5G_MODULATION_64QAM,
    NR5G_MODULATION_256QAM,
    NR5G_MODULATION_1024QAM
};

/**
 * Cell Set
 */
enum cell_neighbor_cell_set
{
    CELL_NEIGHBOR_CELL_SET_UNSPECIFIED,
    CELL_NEIGHBOR_CELL_SET_ACTIVE_SET,
    CELL_NEIGHBOR_CELL_SET_SYNC_NEIGHBOR,
    CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR
};

typedef struct cell_data_usage_
{
    uint64_t rx_bytes;
    uint64_t tx_bytes;
} cell_data_usage_t;

typedef struct cellular_nr5g_sa_net_serving_cell_info_
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
} cellular_nr5g_sa_net_serving_cell_info_t;

/* 5GNR-ENDC mode */
/*
 *
 * +QENG: "servingcell",<state>
+QENG: "LTE",<is_tdd>,<MCC>,<MNC>,<cellID>,<PCI
D>,<earfcn>,<freq_band_ind>,<UL_bandwidth>,<DL_ban
dwidth>,<TAC>,<RSRP>,<RSRQ>,<RSSI>,<SINR>,<CQI>,
<tx_power>,<srxlev>
+QENG: "NR5G-NSA",<MCC>,<MNC>,<PCID>,<RSRP>,<
SINR>,<RSRQ>,<ARFCN>,<band>,<NR_DL_bandwidth>,
<scs>
 *
 *
 * */
typedef struct cellular_nr5g_nsa_net_serving_cell_info_
{
    /* LTE */
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
} cellular_nr5g_nsa_net_serving_cell_info_t;

typedef struct cell_serving_cell_info_
{
    enum serving_cell_state state;
    enum cell_cellular_mode mode;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t mcc;
    uint32_t mnc;
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
} cell_serving_cell_info_t;

typedef struct cell_serving_cell_wcdma_info_
{
    enum serving_cell_state state;
    enum cell_mode mode;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t lac;
    uint32_t cellid;
    uint32_t uarfcn;
    uint32_t psc;
    uint32_t rac;
    int32_t rscp;
    int32_t ecio;
} cell_serving_cell_wcdma_info_t;

typedef struct cell_neighbor_cell_info_
{
    enum cell_neighbor_freq_mode freq_mode;
    enum cell_mode mode;
    uint32_t earfcn;
    uint32_t pcid;
    int32_t rsrq;
    int32_t rsrp;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
    uint32_t cell_resel_priority;
} cell_neighbor_cell_info_t;

typedef struct cell_neighbor_cell_inter_info_
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
    uint32_t thresh_x_low;
    uint32_t thresh_x_high;
} cell_neighbor_cell_inter_info_t;

typedef struct cell_pca_info_
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum cell_bandwidth bandwidth;
    uint32_t band;
    enum cell_pcell_state pcell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
} cell_pca_info_t;

typedef struct cell_sca_info_
{
    enum cell_carrier_component lcc;
    uint32_t freq;
    enum cell_bandwidth bandwidth;
    uint32_t band;
    enum cell_scell_state scell_state;
    uint32_t pcid;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    int32_t sinr;
} cell_sca_info_t;

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
    enum cell_cellular_mode mode;
};

struct cell_full_scan_neighbor_cell_info
{
    enum rat rat;                  /* Radio access technology. Valid values ["LTE", "NR5G"] */
    uint32_t mcc;                  /* Mobile country code (the first part of PLMN code) */
    uint32_t mnc;                  /* Mobile network code (the second part of PLMN code) */
    uint32_t freq;                 /* The Absolute Radio Frequency Channel Number. Valid ranges:
                                      - LTE EARFCN: [0, 262 143]
                                      - 5G ARFCN:   [0, 3 279 165] */
    uint32_t pcid;                 /* Physical Cell ID */
    int32_t rsrp;                  /* Reference Signal Received Power. Valid range [-140, -44] dBm */
    int32_t rsrq;                  /* Reference Signal Received Quality. Valid range [-20, -3] dBm */
    uint32_t srxlev;               /* Received signal (RX) Level value in dB for cell selection */
    enum nr_scs scs;               /* Sub-carrier spacing value */
    int32_t squal;                 /* Received signal Quality value in dB for cell selection */
    uint32_t cellid;               /* Cell ID, optional */
    uint32_t tac;                  /* Tracking area code, optional */
    enum cell_bandwidth bandwidth; /* Bandwidth, optional */
    uint32_t band;                 /* Frequency band, optional */
};

struct cell_net_pca_info
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

struct cell_net_lte_sca_info
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

struct cell_pdp_ctx_dynamic_params_info
{
    uint32_t cid;
    uint32_t bearer_id;
    char apn[C_AT_CMD_LONG_RESP];
    char local_addr[C_AT_CMD_LONG_RESP];
    char subnetmask[C_AT_CMD_LONGEST_RESP];
    char gw_addr[C_AT_CMD_LONG_RESP];
    char dns_prim_addr[C_AT_CMD_LONGEST_RESP];
    char dns_sec_addr[C_AT_CMD_LONG_RESP];
    char p_cscf_prim_addr[C_AT_CMD_LONG_RESP];
    char p_cscf_sec_addr[C_AT_CMD_LONG_RESP];
    uint32_t im_cn_signalling_flag;
    uint32_t lipaindication;
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
    enum cell_bandwidth ul_bandwidth;
    enum cell_bandwidth dl_bandwidth;
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

/* Modem PDP context parameters */
typedef enum cell_pdp_context_params_
{
    PDP_CTXT_CID = 0,
    PDP_CTXT_PDP_TYPE,
    PDP_CTXT_APN,
    PDP_CTXT_PDP_ADDR,
    PDP_CTXT_DATA_CMP,
    PDP_CTXT_HEAD_CMP,
    PDP_CTXT_IPV4ADDRALLOC,
    PDP_CTXT_REQUEST_TYPE,
} cell_pdp_context_params;

#define PDP_TYPE_IPV4   "IP"
#define PDP_TYPE_PPP    "PPP"
#define PDP_TYPE_IPV6   "IPV6"
#define PDP_TYPE_IPV4V6 "IPV4V6"

#define PDP_TYPE_SZ 16
#define PDP_APN_SZ  64
#define PDP_ADDR_SZ 64

typedef struct cell_pdp_context_
{
    char pdp_type[PDP_TYPE_SZ];
    char apn[PDP_APN_SZ];
    char pdp_addr[PDP_ADDR_SZ];
    int cid;
    int data_comp;
    int head_comp;
    int ipv4addr_alloc;
    int request_type;
    bool valid;
} cell_pdp_context_t;

typedef struct cell_pdp_ctx_dynamic_param_
{
    uint32_t cid;
    uint32_t bearer_id;
    char apn[C_AT_CMD_LONG_RESP];
    char local_addr[C_AT_CMD_LONG_RESP];
    char subnetmask[C_AT_CMD_LONG_RESP];
    char gw_addr[C_AT_CMD_LONG_RESP];
    char dns_prim_addr[C_AT_CMD_LONG_RESP];
    char dns_sec_addr[C_AT_CMD_LONG_RESP];
    char p_cscf_prim_addr[C_AT_CMD_LONG_RESP];
    char p_cscf_sec_addr[C_AT_CMD_LONG_RESP];
    uint32_t im_cn_signalling_flag;
    uint32_t lipaindication;
} cell_pdp_ctx_dynamic_param_info_t;

struct cell_serving_cell_info
{
    enum serving_cell_state state;
    enum fdd_tdd_mode fdd_tdd_mode;
    uint32_t cellid;
    uint32_t pcid;
    uint32_t uarfcn;
    uint32_t earfcn;
    uint32_t band;
    enum cell_bandwidth ul_bandwidth;
    enum cell_bandwidth dl_bandwidth;
    uint32_t tac;
    int32_t rsrp;
    int32_t rsrq;
    int32_t rssi;
    uint32_t sinr;
    uint32_t srxlev;
    enum cell_endc endc;
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

struct osn_cell_ue_band_capability
{
    int *lte_bands;        /* LTE bands list */
    size_t lte_bands_len;  /* TE bands number */
    int *nrg5_bands;       /* NRG5 bands list */
    size_t nrg5_bands_len; /* NRG5 bands number */
};

typedef struct osn_cell_modem_info_
{
    bool modem_present;
    char chip_vendor[C_AT_CMD_RESP];
    char model[C_AT_CMD_RESP];
    char full_model[C_AT_CMD_LONG_RESP];
    bool sim_inserted;
    char imei[C_AT_CMD_RESP];
    char imsi[C_AT_CMD_RESP];
    char iccid[C_AT_CMD_LONG_RESP];
    enum cell_net_reg_status reg_status;
    int32_t rssi;
    int32_t ber;
    int32_t rsrp;
    int32_t sinr;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    enum cell_sim_slot slot;
    char operator[C_AT_CMD_LONG_RESP];
    enum cell_access_tech act;
    cellular_nr5g_sa_net_serving_cell_info_t nr5g_serv_cell_sa;
    cellular_nr5g_nsa_net_serving_cell_info_t nr5g_serv_cell_nsa;
    cell_serving_cell_info_t srv_cell;
    cell_serving_cell_wcdma_info_t srv_cell_wcdma;
    cell_neighbor_cell_info_t neigh_cell[MAX_NEIGH_CELL_COUNT];
    enum cell_sim_status sim_status;
    enum cell_sim_type sim_type;
    uint32_t active_simcard_slot;
    char lte_band_val[C_AT_CMD_LONG_RESP];
    cell_pca_info_t pca_info;
    cell_sca_info_t sca_info;
    cell_pdp_ctx_dynamic_param_info_t pdp_ctx_info;
    char modem_fw_ver[C_AT_CMD_LONGEST_RESP];
    uint64_t last_healthcheck_success;
    uint64_t healthcheck_failures;

    /* Cell Manager info */
    struct cell_net_info cell_net_info;
    struct cell_serving_cell_info cell_srv_cell;
    size_t n_neigh_cells;
    struct cell_net_neighbor_cell_info cell_neigh_cell_info[MAX_CELL_COUNT];
    size_t n_full_scan_neigh_cells;
    struct cell_full_scan_neighbor_cell_info cell_full_scan_neigh_cell_info[MAX_FULL_SCAN_CELL_COUNT];
    struct cell_net_pca_info cell_pca_info;
    size_t n_lte_sca_cells;
    size_t cur_lte_sca_cell_idx;
    struct cell_net_lte_sca_info cell_lte_sca_info[MAX_CELL_COUNT];
    size_t n_pdp_cells;
    size_t cur_pdp_idx;
    struct cell_pdp_ctx_dynamic_params_info cell_pdp_ctx_info[MAX_CELL_COUNT];
    struct cell_nr5g_cell_info nr5g_sa_srv_cell;
    struct cell_nr5g_cell_info nr5g_nsa_srv_cell;
    size_t n_nrg_sca_cells;
    size_t cur_nrg_sca_cell_idx;
    struct cell_nr5g_cell_info cell_nrg_sca_info[MAX_CELL_COUNT];
    struct osn_cell_ue_band_capability ue_band_info;
} osn_cell_modem_info_t;

osn_cell_modem_info_t *osn_cell_get_modem_info(void);
int osn_cell_read_modem(void);
void osn_cell_set_sim_slot(uint32_t slot);
void osn_cell_set_bands(char *bands);
int osn_cell_parse_pdp_context(char *buf, cell_pdp_context_t *pdp_ctxt);
bool osn_cell_set_pdp_context_params(cell_pdp_context_params param_type, char *val);
void osn_cell_reset_modem(void);
void osn_cell_start_vendor_daemon(int source);
void osn_cell_stop_vendor_daemon(void);
void osn_cell_dump_modem_info();
bool osn_cell_modem_init(void);
void osn_cell_start_modem(int source);
void osn_cell_stop_modem(void);
void osn_cell_modem_reset(void);
int osn_cell_set_apn(char *apn);
int osn_cell_set_apn_username(char *apn_username);
int osn_cell_set_apn_password(char *apn_password);
int osn_cell_set_apn_prototype(enum cell_apn_proto_type apn_proto_type);
int osn_cell_set_apn_auth_proto(enum cell_apn_auth_proto apn_auth_proto);
int osn_cell_set_bands_enabled(char *bands);
int osn_cell_set_mode(enum cell_mode mode);

#endif /* OSN_CELL_MODEM_H_INCLUDED */
