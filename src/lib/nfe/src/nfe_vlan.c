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

#include "nfe_vlan.h"
#include "nfe_input.h"
#include "nfe_ether.h"

struct vlan_hdr {
    unsigned short tci;
    unsigned short tpid;
};

struct vlan_ethhdr {
    unsigned char dst[ETH_ALEN];
    unsigned char src[ETH_ALEN];
    struct vlan_hdr vlan;
    unsigned short type;
};

static inline uint16_t 
vlan_id(unsigned short tci)
{
    return tci & 0x0fff;
}

int 
nfe_input_vlan(struct nfe_packet *p)
{
    unsigned short tci, tpid;

    do {
        if ((size_t)(p->tail - p->data) < sizeof(struct vlan_hdr))
            return -1;

        tci  = read16(p->data + offsetof(struct vlan_hdr, tci));
        tpid = read16(p->data + offsetof(struct vlan_hdr, tpid));

        p->data += sizeof(struct vlan_hdr);

    } while (tpid == ETH_P_8021Q);

    p->tuple.vlan = vlan_id(tci);

    return nfe_input_handoff(tpid)(p);
}

