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

#ifndef NFE_TCP_H
#define NFE_TCP_H

#include <stdint.h>

struct tcphdr {

    unsigned short sport;
    unsigned short dport;
    unsigned int   seq;
    unsigned int   ack_seq;
    unsigned char  offres;
#define TH_OFF(th) (((th)->offres & 0xf0) >> 4)
    unsigned char  flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
    unsigned short window;
    unsigned short check;
    unsigned short urg_ptr;
};

struct nfe_tcp_half {
    uint64_t last_seq_time;  /* last time touched */
    uint32_t init_seq_sent;  /* init sequence number */
    uint32_t last_seq_sent;  /* last sequence number */
    uint32_t next_seq_sent;  /* next sequence number */
    uint32_t last_ack_sent;  /* last ack sequence */
    uint32_t curr_seq_wrap;  /* overflow count */
    uint32_t packets;        /* total packets sent */
    uint16_t flags;          /* half connection flags */
    uint16_t mss;            /* maximum segment size */
};

struct nfe_tcp {
    struct nfe_tcp_half half[2];
    uint16_t state;
    uint16_t flags;
};

struct nfe_conntrack;
struct nfe_packet;

int nfe_proto_tcp(struct nfe_packet *);
struct nfe_conn *nfe_tcp_lookup(struct nfe_conntrack *conntrack, struct nfe_packet *packet);

#endif
