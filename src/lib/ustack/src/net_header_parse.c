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

#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "log.h"
#include "net_header_parse.h"
#include "memutil.h"
#include "os.h"

/**
 * @brief parses the network header parts of a message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 if the advertized size is bigger
 *         than the captured size.
 */
size_t net_header_parse(struct net_header_parser *parser)
{
    uint16_t ethertype;
    int ip_protocol;
    bool is_ip;
    size_t len;

    if (parser == NULL) return 0;

    parser->start = parser->data;
    parser->parsed = 0;

    /* Parse ethernet header */
    len = net_header_parse_eth(parser);
    if (len == 0) return 0;

    /* If not IP, leave the eth payload parsing to the packet owner */
    ethertype = net_header_get_ethertype(parser);
    is_ip = ((ethertype == ETH_P_IP) || (ethertype == ETH_P_IPV6));
    if (!is_ip) return len;

    len = net_header_parse_ip(parser);
    if (len == 0) return 0;

    ip_protocol = parser->ip_protocol;
    if (ip_protocol == IPPROTO_TCP) return net_header_parse_tcp(parser);
    if (ip_protocol == IPPROTO_UDP) return net_header_parse_udp(parser);
    if (ip_protocol == IPPROTO_ICMP) return net_header_parse_icmp(parser);
    if (ip_protocol == IPPROTO_ICMPV6) return net_header_parse_icmp6(parser);

    LOGT("%s: failed to parse protocol %x", __func__, ip_protocol);

    /* Unknown protocol, return 0 to prevent further processing */
    return 0;
}


/**
 * @brief parse the ethernet header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the ethernet header if successful,
 *         0 otherwise
 */
size_t net_header_parse_eth(struct net_header_parser *parser)
{
    struct eth_header *eth_header = &parser->eth_header;
    uint8_t *bytes = parser->data;
    size_t parsed = 0;

    parser->parsed = 0;
    if (parser->packet_len < ETH_HLEN) return 0;

    eth_header->dstmac = (os_macaddr_t *)(parser->data);
    eth_header->srcmac = (os_macaddr_t *)(parser->data + ETH_ALEN);

    parsed = 2 * ETH_ALEN;

    if (parser->pcap_datalink == DLT_LINUX_SLL) parsed += 2;

    eth_header->vlan_id = 0;
    eth_header->ethertype = ntohs(*(uint16_t *)(bytes + parsed));
    if (eth_header->ethertype == ETH_P_8021Q)
    {
        eth_header->vlan_id = ntohs(*(uint16_t *)(bytes + parsed + 2));
        eth_header->vlan_id &= 0xfff;
        parsed += 4;
        eth_header->ethertype = ntohs(*(uint16_t *)(bytes + parsed));
    }
    parsed += 2;

    parser->parsed = parsed;
    parser->data += parsed;

    parser->eth_pld.payload = parser->data;
    parser->source = PKT_SOURCE_PCAP;

    return parsed;
}

/**
 * @brief access the ethernet header of a pcap capture
 *
 * @param parser the parsed data container
 * @return a pointer to the ethernet header if successful,
 *         NULL otherwise
 */
struct eth_header * net_header_get_eth(struct net_header_parser *parser)
{
    return &parser->eth_header;
}


/**
 * @brief get parsed ethertype
 *
 * @param parser the parsed data container
 * @return the parsed ethertype
 */
uint16_t net_header_get_ethertype(struct net_header_parser *parser)
{
    struct eth_header *eth_header;

    eth_header = &parser->eth_header;
    return eth_header->ethertype;
}


/**
 * @brief parse the ip header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the ip header if successful,
 *         0 otherwise
 */
size_t net_header_parse_ip(struct net_header_parser *parser)
{
    int ethertype;

    ethertype = net_header_get_ethertype(parser);

    if (ethertype == ETH_P_IP) return net_header_parse_ipv4(parser);
    if (ethertype == ETH_P_IPV6) return net_header_parse_ipv6(parser);

    return 0;
}


/**
 * @brief parse the ipv4 header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the ipv4 header if successful,
 *         0 otherwise
 */
size_t net_header_parse_ipv4(struct net_header_parser *parser)
{
    struct iphdr *hdr;
    size_t len, ip_hlen, ip_dlen;

    len = parser->packet_len - parser->parsed;
    if (len < sizeof(struct iphdr)) return 0;

    hdr = (struct iphdr *)(parser->data);
    parser->eth_pld.ip.iphdr = hdr;

    /* ip data length as advertized in the ip header */
    ip_hlen = (hdr->ihl * 4);
    ip_dlen = ntohs(hdr->tot_len) - ip_hlen;

    len = len - ip_hlen;

    /* check ip payload length against the captured packet size */
    if (len < ip_dlen) return 0;

    parser->parsed += ip_hlen;
    parser->data += ip_hlen;
    parser->ip_version = 4;
    parser->ip_protocol = hdr->protocol;

    /* Adjust packet length to account for ethernet padding */
    parser->packet_len = parser->parsed + ip_dlen;

    return parser->parsed;
}


#define IPV6_N_EXTS 11
/**
 * @brief _sorted_ array of standard IPv6 extensions headers ids
 */
int ipv6_extensions_ids[IPV6_N_EXTS] =
{
    IPPROTO_HOPOPTS,  	/* IPv6 Hop-by-Hop Option */
    IPPROTO_ROUTING, 	/* Routing Header for IPv6 */
    IPPROTO_FRAGMENT,	/* Fragment Header for IPv6 */
    IPPROTO_ESP, 	/* Encapsulating Security Payload */
    IPPROTO_AH, 	/* Authentication Header */
    IPPROTO_DSTOPTS, 	/* Destination Options for IPv6 */
    135, 	        /* Mobility Header */
    139,	        /* Host Identity Protocol */
    140, 	        /* Shim6 Protocol */
    253, 	        /* Use for experimentation and testing */
    254  	        /* Use for experimentation and testing */
};


static int cmphdr(const void *hdr_a, const void *hdr_b)
{
    return (*(int16_t *)hdr_a - *(int16_t *)hdr_b);
}


static bool is_ipv6_extension(uint8_t id)
{
    int16_t *found;
    int16_t key;

    key = (int16_t)id;
    found = (int16_t *)bsearch(&key, ipv6_extensions_ids, IPV6_N_EXTS,
                               sizeof(int16_t), cmphdr);

    return (found != NULL);
}


/**
 * @brief hop through IPv6 extensions headers
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the ipv6 header extensions if successful,
 *         0 otherwise
 */
size_t net_header_get_ipv6_payload(struct net_header_parser *parser)
{
    struct ip6_hdr *hdr;
    uint8_t next_header;
    bool is_ext;

    hdr = net_header_get_ipv6_hdr(parser);
    if (hdr == NULL) return 0;

    next_header = hdr->ip6_nxt;
    is_ext = is_ipv6_extension(next_header);
    while (is_ext)
    {
        uint8_t ext_len;
        size_t len;

        next_header = (uint8_t)(parser->data[0]);
        ext_len = (uint8_t)(parser->data[1]);
        len = 8 * (ext_len + 1);
        if ((parser->parsed + len) > parser->packet_len) return 0;

        parser->data += len;
        parser->parsed += len;
        is_ext = is_ipv6_extension(next_header);
    }
    parser->ip_protocol = next_header;

    return parser->parsed;
};


/**
 * @brief parse the ipv6 header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the ipv6 header if successful,
 *         0 otherwise
 */
size_t net_header_parse_ipv6(struct net_header_parser *parser)
{
    struct ip6_hdr *hdr;
    size_t len, ip_hlen, ip_dlen;

    len = parser->packet_len - parser->parsed;
    ip_hlen = sizeof(struct ip6_hdr);
    if (len < ip_hlen) return 0;

    hdr = (struct ip6_hdr *)(parser->data);
    parser->eth_pld.ip.ipv6hdr = hdr;
    ip_dlen = ntohs(hdr->ip6_plen);

    len = len - ip_hlen;
    /* check ip payload length against the captured packet size */
    if (len < ip_dlen) return 0;
    parser->parsed += ip_hlen;
    parser->data += ip_hlen;
    parser->ip_version = 6;

    /* Adjust packet length to account for ethernet padding */
    parser->packet_len = parser->parsed + ip_dlen;

    /* Hop through extension headers if any */
    len = net_header_get_ipv6_payload(parser);
    if (len == 0) return 0;


    return parser->parsed;
}


/**
 * @brief access the parser's ipv4 information
 *
 * @param parser the parsed data container
 * @return a pointer to the ipv6 info if present, NULL otherwise
 */
struct iphdr * net_header_get_ipv4_hdr(struct net_header_parser *parser)
{
    int ethertype;
    if (parser == NULL) return NULL;

    ethertype = net_header_get_ethertype(parser);
    if (ethertype != ETH_P_IP) return NULL;

    if (parser->ip_version != 4) return NULL;

    return parser->eth_pld.ip.iphdr;
}


/**
 * @brief access the parser's ipv6 information
 *
 * @param parser the parsed data container
 * @return a pointer to the ipv6 info if present, NULL otherwise
 */
struct ip6_hdr * net_header_get_ipv6_hdr(struct net_header_parser *parser)
{
    int ethertype;
    if (parser == NULL) return NULL;

    ethertype = net_header_get_ethertype(parser);
    if (ethertype != ETH_P_IPV6) return NULL;

    if (parser->ip_version != 6) return NULL;

    return parser->eth_pld.ip.ipv6hdr;
}


/**
 * @brief write the requested ip address as a string in the provided buffer
 *
 * The caller must provide a buffer with enough capacity.
 * @param parser the parsed data container
 * @param bool src reqesting source IP address if true, dest IP address if false
 * @param ip_buf the buffer to be filled with the ip as a string
 * @param ip_buf_len the provided buffer length
 * @return true if the conversion succeeded, false otherwise
 */
bool net_header_ip_str(struct net_header_parser *parser, bool src,
                       char *ip_buf, size_t ip_buf_len)
{
    const void *ip, *ret;

    ip = NULL;
    if (parser->ip_version == 4)
    {
        struct iphdr *hdr = net_header_get_ipv4_hdr(parser);

        if (ip_buf_len < INET_ADDRSTRLEN) return false;
        if (hdr == NULL) return false;

        ip = (src ? &hdr->saddr : &hdr->daddr);
    }
    if (parser->ip_version == 6)
    {
        struct ip6_hdr *hdr = net_header_get_ipv6_hdr(parser);

        if (hdr == NULL) return false;
        if (ip_buf_len < INET6_ADDRSTRLEN) return false;

        ip = (src ? &hdr->ip6_src : &hdr->ip6_dst);
    }
    if (ip == NULL) return false;

    /* Get the source IP as a string for further message content lookup */
    ret = inet_ntop(parser->ip_version == 4 ? AF_INET : AF_INET6,
                    ip, ip_buf, ip_buf_len);

    return (ret != NULL);
}


/**
 * @brief write the parsed ip source address as a string in the provided buffer
 *
 * The caller must provide a buffer with enough capacity.
 * @param parser the parsed data container
 * @param ip_buf the buffer to be filled with the source ip as a string
 * @param ip_buf_len the provided buffer length
 * @param buffer length
 * @return true if the conversion succeeded, false otherwise
 */
bool net_header_srcip_str(struct net_header_parser *parser, char *ip_buf,
                          size_t ip_buf_len)
{
    return net_header_ip_str(parser, true, ip_buf, ip_buf_len);
}


/**
 * @brief write the ip destination address as a string in the provided buffer
 *
 * The caller must provide a buffer with enough capacity.
 * @param parser the parsed data container
 * @param ip_buf the buffer to be filled with the destination ip as a string
 * @return true if the conversion succeeded, false otherwise
 */
bool net_header_dstip_str(struct net_header_parser *parser, char *ip_buf,
                          size_t ip_buf_len)
{
    return net_header_ip_str(parser, false, ip_buf, ip_buf_len);
}


/**
 * @brief parse the tcp header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the tcp header if successful,
 *         0 otherwise
 */
size_t net_header_parse_tcp(struct net_header_parser *parser)
{
    struct tcphdr *hdr;
    size_t len, tcp_hlen;

    hdr = (struct tcphdr *)(parser->data);
    len = parser->packet_len - parser->parsed;
    tcp_hlen = hdr->doff * 4;
    if (len < tcp_hlen) return 0;

    hdr = (struct tcphdr *)(parser->data);
    parser->ip_pld.tcphdr = hdr;

    parser->parsed += tcp_hlen;
    parser->data += tcp_hlen;

    return tcp_hlen;
}


/**
 * @brief parse the udp header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the udp header if successful,
 *         0 otherwise
 */
size_t net_header_parse_udp(struct net_header_parser *parser)
{
    struct udphdr *hdr;
    size_t len, udp_hlen, udp_dlen;

    len = parser->packet_len - parser->parsed;
    udp_hlen = sizeof(*hdr);
    if (len < udp_hlen) return 0;

    hdr = (struct udphdr *)(parser->data);
    udp_dlen = ntohs(hdr->len) - udp_hlen;

    parser->ip_pld.udphdr = hdr;

    parser->parsed += udp_hlen;
    parser->data += udp_hlen;

    len = len - udp_hlen;
    if (len < udp_dlen) return 0;

    return udp_hlen;
}


/**
 * @brief parse the icmp header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the udp header if successful,
 *         0 otherwise
 */
size_t net_header_parse_icmp(struct net_header_parser *parser)
{
    struct icmphdr *hdr;
    size_t len, icmp_hlen;

    len = parser->packet_len - parser->parsed;
    icmp_hlen = sizeof(*hdr);
    if (len < icmp_hlen) return 0;

    hdr = (struct icmphdr *)(parser->data);
    parser->ip_pld.icmphdr = hdr;

    parser->parsed += icmp_hlen;
    parser->data += icmp_hlen;

    return icmp_hlen;
}


/**
 * @brief parse the icmp6 header of a pcap capture
 *
 * @param parser the parsed data container
 * @return the buffer offset passed the udp header if successful,
 *         0 otherwise
 */
size_t net_header_parse_icmp6(struct net_header_parser *parser)
{
    struct icmp6_hdr *hdr;
    size_t len, icmp6_hlen;

    len = parser->packet_len - parser->parsed;
    icmp6_hlen = sizeof(*hdr);
    if (len < icmp6_hlen) return 0;

    hdr = (struct icmp6_hdr *)(parser->data);
    parser->ip_pld.icmp6hdr = hdr;

    parser->parsed += icmp6_hlen;
    parser->data += icmp6_hlen;

    // Avoid processing MLD packets.
    if (hdr->icmp6_type == 143) return 0;

    return icmp6_hlen;
}


/**
 * @brief logs the network header info
 *
 * @param log_level the requested log level
 * @param parser the parsed data container
 * @return none
 *
 * Logs the ethernet, ip and transport info when available
 * at the requested log level
 */
void
net_header_log(int log_level, struct net_header_parser *parser)
{
    size_t len;
    char *buf;

    if (parser == NULL)
    {
        LOG_SEVERITY(log_level, "%s: net header pointer is NULL", __func__);
        return;
    }

    len = 128 + (2 * INET6_ADDRSTRLEN + 128) + 256;
    buf = CALLOC(1, len);
    if (buf == NULL)
    {
        LOG_SEVERITY(log_level, "%s: could not allocate string", __func__);
        return;
    }

    LOG_SEVERITY(log_level, "%s: %s", __func__,
                 net_header_fill_buf(buf, len, parser));
    FREE(buf);
}

char *
dir2str(uint8_t direction)
{
    switch (direction)
    {
        case NET_MD_ACC_INBOUND_DIR     : return "inbound";
        case NET_MD_ACC_OUTBOUND_DIR    : return "outbound";
        case NET_MD_ACC_LAN2LAN_DIR     : return "lan2lan";
        case NET_MD_ACC_UNSET_DIR       :
        default:                          return "unspecified";
    }
}

char *
orig2str(uint8_t originator)
{
    switch (originator)
    {
        case NET_MD_ACC_ORIGINATOR_SRC      : return "source";
        case NET_MD_ACC_ORIGINATOR_DST      : return "destination";
        case NET_MD_ACC_UNKNOWN_ORIGINATOR  :
        default:                              return "unspecified";
    }
}

static char *
net_header_tap2str(struct net_header_parser *parser)
{
    char *ret;

    ret = "invalid";

    if (parser == NULL) return "header NULL";

    switch (parser->source)
    {
        case PKT_SOURCE_NONE:
        {
            ret = "none";
            break;
        }
        case PKT_SOURCE_PCAP:
        {
            ret = "pcap";
            break;
        }
        case PKT_SOURCE_NFQ:
        {
            ret = "nfqueue";
            break;
        }
        case PKT_SOURCE_SOCKET:
        {
            ret = "socket";
            break;
        }
        default:
        {
            ret = "invalid";
            break;
        }
    }
    return ret;
};

static void
net_header_src_info(struct net_header_parser *parser, char *info, size_t len)
{
    if (parser == NULL) return;

    switch (parser->source)
    {
        case PKT_SOURCE_PCAP:
        {
            if (parser->tap_intf)
            {
                snprintf(info, len, "%s", parser->tap_intf);
            }
            break;
        }
        case PKT_SOURCE_NFQ:
        {
            snprintf(info, len, "queue num %d", parser->nfq_queue_num);
        }
        default:
        {
            break;
        }
    }
}


/**
 * @brief fills the given string with the network header content
 *
 * @param buf the buffer to fill
 * @param len the buffer length
 * @param parser the parsed data container
 * @return a pointer to the buffer passed in argument
 *
 * Logs the ethernet, ip and transport info when available
 */
char *
net_header_fill_buf(char *buf, size_t len, struct net_header_parser *parser)
{
    struct net_md_stats_accumulator *acc;
    char ip_pres[2 * INET6_ADDRSTRLEN + 128];
    char ip_src[INET6_ADDRSTRLEN];
    char ip_dst[INET6_ADDRSTRLEN];
    struct eth_header *eth;
    char pkt_id_pres[256];
    uint8_t has_src_mac;
    uint8_t has_dst_mac;
    char flow_pres[256];
    char eth_pres[256];
    char tpt_pres[256];
    char src_info[256];
    uint8_t eth_switch;
    uint16_t ethertype;
    bool has_ip;

    eth = net_header_get_eth(parser);
    MEMZERO(eth_pres);
    MEMZERO(ip_pres);
    MEMZERO(tpt_pres);
    MEMZERO(flow_pres);
    MEMZERO(pkt_id_pres);
    memset(buf, 0, len);

    /* Prepare ethernet presentation */
    has_src_mac = eth->srcmac ? 1 : 0;
    has_dst_mac = eth->dstmac ? 2 : 0;
    eth_switch = (has_src_mac | has_dst_mac);
    switch (eth_switch)
    {
        case 3:
            snprintf(eth_pres, sizeof(eth_pres),
                     "ETH: src: " PRI_os_macaddr_lower_t
                     ", dst: " PRI_os_macaddr_lower_t
                     ", ethertype 0x%X, vlan id %u",
                     FMT_os_macaddr_pt(eth->srcmac),
                     FMT_os_macaddr_pt(eth->dstmac),
                     net_header_get_ethertype(parser), eth->vlan_id);
            break;

        case 2:
            snprintf(eth_pres, sizeof(eth_pres),
                     "ETH: src not set"
                     ", dst: " PRI_os_macaddr_lower_t
                     ", ethertype 0x%X, vlan id %u",
                     FMT_os_macaddr_pt(eth->dstmac),
                     net_header_get_ethertype(parser), eth->vlan_id);
            break;

        case 1:
            snprintf(eth_pres, sizeof(eth_pres),
                     "ETH: src: " PRI_os_macaddr_lower_t
                     ", dst not set"
                     ", ethertype 0x%X, vlan id %u",
                     FMT_os_macaddr_pt(eth->srcmac),
                     net_header_get_ethertype(parser), eth->vlan_id);
            break;

        case 0:
            snprintf(eth_pres, sizeof(eth_pres),
                     "ETH: N/A");
            break;
    }

    ethertype = net_header_get_ethertype(parser);
    if (ethertype != ETH_P_IP && ethertype != ETH_P_IPV6) goto out;

    /* Prepare ip presentation */
    has_ip = net_header_srcip_str(parser, ip_src, sizeof(ip_src));
    has_ip &= net_header_dstip_str(parser, ip_dst, sizeof(ip_dst));
    if (has_ip)
    {
        snprintf(ip_pres, sizeof(ip_pres),
                 ", IP: src: %s, dst: %s, protocol %d",
                 ip_src, ip_dst, parser->ip_protocol);
    }

    /* Prepare transport presentation */
    if (parser->ip_protocol == IPPROTO_TCP)
    {
        struct tcphdr *tcph;

        tcph = parser->ip_pld.tcphdr;

        snprintf(tpt_pres, sizeof(tpt_pres),
                 ", TCP: sport %u, dport %u, seq %u, ack_seq %u, "
                 "syn %u, ack %u, push %u, rst %u, fin %u",
                 ntohs(tcph->source), ntohs(tcph->dest),
                 ntohl(tcph->seq), ntohl(tcph->ack_seq),
                 tcph->syn, tcph->ack, tcph->psh, tcph->rst, tcph->fin);
    }
    else if (parser->ip_protocol == IPPROTO_UDP)
    {
        struct udphdr *udph;

        udph = parser->ip_pld.udphdr;

        snprintf(tpt_pres, sizeof(tpt_pres),
                 ", UDP: sport %u, dport %u",
                 ntohs(udph->source), ntohs(udph->dest));
    }
    else if (parser->ip_protocol == IPPROTO_ICMP)
    {
        struct icmphdr *icmph;
        icmph = parser->ip_pld.icmphdr;
        snprintf(tpt_pres, sizeof(tpt_pres),
                 ", ICMP: type %u, idt 0x%x", icmph->type, ntohs(icmph->un.echo.id));
    }
    else if (parser->ip_protocol == IPPROTO_ICMPV6)
    {
        struct icmp6_hdr *icmp6h;
        icmp6h = parser->ip_pld.icmp6hdr;
        snprintf(tpt_pres, sizeof(tpt_pres),
                 ", ICMP: type %u, idt 0x%x", icmp6h->icmp6_type,
                 ntohs(icmp6h->icmp6_dataun.icmp6_un_data16[0]));
    }

    /* Prepare flow presentation */
    MEMZERO(src_info);
    net_header_src_info(parser, src_info, sizeof(src_info));
    acc = parser->acc;
    if (acc != NULL)
    {
        snprintf(flow_pres, sizeof(flow_pres), ", FLOW: direction: %s, "
                 "originator: %s, tap : %s %s", dir2str(acc->direction),
                 orig2str(acc->originator),
                 net_header_tap2str(parser),
                 src_info);
    }
    else
    {
        snprintf(flow_pres, sizeof(flow_pres), ", tap : %s %s",
                 net_header_tap2str(parser),
                 src_info);
    }

    MEMZERO(pkt_id_pres);
    if (parser->source == PKT_SOURCE_SOCKET)
    {
        snprintf(pkt_id_pres, sizeof(pkt_id_pres), ", packet id: %d",
                 parser->packet_id);
    }

out:
    snprintf(buf, len, "%s%s%s%s%s", eth_pres, ip_pres, tpt_pres, flow_pres,
             pkt_id_pres);

    return buf;
}

static os_macaddr_t ipv4_mcast_mac =
{
    .addr =
    {
        [0] = 0x01,
        [1] = 0x00,
        [2] = 0x5e,
        [3] = 0x00,
        [4] = 0x00,
        [5] = 0x00,
    }
};

static os_macaddr_t ipv6_mcast_mac =
{
    .addr =
    {
        [0] = 0x33,
        [1] = 0x33,
        [2] = 0x00,
        [3] = 0x00,
        [4] = 0x00,
        [5] = 0x00,
    }
};

static bool
net_header_is_mac_mcast(os_macaddr_t *mac, int ethertype)
{
    os_macaddr_t *mcast_mac;
    size_t limit;
    size_t i;
    bool rc;

    if (mac == NULL) return false;

    limit = 0;
    mcast_mac = NULL;
    if (ethertype == ETH_P_IP)
    {
        limit = 3;
        mcast_mac = &ipv4_mcast_mac;
    }
    else if (ethertype == ETH_P_IPV6)
    {
        limit = 2;
        mcast_mac = &ipv6_mcast_mac;
    }
    else
    {
        return false;
    }

    rc = true;
    for (i = 0; i < limit; i++)
    {
        if (mac->addr[i] != mcast_mac->addr[i]) return false;
    }
    return rc;
}

bool
net_header_is_mcast(struct net_header_parser *header)
{
    struct eth_header *eth;
    os_macaddr_t *mac;
    int ethertype;
    bool rc;

    if (header == NULL) return false;

    ethertype = net_header_get_ethertype(header);
    if ((ethertype != ETH_P_IP) && (ethertype != ETH_P_IPV6)) return false;

    /* Get the ethernet header */
    eth = net_header_get_eth(header);
    if (eth == NULL) return false;

    /* Check the source mac address */
    mac = eth->srcmac;
    if (mac == NULL) return false;
    rc = net_header_is_mac_mcast(mac, ethertype);

    /* Check the destination mac address */
    mac = eth->dstmac;
    if (mac == NULL) return false;
    rc |= net_header_is_mac_mcast(mac, ethertype);

    return rc;
}
