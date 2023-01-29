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

#include <arpa/inet.h>
#include "nfe_conn.h"
#include "nfe_proto.h"
#include "nfe_ipv4.h"
#include "nfe_ipv6.h"
#include "nfe_gre.h"
#include "nfe_tcp.h"
#include "nfe_udp.h"
#include "nfe_icmp.h"

static int
nfe_proto_unknown(struct nfe_packet *p)
{
    (void) p;
    return 0;
}

nfe_proto_handler
nfe_proto_handoff(unsigned char proto)
{
    switch (proto) {
        case IPPROTO_IPIP:
            return nfe_proto_ipv4;
        case IPPROTO_TCP:
            return nfe_proto_tcp;
        case IPPROTO_UDP:
            return nfe_proto_udp;
        case IPPROTO_IPV6:
            return nfe_proto_ipv6;
        case IPPROTO_GRE:
            return nfe_proto_gre;
        case IPPROTO_ICMP:
            return nfe_proto_icmp;
        case IPPROTO_ICMPV6:
            return nfe_proto_icmp6;
        default:
            return nfe_proto_unknown;
    }
}
