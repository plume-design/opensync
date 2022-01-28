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

#ifndef OSN_LTE_MODEM_H_INCLUDED
#define OSN_LTE_MODEM_H_INCLUDED

#include "lte_info.h"

#define SERVING_CELL_LTE_FIELD_CNT 16
#define SERVING_CELL_WCDMA_FIELD_CNT 15

/*
 * These should eventually mode to const.h
 */
#define C_AT_CMD_RESP 16
#define C_AT_CMD_LONG_RESP 32
#define C_AT_CMD_LONGEST_RESP 64

typedef struct lte_chip_info_ // ati
{
    char cmd[C_AT_CMD_RESP];
    char vendor[C_AT_CMD_RESP];
    char model[C_AT_CMD_RESP];
    char full_model[C_AT_CMD_LONG_RESP];
} lte_chip_info_t;

typedef struct lte_sim_insertion_status_ // at+qsimstat
{
    char cmd[C_AT_CMD_RESP];
    bool enabled;
    bool inserted;
} lte_sim_insertion_status_t;

typedef struct lte_imei_ // at+gsn
{
    char cmd[C_AT_CMD_RESP];
    char imei[C_AT_CMD_RESP];
} lte_imei_t;

typedef struct lte_imsi_ // at+cimi
{
    char cmd[C_AT_CMD_RESP];
    char imsi[C_AT_CMD_RESP];
} lte_imsi_t;

typedef struct lte_iccid_ // at+qccid
{
    char cmd[C_AT_CMD_RESP];
    char iccid[C_AT_CMD_LONG_RESP];
} lte_iccid_t;

typedef struct lte_reg_status_ // at+creg
{
    char cmd[C_AT_CMD_RESP];
    char net_reg_code[C_AT_CMD_RESP];
    char net_reg_status[C_AT_CMD_RESP];
} lte_reg_status_t;

typedef struct lte_sig_qual_ // at+csq
{
    char cmd[C_AT_CMD_RESP];
    char rssi_index[C_AT_CMD_RESP];
    char ber[C_AT_CMD_RESP];
} lte_sig_qual_t;

typedef struct lte_byte_counts_ // at+qgdcnt
{
    char cmd[C_AT_CMD_RESP];
    char tx_bytes[C_AT_CMD_RESP];
    char rx_bytes[C_AT_CMD_RESP];
} lte_byte_counts_t;

typedef struct lte_sim_slot_
{
    char cmd[C_AT_CMD_RESP];
    char slot[C_AT_CMD_RESP];
} lte_sim_slot_t;

typedef struct lte_operator_ // at+cops
{
    char cmd[C_AT_CMD_RESP];
    char mode[C_AT_CMD_RESP];
    char format[C_AT_CMD_RESP];
    char operator[C_AT_CMD_LONG_RESP];
    char act[C_AT_CMD_RESP];
} lte_operator_t;

/*
char *at_srv_cell="at+qeng=\"servingcell\"\r\r\n+QENG: 
\"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",310,410,A1FBF0A,310,800,2,5,5,8B1E,-115,-14,-80,10,8\r\n\r\nOK\r\n";
+QENG: "servingcell",<state>,"LTE",<is_tdd>,<mcc>,<mnc>,<cellid>,<pcid>,<earfcn>,<freq_band_ind>,<ul_bandwidth>,<dl_bandwidth>,<tac>,<rsrp>,<rsrq>,<rssi>,<sinr>,<srxlev>
*/
typedef struct lte_srv_cell_ // at+srv_cell
{
    char cmd[C_AT_CMD_LONGEST_RESP];
    char cell_type[C_AT_CMD_RESP];
    char state[C_AT_CMD_RESP];
    char mode[C_AT_CMD_RESP];
    char fdd_tdd_mode[C_AT_CMD_RESP];
    char mcc[C_AT_CMD_RESP];
    char mnc[C_AT_CMD_RESP];
    char cellid[C_AT_CMD_RESP];
    char pcid[C_AT_CMD_RESP];
    char earfcn[C_AT_CMD_RESP];
    char freq_band[C_AT_CMD_RESP];
    char ul_bandwidth[C_AT_CMD_RESP];
    char dl_bandwidth[C_AT_CMD_RESP];
    char tac[C_AT_CMD_RESP];
    char rsrp[C_AT_CMD_RESP];
    char rsrq[C_AT_CMD_RESP];
    char rssi[C_AT_CMD_RESP];
    char sinr[C_AT_CMD_RESP];
    char srxlev[C_AT_CMD_RESP];
} lte_srv_cell_t;

/*
 * +QENG: "servingcell",<state> ,"WCDMA",<mcc>,<mnc>,<lac>,<cellid>,<uarfcn>,<psc>,<rac>,<rscp>,<ecio>,<phych>,<sf>,<slot>,<speech_code>,<comMod>
 * +QENG: "servingcell","NOCONN","WCDMA",310,  410,  DEA6, 2883C,   4385,     84,   254, -98,   -7,    -,      -,   -,      -,           -
 */
typedef struct lte_srv_cell_wcdma //at_srv_cell
{
    char cmd[C_AT_CMD_LONGEST_RESP];
    char cell_type[C_AT_CMD_RESP];
    char state[C_AT_CMD_RESP];
    char mode[C_AT_CMD_RESP];
    char mcc[C_AT_CMD_RESP];
    char mnc[C_AT_CMD_RESP];
    char lac[C_AT_CMD_RESP];
    char cellid[C_AT_CMD_RESP];
    char pcid[C_AT_CMD_RESP];
    char uarfcn[C_AT_CMD_RESP];
    char psc[C_AT_CMD_RESP];
    char rac[C_AT_CMD_RESP];
    char rscp[C_AT_CMD_RESP];
    char ecio[C_AT_CMD_RESP];
    char phych[C_AT_CMD_RESP];
    char sf[C_AT_CMD_RESP];
    char slot[C_AT_CMD_RESP];
    char speed_code[C_AT_CMD_RESP];
    char com_mod[C_AT_CMD_RESP];
} lte_srv_cell_wcdma_t;

// [+QENG:"neighbourcell intra","LTE",<earfcn>,<pcid>,<rsrq>,<rsrp>,<rssi>,<sinr>,<srxlev>,<cell_resel_priority>,<s_non_intra_search>,<thresh_serving_low>,<s_intra_search>
/*
char *at_neigh_cell=
1. "at+qeng=\"neighbourcell\"\r\r\n+QENG:(sp)
2. \"neighbourcell intra\",\"LTE\",800,310,-14,-115,-80,0,8,4,10,2,62\r\n
3. +QENG: \"neighbourcell inter\",\"LTE\",5110,263,-11,-102,-82,0,8,2,6,6\r\n
4. +QENG: \"neighbourcell inter\",\"LTE\",66986,-,-,-,-,-,0,6,6,1,-,-,-,-\r\n
5. +QENG: \"neighbourcell\",\"WCDMA\",512,6,14,62,-,-,-,-\r\n
6. +QENG: \"neighbourcell\",\"WCDMA\",4385,0,14,62,84,-1030,-110,15\r\n\r\nOK\r\n";
*/
typedef struct lte_neigh_cell_intra // at+neigh_cell
{
    char cmd[C_AT_CMD_LONGEST_RESP];
    char cell_type[C_AT_CMD_LONG_RESP];
    char mode[C_AT_CMD_RESP];
    char freq_mode[C_AT_CMD_RESP];
    char earfcn[C_AT_CMD_RESP];
    char pcid[C_AT_CMD_RESP];
    char rsrq[C_AT_CMD_RESP];
    char rsrp[C_AT_CMD_RESP];
    char rssi[C_AT_CMD_RESP];
    char sinr[C_AT_CMD_RESP];
    char srxlev_base_station[C_AT_CMD_RESP];
    char cell_resel_priority[C_AT_CMD_RESP];
    char s_non_intra_search[C_AT_CMD_RESP];
    char thresh_serving_low[C_AT_CMD_RESP];
    char s_intra_search[C_AT_CMD_RESP];
} lte_neigh_cell_intra_t;

// [+QENG: "neighbourcell inter","LTE",<earfcn>,<pcid>,<rsrq>,<rsrp>,<rssi>,<sinr>,<srxlev>,<cell_resel_priority>,<threshX_low>,<threshX_high>
typedef struct lte_neigh_cell_inter // at+neigh_cell
{
    char cmd[C_AT_CMD_LONGEST_RESP];
    char cell_type[C_AT_CMD_LONG_RESP];
    char mode[C_AT_CMD_RESP];
    char freq_mode[C_AT_CMD_RESP];
    char earfcn[C_AT_CMD_RESP];
    char pcid[C_AT_CMD_RESP];
    char rsrq[C_AT_CMD_RESP];
    char rsrp[C_AT_CMD_RESP];
    char rssi[C_AT_CMD_RESP];
    char sinr[C_AT_CMD_RESP];
    char srxlev[C_AT_CMD_RESP];
    char cell_resel_priority[C_AT_CMD_RESP];
    char thresh_x_low[C_AT_CMD_RESP];
    char thresh_x_high[C_AT_CMD_RESP];
} lte_neigh_cell_inter_t;

// [+QENG:"neighbourcell","WCDMA",<uarfcn>,<cell_resel_priority>,<thresh_Xhigh>,<thresh_Xlow>,<psc>,<rscp><ecno>,<srxlev>
// [+QENG: "neighbourcell","LTE",<earfcn>,<cellid>,<rsrp>,<rsrq>,<s_rxlev>
//TBD

typedef struct lte_net_info_
{
    enum lte_net_reg_status net_status;
    int32_t rssi;
    int32_t ber;
} lte_net_info_t;

typedef struct lte_data_usage_
{
    uint64_t rx_bytes;
    uint64_t tx_bytes;
} lte_data_usage_t;

enum lte_sim_slot
{
    LTE_SLOT_0,
    LTE_SLOT_1,
};

enum lte_access_tech
{
    LTE_ACT_UNSPECIFIED = 0,
    LTE_ACT_UTRAN = 2,
    LTE_ACT_UTRAN_HSDPA = 4,
    LTE_ACT_UTRAN_HSUPA = 5,
    LTE_ACT_UTRAN_HSDPA_HSUPA = 6,
    LTE_ACT_EUTRAN = 7,
};

typedef struct lte_serving_cell_info_
{
    enum lte_serving_cell_state state;
    enum lte_cell_mode mode;
    enum lte_fdd_tdd_mode fdd_tdd_mode;
    uint32_t mcc;
    uint32_t mnc;
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
} lte_serving_cell_info_t;

typedef struct lte_serving_cell_wcdma_info_
{
    enum lte_serving_cell_state state;
    enum lte_cell_mode mode;
    uint32_t mcc;
    uint32_t mnc;
    uint32_t lac;
    uint32_t cellid;
    uint32_t uarfcn;
    uint32_t psc;
    uint32_t rac;
    int32_t rscp;
    int32_t ecio;
} lte_serving_cell_wcdma_info_t;

typedef struct lte_neighbor_cell_intra_info_
{
    enum lte_cell_mode mode;
    enum lte_neighbor_freq_mode freq_mode;
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
} lte_neighbor_cell_intra_info_t;

typedef struct lte_neighbor_cell_inter_info_
{
    enum lte_cell_mode mode;
    enum lte_neighbor_freq_mode freq_mode;
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
} lte_neighbor_cell_inter_info_t;

typedef struct lte_band_info_
{
    char cmd[C_AT_CMD_RESP];
    char wcdma_band_val[C_AT_CMD_LONG_RESP];
    char lte_band_val[C_AT_CMD_LONG_RESP];
} lte_band_info_t;


/* Modem PDP context parameters */
typedef enum lte_pdp_context_params_
{
    PDP_CTXT_CID = 0,
    PDP_CTXT_PDP_TYPE,
    PDP_CTXT_APN,
    PDP_CTXT_PDP_ADDR,
    PDP_CTXT_DATA_CMP,
    PDP_CTXT_HEAD_CMP,
    PDP_CTXT_IPV4ADDRALLOC,
    PDP_CTXT_REQUEST_TYPE,
} lte_pdp_context_params;

#define PDP_TYPE_IPV4   "IP"
#define PDP_TYPE_PPP    "PPP"
#define PDP_TYPE_IPV6   "IPV6"
#define PDP_TYPE_IPV4V6 "IPV4V6"

#define PDP_TYPE_SZ 16
#define PDP_APN_SZ  64
#define PDP_ADDR_SZ 64

typedef struct  lte_pdp_context_
{
    char pdp_type[PDP_TYPE_SZ];
    char apn[PDP_APN_SZ];
    char pdp_addr[PDP_ADDR_SZ];
    int  cid;
    int  data_comp;
    int  head_comp;
    int  ipv4addr_alloc;
    int  request_type;
    bool valid;
} lte_pdp_context_t;

typedef struct osn_lte_modem_info_
{
    bool modem_present;
    char chip_vendor[C_AT_CMD_RESP];
    char model[C_AT_CMD_RESP];
    char full_model[C_AT_CMD_LONG_RESP];
    bool sim_inserted;
    char imei[C_AT_CMD_RESP];
    char imsi[C_AT_CMD_RESP];
    char iccid[C_AT_CMD_LONG_RESP];
    enum lte_net_reg_status reg_status;
    int32_t rssi;
    int32_t ber;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    enum lte_sim_slot slot;
    char operator[C_AT_CMD_LONG_RESP];
    enum lte_access_tech act;
    lte_serving_cell_info_t srv_cell;
    lte_serving_cell_wcdma_info_t srv_cell_wcdma;
    lte_neighbor_cell_intra_info_t neigh_cell_intra;
    lte_neighbor_cell_inter_info_t neigh_cell_inter;
    enum lte_sim_status sim_status;
    enum lte_sim_type sim_type;
    uint32_t active_simcard_slot;
    char lte_band_val[C_AT_CMD_LONG_RESP];
} osn_lte_modem_info_t;

int osn_lte_parse_chip_info(char *buf, lte_chip_info_t *chip_info);
int osn_lte_save_chip_info(lte_chip_info_t *chip_info, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_at_output(char *buf);
int osn_lte_parse_sim_status(char *buf, lte_sim_insertion_status_t *sim_status);
int osn_lte_save_sim_status(lte_sim_insertion_status_t *sim_status, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_imei(char *buf, lte_imei_t *imei);
int osn_lte_save_imei(lte_imei_t *imei, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_imsi(char *buf, lte_imsi_t *imsi);
int osn_lte_save_imsi(lte_imsi_t *imsi, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_iccid(char *buf, lte_iccid_t *iccid);
int osn_lte_save_iccid(lte_iccid_t *iccid, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_reg_status(char *buf, lte_reg_status_t *reg_status);
int osn_lte_save_reg_status(lte_reg_status_t *reg_status, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_sig_qual(char *buf, lte_sig_qual_t *sig_qual);
int osn_lte_save_sig_qual(lte_sig_qual_t *sig_qual, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_byte_counts(char *buf, lte_byte_counts_t *byte_counts);
int osn_lte_save_byte_counts(lte_byte_counts_t *byte_counts, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_sim_slot(char *buf, lte_sim_slot_t *sim_slot);
int osn_lte_save_sim_slot(lte_sim_slot_t *sim_slot, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_operator(char *buf, lte_operator_t *operator);
int osn_lte_save_operator(lte_operator_t *operator, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_serving_cell(char *buf, lte_srv_cell_t *srv_cell, lte_srv_cell_wcdma_t *srv_cell_wcdma);
int osn_lte_save_serving_cell(lte_srv_cell_t *srv_cell, lte_srv_cell_wcdma_t *srv_cell_wcdma, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_neigh_cell_intra(char *buf, lte_neigh_cell_intra_t *neigh_cell_intra);
int osn_lte_save_neigh_cell_intra(lte_neigh_cell_intra_t *neigh_cell_intra, osn_lte_modem_info_t *modem_info);
int osn_lte_parse_neigh_cell_inter(char *buf, lte_neigh_cell_inter_t *neigh_cell_inter);
int osn_lte_save_neigh_cell_inter(lte_neigh_cell_inter_t *neigh_cell_inter, osn_lte_modem_info_t *modem_info);
int osn_parse_band_info(char *buf, lte_band_info_t *band_info);
int osn_save_band_info(lte_band_info_t *band_info, osn_lte_modem_info_t *modem_info);
int osn_lte_modem_open(char *modem_path);
ssize_t ons_lte_modem_write(int fd, const char *cmd);
ssize_t osn_lte_modem_read(int fd, char *at_buf, ssize_t at_len);
void osn_lte_modem_close(int fd);
int osn_ltem_get_modem_info(void);
char *osn_lte_run_microcom_cmd(char *cmd);
char *osn_lte_run_modem_cmd(const char *cmd);
osn_lte_modem_info_t *osn_get_modem_info(void);
int osn_lte_read_modem(void);
void osn_lte_set_sim_slot(uint32_t slot);
void osn_lte_set_qmi_mode(void);
void osn_lte_enable_sim_detect(void);
void osn_lte_set_bands(char *bands);
void osn_lte_read_pdp_context(void);
int osn_lte_parse_pdp_context(char *buf, lte_pdp_context_t *pdp_ctxt);
bool osn_lte_set_pdp_context_params(lte_pdp_context_params param_type, char *val);
bool osn_lte_set_ue_data_centric(void);
void osn_lte_reset_modem(void);

#endif /* OSN_LTE_MODEM_H_INCLUDED */
