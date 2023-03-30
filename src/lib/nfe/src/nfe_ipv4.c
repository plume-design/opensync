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

#include "nfe_ipv4.h"
#include "nfe_ipaddr.h"
#include "nfe_proto.h"
#include "nfe_priv.h"

int
nfe_input_ipv4(struct nfe_packet *p)
{
    struct ipv4hdr *ip4;
    uint16_t correct_len;

    nfe_assert(p);

    if ((size_t)(p->tail - p->data) < sizeof(*ip4))
        return -1;

    ip4 = (struct ipv4hdr *)p->data;

    if (ip4->frag_off & htobe16(IP_MF | IP_OFFSET))
        return -1;

    correct_len = be16toh(ip4->tot_len);

    p->tail = p->data + nfe_min(correct_len, (uint16_t)(p->tail - p->data));
    p->data += (IPV4_IHL(ip4) << 2);
    p->prot = p->data;

    ipaddr_copy_in4(&p->tuple.addr[0], &ip4->saddr);
    ipaddr_copy_in4(&p->tuple.addr[1], &ip4->daddr);
    p->tuple.domain = NFE_AF_INET;
    p->tuple.proto = ip4->proto;

    if (p->type == NFE_PACKET_TYPE_UNKNOWN) {
        if (ipaddr_v4_multicast(&p->tuple.addr[1]))
            p->type = NFE_PACKET_TYPE_MULTICAST;
        else if (!ipaddr_unspec(&p->tuple.addr[1]))
            p->type = NFE_PACKET_TYPE_HOST;
    }

    return nfe_proto_handoff(ip4->proto)(p);
}

/*
 * This path handles arrival thru another layer 3 encapsulation. The options are:
 *
 * 1.) Queue the next handler and payload and return immediately. If the caller wants
 *     to process the packet (which will contain encapsulation and ntuple data specific
 *     to how far up this packet we are), they can. This has the nice side effect of
 *     letting the user determine whether the tunnel is meaningful or not.
 *
 * 2.) Add a tunnel member to the packet struct, and mark the packet as belonging to
 *     a tunnel, but continue. This is interesting, but outer encapsulation data would
 *     be lost.
 *
 * 3.) Do nothing, overwrite the outer encapsulation ntuple, and continue on.
 *
 */ 
int
nfe_proto_ipv4(struct nfe_packet *p)
{
    return nfe_input_ipv4(p);
}
