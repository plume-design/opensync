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

#include "cell_info.h"

#define C_CMD_RESP 16
#define C_CMD_LONG_RESP 32
#define C_CMD_LONGEST_RESP 64

#define MAX_CELL_COUNT 6

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

typedef struct osn_cell_modem_info_
{
    bool modem_present;
    char chip_vendor[C_CMD_RESP];
    char model[C_CMD_RESP];
    char full_model[C_CMD_LONG_RESP];
    char modem_fw_ver[C_CMD_LONGEST_RESP];
    struct cell_common_header header;
    struct cell_net_info cell_net_info;
    struct cell_data_usage cell_data_usage;
    struct lte_serving_cell_info cell_srv_cell;
    size_t n_neigh_cells;
    size_t cur_neigh_cell_idx;
    struct cell_net_neighbor_cell_info cell_neigh_cell_info[MAX_CELL_COUNT];
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
} osn_cell_modem_info_t;

osn_cell_modem_info_t *osn_get_cell_modem_info(void);
void osn_cell_dump_modem_info();
int osn_cell_read_modem(void);
void osn_cell_start_modem(int source);
void osn_cell_stop_modem(void);
bool osn_cell_modem_init(void);
void osn_cell_modem_reset(void);
int osn_cell_set_apn(char *apn);
int osn_cell_set_apn_username(char *apn_username);
int osn_cell_set_apn_password(char *apn_password);
int osn_cell_set_apn_prototype(enum cell_apn_proto_type apn_proto_type);
int osn_cell_set_apn_auth_proto(enum cell_apn_auth_proto apn_auth_proto);
int osn_cell_set_bands_enabled(char *bands);
int osn_cell_set_mode(enum cell_mode mode);

#endif /* OSN_CELL_MODEM_H_INCLUDED */
