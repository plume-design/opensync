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

#ifndef PM_ERP_H_INCLUDED
#define PM_ERP_H_INCLUDED

#include <stdbool.h>

#define RX_CHAINMASK 0x1
#define ETH_PORT     0x2

struct osp_erp_ctx
{
    struct ev_timer erp_timer;

    int curr_clnt_state;
    int prev_clnt_state;
};

struct iface_chainmask
{
    char if_name[32];  // interface name
    int chainmask;     // mask
};

struct iface_list
{
    struct iface_chainmask *entries;
    size_t count;
};

int pm_erp_ovsdb_init(struct osp_erp_ctx *ctx);
bool pm_erp_ovsdb_is_radio_enabled(const char *if_name);

int pm_erp_ovsdb_get_radio_interfaces(struct iface_list *if_list);
int pm_erp_ovsdb_set_radio_txchainmask(const char *if_name, unsigned int txchainmask);
int pm_erp_ovsdb_get_eth_interfaces(struct iface_list *if_list);
void pm_erp_set_node_state(const char *module, const char *key, const char *value);
int pm_erp_ovsdb_get_clients(void);
void pm_erp_send_event(void);

bool pm_erp_ovsdb_erp_mode_ready(void);
bool pm_erp_ovsdb_erp_mode_enabled(void);
int pm_erp_get_mask(void);
const char *pm_erp_get_uplink(void);
unsigned int pm_erp_state_tmr_inc(void);

#ifndef CONFIG_PM_ENABLE_ERP_MODE
static inline bool pm_erp_is_active(void)
{
    return false;
}
#else
bool pm_erp_is_active(void);
#endif

#endif /* PM_ERP_H_INCLUDED */
