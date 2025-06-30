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

#ifndef NFE_TYPES_H
#define NFE_TYPES_H
#ifndef NFE_H
# error "Do not include nfe_types.h directly; use nfe.h instead."
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

/* conntrack handle */
typedef struct nfe_conntrack *nfe_conntrack_t;

/* connection handle */
typedef struct nfe_conn *nfe_conn_t;

typedef void (*nfe_get_conntrack_cb_t)(nfe_conn_t conn, void *data);

/* 16 bytes */
struct nfe_ipaddr {
    union {
        uint8_t  addr8[16];
        uint16_t addr16[8];
        uint32_t addr32[4];
        uint64_t addr64[2];
    };
};

/* Communication domain for nfe_tuple */
#define NFE_AF_NONE  0
#define NFE_AF_INET  1
#define NFE_AF_INET6 2

/* connection tuple */
struct nfe_tuple {
    struct nfe_ipaddr addr[2];
    uint16_t port[2];
    uint8_t proto;
    uint8_t domain;
    uint16_t vlan;
};

/* nfe_packet
 *
 * @tuple (populated and available after nfe_packet_hash())
 * @direction (0-from client, 1-from server)
 * @type host/broadcast/multicast
 * @hash is a symmetric jhash of the tuple
 * @user is an opaque pointer for user by an integration
 * @head points to the start of the packet
 * @tail points to the end of the packet
 * @data points to the protocol data
 * @prot points to the protocol header
 */
#define NFE_PACKET_TYPE_UNKNOWN   0
#define NFE_PACKET_TYPE_HOST      1
#define NFE_PACKET_TYPE_BROADCAST 2
#define NFE_PACKET_TYPE_MULTICAST 3

struct nfe_packet {
    struct nfe_tuple tuple;
    uint64_t timestamp;
    uint16_t direction;
    uint8_t  type;
    uint8_t  next;
    uint32_t hash;
    void    *user;
    const unsigned char *head;
    const unsigned char *tail;
    const unsigned char *data;
    const unsigned char *prot;
};

#endif
