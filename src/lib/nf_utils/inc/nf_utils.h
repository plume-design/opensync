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

#ifndef NF_UTILS_H_INCLUDED
#define NF_UTILS_H_INCLUDED

#include <arpa/inet.h>
#include <ev.h>
#include <libmnl/libmnl.h>
#include "os_types.h"
#include "net_header_parse.h"

typedef union nf_ip_addr_
{
    struct in_addr ipv4;
    struct in6_addr ipv6;
} nf_ip_addr_t;

typedef struct nf_addr_
{
    nf_ip_addr_t src_ip;
    nf_ip_addr_t dst_ip;
} nf_addr_t;

typedef struct nf_port_
{
    uint16_t src_port;
    uint16_t dst_port;
} nf_port_t;

typedef struct nf_icmp_
{
    uint16_t id;
    uint8_t  type;
    uint8_t  code;
} nf_icmp_t;

typedef union nf_proto_fields_
{
    nf_port_t port;
    nf_icmp_t icmp;
} nf_proto_fields_t;

typedef struct nf_flow_
{
    nf_addr_t addr;
    uint8_t proto;
    nf_proto_fields_t fields;
    uint16_t family;
    uint16_t zone;
    uint32_t mark;
    ev_timer timeout;
} nf_flow_t;

struct nf_neigh_info
{
    int event;
    int af_family;
    int ifindex;
    void *ipaddr;
    os_macaddr_t *hwaddr;
    int state;
    uint32_t source;
    bool add;
    bool delete;
};

/**
 * @brief nfq packet info.
 */
struct nfq_pkt_info
{
    int                queue_num;
    uint32_t           packet_id;
    uint16_t           hw_protocol;
    uint8_t            *hw_addr;
    size_t             hw_addr_len;
    size_t             payload_len;
    void               *payload;
    int                flow_mark;
    uint32_t           mark_policy;
    uint16_t           rx_vidx;
    uint16_t           tx_vidx;
    uint16_t           rx_pidx;
    uint16_t           tx_pidx;
};

typedef struct layer3_ct_info
{
    struct sockaddr_storage src_ip;
    struct sockaddr_storage dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t proto_type;
    uint8_t family_type;
} layer3_ct_info_t;

typedef struct pkts_ct_info
{
    uint64_t pkt_cnt;
    uint64_t bytes;
} pkts_ct_info_t;

typedef struct ct_flow
{
    layer3_ct_info_t layer3_info;
    pkts_ct_info_t pkt_info;
    uint16_t dir;
    uint16_t ct_zone;
    uint32_t ct_mark;
    bool start;
    bool end;
} ct_flow_t;

typedef struct ctflow_info
{
    ct_flow_t flow;
    ds_dlist_node_t ct_node;
} ctflow_info_t;

enum
{
    CT_MARK_INSPECT = 1,
    CT_MARK_ACCEPT,
    CT_MARK_DROP
};

struct flow_tracker
{
    ctflow_info_t *flowptr;
    uint16_t  zone_id;
    ds_tree_node_t  ft_tnode;
};

struct nf_ct_context
{
    bool initialized;
    struct ev_loop *loop;
    struct ev_io wmnl;
    struct mnl_socket *mnl;
    struct nlmsghdr *nlh;
    int fd;

    uint16_t zone_id;
};


enum
{
    NF_UTIL_NEIGH_EVENT = 0,
    NF_UTIL_LINK_EVENT,
};

typedef void (*process_nl_event_cb)(struct nf_neigh_info *neigh_info);

struct nf_neigh_settings
{
    struct ev_loop *loop;
    process_nl_event_cb neigh_cb;
    process_nl_event_cb link_cb;
    int source;
};

const char *nf_util_get_str_state(int state);

int nf_neigh_init(struct nf_neigh_settings *neigh_settings);

int nf_neigh_exit(void);

bool nf_util_dump_neighs(int af_family);

bool nf_neigh_set_nlsockbuffsz(uint32_t sock_buff_sz);

typedef void (*process_nfq_event_cb)(struct nfq_pkt_info *pkt_info, void *data);

struct nfq_settings
{
    struct ev_loop *loop;
    process_nfq_event_cb nfq_cb;
    int queue_num;
    void *data;
};

struct nf_queue_context
{
    bool initialized;
    ds_tree_t nfq_tree;
};

#define NF_ERRNO_MAX 160
struct nf_queue_err_counters
{
    int error;
    uint64_t prev_counter;
    uint64_t counter;
    int32_t to_report;
};

struct nf_queue_context_errors
{
    struct nf_queue_err_counters **counters;
    size_t count;
};

struct nfqueue_ctxt
{
    uint32_t queue_num;
    struct ev_loop *loop;
    struct ev_io nfq_io_mnl;
    struct mnl_socket *nfq_mnl;
    struct nfq_pkt_info pkt_info;
    process_nfq_event_cb nfq_cb;
    int nfq_fd;
    void *user_data;
    char send_buf[0xFFFF];
    struct nlmsghdr *nlh;
    size_t errs_to_report;
    struct nf_queue_err_counters err_counters[NF_ERRNO_MAX + 2];
    ds_tree_node_t  nfq_tnode;
};

/* NFQUEUE APIs */
int nf_queue_set_dpi_mark(struct net_header_parser *net_hdr,
                          struct dpi_mark_policy *mark_policy);

bool nf_queue_init();

bool nf_queue_open(struct nfq_settings *nfqs);

void nf_queue_close(uint32_t queue_num);

void nf_queue_exit(void);

bool nf_queue_set_ct_mark(uint32_t packet_id, struct dpi_mark_policy *mark_policy,
                          uint32_t queue_num);

bool nf_queue_set_nlsock_buffsz(uint32_t queue_num, uint32_t sock_buff_sz);

bool nf_queue_get_nlsock_buffsz(uint32_t queue_num);

bool nf_queue_set_queue_maxlen(uint32_t queue_num, uint32_t queue_maxlen);

bool nf_queue_update_payload(uint32_t packet_id, uint32_t queue_num);

/* NF CONNTRACK APIs */
int nf_process_ct_cb(const struct nlmsghdr *nlh, void *data);

int nf_ct_init(struct ev_loop *loop);

int nf_ct_exit(void);

int nf_ct_set_mark(nf_flow_t *flow);

int nf_ct_set_mark_timeout(nf_flow_t *flow, uint32_t timeout);

int nf_ct_set_flow_mark(struct net_header_parser *net_pkt,
                        uint32_t mark, uint16_t zone);

bool nf_ct_get_flow_entries(int af_family, ds_dlist_t *g_nf_ct_list, uint16_t zone_id);

void nf_ct_print_entries(ds_dlist_t *g_nf_ct_list);

bool nf_ct_filter_ip(int af, void *ip);

void nf_free_ct_flow_list(ds_dlist_t *ct_list);

struct nf_queue_context_errors * nfq_get_err_counters(int queue_num);

void nfq_log_err_counters(int queue_num);

#endif /* NF_UTILS_H_INCLUDED */
