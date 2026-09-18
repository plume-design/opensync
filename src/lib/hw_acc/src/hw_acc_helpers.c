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
#include <string.h>
#include <arpa/inet.h>
#include <libmnl/libmnl.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

#include "log.h"
#include "hw_acc.h"

static struct mnl_socket *g_nl = NULL;

static struct mnl_socket *hw_acc_get_nl_socket(void)
{
    if (g_nl) return g_nl;

    g_nl = mnl_socket_open(NETLINK_NETFILTER);
    if (!g_nl)
    {
        LOGE("%s: mnl_socket_open() failed: %s", __func__, strerror(errno));
        return NULL;
    }

    if (mnl_socket_bind(g_nl, 0, MNL_SOCKET_AUTOPID) < 0)
    {
        LOGE("%s: mnl_socket_bind() failed: %s", __func__, strerror(errno));
        mnl_socket_close(g_nl);
        g_nl = NULL;
        return NULL;
    }

    return g_nl;
}

__attribute__((destructor)) static void hw_acc_nl_fini(void)
{
    if (g_nl)
    {
        mnl_socket_close(g_nl);
        g_nl = NULL;
    }
}

struct ct_lookup_cb_ctx
{
    struct hw_acc_flush_flow_t *original;
    struct hw_acc_flush_flow_t *reply;
    bool found;
};

static int parse_ctattr_type_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_TUPLE_ORIG:
        case CTA_TUPLE_REPLY:
            if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int parse_ctattr_tuple_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_TUPLE_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_TUPLE_IP:
        case CTA_TUPLE_PROTO:
            if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int parse_ctattr_ip_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_IP_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_IP_V4_SRC:
        case CTA_IP_V4_DST:
            if (mnl_attr_validate2(attr, MNL_TYPE_BINARY, 4) < 0) return MNL_CB_ERROR;
            break;
        case CTA_IP_V6_SRC:
        case CTA_IP_V6_DST:
            if (mnl_attr_validate2(attr, MNL_TYPE_BINARY, 16) < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int parse_ctattr_l4proto_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);

    if (mnl_attr_type_valid(attr, CTA_PROTO_MAX) < 0) return MNL_CB_OK;

    switch (type)
    {
        case CTA_PROTO_NUM:
            if (mnl_attr_validate(attr, MNL_TYPE_U8) < 0) return MNL_CB_ERROR;
            break;
        case CTA_PROTO_SRC_PORT:
        case CTA_PROTO_DST_PORT:
            if (mnl_attr_validate(attr, MNL_TYPE_U16) < 0) return MNL_CB_ERROR;
            break;
    }

    tb[type] = attr;
    return MNL_CB_OK;
}

static int parse_tuple(const struct nlattr *nested, struct hw_acc_flush_flow_t *tuple)
{
    if (!nested || !tuple) return MNL_CB_ERROR;

    const struct nlattr *tb[CTA_TUPLE_MAX + 1] = {0};
    if (mnl_attr_parse_nested(nested, parse_ctattr_tuple_cb, tb) < 0) return MNL_CB_ERROR;

    /* Parse network layer */
    if (tb[CTA_TUPLE_IP])
    {
        const struct nlattr *tb_ip[CTA_IP_MAX + 1] = {0};
        if (mnl_attr_parse_nested(tb[CTA_TUPLE_IP], parse_ctattr_ip_cb, tb_ip) < 0) return MNL_CB_ERROR;

        if (tb_ip[CTA_IP_V4_SRC] && tb_ip[CTA_IP_V4_DST])
        {
            tuple->ip_version = 4;
            memcpy(tuple->src_ip, mnl_attr_get_payload(tb_ip[CTA_IP_V4_SRC]), 4);
            memcpy(tuple->dst_ip, mnl_attr_get_payload(tb_ip[CTA_IP_V4_DST]), 4);
        }
        else if (tb_ip[CTA_IP_V6_SRC] && tb_ip[CTA_IP_V6_DST])
        {
            tuple->ip_version = 6;
            memcpy(tuple->src_ip, mnl_attr_get_payload(tb_ip[CTA_IP_V6_SRC]), 16);
            memcpy(tuple->dst_ip, mnl_attr_get_payload(tb_ip[CTA_IP_V6_DST]), 16);
        }
    }

    /* Parse protocol layer */
    if (tb[CTA_TUPLE_PROTO])
    {
        const struct nlattr *tb_proto[CTA_PROTO_MAX + 1] = {0};
        if (mnl_attr_parse_nested(tb[CTA_TUPLE_PROTO], parse_ctattr_l4proto_cb, tb_proto) < 0) return MNL_CB_ERROR;

        if (tb_proto[CTA_PROTO_NUM]) tuple->protocol = mnl_attr_get_u8(tb_proto[CTA_PROTO_NUM]);
        if (tb_proto[CTA_PROTO_SRC_PORT]) tuple->src_port = ntohs(mnl_attr_get_u16(tb_proto[CTA_PROTO_SRC_PORT]));
        if (tb_proto[CTA_PROTO_DST_PORT]) tuple->dst_port = ntohs(mnl_attr_get_u16(tb_proto[CTA_PROTO_DST_PORT]));
    }

    return MNL_CB_OK;
}

static int ct_entry_cb(const struct nlmsghdr *nlh, void *data)
{
    struct ct_lookup_cb_ctx *ctx = data;
    const struct nlattr *tb[CTA_MAX + 1] = {0};

    if (!nlh) return MNL_CB_OK;

    if (mnl_attr_parse(nlh, sizeof(struct nfgenmsg), parse_ctattr_type_cb, tb) < 0) return MNL_CB_ERROR;

    if (tb[CTA_TUPLE_ORIG])
    {
        if (parse_tuple(tb[CTA_TUPLE_ORIG], ctx->original) < 0) return MNL_CB_ERROR;
        ctx->found = true;
    }
    if (tb[CTA_TUPLE_REPLY])
    {
        if (parse_tuple(tb[CTA_TUPLE_REPLY], ctx->reply) < 0) return MNL_CB_ERROR;
    }

    return MNL_CB_OK;
}

static bool ct_entry_lookup(
        struct hw_acc_flush_flow_t *lookup,
        struct hw_acc_flush_flow_t *orig,
        struct hw_acc_flush_flow_t *reply)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh;
    struct nfgenmsg *nfh;
    int ret;

    memset(orig, 0, sizeof(*orig));
    memset(reply, 0, sizeof(*reply));

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;

    nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
    nfh->nfgen_family = (lookup->ip_version == 4) ? AF_INET : AF_INET6;
    nfh->version = NFNETLINK_V0;
    nfh->res_id = 0;

    /* Build the search query */
    struct nlattr *nest = mnl_attr_nest_start(nlh, CTA_TUPLE_ORIG);
    struct nlattr *nest_ip = mnl_attr_nest_start(nlh, CTA_TUPLE_IP);
    if (lookup->ip_version == 4)
    {
        mnl_attr_put(nlh, CTA_IP_V4_SRC, 4, lookup->src_ip);
        mnl_attr_put(nlh, CTA_IP_V4_DST, 4, lookup->dst_ip);
    }
    else
    {
        mnl_attr_put(nlh, CTA_IP_V6_SRC, 16, lookup->src_ip);
        mnl_attr_put(nlh, CTA_IP_V6_DST, 16, lookup->dst_ip);
    }
    mnl_attr_nest_end(nlh, nest_ip);

    struct nlattr *nest_proto = mnl_attr_nest_start(nlh, CTA_TUPLE_PROTO);
    mnl_attr_put_u8(nlh, CTA_PROTO_NUM, lookup->protocol);
    mnl_attr_put_u16(nlh, CTA_PROTO_SRC_PORT, htons(lookup->src_port));
    mnl_attr_put_u16(nlh, CTA_PROTO_DST_PORT, htons(lookup->dst_port));
    mnl_attr_nest_end(nlh, nest_proto);
    mnl_attr_nest_end(nlh, nest);

    /* Get the shared netlink socket */
    struct mnl_socket *nl = hw_acc_get_nl_socket();
    if (!nl) return false;

    /* Send the query via netlink */
    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
    {
        LOGE("%s: ct lookup sendto failed: %s", __func__, strerror(errno));
        return false;
    }

    struct ct_lookup_cb_ctx ctx = {
        .original = orig,
        .reply = reply,
        .found = false,
    };

    /* Parse netlink response */
    while (true)
    {
        ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
        if (ret < 0)
        {
            LOGE("%s: mnl_socket_recvfrom failed: %s", __func__, strerror(errno));
            break;
        }

        ret = mnl_cb_run(buf, ret, 0, mnl_socket_get_portid(nl), ct_entry_cb, &ctx);
        if (ret <= MNL_CB_ERROR)
        {
            if (errno != ENOENT) LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(errno));
            break;
        }
        if (ret <= MNL_CB_STOP) break;
    }

    return ctx.found;
}

/* Reverse direction of given struct hw_acc_flush_flow_t */
void hw_acc_reverse_flush_flow(struct hw_acc_flush_flow_t *dst, struct hw_acc_flush_flow_t *src)
{
    struct hw_acc_flush_flow_t tmp;
    memcpy(&tmp, src, sizeof(tmp));
    memcpy(dst, &tmp, sizeof(*dst));
    memcpy(dst->src_ip, tmp.dst_ip, sizeof(dst->src_ip));
    memcpy(dst->dst_ip, tmp.src_ip, sizeof(dst->dst_ip));
    dst->src_port = tmp.dst_port;
    dst->dst_port = tmp.src_port;
}

bool hw_acc_lookup_ct_entry(
        struct hw_acc_flush_flow_t *lookup,
        struct hw_acc_flush_flow_t *orig,
        struct hw_acc_flush_flow_t *reply)
{
    if (ct_entry_lookup(lookup, orig, reply)) return true;

    /* Try reverse lookup */
    struct hw_acc_flush_flow_t reverse_lookup = {0};
    hw_acc_reverse_flush_flow(&reverse_lookup, lookup);
    return ct_entry_lookup(&reverse_lookup, orig, reply);
}
