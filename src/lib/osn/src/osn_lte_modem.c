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

#include "memutil.h"
#include "lte_info.h"
#include "osn_lte_modem.h"

#include "log.h"
#include "neigh_table.h"

/*
 * ===========================================================================
 *  Public API implementation stubs. 
 * ===========================================================================
 */

static osn_lte_modem_info_t modem_info;

osn_lte_modem_info_t *
osn_get_modem_info(void)
{
    return &modem_info;
}

int
osn_lte_modem_open(char *modem_path)
{
    return 0;
}

ssize_t
osn_lte_modem_write(int fd, const char *cmd)
{
    return 0;
}

ssize_t
osn_lte_modem_read(int fd, char *at_buf, ssize_t at_len)
{
    return 0;
}

void
osn_lte_modem_close(int fd)
{
    return;
}

int
osn_lte_parse_at_output(char *buf)
{
    return 0;
}

int
osn_lte_parse_chip_info(char *buf, lte_chip_info_t *chip_info)
{
    return 0;
}

int
osn_lte_save_chip_info(lte_chip_info_t *chip_info, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_sim_status(char *buf, lte_sim_insertion_status_t *sim_status)
{
    return 0;
}

int
osn_lte_save_sim_status(lte_sim_insertion_status_t *sim_status, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_imei(char *buf, lte_imei_t *imei)
{
    return 0;
}

int
osn_lte_save_imei(lte_imei_t *imei, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_imsi(char *buf, lte_imsi_t *imsi)
{
    return 0;
}

int
osn_lte_save_imsi(lte_imsi_t *imsi, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_iccid(char *buf, lte_iccid_t *iccid)
{
    return 0;
}

int
osn_lte_save_iccid(lte_iccid_t *iccid, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_reg_status(char *buf, lte_reg_status_t *reg_status)
{
    return 0;
}

int
osn_lte_save_reg_status(lte_reg_status_t *reg_status, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_sig_qual(char *buf, lte_sig_qual_t *sig_qual)
{
    return 0;
}

int
osn_lte_parse_gmr(char *buf, lte_gmr_t *gmr)
{
    return 0;
}

int
osn_lte_save_gmr(lte_gmr_t *gmr, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_save_sig_qual(lte_sig_qual_t *sig_qual, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_byte_counts(char *buf, lte_byte_counts_t *byte_counts)
{
    return 0;
}

int
osn_lte_save_byte_counts(lte_byte_counts_t *byte_counts, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_sim_slot(char *buf, lte_sim_slot_t *sim_slot)
{
    return 0;
}

int
osn_lte_save_sim_slot(lte_sim_slot_t *sim_slot, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_operator(char *buf, lte_operator_t *operator)
{
    return 0;
}

int
osn_lte_save_operator(lte_operator_t *operator, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_serving_cell(char *buf, lte_srv_cell_t *srv_cell, lte_srv_cell_wcdma_t *srv_cell_wcdma)
{
    return 0;
}

int osn_lte_save_serving_cell(lte_srv_cell_t *srv_cell, lte_srv_cell_wcdma_t *srv_cell_wcdma, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

int
osn_lte_parse_neigh_cell(char *buf, lte_neigh_cell_t *neigh_cell)
{
    return 0;
}

int
osn_lte_save_neigh_cell(lte_neigh_cell_t *neigh_cell, osn_lte_modem_info_t *modem_info, int resp_idx)
{
    return 0;
}

int
osn_lte_parse_dynamic_pdp_context_info(char *buf, lte_pdp_ctx_dynamic_param_info_t *pdp_dyn_ctx)
{
    return 0;
}

void
osn_lte_save_dynamic_pdp_context_info(lte_pdp_ctx_dynamic_param_info_t *pdp_ctx, osn_lte_modem_info_t *modem_info)
{
    return;
}

int
osn_lte_parse_ca_info(char *buf, lte_ca_info_t *pcc_info, lte_ca_info_t *scc_info)
{
    return 0;
}


int
osn_lte_save_ca_info(lte_ca_info_t *pca_info, lte_ca_info_t *sca_info, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

void
osn_lte_set_bands(char *bands)
{
    return;
}

char *
osn_lte_run_modem_cmd (const char *cmd)
{
    return NULL;
}

void
osn_lte_reset_modem(void)
{
    return;
}

void
osn_lte_set_sim_slot(uint32_t slot)
{
    return;
}

void
osn_lte_set_qmi_mode(void)
{
    return;
}

void
osn_lte_set_apn(char *apn)
{
    return;
}

void
osn_lte_enable_sim_detect(void)
{
    return;
}

void
osn_lte_dump_modem_info(void)
{
    return;
}

char *
osn_lte_run_microcom_cmd(char *cmd)
{
    return NULL;
}

int
osn_lte_read_modem(void)
{
    return 0;
}
int
osn_lte_read_pdp_context(void)
{
    return 0;
}
bool
osn_lte_set_pdp_context_params(lte_pdp_context_params param_type, char *val)
{
    return true;
}
int
osn_lte_parse_pdp_context(char *buf, lte_pdp_context_t *pdp_ctxt)
{
    return 0;
}
bool
osn_lte_set_ue_data_centric(void)
{
    return true;
}
void
osn_lte_start_vendor_daemon(int source)
{
    return;
}
void
osn_lte_stop_vendor_daemon(void)
{
    return;
}
int
osn_gen_parser_sa(char *modem_resp, gen_resp_tokens *resp_tokens)
{
    return 0;
}
void
osn_nr5g_save_serving_cell_5g_sa(gen_resp_tokens *resp_tokens)
{
    return;
}

int
osn_get_module_id(void)
{
    return 0;
}
int
osn_parse_sinr_rsrp(char *buf, cellular_sinr_rsrp_t *net_response)
{
    return 0;
}
int
osn_cellular_save_sinr(cellular_sinr_rsrp_t *net_resp, osn_lte_modem_info_t *modem_info)
{
    return 0;
}
int
osn_cellular_save_qsrp(cellular_sinr_rsrp_t *net_resp, osn_lte_modem_info_t *modem_info)
{
    return 0;
}

bool
osn_lte_modem_init(void)
{
    return 0;
}
