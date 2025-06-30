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

#ifndef NFE_ETHER_H
#define NFE_ETHER_H

#define ETH_P_IP     0x0800
#define ETH_P_IPV6   0x86DD
#define ETH_P_8021Q  0x8100
#define ETH_P_ARP    0x0806

#define ETH_ALEN 6

struct nfe_packet;
struct nfe_conntrack;

int nfe_input_eth(struct nfe_packet *p);
int nfe_proto_eth(struct nfe_packet *p);
struct nfe_conn *nfe_eth_bypass(struct nfe_conntrack *ct, struct nfe_packet *p);

struct nfe_conn *
nfe_ether_lookup(struct nfe_conntrack *conntrack, struct nfe_packet *packet);

struct ethhdr {
    unsigned char dst[ETH_ALEN];
    unsigned char src[ETH_ALEN];
    unsigned short type;
};

static inline unsigned short
eth_type_from_ip(unsigned char byte)
{
    byte = ((byte & 0xf0) >> 4);
    if (byte == 4)
        return ETH_P_IP;
    if (byte == 6)
        return ETH_P_IPV6;
    return 0;
}

#endif
