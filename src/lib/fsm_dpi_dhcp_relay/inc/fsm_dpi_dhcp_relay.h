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

#ifndef FSM_DPI_DHCP_RELAY_INCLUDED
#define FSM_DPI_DHCP_RELAY_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ds_tree.h"
#include "fsm.h"
#include "osn_dhcp.h"

/******************************************************************************
 * Struct Declarations
 *******************************************************************************/

/**
 * DHCPv4 message types
 */

#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_DECLINE  4
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NACK     6
#define DHCP_MSG_RELEASE  7
#define DHCP_MSG_INFORM   8

/**
 * DHCPv6 message types, defined in section 5.3 of RFC 3315
 */

#define DHCPV6_SOLICIT             1
#define DHCPV6_ADVERTISE           2
#define DHCPV6_REQUEST             3
#define DHCPV6_CONFIRM             4
#define DHCPV6_RENEW               5
#define DHCPV6_REBIND              6
#define DHCPV6_REPLY               7
#define DHCPV6_RELEASE             8
#define DHCPV6_DECLINE             9
#define DHCPV6_RECONFIGURE         10
#define DHCPV6_INFORMATION_REQUEST 11
#define DHCPV6_RELAY_FORW          12
#define DHCPV6_RELAY_REPL          13
#define DHCPV6_LEASEQUERY          14
#define DHCPV6_LEASEQUERY_REPLY    15

#define DHCP_MAGIC 0x63825363

#ifndef MAC_STR_LEN
#define MAC_STR_LEN 18
#endif /* MAC_STR_LEN */

#define MAX_LABEL_LEN_PER_DN 63
#define MAX_DN_LEN           253

#ifndef DHCP_MTU_MAX
#define DHCP_MTU_MAX 1500
#endif
#define DHCP_FIXED_NON_UDP  236
#define DHCP_UDP_OVERHEAD   (20 /* IP header */ + 8 /* UDP header */)
#define DHCP_FIXED_LEN      (DHCP_FIXED_NON_UDP + DHCP_UDP_OVERHEAD)
#define DHCP_MAX_OPTION_LEN (DHCP_MTU_MAX - DHCP_FIXED_LEN)

#define DHCP_RELAY_CONF_FILE          "/tmp/dhcp_relay.conf"
#define DHCP_OPTION_AGENT_INFORMATION 82
#define RAI_CIRCUIT_ID                1
#define RAI_REMOTE_ID                 2

#define DHCPv6_INTERFACE_ID 18
#define DHCPv6_REMOTE_ID    37 /* RFC4649 */

/*
 * DHCP State machines enums
 */

enum dhcp_state
{
    UNDEFINED = 0,
    BEGIN_DHCP,
    DHCP_MESSAGE_TYPE,
    END_DHCP
};

struct dhcp_relay_conf_options
{
    uint8_t version; /* IPv6 or IPv4 */
    uint8_t id;
    char *value;
    ds_dlist_t d_node;
};

/**
 * Normal packet format, defined in section 6 of RFC 3315
 */

struct dhcpv6_hdr
{
    uint8_t msg_type;
    uint8_t transaction_id[3];
    uint8_t options[0];
};

/**
 * DHCPv4 packet format
 */

struct dhcp_hdr
{
    uint8_t dhcp_op;
    uint8_t dhcp_htype;
    uint8_t dhcp_hlen;
    uint8_t dhcp_hops;
    uint32_t dhcp_xid;
    uint16_t dhcp_secs;
    uint16_t dhcp_flags;
    os_ipaddr_t dhcp_ciaddr;
    os_ipaddr_t dhcp_yiaddr;
    os_ipaddr_t dhcp_siaddr;
    os_ipaddr_t dhcp_giaddr;
    union
    {
        os_macaddr_t dhcp_chaddr;
        uint8_t dhcp_padaddr[16];
    };
    uint8_t dhcp_server[64];
    uint8_t dhcp_boot_file[128];
    uint32_t dhcp_magic;
    uint8_t dhcp_options[DHCP_MAX_OPTION_LEN];
};

struct dhcp_parser
{
    struct net_header_parser *net_parser;
    size_t dhcp_len;
    size_t parsed;
    size_t caplen;
    uint8_t *data;
    struct pcap_pkthdr header;
    uint8_t relay_options_len;
};

struct dhcp_relay_session
{
    struct fsm_session *session;
    struct dhcp_parser parser;
    ds_tree_t dhcp_leases;
    ds_tree_node_t session_node;
    bool initialized;
    ds_tree_t dhcp_local_domains;
    size_t num_local_domains;
    int sock_fd;
    struct sockaddr_ll raw_dst;
    os_macaddr_t src_eth_addr;
    bool forward_ctxt_initialized;
};

struct dhcp_record
{
    enum dhcp_state next_state;
    uint8_t dhcp_message_type;
};

struct dpi_dhcp_client
{
    bool initialized;
    ds_tree_t fsm_sessions;
    struct dhcp_record cur_rec_processed;
    int (*dhcp_set_forward_context)(struct fsm_session *);
    void (*dhcp_forward)(struct dhcp_relay_session *);
};

/**
 * @brief dpi dhcp relay manager accessor
 */
struct dpi_dhcp_client *fsm_dpi_dhcp_get_mgr(void);

/**
 * @brief reset dhcp record
 */
void fsm_dpi_dhcp_reset_state(struct fsm_session *session);

/**
 * @brief looks up a session
 *
 * Looks up a dhcp relay session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dhcp_relay_session *fsm_dpi_dhcp_get_session(struct fsm_session *session);

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the ndp session to delete
 */
void fsm_dpi_dhcp_delete_session(struct fsm_session *session);

/**
 * @brief Initialize all the required structures
 *
 * @param session used to extract information about the session.
 */
int fsm_dpi_dhcp_relay_init(struct fsm_session *session);

/**
 * @brief Releases all allocated memory and un-initialize global
 *        aggregator.
 *
 * @param session
 */
void fsm_dpi_dhcp_relay_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_ndp session to update
 */
void fsm_dpi_dhcp_relay_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_ndp session to process
 */
void fsm_dpi_dhcp_relay_periodic(struct fsm_session *session);

/**
 * @brief process specifically a DHCP flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int fsm_dpi_dhcp_process_attr(
        struct fsm_session *session,
        const char *attr,
        uint8_t type,
        uint16_t length,
        const void *value,
        struct fsm_dpi_plugin_client_pkt_info *pkt_info);

bool fsm_dpi_process_dhcp_packet(struct fsm_session *session, struct net_header_parser *net_parser);

void fsm_dpi_dhcp_relay_process_dhcpv4_message(struct dhcp_relay_session *d_session);
void fsm_dpi_dhcp_relay_process_dhcpv6_message(struct dhcp_relay_session *d_session);

size_t fsm_dpi_dhcp_parse_message(struct dhcp_parser *parser);
bool dhcp_relay_check_option82(struct dhcp_relay_session *d_session);
bool dhcp_relay_check_dhcpv6_option(struct dhcp_relay_session *d_session, int dhcpv6_option);

void dhcp_prepare_forward(struct dhcp_relay_session *d_session, uint8_t *packet);
void dhcp_forward(struct dhcp_relay_session *d_session);
ds_dlist_t *dhcp_get_relay_options(void);
void dhcp_relay_update_headers(struct net_header_parser *net_parser, uint8_t *popt, uint16_t size_options);

#endif /* FSM_DPI_DHCP_RELAY_INCLUDED */
