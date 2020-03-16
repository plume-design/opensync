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

#ifndef DNSMASQ_SERVER_H_INCLUDED
#define DNSMASQ_SERVER_H_INCLUDED

#include "osn_dhcp.h"
#include "ds_tree.h"
#include "ds_dlist.h"

typedef struct dnsmasq_server dnsmasq_server_t;

typedef void dnsmasq_server_status_fn_t(
        dnsmasq_server_t *self,
        struct osn_dhcp_server_status *status);

typedef void dnsmasq_server_error_fn_t(dnsmasq_server_t *self);

struct dnsmasq_server
{
    char                            ds_ifname[C_IFNAME_LEN];
    struct osn_dhcp_server_cfg      ds_cfg;
    dnsmasq_server_status_fn_t     *ds_status_fn;
    dnsmasq_server_error_fn_t      *ds_error_fn;
    char                           *ds_opts[DHCP_OPTION_MAX];
    struct osn_dhcp_server_status   ds_status;
    ds_tree_t                       ds_range_list;          /** IP pool range list */
    ds_tree_t                       ds_reservation_list;    /** IP reservation list */
    ds_dlist_node_t                 ds_dnode;
};

bool dnsmasq_server_init(dnsmasq_server_t *self, const char *ifname);
bool dnsmasq_server_fini(dnsmasq_server_t *self);
void dnsmasq_server_apply(void);

bool dnsmasq_server_option_set(
        dnsmasq_server_t *self,
        enum osn_dhcp_option opt,
        const char *value);

bool dnsmasq_server_cfg_set(dnsmasq_server_t *self, struct osn_dhcp_server_cfg *cfg);

bool dnsmasq_server_range_add(dnsmasq_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop);
bool dnsmasq_server_range_del(dnsmasq_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop);

bool dnsmasq_server_reservation_add(
        dnsmasq_server_t *self,
        osn_mac_addr_t macaddr,
        osn_ip_addr_t ipaddr,
        const char *hostname);

bool dnsmasq_server_reservation_del(
        dnsmasq_server_t *self,
        osn_mac_addr_t macaddr);

void dnsmasq_server_status_notify(dnsmasq_server_t *self, dnsmasq_server_status_fn_t *fn);
void dnsmasq_server_error_notify(dnsmasq_server_t *self, dnsmasq_server_error_fn_t *fn);

#endif /* DNSMASQ_SERVER_H_INCLUDED */
