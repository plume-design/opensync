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

#ifndef FSM_DPI_ARP_NDP_INCLUDED
#define FSM_DPI_ARP_NDP_INCLUDED

#include <arpa/nameser.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ds_tree.h"
#include "fsm.h"

enum arp_state
{
    UNDEFINED = 0,
    BEGIN_ARP,
    ARP_REQ_SIP,
    ARP_REQ_SMAC,
    ARP_REQ_TIP,
    ARP_REQ_TMAC,
    ARP_RES_SIP,
    ARP_RES_SMAC,
    ARP_RES_TIP,
    ARP_RES_TMAC,
    END_ARP
};

enum ndp_state
{
    ndp_state_UNDEFINED = 0,
    BEGIN_NDP=2,
    ICMPv6_NS_SIP,
    ICMPv6_NS_SMAC,
    ICMPv6_NA_TIP,
    ICMPv6_NA_TMAC,
    END_NDP
};

struct arp_record
{
    uint32_t s_ip_addr;
    os_macaddr_t s_mac;
    uint32_t t_ip_addr;
    os_macaddr_t t_mac;
    enum arp_state next_state;
};

struct ndp_record
{
    uint8_t ip_addr[NS_IN6ADDRSZ];
    os_macaddr_t mac;
    enum ndp_state next_state;
};

struct ndp_session
{
    struct neighbour_entry entry;
    bool initialized;
    time_t timestamp;
    uint64_t ttl;
    ds_tree_node_t next;
};

struct dpi_ndp_client
{
    bool initialized;
    ds_tree_t fsm_sessions;
    bool identical_plugin_enabled;
    struct arp_record curr_arp_rec_processed;
    struct ndp_record curr_ndp_rec_processed;
};

/**
 * @brief dpi ndp manager accessor
 */
struct dpi_ndp_client *
fsm_dpi_ndp_get_mgr(void);

/**
 * @brief reset ndp record
 */
void
fsm_dpi_ndp_reset_state(struct fsm_session *session);

/**
 * @brief reset arp record
 */
void
fsm_dpi_arp_reset_state(struct fsm_session *session);

/**
  * @brief looks up a session
  *
  * Looks up a session, and allocates it if not found.
  * @param session the session to lookup
  * @return the found/allocated session, or NULL if the allocation failed
  */
struct ndp_session *fsm_dpi_ndp_get_session(struct fsm_session *session);

/**
  * @brief Frees a ndp session
  *
  * @param n_session the ndp session to delete
  */
void fsm_dpi_ndp_free_session(struct ndp_session *n_session);

/**
  * @brief deletes a session
  *
  * @param session the fsm session keying the ndp session to delete
  */
void fsm_dpi_ndp_delete_session(struct fsm_session *session);

/**
 * @brief Initialize all the required structures
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_ndp_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session
 */
void fsm_dpi_ndp_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_ndp session to update
 */
void fsm_dpi_ndp_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_ndp session to process
 */
void fsm_dpi_ndp_periodic(struct fsm_session *session);

/**
 * @brief process specifically a ARP/NDP flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_ndp_process_attr(struct fsm_session *session, const char *attr,
                             uint8_t type, uint16_t length, const void *value,
                             struct fsm_dpi_plugin_client_pkt_info *pkt_info);

bool fsm_dpi_ndp_process_arp_record(struct fsm_session *session,
                                   struct net_md_stats_accumulator *acc,
                                   struct net_header_parser *net_parser);

bool fsm_dpi_ndp_process_icmpv6_record(struct fsm_session *session,
                                      struct net_md_stats_accumulator *acc,
                                      struct net_header_parser *net_parser);
#endif /* FSM_DPI_ARP_NDP_INCLUDED */
