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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include "memutil.h"
#include "ltem_mgr.h"
#include "ltem_lte_modem.h"

#include "log.h"
#include "neigh_table.h"
#ifndef ARCH_X86
#include "prov.h"
#endif

char lte_at_buf[1024];

static char *at_error = "AT command failed";
static char *modem_path = "/dev/ttyUSB2";
static char *microcom_path = "/usr/bin/microcom";

#define LTE_MODEM_DELAY (100 * 1000)

int
ltem_parse_chip_info(char *buf, lte_chip_info_t *chip_info)
{
    char *delim1 = "\n";
    char *delim2 = "\r";
    char *delim3 = " ";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(chip_info, 0, sizeof(lte_chip_info_t));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(chip_info->cmd, token, sizeof(chip_info->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(chip_info->vendor, token, sizeof(chip_info->vendor));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(chip_info->model, &token[1], sizeof(chip_info->model));
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL)
    {
        LOGI("%s: full model token[NULL]", __func__);
        goto error;
    }
    strncpy(chip_info->full_model, token, sizeof(chip_info->full_model));
    LOGI("%s: full_model[%s]", __func__, chip_info->full_model);
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_chip_info(lte_chip_info_t *chip_info, lte_modem_info_t *modem_info)
{
    if (sizeof(modem_info->chip_vendor) != sizeof(chip_info->vendor)) return -1;
    strncpy(modem_info->chip_vendor, chip_info->vendor, sizeof(modem_info->chip_vendor));

    if (sizeof(modem_info->model) != sizeof(chip_info->model)) return -1;
    strncpy(modem_info->model, chip_info->model, sizeof(modem_info->model));

    if (sizeof(modem_info->full_model) != sizeof(chip_info->full_model)) return -1;
    strncpy(modem_info->full_model, chip_info->full_model, sizeof(modem_info->full_model));

    return 0;
}

int
ltem_parse_imei(char *buf, lte_imei_t *imei)
{
    char *delim1 = "\n";
    char *delim2 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(imei, 0, sizeof(*imei));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(imei->cmd, token, sizeof(imei->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(imei->imei, token, sizeof(imei->imei));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_imei(lte_imei_t *imei, lte_modem_info_t *modem_info)
{
    if (sizeof(modem_info->imei) != sizeof(imei->imei)) return -1;
    strncpy(modem_info->imei, imei->imei, sizeof(modem_info->imei));
    return 0;
}

int
ltem_parse_imsi(char *buf, lte_imsi_t *imsi)
{
    char *delim1 = "\n";
    char *delim2 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(imsi, 0, sizeof(*imsi));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    LOGI("%s: cmd[%s]", __func__, token);
    if (token == NULL) goto error;
    strncpy(imsi->cmd, token, sizeof(imsi->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(imsi->imsi, token, sizeof(imsi->imsi));
    LOGI("%s: imsi[%s]", __func__, token);
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_imsi(lte_imsi_t *imsi, lte_modem_info_t *modem_info)
{
    if (sizeof(modem_info->imsi) != sizeof(imsi->imsi)) return -1;
    strncpy(modem_info->imsi, imsi->imsi, sizeof(modem_info->imsi));
    LOGI("%s: imsi[%s]", __func__, modem_info->imsi);
    return 0;
}

int
ltem_parse_iccid(char *buf, lte_iccid_t *iccid)
{
    char *delim1 = " ";
    char *delim2 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(iccid, 0, sizeof(*iccid));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(iccid->cmd, token, sizeof(iccid->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(iccid->iccid, token, sizeof(iccid->iccid));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_iccid(lte_iccid_t *iccid, lte_modem_info_t *modem_info)
{
    if (sizeof(modem_info->iccid) != sizeof(iccid->iccid)) return -1;
    strncpy(modem_info->iccid, iccid->iccid, sizeof(modem_info->iccid));
    return 0;
}

int
ltem_parse_reg_status(char *buf, lte_reg_status_t *reg_status)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(reg_status, 0, sizeof(*reg_status));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(reg_status->cmd, token, sizeof(reg_status->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(reg_status->net_reg_code, token, sizeof(reg_status->net_reg_code));
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(reg_status->net_reg_status, token, sizeof(reg_status->net_reg_status));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_reg_status(lte_reg_status_t *reg_status, lte_modem_info_t *modem_info)
{
    uint32_t reg_net_status;

    reg_net_status = atoi(reg_status->net_reg_status);

    switch (reg_net_status)
    {
        case 0:
            modem_info->reg_status = LTE_NET_REG_STAT_NOTREG;
            break;
        case 1:
            modem_info->reg_status = LTE_NET_REG_STAT_REG;
            break;
        case 2:
            modem_info->reg_status = LTE_NET_REG_STAT_SEARCH;
            break;
        case 3:
            modem_info->reg_status = LTE_NET_REG_STAT_DENIED;
            break;
        case 4:
            modem_info->reg_status = LTE_NET_REG_STAT_UNKNOWN;
            break;
        case 5:
            modem_info->reg_status = LTE_NET_REG_STAT_ROAMING;
            break;
        default:
            modem_info->reg_status = LTE_NET_REG_STAT_UNSPECIFIED;
            break;
    }

    return 0;
}

int
ltem_parse_sig_qual(char *buf, lte_sig_qual_t *sig_qual)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(sig_qual, 0, sizeof(*sig_qual));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(sig_qual->cmd, token, sizeof(sig_qual->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(sig_qual->rssi_index, token, sizeof(sig_qual->rssi_index));
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(sig_qual->ber, token, sizeof(sig_qual->ber));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_sig_qual(lte_sig_qual_t *sig_qual, lte_modem_info_t *modem_info)
{
    int32_t rssi_index;
    int32_t rssi = 0;

    rssi_index = atoi(sig_qual->rssi_index);
    if (rssi_index == 0) rssi = -113;
    if (rssi_index == 1) rssi = -111;
    if (rssi_index >= 2 && rssi_index <= 30)
    {
        rssi = -109 + ((rssi_index - 2) * 2);
    }
    if (rssi_index >= 31) rssi = -51;
    modem_info->rssi = rssi;
    modem_info->ber = atoi(sig_qual->ber);
    return 0;
}

int
ltem_parse_byte_counts(char *buf, lte_byte_counts_t *byte_counts)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(byte_counts, 0, sizeof(*byte_counts));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(byte_counts->cmd, token, sizeof(byte_counts->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(byte_counts->tx_bytes, token, sizeof(byte_counts->tx_bytes));
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(byte_counts->rx_bytes, token, sizeof(byte_counts->rx_bytes));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_byte_counts(lte_byte_counts_t *byte_counts, lte_modem_info_t *modem_info)
{
    modem_info->tx_bytes = atoi(byte_counts->tx_bytes);
    modem_info->rx_bytes = atoi(byte_counts->rx_bytes);
    return 0;
}

int
ltem_parse_sim_slot(char *buf, lte_sim_slot_t *sim_slot)
{
    char *delim1 = " ";
    char *delim2 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(sim_slot, 0, sizeof(*sim_slot));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(sim_slot->cmd, token, sizeof(sim_slot->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(sim_slot->slot, token, sizeof(sim_slot->slot));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_sim_slot(lte_sim_slot_t *sim_slot, lte_modem_info_t *modem_info)
{
    modem_info->slot = atoi(sim_slot->slot);
    return 0;
}

int
ltem_parse_operator(char *buf, lte_operator_t *operator)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;

    memset(operator, 0, sizeof(*operator));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(operator->cmd, token, sizeof(operator->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(operator->mode, token, sizeof(operator->mode));
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(operator->format, token, sizeof(operator->format));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(operator->operator, token, sizeof(operator->operator));
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(operator->act, token, sizeof(operator->act));
    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_operator(lte_operator_t *operator, lte_modem_info_t *modem_info)
{
    if (sizeof(modem_info->operator) != sizeof( operator->operator)) return -1;
    strncpy(modem_info->operator, operator->operator, sizeof(modem_info->operator));
    modem_info->act = atoi(operator->act);
    return 0;
}

int
ltem_parse_serving_cell(char *buf, lte_srv_cell_t *srv_cell)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;
    int i;
    char *field;

    memset(srv_cell, 0, sizeof(*srv_cell));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(srv_cell->cmd, token, sizeof(srv_cell->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(srv_cell->cell_type, token, sizeof(srv_cell->cell_type));
    field = (char *)srv_cell->state;
    for (i = 0; i < 16; i++)
    {
        token = strtok_r(NULL, delim2, &context);
        if (token == NULL) goto error;
        strncpy(field, token, sizeof(srv_cell->state));
        field += sizeof(srv_cell->cell_type);
    }
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(srv_cell->srxlev, token, sizeof(srv_cell->srxlev));

    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_serving_cell(lte_srv_cell_t *srv_cell, lte_modem_info_t *modem_info)
{
    lte_serving_cell_info_t *srv_cell_info;
    int res;
    uint32_t bandwidth;

    srv_cell_info = &modem_info->srv_cell;

    res = strncmp(srv_cell->state, "\"SEARCH\"", strlen(srv_cell->state));
    if (!res) srv_cell_info->state = LTE_SERVING_CELL_SEARCH;

    res = strncmp(srv_cell->state, "\"LIMSERV\"", strlen(srv_cell->state));
    if (!res) srv_cell_info->state = LTE_SERVING_CELL_LIMSERV;

    res = strncmp(srv_cell->state, "\"NOCONN\"", strlen(srv_cell->state));
    if (!res) srv_cell_info->state = LTE_SERVING_CELL_NOCONN;

    res = strncmp(srv_cell->state, "\"CONNECT\"", strlen(srv_cell->state));
    if (!res) srv_cell_info->state = LTE_SERVING_CELL_CONNECT;

    res = strncmp(srv_cell->mode, "\"LTE\"", strlen(srv_cell->mode));
    if (!res) srv_cell_info->mode = LTE_CELL_MODE_LTE;

    res = strncmp(srv_cell->mode, "\"WCDMA\"", strlen(srv_cell->mode));
    if (!res) srv_cell_info->mode = LTE_CELL_MODE_WCDMA;

    srv_cell_info->fdd_tdd_mode = LTE_MODE_TDD;
    res = strncmp(srv_cell->fdd_tdd_mode, "\"FDD\"", strlen(srv_cell->fdd_tdd_mode));
    if (!res) srv_cell_info->fdd_tdd_mode = LTE_MODE_FDD;

    srv_cell_info->mcc = atoi(srv_cell->mcc);
    srv_cell_info->mnc = atoi(srv_cell->mnc);
    srv_cell_info->cellid = atoi(srv_cell->cellid);
    srv_cell_info->pcid = atoi(srv_cell->pcid);
    srv_cell_info->earfcn = atoi(srv_cell->earfcn);
    srv_cell_info->freq_band = atoi(srv_cell->freq_band);

    bandwidth = atoi(srv_cell->ul_bandwidth);
    switch (bandwidth)
    {
        case 0:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_1P4_MHZ;
            break;
        case 1:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_3_MHZ;
            break;
        case 2:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_5_MHZ;
            break;
        case 3:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_10_MHZ;
            break;
        case 4:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_15_MHZ;
            break;
        case 5:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_20_MHZ;
            break;
        default:
            srv_cell_info->ul_bandwidth = LTE_BANDWIDTH_UNSPECIFIED;
            break;
    }

    bandwidth = atoi(srv_cell->dl_bandwidth);
    switch (bandwidth)
    {
        case 0:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_1P4_MHZ;
            break;
        case 1:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_3_MHZ;
            break;
        case 2:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_5_MHZ;
            break;
        case 3:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_10_MHZ;
            break;
        case 4:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_15_MHZ;
            break;
        case 5:
            srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_20_MHZ;
            break;
    default:
        srv_cell_info->dl_bandwidth = LTE_BANDWIDTH_UNSPECIFIED;
        break;
    }

    srv_cell_info->tac = atoi(srv_cell->tac);
    srv_cell_info->rsrq = atoi(srv_cell->rsrq);
    srv_cell_info->rsrp = atoi(srv_cell->rsrp);
    srv_cell_info->rssi = atoi(srv_cell->rssi);
    srv_cell_info->sinr = atoi(srv_cell->sinr);
    srv_cell_info->srxlev = atoi(srv_cell->srxlev);

    return 0;
}

int
ltem_parse_neigh_cell_intra(char *buf, lte_neigh_cell_intra_t *neigh_cell_intra)
{
    char *delim1 = " ";
    char *delim2 = ",";
    char *delim3 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;
    int i;
    char *field;

    memset(neigh_cell_intra, 0, sizeof(*neigh_cell_intra));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context);
    if (token == NULL) goto error;
    strncpy(neigh_cell_intra->cmd, token, sizeof(neigh_cell_intra->cmd));
    token = strtok_r(NULL, delim2, &context);
    if (token == NULL) goto error;
    strncpy(neigh_cell_intra->cell_type, token, sizeof(neigh_cell_intra->cell_type));
    field = (char *)neigh_cell_intra->mode;
    for (i = 0; i < 11; i++)
    {
        token = strtok_r(NULL, delim2, &context);
        if (token == NULL) goto error;
        strncpy(field, token, sizeof(neigh_cell_intra->mode));
        field += sizeof(neigh_cell_intra->mode);
    }
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(neigh_cell_intra->s_intra_search, token, sizeof(neigh_cell_intra->s_intra_search));

    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_neigh_cell_intra(lte_neigh_cell_intra_t *neigh_cell_intra, lte_modem_info_t *modem_info)
{
    lte_neighbor_cell_intra_info_t *neigh_cell_info;
    int res;

    neigh_cell_info = &modem_info->neigh_cell_intra;

    res = strncmp(neigh_cell_intra->mode, "\"LTE\"", strlen(neigh_cell_intra->mode));
    if (!res) neigh_cell_info->mode = LTE_CELL_MODE_LTE;

    res = strncmp(neigh_cell_intra->mode, "\"WCDMA\"", strlen(neigh_cell_intra->mode));
    if (!res) neigh_cell_info->mode = LTE_CELL_MODE_WCDMA;

    neigh_cell_info->freq_mode = LTE_FREQ_MODE_INTRA;
    neigh_cell_info->earfcn = atoi(neigh_cell_intra->earfcn);
    neigh_cell_info->pcid = atoi(neigh_cell_intra->pcid);
    neigh_cell_info->rsrq = atoi(neigh_cell_intra->rsrq);
    neigh_cell_info->rsrp = atoi(neigh_cell_intra->rsrp);
    neigh_cell_info->rssi = atoi(neigh_cell_intra->rssi);
    neigh_cell_info->sinr = atoi(neigh_cell_intra->sinr);
    neigh_cell_info->srxlev = atoi(neigh_cell_intra->srxlev_base_station);
    neigh_cell_info->cell_resel_priority = atoi(neigh_cell_intra->cell_resel_priority);
    neigh_cell_info->s_non_intra_search = atoi(neigh_cell_intra->s_non_intra_search);
    neigh_cell_info->thresh_serving_low = atoi(neigh_cell_intra->thresh_serving_low);
    neigh_cell_info->s_intra_search = atoi(neigh_cell_intra->s_intra_search);

    return 0;
}

int
ltem_parse_neigh_cell_inter(char *buf, lte_neigh_cell_inter_t *neigh_cell_inter)
{
    char *delim1 = " ";
    char *delim2 = "\n";
    char *delim3 = ",";
    char *delim4 = "\r";
    char *context = NULL;
    int len;
    char *str;
    char *token;
    int i;
    char *field;

    memset(neigh_cell_inter, 0, sizeof(*neigh_cell_inter));
    len = strlen(buf);
    str = (char *)CALLOC(1, len);
    if (!str) return -1;
    strncpy(str, buf, len);
    token = strtok_r(str, delim1, &context); // neighbourcell intra...
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim2, &context); // ...neighbourcell intra
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim1, &context); // neighbourcell inter
    if (token == NULL) goto error;
    token = strtok_r(NULL, delim3, &context); // neighbourcell inter
    if (token == NULL) goto error;
    strncpy(neigh_cell_inter->cell_type, token, sizeof(neigh_cell_inter->cell_type));
    token = strtok_r(NULL, delim3, &context);
    if (token == NULL) goto error;
    strncpy(neigh_cell_inter->mode, token, sizeof(neigh_cell_inter->mode));
    field = (char *)neigh_cell_inter->freq_mode;
    for (i = 0; i < 9; i++)
    {
        token = strtok_r(NULL, delim3, &context);
        if (token == NULL) goto error;
        strncpy(field, token, sizeof(neigh_cell_inter->freq_mode));
        field += sizeof(neigh_cell_inter->mode);
    }
    token = strtok_r(NULL, delim4, &context);
    if (token == NULL) goto error;
    strncpy(neigh_cell_inter->thresh_x_high, token, sizeof(neigh_cell_inter->thresh_x_high));

    FREE(str);
    return 0;

error:
    FREE(str);
    LOGE("%s: failed", __func__);
    return -1;
}

int
ltem_save_neigh_cell_inter(lte_neigh_cell_inter_t *neigh_cell_inter, lte_modem_info_t *modem_info)
{
    lte_neighbor_cell_inter_info_t *neigh_cell_info;
    int res;

    neigh_cell_info = &modem_info->neigh_cell_inter;

    res = strncmp(neigh_cell_inter->mode, "\"LTE\"", strlen(neigh_cell_inter->mode));
    if (!res) neigh_cell_info->mode = LTE_CELL_MODE_LTE;

    res = strncmp(neigh_cell_inter->mode, "\"WCDMA\"", strlen(neigh_cell_inter->mode));
    if (!res) neigh_cell_info->mode = LTE_CELL_MODE_WCDMA;

    neigh_cell_info->freq_mode = LTE_FREQ_MODE_INTER;
    neigh_cell_info->earfcn = atoi(neigh_cell_inter->earfcn);
    neigh_cell_info->pcid = atoi(neigh_cell_inter->pcid);
    neigh_cell_info->rsrq = atoi(neigh_cell_inter->rsrq);
    neigh_cell_info->rsrp = atoi(neigh_cell_inter->rsrp);
    neigh_cell_info->rssi = atoi(neigh_cell_inter->rssi);
    neigh_cell_info->sinr = atoi(neigh_cell_inter->sinr);
    neigh_cell_info->srxlev = atoi(neigh_cell_inter->srxlev);
    neigh_cell_info->cell_resel_priority = atoi(neigh_cell_inter->cell_resel_priority);
    neigh_cell_info->thresh_x_low = atoi(neigh_cell_inter->thresh_x_low);
    neigh_cell_info->thresh_x_high = atoi(neigh_cell_inter->thresh_x_high);

    return 0;

}

/*
 * Convert an LTE_ATCMD_* enum to a human readable string
 */
const char *
lte_at_cmd_tostr(enum lte_at_cmd cmd)
{
    const char *at_str[AT_MAX + 1] =
    {
        #define _STR(sym, str) [sym] = str,
        LTE_ATCMD(_STR)
        #undef _STR
    };

    return at_str[cmd];
}

/**
 * @brief
 */
char *
ltem_run_modem_cmd (const char *cmd)
{
    ltem_mgr_t *mgr;
    ltem_handlers_t *handlers;
    int fd;
    int res;

    mgr = ltem_get_mgr();
    handlers = &mgr->handlers;

    fd = handlers->lte_modem_open(modem_path);
    if (fd < 0) return at_error;

    res = handlers->lte_modem_write(fd, cmd);
    if (res < 0)
    {
        LOGE("%s: modem write failed: %s", __func__, strerror(errno));
        handlers->lte_modem_close(fd);
        return at_error;
    }

    memset(lte_at_buf, 0, sizeof(*lte_at_buf));
    res = handlers->lte_modem_read(fd, lte_at_buf, sizeof(lte_at_buf));
    if (res < 0)
    {
        LOGE("%s: modem read failed: %s", __func__, strerror(errno));
        handlers->lte_modem_close(fd);
        return at_error;
    }

    handlers->lte_modem_close(fd);
    return lte_at_buf;
}

/**
 * @brief
 */
char *
ltem_run_at_cmd (enum lte_at_cmd cmd)
{
    const char *at_cmd;
    at_cmd = lte_at_cmd_tostr(cmd);
    return ltem_run_modem_cmd(at_cmd);
}

void
ltem_reset_modem(void)
{
    char *modem_resp;

    modem_resp = ltem_run_at_cmd(AT_MODEM_RESET);
    LOGI("%s: at cmd: %s, at resp: %s", __func__, lte_at_cmd_tostr(AT_MODEM_RESET), modem_resp);

    sleep(30);
}

bool
ltem_check_evt(void)
{
#ifndef ARCH_X86
    char evt[16];

    prov_fld_read(PROV_FLD_VER_HW_MAJ, evt, sizeof(evt));
    if (strncmp(evt, "EVT", sizeof(evt))) return false; // We're not on an EVT unit
    return true;
#endif
    return false;
}

void
ltem_evt_switch_slot(void)
{
    char *modem_resp;

    if (!ltem_check_evt()) return;

    /*
     * On EVT units, the physical SIM is in slot 1. The modem defaults to slot 0, which points at the eSim.
     * Switch sim slot to point at physical sim not esim.
     */
    modem_resp = ltem_run_at_cmd(AT_SWITCH_SLOT);
    LOGI("%s: at cmd: %s, at resp: %s", __func__, lte_at_cmd_tostr(AT_SWITCH_SLOT), modem_resp);

    ltem_reset_modem();
}

void
ltem_set_qmi_mode(void)
{
    char *modem_resp;

    modem_resp = ltem_run_at_cmd(AT_SET_QMI_MODE);
    LOGI("%s: at cmd: %s, at resp: %s", __func__, lte_at_cmd_tostr(AT_SET_QMI_MODE), modem_resp);
}

void
ltem_set_kore_apn()
{
    char *modem_resp;

    modem_resp = ltem_run_at_cmd(AT_SET_KORE_APN);
    LOGI("%s: at cmd: %s, at resp: %s", __func__, lte_at_cmd_tostr(AT_SET_KORE_APN), modem_resp);
}

void
ltem_set_apn(char *apn)
{
    char cmd[128];
    char *modem_resp;

    MEMZERO(cmd);
    sprintf(cmd, "AT+CGDCONT=1,\"IPV4V6\",\"%s\",\"0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0\",0,0,0,0\r", apn);

    modem_resp = ltem_run_modem_cmd(cmd);
    LOGI("%s: at cmd: %s, at resp: %s", __func__, cmd, modem_resp);
}

void
lte_dump_modem_info(void)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    lte_modem_info_t *mi;
    lte_serving_cell_info_t *srv_cell;
    lte_neighbor_cell_intra_info_t *neigh_cell_intra;
    lte_neighbor_cell_inter_info_t *neigh_cell_inter;

    mi = &mgr->modem_info;
    srv_cell = &mi->srv_cell;
    neigh_cell_intra = &mi->neigh_cell_intra;
    neigh_cell_inter = &mi->neigh_cell_inter;

    LOGI("chip_vendor[%s], model[%s], full_model[%s], imei[%s], imsi[%s], iccid[%s], reg_status[%d], rssi[%d], ber[%d], tx_bytes[%d], rx_bytes[%d], slot[%d], operator[%s], access_tech[%d]",
         mi->chip_vendor,
         mi->model,
         mi->full_model,
         mi->imei,
         mi->imsi,
         mi->iccid,
         mi->reg_status,
         mi->rssi,
         mi->ber,
         (int)mi->tx_bytes,
         (int)mi->rx_bytes,
         mi->slot,
         mi->operator,
         mi->act);

    LOGI("ServingCell: state[%d], mode[%d], fdd_tdd[%d], mcc[%d], mnc[%d], cellid[%d], pcid[%d], earfcn[%d], freq_band[%d], ul_bandwidth[%d], dl_bandwidth[%d], tac[%d], rsrp[%d], rsrq[%d], rssi[%d], sinr[%d], srxlev[%d]",
         srv_cell->state,
         srv_cell->mode,
         srv_cell->fdd_tdd_mode,
         srv_cell->mcc,
         srv_cell->mnc,
         srv_cell->cellid,
         srv_cell->pcid,
         srv_cell->earfcn,
         srv_cell->freq_band,
         srv_cell->ul_bandwidth,
         srv_cell->dl_bandwidth,
         srv_cell->tac,
         srv_cell->rsrp,
         srv_cell->rsrq,
         srv_cell->rssi,
         srv_cell->sinr,
         srv_cell->srxlev);

    LOGI("NeighborCell Intra: mode[%d], freq_mode[%d], earfcn[%d], pcid[%d], rsrq[%d], rsrp[%d], rssi[%d], sinr[%d], srxlev[%d], cell_reselect_priority[%d], s_non_intra_search[%d], thresh_serving_low[%d], s_intra_search[%d]",
         neigh_cell_intra->mode,
         neigh_cell_intra->freq_mode,
         neigh_cell_intra->earfcn,
         neigh_cell_intra->pcid,
         neigh_cell_intra->rsrq,
         neigh_cell_intra->rsrp,
         neigh_cell_intra->rssi,
         neigh_cell_intra->sinr,
         neigh_cell_intra->srxlev,
         neigh_cell_intra->cell_resel_priority,
         neigh_cell_intra->s_non_intra_search,
         neigh_cell_intra->thresh_serving_low,
         neigh_cell_intra->s_intra_search);

    LOGD("NeighborCell Inter: mode[%d], freq_mode[%d], earfcn[%d], pcid[%d], rsrq[%d], rsrp[%d], rssi[%d], sinr[%d], srxlev[%d], cell_reselect_priority[%d], thresh_x_low[%d], thresh_x_high[%d]",
         neigh_cell_inter->mode,
         neigh_cell_inter->freq_mode,
         neigh_cell_inter->earfcn,
         neigh_cell_inter->pcid,
         neigh_cell_inter->rsrq,
         neigh_cell_inter->rsrp,
         neigh_cell_inter->rssi,
         neigh_cell_inter->sinr,
         neigh_cell_inter->srxlev,
         neigh_cell_inter->cell_resel_priority,
         neigh_cell_inter->thresh_x_low,
         neigh_cell_inter->thresh_x_high);
}

char *
lte_run_microcom_cmd(char *cmd)
{
    FILE *fp;
    char at_line[256];
    char at_cmd_str[256];
    uint32_t offset = 0;
    char *fres;

    snprintf(at_cmd_str, sizeof(at_cmd_str), "echo -e \"%s\r\" | %s -t 100 %s", cmd, microcom_path, modem_path);

    fp = popen(at_cmd_str, "r");
    if (fp == NULL)
    {
        LOGE("popen failed: %s", strerror(errno));
        return NULL;
    }

    memset(lte_at_buf, 0, sizeof(*lte_at_buf));
    while (true)
    {
        MEMZERO(at_line);
        fres = fgets(at_line, sizeof(at_line), fp);
        if (offset >= sizeof(lte_at_buf)) break;
        strncpy(&lte_at_buf[offset], at_line, strlen(at_line));
        offset += strlen(at_line);
        if (fres == NULL) break;
    }

    pclose(fp);
    return lte_at_buf;
}

int
ltem_get_modem_info(void)
{
    ltem_mgr_t *mgr;
    ltem_handlers_t *handlers;
    int res;
    lte_modem_info_t *modem_info;
    char *at_resp;
    lte_chip_info_t chip_info;
    lte_imei_t imei;
    lte_imsi_t imsi;
    lte_iccid_t iccid;
    lte_reg_status_t reg_status;
    lte_sig_qual_t sig_qual;
    lte_byte_counts_t byte_counts;
    lte_sim_slot_t sim_slot;
    lte_operator_t operator;
    lte_srv_cell_t srv_cell;
    lte_neigh_cell_intra_t neigh_cell_intra;

    mgr = ltem_get_mgr();
    handlers = &mgr->handlers;
    modem_info = &mgr->modem_info;

    char *ati_cmd = "ati";
    at_resp = handlers->lte_run_microcom_cmd(ati_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_chip_info(at_resp, &chip_info);
    if (res)
    {
        LOGI("ltem_parse_chip_info:failed");
        return res;
    }
    ltem_save_chip_info(&chip_info, modem_info);

    char *gsn_cmd = "at+gsn";
    at_resp = handlers->lte_run_microcom_cmd(gsn_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_imei(at_resp, &imei);
    if (res)
    {
        LOGI("ltem_parse_imei:failed");
        return res;
    }
    ltem_save_imei(&imei, modem_info);

    char *imsi_cmd = "at+cimi";
    at_resp = handlers->lte_run_microcom_cmd(imsi_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_imsi(at_resp, &imsi);
    if (res)
    {
        LOGI("ltem_parse_imsi:failed");
        return res;
    }
    ltem_save_imsi(&imsi, modem_info);

    char *iccid_cmd = "at+qccid";
    at_resp = handlers->lte_run_microcom_cmd(iccid_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_iccid(at_resp, &iccid);
    if (res)
    {
        LOGI("ltem_parse_iccid:failed");
        return res;
    }
    ltem_save_iccid(&iccid, modem_info);

    char *creg_cmd = "at+creg?"; // net reg status
    at_resp = handlers->lte_run_microcom_cmd(creg_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_reg_status(at_resp, &reg_status);
    if (res)
    {
        LOGI("ltem_parse_reg_status:failed");
        return res;
    }
    ltem_save_reg_status(&reg_status, modem_info);

    char *csq_cmd = "at+csq"; // rssi, ber
    at_resp = handlers->lte_run_microcom_cmd(csq_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_sig_qual(at_resp, &sig_qual);
    if (res)
    {
        LOGI("ltem_parse_sig_qual:failed");
        return res;
    }
    ltem_save_sig_qual(&sig_qual, modem_info);

    char *qgdcnt_cmd = "at+qgdcnt?"; //tx/rx bytes
    at_resp = handlers->lte_run_microcom_cmd(qgdcnt_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_byte_counts(at_resp, &byte_counts);
    if (res)
    {
        LOGI("ltem_parse_byte_counts:failed");
        return res;
    }
    ltem_save_byte_counts(&byte_counts, modem_info);

    char *qdsim_cmd = "at+qdsim?";
    at_resp = handlers->lte_run_microcom_cmd(qdsim_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_sim_slot(at_resp, &sim_slot);
    if (res)
    {
        LOGI("ltem_parse_sim_slot:failed");
        return res;
    }
    ltem_save_sim_slot(&sim_slot, modem_info);

    char *cops_cmd = "at+cops?";
    at_resp = handlers->lte_run_microcom_cmd(cops_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_operator(at_resp, &operator);
    if (res)
    {
        LOGI("ltem_parse_operator:failed");
        return res;
    }
    ltem_save_operator(&operator, modem_info);

    char *srv_cell_cmd = "at+qeng=\\\"servingcell\\\"";
    at_resp = handlers->lte_run_microcom_cmd(srv_cell_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_serving_cell(at_resp, &srv_cell);
    if (res)
    {
        LOGI("ltem_parse_serving_cell:failed");
        return res;
    }
    ltem_save_serving_cell(&srv_cell, modem_info);

    char *neigh_cell_cmd = "at+qeng=\\\"neighbourcell\\\"";
    at_resp = handlers->lte_run_microcom_cmd(neigh_cell_cmd);
    if (!at_resp) return -1;
    res = ltem_parse_neigh_cell_intra(at_resp, &neigh_cell_intra);
    if (res)
    {
        LOGI("ltem_parse_neigh_cell_intra:failed");
        return res;
    }
    ltem_save_neigh_cell_intra(&neigh_cell_intra, modem_info);

    lte_neigh_cell_inter_t neigh_cell_inter;
    res = ltem_parse_neigh_cell_inter(at_resp, &neigh_cell_inter);
    if (res)
    {
        LOGI("ltem_parse_neigh_cell_inter:failed");
        return res;
    }
    ltem_save_neigh_cell_inter(&neigh_cell_inter, modem_info);

    lte_dump_modem_info();
    return 0;
}
