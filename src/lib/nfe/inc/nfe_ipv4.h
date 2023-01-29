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

#ifndef NFE_IPV4_H
#define NFE_IPV4_H

struct ipv4hdr {
    unsigned char   ihl_ver;
    unsigned char   tos;
    unsigned short  tot_len;
    unsigned short  id;
    unsigned short  frag_off;
    unsigned char   ttl;
    unsigned char   proto;
    unsigned short  csum;
    unsigned int    saddr;
    unsigned int    daddr;
};

#define IPV4_IHL(iph) (((iph)->ihl_ver & 0x0f))
#define IPV4_VER(iph) (((iph)->ihl_ver & 0xf0) >> 4)
#define IP_CE        0x8000
#define IP_DF        0x4000
#define IP_MF        0x2000
#define IP_OFFSET    0x1FFF

struct nfe_packet;

int nfe_input_ipv4(struct nfe_packet *p);
int nfe_proto_ipv4(struct nfe_packet *p);

#endif
