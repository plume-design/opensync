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

#ifndef FSM_DPI_DNS_H_INCLUDED
#define FSM_DPI_DNS_H_INCLUDED

#include <arpa/nameser.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dns_utils.h"
#include "fsm_policy.h"
#include "network_metadata_report.h"

enum dns_state
{
    UNDEFINED = 0,
    BEGIN_DNS,
    DNS_QNAME,
    DNS_TYPE,
    DNS_TTL,
    DNS_A,
    DNS_A_OFFSET,
    DNS_AAAA,
    DNS_AAAA_OFFSET,
    END_DNS,
    IP_ADDRESS, /* This is not a real attribute: it wraps IPv4 and IPv6 into address */
};

struct dns_responses_s
{
    int num_replies;
    int ipv4_cnt;
    int ipv6_cnt;
    char *ipv4_addrs[MAX_RESOLVED_ADDRS];
    char *ipv6_addrs[MAX_RESOLVED_ADDRS];
};

struct dns_response
{
    uint8_t type;
    uint8_t ip_v;
    uint16_t ttl;
    uint16_t offset; /* offset of IP address in DNS payload */
    uint8_t address[NS_IN6ADDRSZ];
};

struct dns_record
{
    char qname[NS_MAXCDNAME];
    struct dns_response resp[64];
    size_t idx;
    enum dns_state next_state;
};

struct dns_session
{
    ds_tree_t cached_entries;
    uint8_t service_provider;
    ds_tree_t device_cache;
    bool initialized;

    ds_tree_node_t next;
};

struct dpi_dns_client
{
    bool initialized;
    ds_tree_t fsm_sessions;
    bool identical_plugin_enabled;
    struct dns_record curr_rec_processed;
    void (*update_tag)(struct fsm_dns_update_tag_param *);
};

/**
 * @brief dpi dns manager accessor
 */
struct dpi_dns_client *
fsm_dpi_dns_get_mgr(void);

/**
 * @brief reset dns record
 */
void
fsm_dpi_dns_reset_state(void);

/**
 * @brief Initialize all the required structures
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_dns_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session
 */
void fsm_dpi_dns_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_dns session to update
 */
void fsm_dpi_dns_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_dns session to process
 */
void fsm_dpi_dns_periodic(struct fsm_session *session);

/**
 * @brief process specifically a DNS flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_dns_process_attr(struct fsm_session *session, const char *attr,
                             uint8_t type, uint16_t length, const void *value,
                             struct fsm_dpi_plugin_client_pkt_info *pkt_info);

/**
 * @brief update dns response ips
 *
 * @param net_parser the packet container
 * @param acc the flow
 * @param policy_reply verdict info for the flow
 */
bool
fsm_dpi_dns_update_response_ips(struct net_header_parser *net_header,
                                struct net_md_stats_accumulator *acc,
                                struct fsm_policy_reply *policy_reply);

/**
 * @brief populate dns response ips
 *
 * @param dns_response container
 */
void
fsm_dpi_dns_populate_response_ips(struct dns_response_s *dns_resp_ips);

/**
 * @brief free dns response ips
 *
 * @param dns_response container
 */
void
fsm_dpi_dns_free_dns_response_ips(struct dns_response_s *dns_response);

/**
 * @brief process dns record
 *
 * @param session the fsm session
 * @param acc the flow
 * @param net_parser the packet container
 */
int
fsm_dpi_dns_process_dns_record(struct fsm_session *session,
                               struct net_md_stats_accumulator *acc,
                               struct net_header_parser *net_parser);

#endif /* FSM_DPI_DNS_H_INCLUDED */
