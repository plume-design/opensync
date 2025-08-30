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

#include <string.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>

#include "log.h"
#include "fsm_csum_utils.h"

static uint32_t
fsm_compute_pseudo_iph_checksum(uint8_t *packet, struct net_header_parser *net_parser)
{
    uint32_t            sum = 0;
    struct   udphdr     *udp_hdr;
    uint16_t            udp_len;

    udp_hdr = net_parser->ip_pld.udphdr;
    udp_len = htons(udp_hdr->len);

    if (net_parser->ip_version == 4)
    {
        struct iphdr *ip_hdr = net_parser->eth_pld.ip.iphdr;
        uint32_t ip_addr = ip_hdr->saddr;
        sum += (ip_addr >> 16) & 0xffff;
        sum += ip_addr & 0xffff;

        ip_addr = ip_hdr->daddr;
        sum += (ip_addr >> 16) & 0xffff;
        sum += ip_addr & 0xffff;
    }
    else
    {   /*  IPv6 */
        struct ip6_hdr *ip_hdr = net_parser->eth_pld.ip.ipv6hdr;

        uint16_t *ip_addr = (uint16_t *)ip_hdr->ip6_src.s6_addr16;
        int i;

        for (i = 0; i < 8; i++)
        {
            sum += *ip_addr++;
        }

        ip_addr = (uint16_t *)ip_hdr->ip6_dst.s6_addr16;
        for (i = 0; i < 8; i++)
        {
            sum += *ip_addr++;
        }
    }

    sum += htons(IPPROTO_UDP);
    sum += htons(udp_len);

    udp_hdr->check = 0;
    return sum;
}

uint16_t
fsm_compute_udp_checksum(uint8_t *packet, struct net_header_parser *net_parser)
{
    uint32_t        sum = 0;
    uint16_t        csum = 0;
    struct   udphdr *udp_hdr;
    uint16_t        udp_len;

    udp_hdr = net_parser->ip_pld.udphdr;
    udp_len = htons(udp_hdr->len);

    uint16_t *udp_hdr_pos = (uint16_t *)(net_parser->ip_pld.udphdr);

    sum = fsm_compute_pseudo_iph_checksum(packet, net_parser);
    while (udp_len > 1)
    {
        sum += *udp_hdr_pos++;
        udp_len -= 2;
    }

    if(udp_len > 0)
    {
        sum += *udp_hdr_pos & htons(0xff00);
    }

    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    csum = sum;
    csum = ~csum;

    return (csum == 0 ? 0xffff : csum);
}

/**
 * Calculate the IP checksum for the given IP header.
 *
 * @param ip_header The IP header to calculate the checksum for.
 * @return The calculated checksum.
 */
uint16_t fsm_compute_ip_checksum(struct iphdr *ip_header)
{
    uint8_t *header;
    uint32_t sum;
    int len;

    len = (ip_header->ihl) * 4;
    header = (uint8_t *)ip_header;
    sum = 0;

    /* Calculate the sum of each 16-bit word in the IP header */
    for (size_t i = 0; i < (size_t)len; i += 2)
    {
        uint16_t word = (header[i] << 8) | header[i + 1];
        sum += word;
    }

    /* handle odd-length header */
    if (len & 1) {
        sum += (header[len - 1] << 8);
    }

    /* fold the 32 bit sum into 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }

    return ~sum;
}
