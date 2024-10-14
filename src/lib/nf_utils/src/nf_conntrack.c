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

#include <errno.h>
#include <ev.h>
#include <libmnl/libmnl.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>

#include "sockaddr_storage.h"
#include "log.h"
#include "memutil.h"
#include "nf_utils.h"
#include "os_types.h"

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

#define ZONE_2      (USHRT_MAX -1)

static struct nf_ct_context
nfct_context =
{
    .initialized = false,
};


struct nf_ct_context *
nf_ct_get_context(void)
{
    return &nfct_context;
}

/*
 * ===========================================================================
 *  Private implementation
 * ===========================================================================
 */

/**
 * @brief compare flows.
 *
 * @param a flow pointer
 * @param b flow pointer
 * @return 0 if flows match
 */
static int
flow_cmp (const void *a, const void *b)
{
    const layer3_ct_info_t  *l3_a = (const layer3_ct_info_t *)a;
    const layer3_ct_info_t  *l3_b = (const layer3_ct_info_t *)b;

    return memcmp(l3_a, l3_b, sizeof(layer3_ct_info_t));
}

/**
 * Temporary list to merge similar flows across zones.
 */
ds_tree_t flow_tracker_list = DS_TREE_INIT(flow_cmp, struct flow_tracker, ft_tnode);

/**
 * @brief validates and stores conntrack network proto
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int
parse_proto_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_PROTO_MAX) < 0)
        return MNL_CB_OK;

    switch(type) {
    case CTA_PROTO_NUM:
    case CTA_PROTO_ICMP_TYPE:
    case CTA_PROTO_ICMP_CODE:
        if (mnl_attr_validate(attr, MNL_TYPE_U8) < 0) {
            perror("mnl_attr_validate");
            return MNL_CB_ERROR;
        }
        break;
    case CTA_PROTO_SRC_PORT:
    case CTA_PROTO_DST_PORT:
    case CTA_PROTO_ICMP_ID:
        if (mnl_attr_validate(attr, MNL_TYPE_U16) < 0) {
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
static int
parse_ip_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_IP_MAX) < 0)
        return MNL_CB_OK;

    switch(type) {
    case CTA_IP_V4_SRC:
    case CTA_IP_V4_DST:
        if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
            perror("mnl_attr_validate");
            return MNL_CB_ERROR;
        }
        break;
    case CTA_IP_V6_SRC:
    case CTA_IP_V6_DST:
        if (mnl_attr_validate2(attr, MNL_TYPE_BINARY, sizeof(struct in6_addr)) < 0) {
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
static int
parse_tuple_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_TUPLE_MAX);
    if (rc < 0)
        return MNL_CB_OK;

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

static int
cb_err(const struct nlmsghdr *nlh, void *data)
{
    struct nlmsgerr *err = (void *)(nlh + 1);
    if (err->error != 0)
        LOGD("%s: message with seq %u has failed: %s", __func__, nlh->nlmsg_seq,
             strerror(-err->error));

    return MNL_CB_OK;
}

static int
cb_overrun(const struct nlmsghdr *nlh, void *data)
{
    LOGD("%s: message with seq %u has run out of memory", __func__, nlh->nlmsg_seq);

    return MNL_CB_OK;
}

static mnl_cb_t cb_ctl_array[NLMSG_MIN_TYPE] = {
    [NLMSG_ERROR] = cb_err,
    [NLMSG_OVERRUN] = cb_overrun,
};

/**
 * @brief validates conntrack attributes
 *
 * @param attr the netlink attribute
 * @param data the table of <attribute, value>
 * @return MNL_CB_OK when successful, -1 otherwise
 */
static int
data_attr_cb(const struct nlattr *attr, void *data)
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
            rc =  mnl_attr_validate(attr, MNL_TYPE_U32);
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
static int
get_tuple(const struct nlattr *nest, ct_flow_t *flow)
{
    struct nlattr *proto_tb[CTA_PROTO_MAX+1];
    struct nlattr *tb[CTA_TUPLE_MAX+1];
    struct nlattr *ip_tb[CTA_IP_MAX+1];
    struct in6_addr *in6;
    struct in_addr *in;
    int rc;

    memset(tb, 0, (CTA_TUPLE_MAX+1) * sizeof(tb[0]));

    rc = mnl_attr_parse_nested(nest, parse_tuple_cb, tb);
    if (rc < 0) return MNL_CB_ERROR;

    if (tb[CTA_TUPLE_IP] != NULL)
    {
        memset(ip_tb, 0, (CTA_IP_MAX+1) * sizeof(ip_tb[0]));

        rc = mnl_attr_parse_nested(tb[CTA_TUPLE_IP], parse_ip_cb, ip_tb);
        if (rc < 0) return MNL_CB_ERROR;

        if (ip_tb[CTA_IP_V4_SRC] != NULL)
        {
            in = mnl_attr_get_payload(ip_tb[CTA_IP_V4_SRC]);
            sockaddr_storage_populate(AF_INET, &in->s_addr,
                                      &flow->layer3_info.src_ip);
        }

        if (ip_tb[CTA_IP_V4_DST] != NULL)
        {
            in = mnl_attr_get_payload(ip_tb[CTA_IP_V4_DST]);
            sockaddr_storage_populate(AF_INET, &in->s_addr,
                                      &flow->layer3_info.dst_ip);
        }

        if (ip_tb[CTA_IP_V6_SRC] != NULL)
        {
            in6 = mnl_attr_get_payload(ip_tb[CTA_IP_V6_SRC]);
            sockaddr_storage_populate(AF_INET6, in6->s6_addr,
                                      &flow->layer3_info.src_ip);
        }
        if (ip_tb[CTA_IP_V6_DST] != NULL)
        {
           in6 = mnl_attr_get_payload(ip_tb[CTA_IP_V6_DST]);
           sockaddr_storage_populate(AF_INET6, in6->s6_addr,
                                     &flow->layer3_info.dst_ip);
        }
    }

    if (tb[CTA_TUPLE_PROTO] != NULL)
    {
        uint16_t val16;
        uint8_t val8;

        memset(proto_tb, 0, (CTA_PROTO_MAX+1) * sizeof(proto_tb[0]));

        rc = mnl_attr_parse_nested(tb[CTA_TUPLE_PROTO],
                                   parse_proto_cb, proto_tb);
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
static int
parse_counters_cb(const struct nlattr *attr, void *data)
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
static int
parse_tcp_protoinfo(const struct nlattr *attr, void *data)
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
static int
parse_protoinfo(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb;
    int type;
    int rc;

    tb = data;
    type = mnl_attr_get_type(attr);

    rc = mnl_attr_type_valid(attr, CTA_PROTOINFO_MAX);
    if (rc < 0) return MNL_CB_OK;

    switch(type)
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
static int
get_protoinfo(const struct nlattr *nest, ct_flow_t *flow)
{
    struct nlattr *tb[CTA_PROTOINFO_MAX + 1];
    int rc;

    memset(tb, 0 , (CTA_PROTOINFO_MAX + 1) * sizeof(tb[0]));
    rc = mnl_attr_parse_nested(nest, parse_protoinfo, tb);
    if (rc < 0) return MNL_CB_ERROR;

    if (tb[CTA_PROTOINFO_TCP] != NULL)
    {
        struct nlattr *tcp_tb[CTA_PROTOINFO_TCP_MAX + 1];

        memset(tcp_tb, 0 , (CTA_PROTOINFO_TCP_MAX + 1) * sizeof(tcp_tb[0]));
        rc = mnl_attr_parse_nested(tb[CTA_PROTOINFO_TCP],
                                   parse_tcp_protoinfo, tcp_tb);
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
static int
get_counter(const struct nlattr *nest, ct_flow_t *_flow)
{
    struct nlattr *count_tb[CTA_COUNTERS_MAX+1];
    uint64_t val64;
    uint32_t val32;
    int rc;

    memset(count_tb, 0, (CTA_COUNTERS_MAX+1) * sizeof(count_tb[0]));
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


static void
read_mnl_socket_cbk(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    char rcv_buf[MNL_SOCKET_BUFFER_SIZE];
    struct nf_ct_context *nf_ct;
    int portid;
    int ret;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return;

    if (EV_ERROR & revents)
    {
        LOGE("%s: Invalid mnl socket event", __func__);
        return;
    }

    ret = mnl_socket_recvfrom(nf_ct->mnl, rcv_buf, sizeof(rcv_buf));
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_recvfrom failed: %s", __func__, strerror(errno));
        return;
    }

    portid = mnl_socket_get_portid(nf_ct->mnl);

    ret = mnl_cb_run2(rcv_buf, ret, 0, portid, NULL, NULL, cb_ctl_array,
                      MNL_ARRAY_SIZE(cb_ctl_array));

    if (ret == -1)
    {
        LOGE("%s: mnl_cb_run2 failed: %s", __func__, strerror(errno));
    }
}

static int
build_ipv4_addr(struct nlmsghdr *nlh, uint32_t saddr, uint32_t daddr)
{
    char src_ip_str[INET_ADDRSTRLEN];
    char dst_ip_str[INET_ADDRSTRLEN];
    struct nlattr *nest;

    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_IP);
    mnl_attr_put_u32(nlh, CTA_IP_V4_SRC, saddr);
    inet_ntop(AF_INET, &saddr, src_ip_str, INET_ADDRSTRLEN);

    mnl_attr_put_u32(nlh, CTA_IP_V4_DST, daddr);
    inet_ntop(AF_INET, &daddr, dst_ip_str, INET_ADDRSTRLEN);
    LOGD("%s: Added src_ip4: %s dst_ip4: %s", __func__, src_ip_str, dst_ip_str);
    mnl_attr_nest_end(nlh, nest);
    return 0;
}

static int
build_ipv6_addr(struct nlmsghdr *nlh, const void *saddr, const void *daddr)
{
    struct nlattr *nest;

    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_IP);
    if (saddr)
    {
        mnl_attr_put(nlh, CTA_IP_V6_SRC, 16, saddr);
    }
    if (daddr)
    {
        mnl_attr_put(nlh, CTA_IP_V6_DST, 16, daddr);
    }
    mnl_attr_nest_end(nlh, nest);

    return 0;
}

static int
build_ip_tuple_v4(struct nlmsghdr *nlh, uint32_t saddr, uint32_t daddr, uint16_t sport,
                  uint16_t dport, uint8_t proto)
{
    struct nlattr *nest;
    build_ipv4_addr(nlh, saddr, daddr);

    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
    mnl_attr_put_u8(nlh, CTA_PROTO_NUM, proto);
    switch (proto)
    {
    case IPPROTO_UDP:
    case IPPROTO_TCP:
        mnl_attr_put_u16(nlh, CTA_PROTO_SRC_PORT, sport);
        mnl_attr_put_u16(nlh, CTA_PROTO_DST_PORT, dport);
        LOGD("%s: Added proto: %d src_port: %d dst_port: %d", __func__, proto, ntohs(sport),
             ntohs(dport));
        break;
    default:
        LOGD("%s: Unknown protocol %d", __func__, proto);
        mnl_attr_nest_cancel(nlh, nest);
        return -1;
    }
    mnl_attr_nest_end(nlh, nest);
    return 0;
}

static int
build_ip_tuple_v6(struct nlmsghdr *nlh, const void *saddr, const void *daddr, uint16_t sport,
                  uint16_t dport, uint8_t proto)
{
    struct nlattr *nest;

    build_ipv6_addr(nlh, saddr, daddr);
    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
    mnl_attr_put_u8(nlh, CTA_PROTO_NUM, proto);
    switch (proto)
    {
    case IPPROTO_UDP:
    case IPPROTO_TCP:
        if (sport > 0)
        {
            mnl_attr_put_u16(nlh, CTA_PROTO_SRC_PORT, sport);
            LOGD("%s: Added src_port: %d", __func__, ntohs(sport));
        }
        if (dport > 0)
        {
            mnl_attr_put_u16(nlh, CTA_PROTO_DST_PORT, dport);
            LOGD("%s: Added dst_port: %d", __func__, ntohs(dport));
        }
        break;
    default:
        LOGD("%s: Unknown protocol %d", __func__, proto);
        mnl_attr_nest_cancel(nlh, nest);
        return -1;
    }
    mnl_attr_nest_end(nlh, nest);
    return 0;
}

static int
build_icmp_params_v4(struct nlmsghdr *nlh, uint32_t saddr, uint32_t daddr, uint16_t id,
                     uint8_t type, uint8_t code, uint8_t proto)
{
    struct nlattr *nest;

    build_ipv4_addr(nlh, saddr, daddr);
    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
    mnl_attr_put_u8(nlh, CTA_PROTO_NUM, proto);
    switch (proto)
    {
    case IPPROTO_ICMP:
        mnl_attr_put_u8(nlh, CTA_PROTO_ICMP_CODE, code);
        mnl_attr_put_u8(nlh, CTA_PROTO_ICMP_TYPE, type);
        mnl_attr_put_u16(nlh, CTA_PROTO_ICMP_ID, id);
        LOGD("%s: Added proto: %d icmp code: %d type: %d id: %d", __func__, proto, code, type,
             ntohs(id));
        break;
    default:
        LOGD("%s: Unknown protocol %d", __func__, proto);
        mnl_attr_nest_cancel(nlh, nest);
        return -1;
    }
    mnl_attr_nest_end(nlh, nest);
    return 0;
}

static int
build_icmp_params_v6(struct nlmsghdr *nlh, const void *saddr, const void *daddr, uint16_t id,
                     uint8_t type, uint8_t code, uint8_t proto)
{
    struct nlattr *nest;

    build_ipv6_addr(nlh, saddr, daddr);
    nest = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
    mnl_attr_put_u8(nlh, CTA_PROTO_NUM, proto);
    switch (proto)
    {
    case IPPROTO_ICMPV6:
        mnl_attr_put_u8(nlh, CTA_PROTO_ICMPV6_CODE, code);
        mnl_attr_put_u8(nlh, CTA_PROTO_ICMPV6_TYPE, type);
        mnl_attr_put_u16(nlh, CTA_PROTO_ICMPV6_ID, id);
        LOGD("%s: Added proto: %d icmpv6 code: %d type: %d id: %d", __func__, proto, ntohs(code),
             ntohs(type), ntohs(id));
        break;
    default:
        LOGD("%s: Unknown protocol %d", __func__, proto);
        mnl_attr_nest_cancel(nlh, nest);
        return -1;
    }
    mnl_attr_nest_end(nlh, nest);
    return 0;
}

static struct nlmsghdr *
nf_build_icmp_nl_msg(char *buf, nf_addr_t *addr, nf_icmp_t *icmp, int proto, int family,
                     uint32_t mark, uint16_t zone)
{
    struct nlmsghdr *nlh = NULL;
    struct nlattr *nest = NULL;
    uint32_t saddr4 = 0;
    uint32_t daddr4 = 0;
    void *src_ip = NULL;
    void *dst_ip = NULL;
    uint16_t id = icmp->id;
    uint8_t type = icmp->type;
    uint8_t code = icmp->code;

    LOGT("%s: Building nlmsg", __func__);

    nlh = nf_ct_build_msg_hdr(buf, (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW,
                              NLM_F_CREATE | NLM_F_REQUEST | NLM_F_ACK, family);
    if (family == AF_INET)
    {
        src_ip = &addr->src_ip.ipv4.s_addr;
        dst_ip = &addr->dst_ip.ipv4.s_addr;
        if (src_ip) memcpy(&saddr4, src_ip, IPV4_ADDR_LEN);
        if (dst_ip) memcpy(&daddr4, dst_ip, IPV4_ADDR_LEN);

        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        LOGT("%s: Building nlmsg origin icmpv4", __func__);
        build_icmp_params_v4(nlh, saddr4, daddr4, id, type, code, proto);
        mnl_attr_nest_end(nlh, nest);
        if (type == ICMP_ECHO_REQUEST)
        {
            LOGT("%s: Building nlmsg reply icmpv4", __func__);
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_icmp_params_v4(nlh, daddr4, saddr4, id, 0, 0, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    else if (family == AF_INET6)
    {
        src_ip = &addr->src_ip.ipv6.s6_addr;
        dst_ip = &addr->dst_ip.ipv6.s6_addr;
        LOGT("%s: Building nlmsg origin icmpv6", __func__);
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_icmp_params_v6(nlh, src_ip, dst_ip, id, type, code, proto);
        mnl_attr_nest_end(nlh, nest);
        if (type == ICMP_ECHO_REQUEST)
        {
            LOGT("%s: Building nlmsg reply icmpv6", __func__);
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_icmp_params_v6(nlh, dst_ip, src_ip, id, 0, 0, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    mnl_attr_put_u32(nlh, CTA_MARK, htonl(mark));
    mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));
    LOGD("%s: Added mark: %d zone: %d", __func__, mark, zone);
    return nlh;
}

static struct nlmsghdr *
nf_build_ip_nl_msg(char *buf, nf_addr_t *addr, nf_port_t *port, int proto, int family,
                   uint32_t mark, uint16_t zone)
{
    struct nlmsghdr *nlh = NULL;
    struct nlattr *nest = NULL;
    uint32_t saddr4 = 0;
    uint32_t daddr4 = 0;
    void *src_ip = NULL;
    void *dst_ip = NULL;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;

    nlh = nf_ct_build_msg_hdr(buf, (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW,
                              NLM_F_CREATE | NLM_F_REQUEST | NLM_F_ACK, family);
    src_port = port->src_port;
    dst_port = port->dst_port;

    if (family == AF_INET)
    {
        src_ip = &addr->src_ip.ipv4.s_addr;
        dst_ip = &addr->dst_ip.ipv4.s_addr;
        if (src_ip) memcpy(&saddr4, src_ip, IPV4_ADDR_LEN);
        if (dst_ip) memcpy(&daddr4, dst_ip, IPV4_ADDR_LEN);

        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_ip_tuple_v4(nlh, saddr4, daddr4, src_port, dst_port, proto);
        mnl_attr_nest_end(nlh, nest);
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
        build_ip_tuple_v4(nlh, daddr4, saddr4, dst_port, src_port, proto);
        mnl_attr_nest_end(nlh, nest);
    }
    else if (family == AF_INET6)
    {
        src_ip = &addr->src_ip.ipv6.s6_addr;
        dst_ip = &addr->dst_ip.ipv6.s6_addr;
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_ip_tuple_v6(nlh, src_ip, dst_ip, src_port, dst_port, proto);
        mnl_attr_nest_end(nlh, nest);
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
        build_ip_tuple_v6(nlh, dst_ip, src_ip, dst_port, src_port, proto);
        mnl_attr_nest_end(nlh, nest);
    }
    mnl_attr_put_u32(nlh, CTA_MARK, htonl(mark));
    mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));
    LOGD("%s: Added mark: %d zone: %d", __func__, mark, zone);
    return nlh;
}

static void
nf_ct_timeout_cbk(EV_P_ ev_timer *timer, int revents)
{
    nf_flow_t *ctx = container_of(timer, nf_flow_t, timeout);

    LOGD("%s: conntrack timer expired", __func__);

    /* clear the conn-track mark */
    ctx->mark = 0;
    nf_ct_set_mark(ctx);
    ev_timer_stop(EV_A_ & ctx->timeout);
    FREE(ctx);
}


static struct nlmsghdr *
nf_build_ip_nl_msg_alt(char *buf, void *src_ip, void *dst_ip, uint16_t src_port, uint16_t dst_port,
                       int proto, int family, uint32_t mark, uint16_t zone, bool build_reply)
{
    struct nlmsghdr *nlh = NULL;
    struct nlattr *nest = NULL;
    uint32_t *saddr4 = NULL;
    uint32_t *daddr4 = NULL;

    nlh = nf_ct_build_msg_hdr(buf, (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW,
                              NLM_F_CREATE | NLM_F_REQUEST | NLM_F_ACK, family);

    if (family == AF_INET)
    {
        if (src_ip) saddr4 = src_ip;
        if (dst_ip) daddr4 = dst_ip;

        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_ip_tuple_v4(nlh, *saddr4, *daddr4, src_port, dst_port, proto);
        mnl_attr_nest_end(nlh, nest);
        if (build_reply)
        {
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_ip_tuple_v4(nlh, *daddr4, *saddr4, dst_port, src_port, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    else if (family == AF_INET6)
    {
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_ip_tuple_v6(nlh, src_ip, dst_ip, src_port, dst_port, proto);
        mnl_attr_nest_end(nlh, nest);
        if (build_reply)
        {
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_ip_tuple_v6(nlh, dst_ip, src_ip, dst_port, src_port, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    mnl_attr_put_u32(nlh, CTA_MARK, htonl(mark));
    mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));
    LOGD("%s: Added mark: %d zone: %d", __func__, mark, zone);
    return nlh;
}

static struct nlmsghdr *
nf_build_icmp_nl_msg_alt(char *buf, void *src_ip, void *dst_ip, uint16_t id, uint8_t type,
                         uint8_t code, int proto, int family, uint32_t mark, uint16_t zone,
                         bool build_reply)
{
    struct nlmsghdr *nlh = NULL;
    struct nlattr *nest = NULL;
    uint32_t *saddr4 = NULL;
    uint32_t *daddr4 = NULL;

    nlh = nf_ct_build_msg_hdr(buf, (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_NEW,
                              NLM_F_CREATE | NLM_F_REQUEST | NLM_F_ACK, family);
    if (family == AF_INET)
    {
        if (src_ip) saddr4 = src_ip;
        if (dst_ip) daddr4 = dst_ip;

        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_icmp_params_v4(nlh, *saddr4, *daddr4, id, type, code, proto);
        mnl_attr_nest_end(nlh, nest);
        if ((type == ICMP_ECHO_REQUEST) && build_reply)
        {
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_icmp_params_v4(nlh, *daddr4, *saddr4, id, 0, 0, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    else if (family == AF_INET6)
    {
        nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
        build_icmp_params_v6(nlh, src_ip, dst_ip, id, type, code, proto);
        mnl_attr_nest_end(nlh, nest);
        if ((type == ICMP_ECHO_REQUEST) && build_reply)
        {
            nest = mnl_attr_nest_start(nlh, CTA_TUPLE_REPLY);
            build_icmp_params_v6(nlh, dst_ip, src_ip, id, type, code, proto);
            mnl_attr_nest_end(nlh, nest);
        }
    }
    mnl_attr_put_u32(nlh, CTA_MARK, htonl(mark));
    mnl_attr_put_u16(nlh, CTA_ZONE, htons(zone));
    LOGD("%s: Added mark: %d zone: %d", __func__, mark, zone);
    return nlh;
}

static int
nf_ct_recv_data(void *buf, size_t buf_size, ds_dlist_t *nf_ct_list)
{
    struct nf_ct_context *nf_ct;
    unsigned int portid;
    struct nlmsghdr *nlh;
    int ret;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return 0;

    nlh = nf_ct->nlh;
    portid = mnl_socket_get_portid(nf_ct->mnl);

    while (true)
    {
        ret = mnl_socket_recvfrom(nf_ct->mnl, buf, buf_size);
        if (ret <= 0)
        {
            LOGE("%s: mnl_socket_recvfrom failed: %s", __func__, strerror(errno));
            break;
        }

        ret = mnl_cb_run(buf, ret, nlh->nlmsg_seq, portid, nf_process_ct_cb, nf_ct_list);
        if (ret == -1)
        {
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(errno));
            break;
        }
        if (ret <= MNL_CB_STOP) break;
    }

    return ret;
}


/*
 * WAR ESW-5906. Need to be removed
 * Process all collected and merged
 * flows and drop flows which are only
 * in zone1.
 */
static void
process_merged_multi_zonestats(ds_dlist_t *list, ds_tree_t *tree)
{
    struct flow_tracker *ft, *next;
    int count = 0;

    if (tree == NULL || list == NULL) return;

    ft = ds_tree_head(tree);
    while (ft != NULL)
    {
        count++;
        if (ft->zone_id == 1)
        {
            ds_dlist_remove(list, ft->flowptr);
            FREE(ft->flowptr);
        }
        next = ds_tree_next(tree, ft);
        ft = next;
    }
    LOGT("%s: Total merged flows alloc'd: %d\n", __func__, count);
    return;
}

static void
flow_free_merged_multi_zonestats(ds_tree_t *tree)
{
    struct flow_tracker *ft, *next;
    int count = 0;

    if (tree == NULL) return;

    ft = ds_tree_head(tree);
    while (ft != NULL)
    {
        next = ds_tree_next(tree, ft);
        ds_tree_remove(tree, ft);
        FREE(ft);
        ft = next;
        count++;
    }
    LOGT("%s: Total merged flows freed: %d\n", __func__, count);
    return;
}

/**
 * @brief merge multiple zone stats.
 *
 * @param flow key to look for.
 * @return true if inserted/merged.
 */
static bool
flow_merge_multi_zonestats(ctflow_info_t *flow, uint16_t zone_id)
{
    layer3_ct_info_t *l3_key = &flow->flow.layer3_info;
    struct flow_tracker *ft;
    pkts_ct_info_t  *ft_pkts;
    pkts_ct_info_t  *ct_pkts;

    ft = ds_tree_find(&flow_tracker_list, l3_key);

    if (ft == NULL)
    {
        // Allocate for the flow.
        ft = CALLOC(1, sizeof(struct flow_tracker));
        ft->flowptr = flow;
        ft->zone_id = zone_id;
        ds_tree_insert(&flow_tracker_list, ft, &flow->flow.layer3_info);
    }
    else
    {
        ct_pkts = &ft->flowptr->flow.pkt_info;
        ft_pkts = &flow->flow.pkt_info;

        if (ft_pkts->pkt_cnt > ct_pkts->pkt_cnt &&
            ft_pkts->bytes > ct_pkts->bytes)
        {
            ct_pkts->pkt_cnt = ft_pkts->pkt_cnt;
            ct_pkts->bytes = ft_pkts->bytes;
        }
        else if (ft_pkts->pkt_cnt < ct_pkts->pkt_cnt &&
                 ft_pkts->bytes < ct_pkts->bytes)
        {
            ft_pkts->pkt_cnt = ct_pkts->pkt_cnt;
            ft_pkts->bytes = ct_pkts->bytes;
        }
        ft->zone_id = USHRT_MAX;
    }
    return true;
}


/**
 * @brief logs a flow content for debug purposes
 *
 * @param flow the flow to log
 */
static void
nf_ct_print_conntrack(ct_flow_t *flow)
{
    char src[INET6_ADDRSTRLEN];
    char dst[INET6_ADDRSTRLEN];

    if (flow == NULL) return;

    memset(src, 0, sizeof(src));
    memset(dst, 0, sizeof(dst));

    getnameinfo((struct sockaddr *)&flow->layer3_info.src_ip,
                sizeof(struct sockaddr_storage), src, sizeof(src),
                0, 0, NI_NUMERICHOST);
    getnameinfo((struct sockaddr *)&flow->layer3_info.dst_ip,
                sizeof(struct sockaddr_storage), dst, sizeof(dst),
                0, 0, NI_NUMERICHOST);
    LOGT("%s: [ proto=%d tx src=%s dst=%s] ", __func__,
         flow->layer3_info.proto_type, src, dst);

    LOGT("%s: [src port=%d dst port=%d] "
         "[packets=%" PRIu64 "  bytes=%" PRIu64 "]", __func__,
        ntohs(flow->layer3_info.src_port),
        ntohs(flow->layer3_info.dst_port),
        flow->pkt_info.pkt_cnt, flow->pkt_info.bytes);
}


struct nlmsghdr *
nf_ct_build_msg_hdr(char *buf, uint32_t type, uint16_t flags, int af_family)
{
    struct nlmsghdr *nlh = NULL;
    struct nfgenmsg *nfh = NULL;
    struct nf_ct_context *nf_ct;

    nf_ct = nf_ct_get_context();
    if(!nf_ct->initialized) return NULL;

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


/*
 * ===========================================================================
 *  Public implementation
 * ===========================================================================
 */

/**
 * @brief checks if an ipv4 or ipv6 address represents
 *        a broadcast or multicast ip.
 *        ipv4 multicast starts 0xE.
 *        ipv4 broadcast starts 0xF.
 *        ipv6 multicast starts 0XFF.
 * @param af the the inet family
 * @param ip a pointer to the ip address buffer
 * @return true if broadcast/multicast false otherwise.
 */
bool
nf_ct_filter_ip(int af, void *ip)
{
    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)ip;
        if (((in4->sin_addr.s_addr & htonl(0xE0000000)) == htonl(0xE0000000) ||
            (in4->sin_addr.s_addr & htonl(0xF0000000)) == htonl(0xF0000000)))
        {
            LOGD("%s: Dropping ipv4 broadcast/multicast[%x]\n",
                  __func__, in4->sin_addr.s_addr);
            return true;
        }
        else if ((in4->sin_addr.s_addr & htonl(0x7F000000)) == htonl(0x7F000000))
        {
            LOGD("%s: Dropping ipv4 localhost[%x]\n",
                  __func__, in4->sin_addr.s_addr);
            return true;
        }
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ip;
        if ((in6->sin6_addr.s6_addr[0] & 0xF0) == 0xF0)
        {
            LOGD("%s: Dropping ipv6 multicast starting with [%x%x]\n",
                  __func__, in6->sin6_addr.s6_addr[0], in6->sin6_addr.s6_addr[1]);
            return true;
        }
        else if (memcmp(&in6->sin6_addr, &in6addr_loopback,
                        sizeof(struct in6_addr)) == 0)
        {
            LOGD("%s: Dropping ipv6 localhost [::%x]\n",
                  __func__, in6->sin6_addr.s6_addr[15]);
            return true;
        }
    }
    return false;
}

int
nf_process_ct_cb(const struct nlmsghdr *nlh, void *data)
{
    ctflow_info_t *forward_entry;
    ctflow_info_t *reverse_entry;
    struct nf_ct_context *nf_ct;
    struct nlattr *tb[CTA_MAX+1];
    ct_flow_t *forward_flow;
    ct_flow_t *reverse_flow;
    ds_dlist_t *nf_ct_list;
    struct nfgenmsg *nfg;
    uint16_t ct_zone;
    int rc;
    int af;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return MNL_CB_ERROR;

    nf_ct_list = (ds_dlist_t *)data;

    memset(tb, 0, (CTA_MAX+1) * sizeof(tb[0]));
    nfg = mnl_nlmsg_get_payload(nlh);

    rc = mnl_attr_parse(nlh, sizeof(*nfg), data_attr_cb, tb);
    if (rc < 0) return MNL_CB_ERROR;

    ct_zone = 0; /* Zone = 0 flows will not have CTA_ZONE */
    if (tb[CTA_ZONE] != NULL)
    {
        ct_zone = ntohs(mnl_attr_get_u16(tb[CTA_ZONE]));
    }

    if (nf_ct->zone_id != USHRT_MAX && (nf_ct->zone_id != ZONE_2) &&
        nf_ct->zone_id != ct_zone) return MNL_CB_OK;


    forward_entry = CALLOC(1, sizeof(struct ctflow_info));
    forward_flow =  &forward_entry->flow;

    reverse_entry = CALLOC(1, sizeof(struct ctflow_info));
    reverse_flow =  &reverse_entry->flow;

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
        rc =  get_counter(tb[CTA_COUNTERS_ORIG], forward_flow);
        if (rc < 0) goto error;
    }

    if (tb[CTA_COUNTERS_REPLY])
    {
        rc =  get_counter(tb[CTA_COUNTERS_REPLY], reverse_flow);
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



    if ((nf_ct->zone_id == USHRT_MAX) ||
        (nf_ct->zone_id == ZONE_2))
    {
        flow_merge_multi_zonestats(forward_entry, ct_zone);
        flow_merge_multi_zonestats(reverse_entry, ct_zone);
    }

    forward_flow->ct_zone = ct_zone;
    reverse_flow->ct_zone = ct_zone;

    ds_dlist_insert_tail(nf_ct_list, forward_entry);
    ds_dlist_insert_tail(nf_ct_list, reverse_entry);

    return MNL_CB_OK;

error:
    FREE(forward_entry);
    FREE(reverse_entry);

    return MNL_CB_OK;
}

int
nf_ct_set_mark(nf_flow_t *flow)
{
    uint8_t proto = 0;
    uint16_t family = 0;
    uint32_t mark = 0;
    uint16_t zone = 0;
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh = NULL;
    struct nf_ct_context *nf_ct;
    int res = 0;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return 0;

    if (flow == NULL)
    {
        LOGE("%s: Empty flow", __func__);
        return -1;
    }
    proto = flow->proto;
    family = flow->family;
    mark = flow->mark;
    zone = flow->zone;
    if (family != AF_INET && family != AF_INET6)
    {
        LOGE("%s: Unknown protocol family", __func__);
        return -1;
    }
    memset(buf, 0, sizeof(buf));
    if (proto == PROTO_NUM_ICMPV4 || proto == PROTO_NUM_ICMPV6)
    {

        nlh = nf_build_icmp_nl_msg(buf, &flow->addr, &flow->fields.icmp, proto, family, mark, zone);
    }
    else
    {
        nlh = nf_build_ip_nl_msg(buf, &flow->addr, &flow->fields.port, proto, family, mark, zone);
    }
    if (nlh == NULL) return -1;
    res = mnl_socket_sendto(nf_ct->mnl, nlh, nlh->nlmsg_len);
    LOGD("%s: nlh->nlmsg_len = %d res = %d\n", __func__, nlh->nlmsg_len, res);
    return (res == (int)nlh->nlmsg_len) ? 0 : -1;
}

int
nf_ct_set_mark_timeout(nf_flow_t *flow, uint32_t timeout)
{
    nf_flow_t *timer_ctx = NULL;
    struct nf_ct_context *nf_ct;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return 0;

    timer_ctx = CALLOC(1, sizeof(nf_flow_t));
    if (timer_ctx == NULL)
    {
        LOGE("%s: Memory allocation failure", __func__);
        return -1;
    }
    memcpy(timer_ctx, flow, sizeof(nf_flow_t));

    if (nf_ct_set_mark(flow) < 0)
    {
        LOGE("%s: setting connection mark failed", __func__);
        goto err_set_mark;
    }
    ev_timer_init(&timer_ctx->timeout, nf_ct_timeout_cbk, timeout, 0);
    ev_timer_start(nf_ct->loop, &timer_ctx->timeout);
    return 0;

err_set_mark:
    FREE(timer_ctx);
    return -1;
}

int
nf_ct_set_flow_mark(struct net_header_parser *net_pkt, uint32_t mark, uint16_t zone)
{
    uint8_t proto = 0;
    uint16_t family = 0;
    char buf[512];
    struct nlmsghdr *nlh = NULL;
    int res = 0;
    struct iphdr *ipv4hdr = NULL;
    struct ip6_hdr *ipv6hdr = NULL;
    void *src_ip = NULL;
    void *dst_ip = NULL;
    uint32_t src_port = 0;
    uint32_t dst_port = 0;
    uint16_t id = 0;
    uint8_t type = 0;
    uint8_t code = 0;
    struct icmphdr *icmpv4hdr;
    struct icmp6_hdr *icmpv6hdr;
    struct nf_ct_context *nf_ct;


    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return 0;

    if (net_pkt == NULL)
    {
        LOGE("%s: Empty flow", __func__);
        return -1;
    }

    proto = net_pkt->ip_protocol;

    if (net_pkt->ip_version == 4)
    {
        family = AF_INET;
        ipv4hdr = net_header_get_ipv4_hdr(net_pkt);
        src_ip = &ipv4hdr->saddr;
        dst_ip = &ipv4hdr->daddr;
    }
    else if (net_pkt->ip_version == 6)
    {
        family = AF_INET6;
        ipv6hdr = net_header_get_ipv6_hdr(net_pkt);
        src_ip = &ipv6hdr->ip6_src;
        dst_ip = &ipv6hdr->ip6_dst;
    }

    if (family != AF_INET && family != AF_INET6)
    {
        LOGE("%s: Unknown protocol family", __func__);
        return -1;
    }
    memset(buf, 0, sizeof(buf));

    switch (net_pkt->ip_protocol)
    {
    case IPPROTO_TCP:
        src_port = net_pkt->ip_pld.tcphdr->source;
        dst_port = net_pkt->ip_pld.tcphdr->dest;
        break;

    case IPPROTO_UDP:
        src_port = net_pkt->ip_pld.udphdr->source;
        dst_port = net_pkt->ip_pld.udphdr->dest;
        break;

    case IPPROTO_ICMP:
        /* icmpv4 hdr present in payload of ip */
        icmpv4hdr = (struct icmphdr *)(net_pkt->ip_pld.icmphdr);
        id = icmpv4hdr->un.echo.id;
        type = icmpv4hdr->type;
        code = icmpv4hdr->code;
        LOGD("%s: icmp: id:%d type:%d code:%d", __func__, icmpv4hdr->un.echo.id, icmpv4hdr->type,
             icmpv4hdr->code);
        break;
    case IPPROTO_ICMPV6:
        /* icmpv6 hdr present in payload of ipv6 */
        icmpv6hdr = (struct icmp6_hdr *)(net_pkt->ip_pld.icmp6hdr);
        id = icmpv6hdr->icmp6_dataun.icmp6_un_data16[0];
        type = icmpv6hdr->icmp6_type;
        code = icmpv6hdr->icmp6_code;
        break;

    default:
        break;
    }

    if (proto == PROTO_NUM_ICMPV4 || proto == PROTO_NUM_ICMPV6)
    {

        nlh = nf_build_icmp_nl_msg_alt(buf, src_ip, dst_ip, id, type, code, proto, family, mark,
                                       zone, true);
    }
    else
    {
        nlh = nf_build_ip_nl_msg_alt(buf, src_ip, dst_ip, src_port, dst_port, proto, family, mark,
                                     zone, true);
    }
    if (nlh == NULL) return -1;
    res = mnl_socket_sendto(nf_ct->mnl, nlh, nlh->nlmsg_len);
    read_mnl_socket_cbk(NULL, NULL, 0);
    LOGD("%s: nlh->nlmsg_len = %d res = %d\n", __func__, nlh->nlmsg_len, res);
    return (res == (int)nlh->nlmsg_len) ? 0 : -1;
}

void
nf_ct_print_entries(ds_dlist_t *nf_ct_list)
{
    ctflow_info_t *ct_entry;
    int count = 0;

    ds_dlist_foreach(nf_ct_list, ct_entry)
    {
        nf_ct_print_conntrack(&ct_entry->flow);
        count++;
    }
    LOGT("%s: Total node count %d", __func__, count);
}

bool
nf_ct_get_flow_entries(int af_family, ds_dlist_t *nf_ct_list, uint16_t zone_id)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nf_ct_context *nf_ct;
    struct mnl_socket *nl;
    struct nlmsghdr *nlh;
    int ret;

    nf_ct = nf_ct_get_context();
    if (!nf_ct->initialized) return false;

    nlh = nf_ct_build_msg_hdr(buf, (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET,
                                     NLM_F_REQUEST | NLM_F_DUMP, af_family);

    nl = nf_ct->mnl;
    ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_sendto", __func__);
        return false;
    }

    nf_ct->zone_id = zone_id;
    nf_ct->nlh = nlh;
    ret = nf_ct_recv_data(buf, sizeof(buf), nf_ct_list);
    if (ret < 0) return false;

    if (nf_ct->zone_id == USHRT_MAX)
        process_merged_multi_zonestats(nf_ct_list,
                                       &flow_tracker_list);

    flow_free_merged_multi_zonestats(&flow_tracker_list);
    return true;
}

int
nf_ct_init(struct ev_loop *loop)
{
    struct mnl_socket *nl = NULL;
    struct nf_ct_context *nf_ct;

    nf_ct = nf_ct_get_context();
    if (nf_ct->initialized) return 0;

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
    nf_ct->mnl = nl;
    nf_ct->loop = loop;
    nf_ct->fd = mnl_socket_get_fd(nl);
    ev_io_init(&nf_ct->wmnl, read_mnl_socket_cbk, nf_ct->fd, EV_READ);
    ev_io_start(loop, &nf_ct->wmnl);
    nf_ct->initialized = true;

    LOGD("%s: nf_ct initialized", __func__);
    return 0;

err:
    mnl_socket_close(nl);
    return -1;
}

int
nf_ct_exit(void)
{
    struct nf_ct_context *nf_ct;

    nf_ct = nf_ct_get_context();
    if (nf_ct->initialized == false) return 0;

    if (ev_is_active(&nf_ct->wmnl))
    {
        ev_io_stop(nf_ct->loop, &nf_ct->wmnl);
    }

    if (nf_ct->mnl != NULL)
    {
        mnl_socket_close(nf_ct->mnl);
        nf_ct->mnl = NULL;
    }

    nf_ct->initialized = false;
    return 0;
}

/**
 * @brief frees the temporary list of parsed flows
 *
 * @param ct_stats the container of the list
 */
void
nf_free_ct_flow_list(ds_dlist_t *ct_list)
{
    ct_flow_t *flow;
    int del_count;

    del_count = 0;
    while (!ds_dlist_is_empty(ct_list))
    {
        flow = ds_dlist_remove_tail(ct_list);
        FREE(flow);
        del_count++;
    }

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        LOGT("%s: del_count %d", __func__, del_count);
    }
}
