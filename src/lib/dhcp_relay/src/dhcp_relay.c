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

#include<stdio.h>
#include<stdlib.h>
#include<stddef.h>
#include<string.h>
#include<net/if.h>

#include<const.h>
#include<log.h>
#include<assert.h>
#include<ovsdb.h>
#include<target.h>
#include<memutil.h>

#include "kconfig.h"
#include "fsm_csum_utils.h"
#include "dhcp_relay.h"

static struct dhcp_relay_mgr dhcp_mgr =
{
    .initialized = false,
};

struct dhcp_relay_mgr *dhcp_get_mgr(void)
{
    return &dhcp_mgr;
}

static ds_dlist_t dhcp_relay_options;

ds_dlist_t *dhcp_get_relay_options(void)
{
    return &dhcp_relay_options;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int dhcp_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    return (p_a - p_b);
}

/**
 * checksum Return the checksum of a specific buffer
 */

uint32_t checksum(unsigned char *buf, unsigned nbytes, uint32_t sum)
{
    unsigned i;

    /* Checksum all the pairs of bytes first... */
    for (i = 0; i < (nbytes & ~1U); i += 2)
    {
        sum += (uint16_t)ntohs(*((uint16_t *)(buf + i)));
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }

    /* If there's a single byte left over, checksum it, too.   Network
    byte order is big-endian, so the remaining byte is the high byte. */

    if (i < nbytes)
    {
        sum += buf[i] << 8;
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }

    return (sum);
}

/**
 *  wrapsum: Return the wrapsum
 */

uint32_t wrapsum(uint32_t sum)
{
    sum = ~sum & 0xFFFF;
    return (htons(sum));
}

/**
 *  Update packet Headers
 */
void
dhcp_relay_update_headers(struct net_header_parser *net_parser, uint8_t *popt, uint16_t size_options)
{
    struct iphdr                *ip_hdr;
    struct ip6_hdr              *ip6_hdr;
    struct udphdr               *udp_hdr;
    struct dhcp_hdr             *dhcp;
    unsigned                    dhcp_len = 0;

    if(size_options == 0) return;

    udp_hdr = net_parser->ip_pld.udphdr;

    udp_hdr->len = htons(ntohs(udp_hdr->len) + size_options);
    udp_hdr->check = 0;

    if (net_parser->ip_version == 4)
    {
        /* Extract DHCPv4 struct */
        dhcp = (void *)(net_parser->data);
        dhcp_len = popt - (unsigned char *)dhcp;
        /* Update IP header Checksum */
        ip_hdr = net_header_get_ipv4_hdr(net_parser);
        ip_hdr->check = 0;
        ip_hdr->tot_len = htons(ntohs(ip_hdr->tot_len) + size_options);
        ip_hdr->check = wrapsum(checksum((unsigned char *)ip_hdr, ((uint8_t)ip_hdr->ihl) * 4 , 0));

        /* Update UDP header Checksum */
        udp_hdr->check = wrapsum(checksum((unsigned char *)udp_hdr, sizeof(udp_hdr),
                        checksum((unsigned char *)dhcp, dhcp_len, checksum( (unsigned char *)&ip_hdr->saddr ,
                                2 * sizeof(&ip_hdr->saddr), IPPROTO_UDP +
                                 (u_int32_t)ntohs(udp_hdr->len)))));
    }
    else if (net_parser->ip_version == 6)
    {
        /* IPv6 : Calculate the checksum TO DO*/
        ip6_hdr = net_header_get_ipv6_hdr(net_parser);
        ip6_hdr->ip6_plen = htons(ntohs(ip6_hdr->ip6_plen) + size_options);
    }

    return;
}

/*
 * Add a relay option to the injection list. Returns a pointer to the option.
 */

static struct dhcp_relay_conf_options *
dhcp_relay_relay_options_store(ds_dlist_t* options,
                               int ip_version,
                               int option_id,
                               const char* value)
{
    struct dhcp_relay_conf_options  *opt = NULL;
    char                            *value_copy = NULL;

    value_copy = STRDUP(value);
    if (value_copy == NULL) {
        return NULL;
    }

    opt = CALLOC(1, sizeof(struct dhcp_relay_conf_options));
    if (opt == NULL)
    {
        LOGE("%s: Unable to allocate dhcp_relay_conf_options!", __func__);
        FREE(value_copy);
        return NULL;
    }

    opt->version = ip_version;
    opt->id = option_id;
    opt->value = value_copy;

    ds_dlist_insert_tail(options, opt);
    LOGT("%s: Storing id '%hhu' version: '%hhu' value: '%s'",
            __func__, opt->id, opt->version, opt->value);

    return opt;
}

/**
 * dhcp_relay_options_init: dso initialization entry point
 */

static int
dhcp_relay_relay_options_init(void)
{
    char        buf[300];
    FILE        *conf = NULL;
    ds_dlist_t  *options = NULL;
    int         rc;

    /* Init DHCP Options Relay */
    options = dhcp_get_relay_options();
    ds_dlist_init(options, struct dhcp_relay_conf_options, d_node);

    /* Open the options file */
    conf = fopen(DHCP_RELAY_CONF_FILE, "r");
    if (!conf)
    {
        LOGW("%s: Cannot open DHCP Relay Conf file '%s', errno: '%s'",
             __func__, DHCP_RELAY_CONF_FILE, strerror(errno));
        return -1;
    }

    while (fgets(buf, sizeof(buf), conf) != NULL)
    {
        int version, id;
        char *p = buf;
        char *str = NULL;
        char *value = NULL;

        /* Check IPv6 or IPv4 option and select version */
        rc = strncmp(p, "DHCPv4_OPTION:", strlen("DHCPv4_OPTION:"));
        if (!rc)
        {
            /* Skip the "export keyword" */
            p += strlen("DHCPv4_OPTION:");
            version = 4;
        }
        else
        {
            /* Skip the "export keyword" */
            p += strlen("DHCPv6_OPTION:");
            version = 6;
        }

        str = strsep(&p, "=");
        if (str == NULL)
        {
            continue;
        }

        id = atoi(str);

        /* Skip the first ' */
        value = strsep(&p, "'");
        if (value == NULL || value[0] != '\0')
        {
            continue;
        }

        value = strsep(&p, "'");
        if (dhcp_relay_relay_options_store(options, version, id, value) == NULL)
        {
            return -1;
        }
   }

   return 1;
}

/*************************************************************************************************/
// DHCP packet re-injection framework

void
dhcp_prepare_forward(struct dhcp_session *d_session,
                     uint8_t *packet)
{
    struct net_header_parser    *net_parser;
    struct dhcp_parser          *parser;
    struct eth_header           *eth_header;
    struct udphdr               *udp_hdr;
    char                        src_ip[128] = { 0 };
    char                        dst_ip[128] = { 0 };

    if (d_session == NULL) return;

    parser = &d_session->parser;
    net_parser = parser->net_parser;

    eth_header = net_header_get_eth(net_parser);
    LOGD("%s source mac: " PRI_os_macaddr_lower_t ", dst mac: " PRI_os_macaddr_lower_t,
         __func__,
         FMT_os_macaddr_pt(eth_header->srcmac),
         FMT_os_macaddr_pt(eth_header->dstmac));

    udp_hdr = net_parser->ip_pld.udphdr;
    udp_hdr->check = 0;

    if (net_parser->ip_version == 4)
    {
        /* UDP checksum to 0 */
        (void)net_header_dstip_str(net_parser, dst_ip, sizeof(dst_ip));
        LOGD("%s: dst address %s, dst_port %d, checksum 0x%x",
             __func__, dst_ip, ntohs(udp_hdr->dest), udp_hdr->check);

        (void)net_header_srcip_str(net_parser, src_ip, sizeof(src_ip));
        LOGD("%s: src address %s, src port %d", __func__, src_ip, ntohs(udp_hdr->source));
    }
    else if (net_parser->ip_version == 6)
    {
        /* IPv6 */
        uint16_t csum = fsm_compute_udp_checksum(packet, net_parser);
        udp_hdr->check = htons(csum);
    }

    memcpy(d_session->raw_dst.sll_addr, eth_header->dstmac, sizeof(os_macaddr_t));
    if (kconfig_enabled(DHCP_RELAY_SPOOF_SOURCE_MAC)) {
        memcpy(packet+6, d_session->src_eth_addr.addr, sizeof(d_session->src_eth_addr.addr));
    }
}

void
dhcp_forward(struct dhcp_session *d_session)
{
    struct dhcp_relay_mgr       *mgr;
    struct net_header_parser    *net_parser;
    struct dhcp_parser          *parser;
    struct pcap_pkthdr          *header;
    uint8_t                     *packet;
    int                          len, rc = 0;

    mgr = dhcp_get_mgr();
    if (!d_session->forward_ctxt_initialized)
    {
        rc = mgr->set_forward_context(d_session->session);
        if (rc != 0)
        {
            LOGE("%s: setting packet forwarding context failed", __func__);
            return;
        }

        d_session->forward_ctxt_initialized = true;
    }

    parser     = &d_session->parser;
    net_parser = parser->net_parser;
    header     = &parser->header;
    len        = header->caplen;

    packet = (uint8_t *)net_parser->start;

    /* Prepare the packet for forwarding */
    dhcp_prepare_forward(d_session, packet);

    rc = sendto(d_session->sock_fd, packet, len, 0,
                (struct sockaddr *)&d_session->raw_dst,
                sizeof(d_session->raw_dst));
    if (rc < 0)
    {
        LOGE("%s: could not forward DHCP packet (%s)", __func__, strerror(errno));
    }
}

/*************************************************************************************************/
// DHCP content and message parsing

static size_t
dhcp_parse_content(struct dhcp_parser *parser)
{
    struct net_header_parser   *net_parser;
    uint16_t                    ethertype;
    int                         ip_protocol;
    struct                      udphdr *hdr;

    if (parser == NULL) return 0;

    net_parser = parser->net_parser;

    // Check ethertype
    ethertype = net_header_get_ethertype(net_parser);
    if ((ethertype != ETH_P_IP) && (ethertype != ETH_P_IPV6)) return 0;

    // Check for IP protocol
    if ((net_parser->ip_version != 4) && (net_parser->ip_version != 6)) return 0;

    // Check for UDP protocol
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_UDP) return 0;

    // Check the UDP src and dst ports
    hdr = net_parser->ip_pld.udphdr;
    if (((ntohs(hdr->source) != 68  || ntohs(hdr->dest) != 67)  &&
        (ntohs(hdr->source) != 67   || ntohs(hdr->dest) != 68)) &&
        ((ntohs(hdr->source) != 546 || ntohs(hdr->dest) != 547) &&
        (ntohs(hdr->source) != 547  || ntohs(hdr->dest) != 546)))
    {
        LOGD("%s: dhcp_parse_content: UDP src/dst port range mismatch,"
             " source port:%d, dest port:%d",
             __func__, ntohs(hdr->source), ntohs(hdr->dest));
        return 0;
    }

    return parser->dhcp_len;
}

/**
 * @brief parses a dhcp message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
dhcp_parse_message(struct dhcp_parser *parser)
{
    struct net_header_parser    *net_parser;
    size_t                      len;
    int                         ip_protocol;

    if (parser == NULL) return 0;

    net_parser = parser->net_parser;

    /* Some basic validation */
    ip_protocol = net_parser->ip_protocol;
    if (ip_protocol != IPPROTO_UDP) return 0;

    /* Parse the dhcp content */
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;
    parser->dhcp_len = net_parser->packet_len - net_parser->parsed;
    len = dhcp_parse_content(parser);
    if (len == 0) return 0;

    return len;
}

void
dhcp_handler(struct fsm_session *session, struct net_header_parser *net_parser)
{
    struct dhcp_session         *d_session;
    struct dhcp_parser          *parser;
    struct dhcp_relay_mgr       *mgr;
    size_t                      len;

    d_session = (struct dhcp_session *)session->handler_ctxt;

    mgr = dhcp_get_mgr();

    parser = &d_session->parser;
    parser->caplen = net_parser->caplen;

    parser->net_parser = net_parser;
    len = dhcp_parse_message(parser);
    if (len == 0) return;

    /* Process the DHCP message, and insert required options */
    if (net_parser->ip_version == 4)     dhcp_relay_process_dhcpv4_message(d_session);
    else                                 dhcp_relay_process_dhcpv6_message(d_session);

    /* Forward the packet */
    mgr->forward(d_session);

    return;
}

void dhcp_periodic(struct fsm_session *session)
{
    struct dhcp_relay_mgr *mgr = dhcp_get_mgr();

    if (!mgr->initialized) return;
}

/*************************************************************************************************/
/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dhcp_session *
dhcp_lookup_session(struct fsm_session *session)
{
    struct dhcp_relay_mgr   *mgr;
    struct dhcp_session     *d_session;
    ds_tree_t               *sessions;

    mgr       = dhcp_get_mgr();
    sessions  = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session != NULL) return d_session;

    LOGD("%s: Adding new session %s", __func__, session->name);

    d_session = CALLOC(1, sizeof(struct dhcp_session));
    if (d_session == NULL) return NULL;

    ds_tree_insert(sessions, d_session, session);

    return d_session;
}

int
dhcp_set_forward_context(struct fsm_session *session)
{
    struct dhcp_session     *dhcp_session;
    struct ifreq            ifreq_c;
    struct ifreq            ifr_i;

    dhcp_session = dhcp_lookup_session(session);
    if (!dhcp_session)
    {
        LOGE("%s: dhcp_session is NULL", __func__);
        return -1;
    }

    /* Open raw socket to re-inject DHCP packets */
    dhcp_session->sock_fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (dhcp_session->sock_fd < 0)
    {
        LOGE("%s: socket() failed (%s)", __func__, strerror(errno));
        return -1;
    }
    memset(&ifr_i, 0, sizeof(ifr_i));
    STRSCPY(ifr_i.ifr_name, session->tx_intf);

    if ((ioctl(dhcp_session->sock_fd, SIOCGIFINDEX, &ifr_i)) < 0)
    {
        LOGE("%s: error in index ioctl reading (%s)", __func__, strerror(errno));
        return -1;
    }

    dhcp_session->raw_dst.sll_family = PF_PACKET;
    dhcp_session->raw_dst.sll_ifindex = ifr_i.ifr_ifindex;
    dhcp_session->raw_dst.sll_halen = ETH_ALEN;

    memset(&ifreq_c, 0, sizeof(ifreq_c));
    STRSCPY(ifreq_c.ifr_name, session->tx_intf);

    if ((ioctl(dhcp_session->sock_fd, SIOCGIFHWADDR, &ifreq_c)) < 0)
    {
        LOGE("%s: error in SIOCGIFHWADDR ioctl reading (%s)", __func__, strerror(errno));
        return -1;
    }

    memcpy(dhcp_session->src_eth_addr.addr, ifreq_c.ifr_hwaddr.sa_data, 6);

    return 0;
}

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the dhcp session to delete
 */
static void
dhcp_delete_session(struct fsm_session *session)
{
    struct dhcp_relay_mgr             *mgr;
    struct dhcp_session               *d_session;
    ds_tree_t                         *sessions;
    ds_dlist_iter_t                   iter;
    ds_dlist_t                        *relay_options;
    struct dhcp_relay_conf_options    *opt = NULL;

    mgr = dhcp_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, d_session);

    close(d_session->sock_fd);

    // Free DHCP relay option conf
    relay_options = dhcp_get_relay_options();

    for (   opt = ds_dlist_ifirst(&iter, relay_options);
            opt != NULL;
            opt = ds_dlist_inext(&iter))
    {
        ds_dlist_iremove(&iter);
        FREE(opt->value);
        FREE(opt);
    }

    FREE(d_session);

    return;
}

/**
 * @brief dhcp_relay plugin exit callback
 *
 * @param session the fsm session container
 * @return none
 */
void dhcp_relay_plugin_exit(struct fsm_session *session)
{
    struct dhcp_relay_mgr *mgr;

    mgr = dhcp_get_mgr();
    if (!mgr->initialized) return;

    dhcp_delete_session(session);
}

void
dhcp_mgr_init(void)
{
    struct dhcp_relay_mgr   *mgr;

    mgr = dhcp_get_mgr();
    if (mgr->initialized)   return;

    ds_tree_init(&mgr->fsm_sessions, dhcp_session_cmp,
            struct dhcp_session, session_node);

    mgr->set_forward_context = dhcp_set_forward_context;
    mgr->forward             = dhcp_forward;

    mgr->initialized = true;
}

/**
 * dhcp_relay_plugin_init: dso initialization entry point
 * @session: session pointer provided by fsm
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 */
int dhcp_relay_plugin_init(struct fsm_session *session)
{
    struct  dhcp_session        *dhcp_session;
    struct  fsm_parser_ops      *parser_ops;
    int                         rc = 0;

    /* Initialize the manager on first call */
    dhcp_mgr_init();

    /* Initialize the config of relay options */
    rc = dhcp_relay_relay_options_init();
    if (rc < 0)
    {
        LOGE("%s: Error in initializing DHCP relay options", __func__);
        return -1;
    }

    /* Look up the dhcp session */
    dhcp_session = dhcp_lookup_session(session);
    if (dhcp_session == NULL)
    {
        LOGE("%s: could not allocate dhcp parser", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (dhcp_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = dhcp_periodic;
    session->ops.exit     = dhcp_relay_plugin_exit;
    session->handler_ctxt = dhcp_session;

    /* Set the plugin specific ops */
    parser_ops            = &session->p_ops->parser_ops;
    parser_ops->handler   = dhcp_handler;

    /* Wrap up the dhcp session initialization */
    dhcp_session->session = session;

    dhcp_session->num_local_domains = 0;
    dhcp_session->initialized = true;

    LOGD("%s: added session %s", __func__, session->name);

    return 0;
}
