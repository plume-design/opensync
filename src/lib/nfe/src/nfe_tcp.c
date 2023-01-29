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

#include "nfe_tcp.h"
#include "nfe_proto.h"
#include "nfe_priv.h"
#include "nfe_config.h"
#include "nfe_conn.h"
#include "nfe_conntrack.h"

#define TCP_DEFAULT_MSS 1460

/* tcp states */
enum
{
    TCP_CONNECTING        = 0,
    TCP_ESTABLISHED       = 1,
    TCP_HALF_DISCONNECTED = 2,
    TCP_LAST_ACK          = 3,
    TCP_CLOSED            = 4,
    TCP_NUM_STATE         = 5
};

/* general flags */
enum
{
    /* tcp flags */
    F_TCP_RETRANSMIT      = 1 << 0,
    F_TCP_OUTOFORDER      = 1 << 1,

    /* tcp half flags */
    F_TCP_HALF_OPEN       = 1 << 0,
    F_TCP_HALF_CLOSED     = 1 << 1
};

/* tcp option definitions */
enum
{
    TCP_OPTION_EOL        = 0,
    TCP_OPTION_NOOP       = 1,
    TCP_OPTION_MSS        = 2
};

typedef void (*nfe_tcpfsm_function)(const struct tcphdr *th, struct nfe_tcp *tcb, int dir, uint16_t datalen);

static uint16_t
tcp_mss(const struct tcphdr *th)
{
    int hdr_len, opt_len;
    uint8_t *opt_ptr;

    opt_ptr = (uint8_t *)th + sizeof(struct tcphdr);
    hdr_len = (TH_OFF(th) << 2) - sizeof(struct tcphdr);

    for (opt_len = 0; hdr_len > 0; hdr_len -= opt_len, opt_ptr += opt_len) {
        if (*opt_ptr == TCP_OPTION_EOL)
            break;

        if (*opt_ptr == TCP_OPTION_NOOP) {
            opt_len = 1;
            continue;
        }

        /* Otherwise the option size follows the option */
        opt_len = opt_ptr[1];

        /* basic sanity */
        if (hdr_len < opt_len || opt_len < 2)
            break;

        /* extract the mss */
        if (*opt_ptr == TCP_OPTION_MSS) {
            uint16_t mss;
            __builtin_memcpy(&mss, &opt_ptr[2], sizeof(mss));
            return be16toh(mss);
        }
    }
    return TCP_DEFAULT_MSS;
}

static void
tcp_rcv_err(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    (void) th;
    (void) dir;
    (void) datalen;

    tcp->state = TCP_CLOSED;
}

static void
tcp_rcv_rst(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    (void) th;
    (void) dir;
    (void) datalen;

    tcp->half[0].flags |= F_TCP_HALF_CLOSED;
    tcp->half[1].flags |= F_TCP_HALF_CLOSED;
    tcp->state = TCP_CLOSED;
}

static void
tcp_rcv_syn(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    if (tcp->half[dir].flags & F_TCP_HALF_OPEN)
        tcp->flags |= F_TCP_RETRANSMIT;

    tcp->half[dir].init_seq_sent = be32toh(th->seq);
    tcp->half[dir].last_seq_sent = be32toh(th->seq);
    tcp->half[dir].next_seq_sent = be32toh(th->seq) + 1 + datalen;
    tcp->half[dir].mss = tcp_mss(th);
    tcp->half[dir].flags |= F_TCP_HALF_OPEN;

    if (!(th->flags & TH_ACK))
        return;

    tcp->half[dir].last_ack_sent = be32toh(th->ack_seq);
    tcp->half[1 - dir].last_ack_sent = tcp->half[dir].next_seq_sent;
}

static void
tcp_rcv_fin(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    if (tcp->half[dir].flags & F_TCP_HALF_CLOSED)
        tcp->flags |= F_TCP_RETRANSMIT;
    tcp->half[dir].flags |= F_TCP_HALF_CLOSED;
    tcp->half[dir].next_seq_sent = be32toh(th->seq) + 1 + datalen;
    tcp->half[dir].last_ack_sent = be32toh(th->ack_seq);
}

static inline bool
tcp_seq_is_after(uint32_t seq, const uint32_t other)
{
    return ((int32_t)(seq - other) > 0);
}

static inline bool
tcp_zero_window_probe(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    uint32_t seq = be32toh(th->seq);
#ifdef OPTION_DEBUG
    nfe_assert(!th->syn && !th->fin && !th->rst);
#endif
    return (datalen == 1 && seq == tcp->half[dir].next_seq_sent && !th->window);
}

static inline bool
tcp_keepalive(const struct tcphdr *th, struct nfe_tcp *tcp, int dir, uint16_t datalen)
{
    uint32_t seq = be32toh(th->seq);
#ifdef OPTION_DEBUG
    nfe_assert(!th->syn && !th->fin && !th->rst);
#endif
    return (datalen <= 1 && seq == tcp->half[dir].next_seq_sent - 1);
}

static inline int64_t
tcp_data_offset(const struct tcphdr *th, struct nfe_tcp *tcp, int dir)
{
    uint32_t isn = tcp->half[dir].init_seq_sent;
    uint32_t seq = tcp->half[dir].last_seq_sent;
    uint32_t wrp = tcp->half[dir].curr_seq_wrap;

    return ((int64_t)seq + ((int64_t)wrp * 0x100000000LL)) -
        (int64_t)(isn + ((th->flags & TH_SYN) ? 0 : 1));
}

static const nfe_tcpfsm_function
nfe_tcpfsm[TCP_NUM_STATE][3] =
{
    /* Fin,          Syn,          Rst */
    { &tcp_rcv_err, &tcp_rcv_syn, &tcp_rcv_rst }, /* TCP_CONNECTING */
    { &tcp_rcv_fin, &tcp_rcv_syn, &tcp_rcv_rst }, /* TCP_ESTABLISHED */
    { &tcp_rcv_fin, &tcp_rcv_err, &tcp_rcv_rst }, /* TCP_HALF_DISCONNECTED */
    { &tcp_rcv_fin, &tcp_rcv_err, &tcp_rcv_rst }, /* LAST_ACK */
    { &tcp_rcv_err, &tcp_rcv_err, &tcp_rcv_err }, /* CLOSED */
};

static int64_t
nfe_tcpfsm_input(struct nfe_conntrack *conntrack, struct nfe_packet *packet, struct nfe_conn **conn)
{
    uint64_t datalen;
    uint32_t seq, ack_seq;
    uint8_t tcpf;
    struct nfe_tcp *tcp;
    int dir, policy;
    const struct tcphdr *th = (const struct tcphdr *) packet->prot;

    if (th->flags & TH_SYN) {
        nfe_conntrack_lru_expire(conntrack, LRU_PROTO_TCP_SYN, packet->timestamp);

        if (th->flags & TH_ACK)
            policy = NFE_ALLOC_POLICY_INVERT;
        else
            policy = NFE_ALLOC_POLICY_CREATE;
    } else {
        nfe_conntrack_lru_expire(conntrack, LRU_PROTO_TCP_EST, packet->timestamp);
        policy = nfe_conntrack_tcp_midflow ? NFE_ALLOC_POLICY_CREATE : NFE_ALLOC_POLICY_NONE;
    }

    /* This is the direction if the conn is new, so it is set here for use in
     * nfe_ext_conn_alloc. It is set correctly for all cases after lookup.
     */
    packet->direction = (policy == NFE_ALLOC_POLICY_INVERT);

    *conn = nfe_conntrack_lookup(conntrack, packet, policy);
    if (*conn == NULL)
        return -1;

    (*conn)->timestamp = packet->timestamp;

    dir = __builtin_memcmp((*conn)->tuple.addr[0].addr8, packet->tuple.addr[0].addr8,
        domain_len((*conn)->tuple.domain)) ? 1 : 0;
    packet->direction = dir;
    seq = be32toh(th->seq);
    ack_seq = be32toh(th->ack_seq);
    tcp = &(*conn)->cb.tcp;
    datalen = packet->tail - packet->data;

    /* Update this connections lru */
    if (th->flags & TH_SYN) {
        if (th->flags & TH_ACK) {
            nfe_conntrack_lru_update(conntrack, LRU_PROTO_TCP_EST, &(*conn)->lru);
        } else {
            nfe_conntrack_lru_update(conntrack, LRU_PROTO_TCP_SYN, &(*conn)->lru);
        }
    } else {
        nfe_conntrack_lru_update(conntrack, LRU_PROTO_TCP_EST, &(*conn)->lru);
    }

    if (tcp->half[dir].packets++ == 0) {
        tcp->half[dir].last_seq_sent = seq;
        tcp->half[dir].init_seq_sent = seq - 1;
        tcp->half[dir].next_seq_sent = seq; /* + datalen is fixed up below */
        tcp->half[dir].last_ack_sent = ack_seq;
    }

    tcpf = (((uint8_t *)th)[13] & 0x7);

    /* syn/fin/rst */
    if (tcpf)
        (nfe_tcpfsm[tcp->state][tcpf >> 1])(th, tcp, dir, datalen);

    /* tcp keepalive */
    else if (tcp_keepalive(th, tcp, dir, datalen))
        datalen = 0;

    /* tcp zero window probe */
    else if (tcp_zero_window_probe(th, tcp, dir, datalen))
        datalen = 0;

    /* in order */
    else if (seq == tcp->half[dir].next_seq_sent) {
        tcp->half[dir].next_seq_sent = seq + datalen;
        tcp->half[dir].last_ack_sent = ack_seq;
        tcp->flags &= ~(F_TCP_RETRANSMIT | F_TCP_OUTOFORDER);
    }

    /* from the past */
    else if (!tcp_seq_is_after(seq, tcp->half[dir].next_seq_sent)) {
        if ((tcp->half[dir].last_seq_time - packet->timestamp) > 30)
            tcp->flags |= F_TCP_RETRANSMIT;
        else
            tcp->flags |= F_TCP_OUTOFORDER;
    }

    /* from the future */
    else {
        tcp->flags |= F_TCP_OUTOFORDER;
        tcp->half[dir].next_seq_sent = seq + datalen;
        tcp->half[dir].last_ack_sent = ack_seq;
    }

    tcp->half[dir].last_seq_time = packet->timestamp;

    /* don't need to process any further if this is a retransmission */
    if (tcp->flags & F_TCP_RETRANSMIT) {
        return 0;
    }

    /* state transition */
    switch (tcp->state) {
        case TCP_CONNECTING:
            if (!(th->flags & TH_SYN) && (datalen || (
                tcp->half[0].flags & F_TCP_HALF_OPEN &&
                tcp->half[1].flags & F_TCP_HALF_OPEN))) {
                tcp->state = TCP_ESTABLISHED;
            }
            break;
        case TCP_ESTABLISHED:
            if (tcp->half[0].flags & F_TCP_HALF_CLOSED ||
                tcp->half[1].flags & F_TCP_HALF_CLOSED) {

                /* Depending on what side shutdown first, this transition for
                 * asymmetric is wrong, however is it impossible to know with
                 * certainty whether the other half will keep flowing. */
                if (!tcp->half[1-dir].packets)
                    tcp->state = TCP_LAST_ACK;
                else
                    tcp->state = TCP_HALF_DISCONNECTED;
            }
            break;
        case TCP_HALF_DISCONNECTED:
            if (tcp->half[0].flags & F_TCP_HALF_CLOSED &&
                tcp->half[1].flags & F_TCP_HALF_CLOSED) {
                tcp->state = TCP_LAST_ACK;
            }
            break;
        case TCP_LAST_ACK:
            if (!tcp->half[1-dir].packets ||
                (tcp->half[dir].last_ack_sent == tcp->half[1 - dir].next_seq_sent &&
                 tcp->half[1 - dir].last_ack_sent == tcp->half[dir].next_seq_sent)) {
                tcp->state = TCP_CLOSED;
                nfe_conn_release(*conn);
            }
            break;
        case TCP_CLOSED:
            nfe_conn_release(*conn);
            return 0;
    }

    if (tcp_seq_is_after(seq, tcp->half[dir].last_seq_sent)) {
        if (seq < tcp->half[dir].last_seq_sent)
            tcp->half[dir].curr_seq_wrap++;
        tcp->half[dir].last_seq_sent = seq;
    }

    return tcp_data_offset(th, tcp, dir);
}

struct nfe_conn *
nfe_tcp_lookup(struct nfe_conntrack *conntrack, struct nfe_packet *packet)
{
    int64_t off;
    struct nfe_conn *conn;

    off = nfe_tcpfsm_input(conntrack, packet, &conn);
    packet->next = 0; packet->prot = NULL;

    if (off != -1) {
        /* If the data isn't in order, consume it and clear flag */
        if ((conn->cb.tcp.flags & (F_TCP_OUTOFORDER | F_TCP_RETRANSMIT)) != 0) {
            packet->data = packet->tail;
            conn->cb.tcp.flags &= ~(F_TCP_OUTOFORDER | F_TCP_RETRANSMIT);
        }

        /* Caller can assume remaining packet data is orderly. If one was so inclined
         * they could deal with buffering here. */
    }

    return conn;
}

int
nfe_proto_tcp(struct nfe_packet *p)
{
    struct tcphdr *th;
    uint8_t th_len;

    nfe_assert(p->data == p->prot);

    th = (struct tcphdr *)p->data;
    th_len = TH_OFF(th) << 2;

    if (p->tail - p->data < th_len)
        return -1;

    __builtin_memcpy(&p->tuple.port[0], p->data + offsetof(struct tcphdr, sport), 2);
    __builtin_memcpy(&p->tuple.port[1], p->data + offsetof(struct tcphdr, dport), 2);

    p->data += th_len;
    p->next = NEXT_LOOKUP_TCP;
    return 0;
}


