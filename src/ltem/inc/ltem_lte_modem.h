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

#ifndef LTEM_LTE_MODEM_H_INCLUDED
#define LTEM_LTE_MODEM_H_INCLUDED

#include "lte_info.h"

#define SERVING_CELL_LTE_FIELD_CNT 16
#define SERVING_CELL_WCDMA_FIELD_CNT 15

typedef struct lte_chip_info_ //ati
{
    char cmd[16];
    char vendor[16];
    char model[16];
    char full_model[32];
} lte_chip_info_t;

typedef struct lte_imei_ //at_gsn
{
    char cmd[16];
    char imei[16];
} lte_imei_t;

typedef struct lte_imsi_ //at_cimi
{
    char cmd[16];
    char imsi[16];
} lte_imsi_t;

typedef struct lte_iccid_ //at_qccid
{
    char cmd[16];
    char iccid[32];
} lte_iccid_t;

typedef struct lte_reg_status_ //at_creg
{
    char cmd[16];
    char net_reg_code[16];
    char net_reg_status[16];
} lte_reg_status_t;

typedef struct lte_sig_qual_ //at_csq
{
    char cmd[16];
    char rssi_index[16];
    char ber[16];
} lte_sig_qual_t;

typedef struct lte_byte_counts_ //at_qgdcnt
{
    char cmd[16];
    char tx_bytes[16];
    char rx_bytes[16];
} lte_byte_counts_t;

typedef struct lte_sim_slot_
{
    char cmd[16];
    char slot[16];
} lte_sim_slot_t;

typedef struct lte_operator_ //at_cops
{
    char cmd[16];
    char mode[16];
    char format[16];
    char operator[32];
    char act[16];
} lte_operator_t;

/*
char *at_srv_cell="at+qeng=\"servingcell\"\r\r\n+QENG: 
\"servingcell\",\"NOCONN\",\"LTE\",\"FDD\",310,410,A1FBF0A,310,800,2,5,5,8B1E,-115,-14,-80,10,8\r\n\r\nOK\r\n";
+QENG: "servingcell",<state>,"LTE",<is_tdd>,<mcc>,<m
nc>,<cellid>,<pcid>,<earfcn>,<freq_band_ind>,<ul_band
width>,<dl_bandwidth>,<tac>,<rsrp>,<rsrq>,<rssi>,<sin
r>,<srxlev>
*/
typedef struct lte_srv_cell_ //at_srv_cell
{
    char cmd[64];
    char cell_type[16];
    char state[16];
    char mode[16];
    char fdd_tdd_mode[16];
    char mcc[16];
    char mnc[16];
    char cellid[16];
    char pcid[16];
    char earfcn[16];
    char freq_band[16];
    char ul_bandwidth[16];
    char dl_bandwidth[16];
    char tac[16];
    char rsrp[16];
    char rsrq[16];
    char rssi[16];
    char sinr[16];
    char srxlev[16];
} lte_srv_cell_t;

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
typedef struct lte_neigh_cell_intra //at_neigh_cell
{
    char cmd[64];
    char cell_type[32];
    char mode[16];
    char freq_mode[16];
    char earfcn[16];
    char pcid[16];
    char rsrq[16];
    char rsrp[16];
    char rssi[16];
    char sinr[16];
    char srxlev_base_station[16];
    char cell_resel_priority[16];
    char s_non_intra_search[16];
    char thresh_serving_low[16];
    char s_intra_search[16];
} lte_neigh_cell_intra_t;

// [+QENG: "neighbourcell inter","LTE",<earfcn>,<pcid>,<rsrq>,<rsrp>,<rssi>,<sinr>,<srxlev>,<cell_resel_priority>,<threshX_low>,<threshX_high>
typedef struct lte_neigh_cell_inter //at_neigh_cell
{
    char cmd[64];
    char cell_type[32];
    char mode[16];
    char freq_mode[16];
    char earfcn[16];
    char pcid[16];
    char rsrq[16];
    char rsrp[16];
    char rssi[16];
    char sinr[16];
    char srxlev[16];
    char cell_resel_priority[16];
    char thresh_x_low[16];
    char thresh_x_high[16];
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

typedef struct lte_modem_info_
{
    char chip_vendor[16];
    char model[16];
    char full_model[32];
    char imei[16];
    char imsi[16];
    char iccid[32];
    enum lte_net_reg_status reg_status;
    int32_t rssi;
    int32_t ber;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    enum lte_sim_slot slot;
    char operator[32];
    enum lte_access_tech act;
    lte_serving_cell_info_t srv_cell;
    lte_neighbor_cell_intra_info_t neigh_cell_intra;
    lte_neighbor_cell_inter_info_t neigh_cell_inter;
} lte_modem_info_t;

int ltem_parse_imei(char *buf, lte_imei_t *imei);
int ltem_parse_imsi(char *buf, lte_imsi_t *imsi);
int ltem_parse_iccid(char *buf, lte_iccid_t *iccid);
int ltem_parse_reg_status(char *buf, lte_reg_status_t *reg_status);
int ltem_parse_sig_qual(char *buf, lte_sig_qual_t *sig_qual);
int ltem_parse_byte_counts(char *buf, lte_byte_counts_t *byte_counts);
int ltem_parse_sim_slot(char *buf, lte_sim_slot_t *sim_slot);
int ltem_parse_operator(char *buf, lte_operator_t *operator);
int ltem_parse_serving_cell(char *buf, lte_srv_cell_t *srv_cell);
int ltem_parse_neigh_cell_intra(char *buf, lte_neigh_cell_intra_t *neigh_cell_intra);
int ltem_parse_neigh_cell_inter(char *buf, lte_neigh_cell_inter_t *neigh_cell_inter);
char *ltem_run_modem_cmd(const char *cmd);

#endif /* LTEM_LTE_MODEM_H_INCLUDED */
