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

#ifndef FSM_DPI_UTILS_H_INCLUDED
#define FSM_DPI_UTILS_H_INCLUDED

#include "fsm.h"
#include "neigh_table.h"
/**
 * @brief FSM DPI APIs using 5 tuples
 */
int fsm_set_ip_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        enum fsm_dpi_state state,
        int flow_marker
);

int fsm_set_icmp_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        enum fsm_dpi_state state,
        int flow_marker
);

/**
 * @brief FSM DPI APIs using pcap data
 */
int fsm_set_dpi_mark(
        struct net_header_parser *net_hdr,
        struct dpi_mark_policy *mark_policy
);

void fsm_dpi_set_plugin_decision(
        struct fsm_session *session,
        struct net_md_stats_accumulator *acc,
        enum fsm_dpi_state state);

char *
fsm_ops_get_network_id(struct fsm_session *session, os_macaddr_t *mac);

int
fsm_dpi_get_mark(int flow_marker, int action);

bool
fsm_nfq_mac_same(os_macaddr_t *lkp_mac, struct nfq_pkt_info *pkt_info);

bool
fsm_update_neigh_cache(void *ipaddr, os_macaddr_t *mac, int domain, int source);

void
fsm_set_dpi_health_stats_cfg(struct fsm_session *session);

#endif /* FSM_DPI_UTILS_H_INCLUDED */
