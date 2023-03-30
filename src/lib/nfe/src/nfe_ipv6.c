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

#include "nfe_ipv6.h"
#include "nfe_proto.h"
#include "nfe_priv.h"

#define NEXTHDR_HOP      0
#define NEXTHDR_TCP      6
#define NEXTHDR_UDP      17
#define NEXTHDR_IPV6     41
#define NEXTHDR_ROUTING  43
#define NEXTHDR_FRAGMENT 44
#define NEXTHDR_GRE      47
#define NEXTHDR_ESP      50
#define NEXTHDR_AUTH     51
#define NEXTHDR_ICMP     58
#define NEXTHDR_NONE     59
#define NEXTHDR_DEST     60
#define NEXTHDR_SCTP     132
#define NEXTHDR_MOBILITY 135
#define NEXTHDR_MAX      255
#define NEXTHDR_INVALID  NEXTHDR_MAX

static uint8_t ipv6_skip_extension(const struct ipv6hdr *ip6, uint32_t size, uint32_t *offset)
{
    const struct ipv6hdr_ext *ext;
    
    ext = (const struct ipv6hdr_ext *)((const char *)ip6 + *offset);

    if (ip6->nhdr != NEXTHDR_ESP) {
        uint32_t elen;
        if (ip6->nhdr == NEXTHDR_AUTH) {
            elen = (ext->elen + 2) * 4;
        } else {
            elen = (ext->elen + 1) * 8;
        }
        if (*offset + elen > size) {
            return NEXTHDR_INVALID;
        }
        *offset += elen;
        return ext->nhdr;
    }
    return NEXTHDR_ESP;
}

static inline bool
ipv6_extension(uint8_t value)
{ 
    switch (value)
    {
        case NEXTHDR_HOP:
/*        NEXTHDR_TCP: */
/*        NEXTHDR_UDP: */
/*        NEXTHDR_IPV6: */
        case NEXTHDR_ROUTING: 
/*        NEXTHDR_FRAGMENT: */
/*        NEXTHDR_GRE: */
        case NEXTHDR_ESP:
        case NEXTHDR_AUTH:
/*        NEXTHDR_ICMP: */
/*        NEXTHDR_NONE: */
        case NEXTHDR_DEST:
/*        NEXTHDR_SCTP: */
        case NEXTHDR_MOBILITY:
            return 1;
        default:
            return 0;
    }
};

int
nfe_input_ipv6(struct nfe_packet *p)
{
    const struct ipv6hdr *ip6;
    uint8_t nhdr;
    uint32_t offset, size;

    size = (uint32_t)(p->tail - p->data);
    ip6 = (const struct ipv6hdr *)p->data;

    if (size < sizeof(*ip6))
        return -1;

    ipaddr_copy_in6(&p->tuple.addr[0], &ip6->saddr);
    ipaddr_copy_in6(&p->tuple.addr[1], &ip6->daddr);
    p->tuple.domain = NFE_AF_INET6;

    nhdr = ip6->nhdr;
    offset = sizeof(*ip6);

    while (ipv6_extension(nhdr)) {
        nhdr = ipv6_skip_extension(ip6, size, &offset);
        if (nhdr == NEXTHDR_ESP)
            break;
        if (nhdr == NEXTHDR_INVALID)
            return -1;
    }

    if (nhdr == NEXTHDR_FRAGMENT || nhdr == NEXTHDR_NONE)
        return 0;

    p->data += offset;
    p->tail = p->data + nfe_min(size - offset, (uint32_t)(p->tail - p->data));
    p->prot = p->data;
    p->tuple.proto = nhdr;

    if (p->type == NFE_PACKET_TYPE_UNKNOWN) {
        if (ipaddr_v6_multicast(&p->tuple.addr[1]))
            p->type = NFE_PACKET_TYPE_MULTICAST;
        else if (!ipaddr_unspec(&p->tuple.addr[1]))
            p->type = NFE_PACKET_TYPE_HOST;
    }

    return nfe_proto_handoff(nhdr)(p);
}

/* See ipv4.c nfe_proto_ipv4() for details. */
int
nfe_proto_ipv6(struct nfe_packet *p)
{
    return nfe_input_ipv6(p);
}
