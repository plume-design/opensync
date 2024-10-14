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

/**
 *
 * The following is mostly a copy of the conntrack related components of nf_conntrack.c
 * which are necessary for this plugin to operate safely in multiple threads.
 *
 */

#include <errno.h>
#include <libmnl/libmnl.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>

#include "log.h"
#include "nf_utils.h"
#include "os.h"
#include "os_types.h"
#include "sockaddr_storage.h"
#include "we.h"

#if defined(CONFIG_PLATFORM_IS_BCM)
// on BCM the kernel header is missing CTA_TUPLE_ZONE
#include <libnetfilter_conntrack/linux_nfnetlink_conntrack.h>
#else
#include <linux/netfilter/nfnetlink_conntrack.h>
#endif

#define IPV4_ADDR_LEN (4)
#define IPV6_ADDR_LEN (16)
#define PROTO_NUM_ICMPV4 (1)
#define PROTO_NUM_ICMPV6 (58)
#define ICMP_ECHO_REQUEST (8)

#define ZONE_2 (USHRT_MAX - 1)

struct we_ct_context
{
    bool initialized;
    struct mnl_socket *mnl;
    struct nlmsghdr *nlh;
    int fd;

    uint16_t zone_id;
};

static struct we_ct_context nfct_context = {
    .initialized = false,
};

static struct we_ct_context *ct_get_context(void)
{
    return &nfct_context;
}

/**
 * @brief validates and stores conntrack network proto
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_proto_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_PROTO_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_PROTO_NUM:
        case CTA_PROTO_ICMP_TYPE:
        case CTA_PROTO_ICMP_CODE:
            if (mnl_attr_validate(attr, MNL_TYPE_U8) < 0)
            {
                perror("mnl_attr_validate");
                return MNL_CB_ERROR;
            }
            break;
        case CTA_PROTO_SRC_PORT:
        case CTA_PROTO_DST_PORT:
        case CTA_PROTO_ICMP_ID:
            if (mnl_attr_validate(attr, MNL_TYPE_U16) < 0)
            {
                perror("mnl_attr_validate");
                return MNL_CB_ERROR;
            }
            break;
    }
    tb[type] = attr;
    return MNL_CB_OK;
}

/**
 * @brief validates and stores conntrack IP info
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_ip_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_IP_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_IP_V4_SRC:
        case CTA_IP_V4_DST:
            if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0)
            {
                perror("mnl_attr_validate");
                return MNL_CB_ERROR;
            }
            break;
        case CTA_IP_V6_SRC:
        case CTA_IP_V6_DST:
            if (mnl_attr_validate2(attr, MNL_TYPE_BINARY, sizeof(struct in6_addr)) < 0)
            {
                perror("mnl_attr_validate2");
                return MNL_CB_ERROR;
            }
            break;
    }
    tb[type] = attr;
    return MNL_CB_OK;
}

/**
 * @brief validates and stores conntrack tuple info
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_tuple_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_TUPLE_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_TUPLE_IP:
            rc = mnl_attr_validate(attr, MNL_TYPE_NESTED);
            if (rc < 0) return MNL_CB_ERROR;
            break;

        case CTA_TUPLE_PROTO:
            rc = mnl_attr_validate(attr, MNL_TYPE_NESTED);
            if (rc < 0) return MNL_CB_ERROR;
            break;

        case CTA_TUPLE_ZONE:
            rc = mnl_attr_validate(attr, MNL_TYPE_U16);
            if (rc < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;

    return MNL_CB_OK;
}

/**
 * @brief validates conntrack attributes
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int data_attr_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_TUPLE_ORIG:
        case CTA_COUNTERS_ORIG:
        case CTA_COUNTERS_REPLY:
            rc = mnl_attr_validate(attr, MNL_TYPE_NESTED);
            if (rc < 0) return MNL_CB_ERROR;
            break;

        case CTA_TIMEOUT:
        case CTA_MARK:
        case CTA_SECMARK:
            rc = mnl_attr_validate(attr, MNL_TYPE_U32);
            if (rc < 0) return MNL_CB_ERROR;
            break;

        case CTA_ZONE:
            rc = mnl_attr_validate(attr, MNL_TYPE_U16);
            if (rc < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;

    return MNL_CB_OK;
}

/**
 * @brief translates conntrack tuple to ct_stats flow
 *
 * @param nest the netlink attribute
 * @param flow the ct_stats flow to update
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int get_tuple(const struct nlattr *nest, ct_flow_t *flow)
{
    struct nlattr *proto_tb[CTA_PROTO_MAX + 1];
    struct nlattr *tb[CTA_TUPLE_MAX + 1];
    struct nlattr *ip_tb[CTA_IP_MAX + 1];
    struct in6_addr *in6;
    struct in_addr *in;
    int rc;

    memset(tb, 0, (CTA_TUPLE_MAX + 1) * sizeof(tb[0]));

    rc = mnl_attr_parse_nested(nest, parse_tuple_cb, tb);
    if (rc < 0) return MNL_CB_ERROR;

    if (tb[CTA_TUPLE_IP] != NULL)
    {
        memset(ip_tb, 0, (CTA_IP_MAX + 1) * sizeof(ip_tb[0]));

        rc = mnl_attr_parse_nested(tb[CTA_TUPLE_IP], parse_ip_cb, ip_tb);
        if (rc < 0) return MNL_CB_ERROR;

        if (ip_tb[CTA_IP_V4_SRC] != NULL)
        {
            in = mnl_attr_get_payload(ip_tb[CTA_IP_V4_SRC]);
            sockaddr_storage_populate(AF_INET, &in->s_addr, &flow->layer3_info.src_ip);
        }

        if (ip_tb[CTA_IP_V4_DST] != NULL)
        {
            in = mnl_attr_get_payload(ip_tb[CTA_IP_V4_DST]);
            sockaddr_storage_populate(AF_INET, &in->s_addr, &flow->layer3_info.dst_ip);
        }

        if (ip_tb[CTA_IP_V6_SRC] != NULL)
        {
            in6 = mnl_attr_get_payload(ip_tb[CTA_IP_V6_SRC]);
            sockaddr_storage_populate(AF_INET6, in6->s6_addr, &flow->layer3_info.src_ip);
        }
        if (ip_tb[CTA_IP_V6_DST] != NULL)
        {
            in6 = mnl_attr_get_payload(ip_tb[CTA_IP_V6_DST]);
            sockaddr_storage_populate(AF_INET6, in6->s6_addr, &flow->layer3_info.dst_ip);
        }
    }

    if (tb[CTA_TUPLE_PROTO] != NULL)
    {
        uint16_t val16;
        uint8_t val8;

        memset(proto_tb, 0, (CTA_PROTO_MAX + 1) * sizeof(proto_tb[0]));

        rc = mnl_attr_parse_nested(tb[CTA_TUPLE_PROTO], parse_proto_cb, proto_tb);
        if (rc < 0) return MNL_CB_ERROR;

        if (proto_tb[CTA_PROTO_NUM] != NULL)
        {
            val8 = mnl_attr_get_u8(proto_tb[CTA_PROTO_NUM]);
            flow->layer3_info.proto_type = val8;
        }

        if (proto_tb[CTA_PROTO_SRC_PORT] != NULL)
        {
            val16 = mnl_attr_get_u16(proto_tb[CTA_PROTO_SRC_PORT]);
            flow->layer3_info.src_port = val16;
        }

        if (proto_tb[CTA_PROTO_DST_PORT] != NULL)
        {
            val16 = mnl_attr_get_u16(proto_tb[CTA_PROTO_DST_PORT]);
            flow->layer3_info.dst_port = val16;
        }

#ifdef CT_ICMP_SUPPORT
        if (proto_tb[CTA_PROTO_ICMP_ID] != NULL)
        {
            val16 = mnl_attr_get_u16(proto_tb[CTA_PROTO_ICMP_ID]);
            LOGT("%s: id=%u ", __func__, val16);
        }

        if (proto_tb[CTA_PROTO_ICMP_TYPE] != NULL)
        {
            val8 = mnl_attr_get_u8(proto_tb[CTA_PROTO_ICMP_TYPE]);
            LOGT("%s: type=%u ", __func__, val8);
        }

        if (proto_tb[CTA_PROTO_ICMP_CODE] != NULL)
        {
            val8 = mnl_attr_get_u8(proto_tb[CTA_PROTO_ICMP_CODE]);
            LOGT("%s: type=%u ", __func__, val8);
        }
#endif
    }
    if (tb[CTA_TUPLE_ZONE] != NULL)
    {
        flow->ct_zone = mnl_attr_get_u16(tb[CTA_TUPLE_ZONE]);
        LOGD("%s: Tuple ct_zone: %d", __func__, ntohs(flow->ct_zone));
    }
    return MNL_CB_OK;
}

/**
 * @brief validates and stores conntrack counters
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_counters_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_COUNTERS_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_COUNTERS_PACKETS:
        case CTA_COUNTERS_BYTES:
            rc = mnl_attr_validate(attr, MNL_TYPE_U64);
            if (rc < 0) return MNL_CB_ERROR;
            break;

        case CTA_COUNTERS32_PACKETS:
        case CTA_COUNTERS32_BYTES:
            rc = mnl_attr_validate(attr, MNL_TYPE_U32);
            if (rc < 0) return MNL_CB_ERROR;
            break;
    }
    tb[type] = attr;

    return MNL_CB_OK;
}

/**
 * @brief validates and stores conntrack tcp proto info
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_tcp_protoinfo(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_PROTOINFO_TCP_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_PROTOINFO_TCP_STATE:
            rc = mnl_attr_validate(attr, MNL_TYPE_U8);
            if (rc < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;

    return MNL_CB_OK;
}

/**
 * @brief validates and stores conntrack proto info
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int parse_protoinfo(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_PROTOINFO_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_PROTOINFO_TCP:
            rc = mnl_attr_validate(attr, MNL_TYPE_NESTED);
            if (rc < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;

    return MNL_CB_OK;
}

/**
 * @brief translates conntrack protoinfo to ct_stats flow
 *
 * @param nest the netlink attribute
 * @param flow the ct_stats flow to update
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int get_protoinfo(const struct nlattr *nest, ct_flow_t *flow)
{
    struct nlattr *tb[CTA_PROTOINFO_MAX + 1];
    int rc;

    memset(tb, 0, (CTA_PROTOINFO_MAX + 1) * sizeof(tb[0]));
    rc = mnl_attr_parse_nested(nest, parse_protoinfo, tb);
    if (rc < 0) return MNL_CB_ERROR;

    if (tb[CTA_PROTOINFO_TCP] != NULL)
    {
        struct nlattr *tcp_tb[CTA_PROTOINFO_TCP_MAX + 1];

        memset(tcp_tb, 0, (CTA_PROTOINFO_TCP_MAX + 1) * sizeof(tcp_tb[0]));
        rc = mnl_attr_parse_nested(tb[CTA_PROTOINFO_TCP], parse_tcp_protoinfo, tcp_tb);
        if (rc < 0) return MNL_CB_ERROR;

        if (tcp_tb[CTA_PROTOINFO_TCP_STATE] != NULL)
        {
            uint8_t state;

            state = mnl_attr_get_u8(tcp_tb[CTA_PROTOINFO_TCP_STATE]);
            switch (state)
            {
                case TCP_CONNTRACK_SYN_SENT:
                case TCP_CONNTRACK_SYN_RECV:
                case TCP_CONNTRACK_ESTABLISHED:
                    flow->start = true;
                    break;

                case TCP_CONNTRACK_FIN_WAIT:
                case TCP_CONNTRACK_CLOSE_WAIT:
                case TCP_CONNTRACK_LAST_ACK:
                case TCP_CONNTRACK_TIME_WAIT:
                case TCP_CONNTRACK_CLOSE:
                case TCP_CONNTRACK_TIMEOUT_MAX:
                    flow->end = true;
                    break;

                default:
                    break;
            }
        }
    }

    return MNL_CB_OK;
}

/**
 * @brief translates conntrack counters to ct_stats flow
 *
 * @param nest the netlink attribute
 * @param flow the ct_stats flow to update
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int get_counter(const struct nlattr *nest, ct_flow_t *_flow)
{
    struct nlattr *count_tb[CTA_COUNTERS_MAX + 1];
    uint64_t val64;
    uint32_t val32;
    int rc;

    memset(count_tb, 0, (CTA_COUNTERS_MAX + 1) * sizeof(count_tb[0]));
    rc = mnl_attr_parse_nested(nest, parse_counters_cb, count_tb);
    if (rc < 0) return MNL_CB_ERROR;

    if (count_tb[CTA_COUNTERS32_PACKETS] != NULL)
    {
        val32 = ntohl(mnl_attr_get_u32(count_tb[CTA_COUNTERS32_PACKETS]));
        _flow->pkt_info.pkt_cnt = val32;
    }

    if (count_tb[CTA_COUNTERS_PACKETS] != NULL)
    {
        val64 = be64toh(mnl_attr_get_u64(count_tb[CTA_COUNTERS_PACKETS]));
        _flow->pkt_info.pkt_cnt = val64;
    }

    if (count_tb[CTA_COUNTERS32_BYTES] != NULL)
    {
        val32 = ntohl(mnl_attr_get_u32(count_tb[CTA_COUNTERS32_BYTES]));
        _flow->pkt_info.bytes = val32;
    }

    if (count_tb[CTA_COUNTERS_BYTES] != NULL)
    {
        val64 = be64toh(mnl_attr_get_u64(count_tb[CTA_COUNTERS_BYTES]));
        _flow->pkt_info.bytes = val64;
    }

    return MNL_CB_OK;
}

#define WE_CT_KEY_SADDR "saddr"
#define WE_CT_KEY_DADDR "daddr"
#define WE_CT_KEY_SPORT "sport"
#define WE_CT_KEY_DPORT "dport"
#define WE_CT_KEY_IPPROTO "ipproto"
#define WE_CT_KEY_DIR "dir"
#define WE_CT_KEY_PKTS "pkts"
#define WE_CT_KEY_BYTES "bytes"
#define WE_CT_KEY_CT_MARK "ct_mark"
#define WE_CT_KEY_NTUPLE "ntuple"
#define WE_CT_KEY_DATA "data"

#define WE_CT_KEYLEN(s) (sizeof((s)) - 1)

struct we_ct_userdata
{
    we_state_t s;
    int reg;
    int index;
};

#define AGENT_NTUPLE_LEN (1 + 16 + 16 + 2 + 2)

/**
 * @brief process conntrack entries
 *
 * This is a replica of the conntrack callback in nf_conntrack.c, specialized to populate a WE array
 * instead of a ds_dlist to avoid unnecessary transformations of the data.
 */
int nf_process_ct_cb_we(const struct nlmsghdr *nlh, void *data)
{
    ctflow_info_t forward_entry;
    ctflow_info_t reverse_entry;
    struct we_ct_context *we_ct;
    struct nlattr *tb[CTA_MAX + 1];
    ct_flow_t *forward_flow;
    ct_flow_t *reverse_flow;
    we_state_t s;
    int array_reg;
    struct we_ct_userdata *user;
    struct nfgenmsg *nfg;
    uint16_t ct_zone;
    char ntuple[AGENT_NTUPLE_LEN];
    unsigned int ntuple_len = 0;
    int rc;
    int af;

    we_ct = ct_get_context();
    if (!we_ct->initialized) return MNL_CB_ERROR;

    user = data;
    array_reg = user->reg;
    s = user->s;

    MEMZERO(forward_entry);
    MEMZERO(reverse_entry);
    memset(tb, 0, (CTA_MAX + 1) * sizeof(tb[0]));
    nfg = mnl_nlmsg_get_payload(nlh);

    rc = mnl_attr_parse(nlh, sizeof(*nfg), data_attr_cb, tb);
    if (rc < 0) return MNL_CB_ERROR;

    ct_zone = 0; /* Zone = 0 flows will not have CTA_ZONE */
    if (tb[CTA_ZONE] != NULL)
    {
        ct_zone = ntohs(mnl_attr_get_u16(tb[CTA_ZONE]));
    }

    if (we_ct->zone_id != USHRT_MAX && (we_ct->zone_id != ZONE_2) && we_ct->zone_id != ct_zone) return MNL_CB_OK;

    forward_flow = &forward_entry.flow;

    reverse_flow = &reverse_entry.flow;

    if (tb[CTA_TUPLE_ORIG])
    {
        rc = get_tuple(tb[CTA_TUPLE_ORIG], forward_flow);
        if (rc < 0) goto error;
    }

    if (tb[CTA_TUPLE_REPLY])
    {
        rc = get_tuple(tb[CTA_TUPLE_REPLY], reverse_flow);
        if (rc < 0) goto error;
    }

    if (tb[CTA_COUNTERS_ORIG])
    {
        rc = get_counter(tb[CTA_COUNTERS_ORIG], forward_flow);
        if (rc < 0) goto error;
    }

    if (tb[CTA_COUNTERS_REPLY])
    {
        rc = get_counter(tb[CTA_COUNTERS_REPLY], reverse_flow);
        if (rc < 0) goto error;
    }

    af = forward_flow->layer3_info.src_ip.ss_family;

    /* Getting the original ip for v4 NAT'ed case */
    if (af == AF_INET)
    {
        forward_flow->layer3_info.dst_ip = reverse_flow->layer3_info.src_ip;
        reverse_flow->layer3_info.dst_ip = forward_flow->layer3_info.src_ip;
    }

    if (tb[CTA_PROTOINFO] && forward_flow->layer3_info.proto_type != 17)
    {
        rc = get_protoinfo(tb[CTA_PROTOINFO], forward_flow);
        if (rc < 0) goto error;
    }

    if (tb[CTA_MARK] != NULL)
    {
        forward_flow->ct_mark = ntohl(mnl_attr_get_u32(tb[CTA_MARK]));
        reverse_flow->ct_mark = forward_flow->ct_mark;
    }

    forward_flow->ct_zone = ct_zone;
    reverse_flow->ct_zone = ct_zone;

    /* filter out DNS and closing TCP connections */
    if (ntohs(forward_entry.flow.layer3_info.src_port) == 53 || ntohs(forward_entry.flow.layer3_info.dst_port) == 53)
        return MNL_CB_OK;
    if (forward_flow->end || reverse_flow->end) return MNL_CB_OK;

    ctflow_info_t *flow_info = &forward_entry;
    layer3_ct_info_t *tuple = &flow_info->flow.layer3_info;
    pkts_ct_info_t *client_stats = &forward_entry.flow.pkt_info;
    pkts_ct_info_t *server_stats = &reverse_entry.flow.pkt_info;

    /* build the ntuple string for the agent */
    memcpy(&ntuple[ntuple_len], &tuple->proto_type, sizeof(tuple->proto_type));
    ntuple_len += sizeof(tuple->proto_type);
    memcpy(&ntuple[ntuple_len], &tuple->src_port, sizeof(tuple->src_port));
    ntuple_len += sizeof(tuple->src_port);
    memcpy(&ntuple[ntuple_len], &tuple->dst_port, sizeof(tuple->dst_port));
    ntuple_len += sizeof(tuple->dst_port);
    if (af == AF_INET)
    {
        memcpy(&ntuple[ntuple_len], &(((struct sockaddr_in *)&tuple->src_ip)->sin_addr.s_addr), 4);
        ntuple_len += 4;
        memcpy(&ntuple[ntuple_len], &(((struct sockaddr_in *)&tuple->dst_ip)->sin_addr.s_addr), 4);
        ntuple_len += 4;
    }
    else
    {
        memcpy(&ntuple[ntuple_len], ((struct sockaddr_in6 *)&tuple->src_ip)->sin6_addr.s6_addr, 16);
        ntuple_len += 16;
        memcpy(&ntuple[ntuple_len], ((struct sockaddr_in6 *)&tuple->dst_ip)->sin6_addr.s6_addr, 16);
        ntuple_len += 16;
    }

    /* push the index */
    we_pushnum(s, user->index++);

    /* push the tab */
    int tab_reg = we_pushtab(s, NULL);

    /* Create the @data member */
    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_DATA), WE_CT_KEY_DATA);
    int data_reg = we_pusharr(s, NULL);

    we_pushnum(s, 0);
    int client_reg = we_pushtab(s, NULL);
    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_PKTS), WE_CT_KEY_PKTS);
    we_pushnum(s, client_stats->pkt_cnt);
    we_set(s, client_reg);
    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_BYTES), WE_CT_KEY_BYTES);
    we_pushnum(s, client_stats->bytes);
    we_set(s, client_reg);
    we_set(s, data_reg);

    we_pushnum(s, 1);
    int server_reg = we_pushtab(s, NULL);
    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_PKTS), WE_CT_KEY_PKTS);
    we_pushnum(s, server_stats->pkt_cnt);
    we_set(s, server_reg);
    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_BYTES), WE_CT_KEY_BYTES);
    we_pushnum(s, server_stats->bytes);
    we_set(s, server_reg);
    we_set(s, data_reg);

    /* set "data" into the table */
    we_set(s, tab_reg);

    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_CT_MARK), WE_CT_KEY_CT_MARK);
    we_pushnum(s, flow_info->flow.ct_mark);
    we_set(s, tab_reg);

    we_pushbuf(s, WE_CT_KEYLEN(WE_CT_KEY_NTUPLE), WE_CT_KEY_NTUPLE);
    we_pushstr(s, ntuple_len, ntuple);
    we_set(s, tab_reg);

    /* set the tab into arr[index] */
    we_set(s, array_reg);

error:
    return MNL_CB_OK;
}

static int we_ct_recv_data_we(void *buf, size_t buf_size, we_state_t s, int nentries)
{
    struct we_ct_context *we_ct;
    unsigned int portid;
    struct nlmsghdr *nlh;
    struct we_ct_userdata user;
    int ret;

    we_ct = ct_get_context();
    if (!we_ct->initialized) return 0;

    nlh = we_ct->nlh;
    portid = mnl_socket_get_portid(we_ct->mnl);

    /* The caller must have an array in @r0 */
    user.reg = 0;
    user.s = s;
    user.index = nentries;

    while (true)
    {
        ret = mnl_socket_recvfrom(we_ct->mnl, buf, buf_size);
        if (ret <= 0)
        {
            LOGE("%s: mnl_socket_recvfrom failed: %s", __func__, strerror(errno));
            break;
        }

        ret = mnl_cb_run(buf, ret, nlh->nlmsg_seq, portid, nf_process_ct_cb_we, &user);
        if (ret == -1)
        {
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(errno));
            break;
        }
        if (ret <= MNL_CB_STOP) break;
    }

    return ret;
}

static struct nlmsghdr *ct_build_msg_hdr(char *buf, uint32_t type, uint16_t flags, int af_family)
{
    struct nlmsghdr *nlh = NULL;
    struct nfgenmsg *nfh = NULL;
    struct we_ct_context *we_ct;

    we_ct = ct_get_context();
    if (!we_ct->initialized) return NULL;

    nlh = mnl_nlmsg_put_header(buf);

    nlh->nlmsg_type = type;
    nlh->nlmsg_flags = flags;
    nlh->nlmsg_seq = time(NULL);

    nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
    nfh->nfgen_family = af_family;
    nfh->version = NFNETLINK_V0;
    nfh->res_id = 0;

    return nlh;
}

bool we_dpi_ct_get_flow_entries(int af_family, we_state_t s, uint16_t zone_id, int nentries)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct we_ct_context *we_ct;
    struct mnl_socket *nl;
    struct nlmsghdr *nlh;
    int ret;

    we_ct = ct_get_context();
    if (!we_ct->initialized) return false;

    nlh = ct_build_msg_hdr(
            buf,
            (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET,
            NLM_F_REQUEST | NLM_F_DUMP,
            af_family);

    nl = we_ct->mnl;
    ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_sendto", __func__);
        return false;
    }

    we_ct->zone_id = zone_id;
    we_ct->nlh = nlh;
    ret = we_ct_recv_data_we(buf, sizeof(buf), s, nentries);
    if (ret < 0) return false;
    return true;
}

int we_dpi_ct_init(void)
{
    struct mnl_socket *nl = NULL;
    struct we_ct_context *we_ct;

    we_ct = ct_get_context();
    if (we_ct->initialized) return 0;

    nl = mnl_socket_open(NETLINK_NETFILTER);
    if (nl == NULL)
    {
        LOGE("%s: mnl_socket_open", __func__);
        return -1;
    }
    if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0)
    {
        LOGE("%s: mnl_socket_bind", __func__);
        goto err;
    }
    we_ct->mnl = nl;
    we_ct->fd = mnl_socket_get_fd(nl);
    we_ct->initialized = true;

    LOGD("%s: we_ct initialized", __func__);
    return 0;

err:
    mnl_socket_close(nl);
    return -1;
}

int we_dpi_ct_exit(void)
{
    struct we_ct_context *we_ct;

    we_ct = ct_get_context();
    if (we_ct->initialized == false) return 0;

    if (we_ct->mnl != NULL)
    {
        mnl_socket_close(we_ct->mnl);
        we_ct->mnl = NULL;
    }

    we_ct->initialized = false;
    return 0;
}
