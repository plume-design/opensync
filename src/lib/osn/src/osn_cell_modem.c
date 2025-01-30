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
#include <termios.h>
#include <sys/types.h>
#include <unistd.h>

#include "osn_cell_modem.h"

/*
 * ===========================================================================
 *  Public API implementation stubs.
 * ===========================================================================
 */

static osn_cell_modem_info_t modem_info;

osn_cell_modem_info_t *osn_cell_get_modem_info(void)
{
    return &modem_info;
}

int osn_cell_parse_dynamic_pdp_context_info(char *buf, cell_pdp_ctx_dynamic_param_info_t *pdp_dyn_ctx)
{
    return 0;
}

void osn_cell_save_dynamic_pdp_context_info(
        cell_pdp_ctx_dynamic_param_info_t *pdp_ctx,
        osn_cell_modem_info_t *modem_info)
{
    return;
}

void osn_cell_set_bands(char *bands)
{
    return;
}

void osn_cell_reset_modem(void)
{
    return;
}

void osn_cell_set_sim_slot(uint32_t slot)
{
    return;
}

void osn_lte_set_apn(char *apn)
{
    return;
}

void osn_cell_dump_modem_info(void)
{
    return;
}

void osn_cell_start_modem(int source)
{
    return;
}

void osn_cell_stop_modem(void)
{
    return;
}

int osn_cell_read_modem(void)
{
    return 0;
}
bool osn_cell_set_pdp_context_params(cell_pdp_context_params param_type, char *val)
{
    return true;
}
int osn_cell_parse_pdp_context(char *buf, cell_pdp_context_t *pdp_ctxt)
{
    return 0;
}

void osn_cell_start_vendor_daemon(int source)
{
    return;
}
void osn_cell_stop_vendor_daemon(void)
{
    return;
}

bool osn_cell_modem_init(void)
{
    return 0;
}
