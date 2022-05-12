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

#include <string.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include "log.h"
#include "ds_tree.h"
#include "nf_utils.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "fsm_dpi_utils.h"

#define DEFAULT_ZONE (0)
#define FSM_DPI_ZONE (1)

#define NET_HDR_BUFF_SIZE 128 + (2 * INET6_ADDRSTRLEN + 128) + 256

static char log_buf[NET_HDR_BUFF_SIZE] = { 0 };

static void copy_nf_ip_flow(
        nf_flow_t *flow,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        uint16_t zone,
        enum fsm_dpi_state state
)
{
    if (family == AF_INET)
    {
        memcpy(&flow->addr.src_ip.ipv4.s_addr, src_ip, 4);
        memcpy(&flow->addr.dst_ip.ipv4.s_addr, dst_ip, 4);
    }
    else if (family == AF_INET6)
    {
        memcpy(flow->addr.src_ip.ipv6.s6_addr, src_ip, 16);
        memcpy(flow->addr.dst_ip.ipv6.s6_addr, dst_ip, 16);
    }
    flow->proto = proto;
    flow->family = family;
    flow->fields.port.src_port = src_port;
    flow->fields.port.dst_port = dst_port;
    flow->mark = state;
    flow->zone = zone;
}

static void copy_nf_icmp_flow(
        nf_flow_t *flow,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        uint16_t zone,
        enum fsm_dpi_state state
)
{
    if (family == AF_INET)
    {
        memcpy(&flow->addr.src_ip.ipv4.s_addr, src_ip, 4);
        memcpy(&flow->addr.dst_ip.ipv4.s_addr, dst_ip, 4);
        flow->proto = 1;
    }
    else if (family == AF_INET6)
    {
        memcpy(flow->addr.src_ip.ipv6.s6_addr, src_ip, 16);
        memcpy(flow->addr.dst_ip.ipv6.s6_addr, dst_ip, 16);
        flow->proto = 58;
    }
    flow->family = family;
    flow->fields.icmp.id = id;
    flow->fields.icmp.type = type;
    flow->fields.icmp.code = code;
    flow->mark = state;
    flow->zone = zone;
}

static void copy_nf_flow_from_net_hdr(
        nf_flow_t *flow,
        struct net_header_parser *net_hdr,
        uint16_t zone,
        enum fsm_dpi_state state
)
{
    struct icmphdr icmpv4hdr;
    struct icmp6_hdr icmpv6hdr;
    struct iphdr *ipv4hdr = NULL;
    struct ip6_hdr *ipv6hdr = NULL;

    LOGT("%s: %s", __func__,
         net_header_fill_trace_buf(log_buf, NET_HDR_BUFF_SIZE, net_hdr));

    if (net_hdr->ip_version == 4)
    {
        ipv4hdr = net_header_get_ipv4_hdr(net_hdr);
        memcpy(&flow->addr.src_ip.ipv4.s_addr, &ipv4hdr->saddr, 4);
        memcpy(&flow->addr.dst_ip.ipv4.s_addr, &ipv4hdr->daddr, 4);
        flow->family = AF_INET;
    }
    else if (net_hdr->ip_version == 6)
    {
        ipv6hdr = net_header_get_ipv6_hdr(net_hdr);
        memcpy(&flow->addr.src_ip.ipv6.s6_addr, ipv6hdr->ip6_src.s6_addr, 16);
        memcpy(&flow->addr.dst_ip.ipv6.s6_addr, ipv6hdr->ip6_dst.s6_addr, 16);
        flow->family = AF_INET6;
    }

    flow->proto = net_hdr->ip_protocol;

    switch (net_hdr->ip_protocol)
    {
        case IPPROTO_TCP:
            flow->fields.port.src_port = ntohs(net_hdr->ip_pld.tcphdr->source);
            flow->fields.port.dst_port = ntohs(net_hdr->ip_pld.tcphdr->dest);
            break;

        case IPPROTO_UDP:
            flow->fields.port.src_port = ntohs(net_hdr->ip_pld.udphdr->source);
            flow->fields.port.dst_port = ntohs(net_hdr->ip_pld.udphdr->dest);
            break;

        case IPPROTO_ICMP:
            // icmpv4 hdr present in payload of ip
            memcpy(&icmpv4hdr, net_hdr->eth_pld.payload,
                    sizeof(struct icmphdr));
            flow->fields.icmp.id = icmpv4hdr.un.echo.id;
            flow->fields.icmp.type = icmpv4hdr.type;
            flow->fields.icmp.code = icmpv4hdr.code;
            LOGD("icmp: id:%d type:%d code:%d",
                 icmpv4hdr.un.echo.id, icmpv4hdr.type, icmpv4hdr.code);
            break;

        case IPPROTO_ICMPV6:
            // icmpv6 hdr present in payload of ipv6
            memcpy(&icmpv6hdr, net_hdr->eth_pld.payload,
                    sizeof(struct icmp6_hdr));
            flow->fields.icmp.id = ICMP6_ECHO_REQUEST;
            flow->fields.icmp.type = icmpv6hdr.icmp6_type;
            flow->fields.icmp.code = icmpv6hdr.icmp6_code;
            break;

        default:
            LOGD("protocol not supported for connection marking");
            break;
    }
    flow->mark = state;
    flow->zone = zone;
}

// TODO ctx used to hold flow and its state when multiple plugins used
int fsm_set_ip_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        enum fsm_dpi_state state
)
{
    nf_flow_t flow;
    int ret0;
    int ret1;

    memset(&flow, 0, sizeof(flow));
    copy_nf_ip_flow(
            &flow,
            src_ip,
            dst_ip,
            src_port,
            dst_port,
            proto,
            family,
            DEFAULT_ZONE,
            state);
    ret0 = nf_ct_set_mark(&flow);
    /* Set the conn mark for FSM_DPI_ZONE also */
    flow.zone = FSM_DPI_ZONE;
    ret1 = nf_ct_set_mark(&flow);

    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}

int fsm_set_ip_dpi_state_timeout(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t src_port,
        uint16_t dst_port,
        uint8_t proto,
        uint16_t family,
        enum fsm_dpi_state state,
        uint32_t timeout
)
{
    nf_flow_t flow;
    int ret0;
    int ret1;

    memset(&flow, 0, sizeof(flow));
    copy_nf_ip_flow(
            &flow,
            src_ip,
            dst_ip,
            src_port,
            dst_port,
            proto,
            family,
            DEFAULT_ZONE,
            state);
    ret0 = nf_ct_set_mark_timeout(&flow, timeout);
    /* Set the conn mark for FSM_DPI_ZONE also */
    flow.zone = FSM_DPI_ZONE;
    ret1 = nf_ct_set_mark(&flow);
    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}

int fsm_set_icmp_dpi_state(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        enum fsm_dpi_state state
)
{
    nf_flow_t flow;
    int ret0;
    int ret1;

    memset(&flow, 0, sizeof(flow));
    copy_nf_icmp_flow(
            &flow,
            src_ip,
            dst_ip,
            id,
            type,
            code,
            family,
            DEFAULT_ZONE,
            state);
    ret0 = nf_ct_set_mark(&flow);
    /* Set the conn mark for FSM_DPI_ZONE also */
    flow.zone = FSM_DPI_ZONE;
    ret1 = nf_ct_set_mark(&flow);
    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}

int fsm_set_icmp_dpi_state_timeout(
        void *ctx,
        void *src_ip,
        void *dst_ip,
        uint16_t id,
        uint8_t type,
        uint8_t code,
        uint16_t family,
        enum fsm_dpi_state state,
        uint32_t timeout
)
{
    nf_flow_t flow;
    int ret0;
    int ret1;

    memset(&flow, 0, sizeof(flow));
    copy_nf_icmp_flow(
            &flow,
            src_ip,
            dst_ip,
            id,
            type,
            code,
            family,
            DEFAULT_ZONE,
            state);
    ret0 = nf_ct_set_mark_timeout(&flow, timeout);
    /* Set the conn mark for FSM_DPI_ZONE also */
    flow.zone = FSM_DPI_ZONE;
    ret1 = nf_ct_set_mark_timeout(&flow, timeout);
    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}


// APIs using net_header_parser
int fsm_set_dpi_state(struct net_header_parser *net_hdr)
{
    uint32_t mark = CT_MARK_INSPECT;
    int ret0;
    int ret1;

    if (net_hdr->acc) mark = net_hdr->acc->flow_marker;

    ret0 = nf_ct_set_flow_mark(net_hdr, mark, 0);
    /*
     * Set the mark for the default zone 0 also.
     * The reason behind it in router mode
     * two flows are present one in zone=1 and
     * another in zone=0 due to NAT functionality.
     * Mark has to be applied to flows in both the zones.
     * In Bridge mode zone=0 will not be present
     * so netlink call  will throw error which
     * now. Cloud will configure appropriate mode.
     * TODO Either check Router/Bridge mode and make this additional call
     * or dump_all_flows and apply mark for all mathching 5 tuple flows.
     */
    ret1 = nf_ct_set_flow_mark(net_hdr, mark, FSM_DPI_ZONE);
    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}

int fsm_set_dpi_state_timeout(
        void *ctx,
        struct net_header_parser *net_hdr,
        enum fsm_dpi_state state,
        uint32_t timeout
)
{
    nf_flow_t flow;
    int ret0;
    int ret1;

    memset(&flow, 0, sizeof(flow));
    copy_nf_flow_from_net_hdr(&flow, net_hdr, DEFAULT_ZONE, state);
    ret0 = nf_ct_set_mark_timeout(&flow, timeout);
    /* Set the conn mark for FSM_DPI_ZONE also */
    flow.zone = FSM_DPI_ZONE;
    ret1 = nf_ct_set_mark_timeout(&flow, timeout);
    /* -ve or 0 - failed in both zones or +ve atleast one zone passed */
    return (ret0 + ret1);
}

void fsm_dpi_set_plugin_decision(
        struct fsm_session *session,
        struct net_header_parser *net_parser,
        enum fsm_dpi_state state)
{
    struct net_md_stats_accumulator *acc;
    struct fsm_dpi_flow_info *info;

    if (session == NULL) return;

    acc = net_parser->acc;
    if (acc == NULL) return;

    if (acc->dpi_plugins == NULL) return;

    info = ds_tree_find(acc->dpi_plugins, session);
    if (info == NULL) return;

    info->decision = state;
}

/**
 * @brief sets the state of the accumulator when plugin
 *        decides to take action on the flow.  If reverse
 *        flow accumulator is present, apply the same state
 *        and flow_marker value to it, so that dpi processing
 *        is not required
 *
 * @param session the dpi plugin session
 * @param acc the accumulator to set the state
 * @param state state to be set for the accumulator
 */
void
fsm_dpi_set_acc_state(struct fsm_session *session, struct net_md_stats_accumulator *acc, int state)
{
    struct net_md_stats_accumulator *rev_acc;
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;

    if (session == NULL || acc == NULL) return;

    acc->dpi_done = state;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return;

    dispatch = &dpi_context->dispatch;

    rev_acc = net_md_lookup_reverse_acc(dispatch->aggr, acc);
    if (rev_acc == NULL) return;

    rev_acc->dpi_done = acc->dpi_done;
    rev_acc->flow_marker = acc->flow_marker;

    return;
}

void
fsm_dpi_block_flow(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    int af = 0;

    fkey = acc->fkey;
    key = acc->key;
    if (key == NULL) return;

    if (key->ip_version == 4) af = AF_INET;
    if (key->ip_version == 6) af = AF_INET6;
    if (af == 0) return;

    LOGI("%s(): blocking flow %s:%d -> %s:%d, proto: %d",
         __func__,
         fkey->src_ip, fkey->sport, fkey->dst_ip, fkey->dport, fkey->protocol);

    fsm_set_ip_dpi_state(NULL, key->src_ip, key->dst_ip,
                         key->sport, key->dport,
                         key->ipprotocol, af, FSM_DPI_DROP);
    fsm_set_ip_dpi_state(NULL, key->dst_ip, key->src_ip,
                         key->dport, key->sport,
                         key->ipprotocol, af, FSM_DPI_DROP);
}


void
fsm_dpi_allow_flow(struct net_md_stats_accumulator *acc)
{
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    int af = 0;

    fkey = acc->fkey;
    key = acc->key;
    if (key == NULL) return;

    if (key->ip_version == 4) af = AF_INET;
    if (key->ip_version == 6) af = AF_INET6;
    if (af == 0) return;

    LOGI("%s(): Allowing flow %s:%d -> %s:%d, proto: %d",
         __func__,
         fkey->src_ip, fkey->sport, fkey->dst_ip, fkey->dport, fkey->protocol);

    fsm_set_ip_dpi_state(NULL, key->src_ip, key->dst_ip,
                         key->sport, key->dport,
                         key->ipprotocol, af, FSM_DPI_PASSTHRU);
    fsm_set_ip_dpi_state(NULL, key->dst_ip, key->src_ip,
                         key->dport, key->sport,
                         key->ipprotocol, af, FSM_DPI_PASSTHRU);
}

/**
 * @brief get the user device's network id
 *
 * @param session the fsm session triggering a report
 * @param mac the uder device mac address
 */
char *
fsm_ops_get_network_id(struct fsm_session *session, os_macaddr_t *mac)
{
    char *network_id;

    if (session == NULL) return NULL;
    if (mac == NULL) return NULL;
    if (session->ops.get_network_id == NULL) return NULL;

    network_id = session->ops.get_network_id(session, mac);
    if (network_id == NULL) network_id = "unknown";

    return network_id;
}

