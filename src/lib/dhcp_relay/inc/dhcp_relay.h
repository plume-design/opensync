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

#ifndef DHCP_RELAY_H_INCLUDED
#define DHCP_RELAY_H_INCLUDED

#include <linux/if_packet.h>
#include <pcap.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#include "schema.h"
#include "inet.h"
#include "fsm.h"
#include "ds_tree.h"
#include "net_header_parse.h"
#include "os.h"
#include "os_types.h"

/******************************************************************************
* Struct Declarations
*******************************************************************************/

/**
 * DHCPv4 message types
 */

#define DHCP_MSG_DISCOVER   1
#define DHCP_MSG_OFFER      2
#define DHCP_MSG_REQUEST    3
#define DHCP_MSG_DECLINE    4
#define DHCP_MSG_ACK        5
#define DHCP_MSG_NACK       6
#define DHCP_MSG_RELEASE    7
#define DHCP_MSG_INFORM     8

/**
 * DHCPv6 message types, defined in section 5.3 of RFC 3315
 */

#define DHCPV6_SOLICIT              1
#define DHCPV6_ADVERTISE            2
#define DHCPV6_REQUEST              3
#define DHCPV6_CONFIRM              4
#define DHCPV6_RENEW                5
#define DHCPV6_REBIND               6
#define DHCPV6_REPLY                7
#define DHCPV6_RELEASE              8
#define DHCPV6_DECLINE              9
#define DHCPV6_RECONFIGURE          10
#define DHCPV6_INFORMATION_REQUEST  11
#define DHCPV6_RELAY_FORW           12
#define DHCPV6_RELAY_REPL           13
#define DHCPV6_LEASEQUERY           14
#define DHCPV6_LEASEQUERY_REPLY     15

#define DHCP_MAGIC          0x63825363

#ifndef MAC_STR_LEN
#define MAC_STR_LEN         18
#endif  /* MAC_STR_LEN */

#define MAX_LABEL_LEN_PER_DN 63
#define MAX_DN_LEN 253

#ifndef DHCP_MTU_MAX
#define DHCP_MTU_MAX                   1500
#endif
#define DHCP_FIXED_NON_UDP             236
#define DHCP_UDP_OVERHEAD              (20 /* IP header */  +  8 /* UDP header */ )
#define DHCP_FIXED_LEN                 (DHCP_FIXED_NON_UDP + DHCP_UDP_OVERHEAD)
#define DHCP_MAX_OPTION_LEN            (DHCP_MTU_MAX - DHCP_FIXED_LEN)

#define DHCP_RELAY_CONF_FILE           "/tmp/dhcp_relay.conf"
#define DHCP_OPTION_AGENT_INFORMATION  82
#define RAI_CIRCUIT_ID                 1
#define RAI_REMOTE_ID                  2

#define DHCPv6_INTERFACE_ID            18
#define DHCPv6_REMOTE_ID               37  /* RFC4649 */

struct dhcp_relay_conf_options
{
    uint8_t                 version;      /* IPv6 or IPv4 */
    uint8_t                 id;
    char                    *value;
    ds_dlist_t              d_node;
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
    uint8_t     dhcp_op;
    uint8_t     dhcp_htype;
    uint8_t     dhcp_hlen;
    uint8_t     dhcp_hops;
    uint32_t    dhcp_xid;
    uint16_t    dhcp_secs;
    uint16_t    dhcp_flags;
    os_ipaddr_t dhcp_ciaddr;
    os_ipaddr_t dhcp_yiaddr;
    os_ipaddr_t dhcp_siaddr;
    os_ipaddr_t dhcp_giaddr;
    union
    {
        os_macaddr_t    dhcp_chaddr;
        uint8_t         dhcp_padaddr[16];
    };
    uint8_t     dhcp_server[64];
    uint8_t     dhcp_boot_file[128];
    uint32_t    dhcp_magic;
    uint8_t     dhcp_options[DHCP_MAX_OPTION_LEN];
};

struct dhcp_parser
{
    struct  net_header_parser *net_parser;
    size_t  dhcp_len;
    size_t  parsed;
    size_t  caplen;
    uint8_t *data;
    struct  pcap_pkthdr      header;
    uint8_t relay_options_len;
};

struct dhcp_session
{
    struct fsm_session      *session;
    struct dhcp_parser      parser;
    ds_tree_t               dhcp_leases;
    ds_tree_node_t          session_node;
    bool                    initialized;
    ds_tree_t               dhcp_local_domains;
    size_t                  num_local_domains;

    int                     sock_fd;
    struct sockaddr_ll      raw_dst;
    os_macaddr_t            src_eth_addr;
    bool                    forward_ctxt_initialized;
};

struct dhcp_relay_mgr
{
    bool                    initialized;
    ds_tree_t               fsm_sessions;
    int                     (*set_forward_context)(struct fsm_session *);
    void                    (*forward)(struct dhcp_session *);
};

/******************************************************************************
* Function Declarations
******************************************************************************/
int                         dhcp_relay_plugin_init(struct fsm_session *session);
void                        dhcp_relay_plugin_exit(struct fsm_session *session);

struct      dhcp_relay_mgr *dhcp_get_mgr(void);
struct      dhcp_session   *dhcp_lookup_session(struct fsm_session *session);

size_t                      dhcp_parse_message(struct dhcp_parser *parser);
void                        dhcp_relay_process_dhcpv4_message(struct dhcp_session *d_session);
void                        dhcp_relay_process_dhcpv6_message(struct dhcp_session *d_session);

bool                        dhcp_relay_check_option82(struct dhcp_session *d_session);
bool                        dhcp_relay_check_dhcpv6_option(struct dhcp_session *d_session,
                                                           int dhcpv6_option);

void                        dhcp_prepare_forward(struct dhcp_session *d_session,
                                                 uint8_t *packet);
void                        dhcp_forward(struct dhcp_session *d_session);
ds_dlist_t                 *dhcp_get_relay_options(void);
void                        dhcp_relay_update_headers(struct net_header_parser *net_parser,
                                                      uint8_t *popt, uint16_t size_options);

#endif /* DHCP_RELAY_H_INCLUDED */
