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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <libmnl/libmnl.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

#include "log.h"
#include "neigh_table.h"
#include "nf_utils.h"
#include "os_types.h"

static struct nf_queue_context
{
    int queue_num;
    struct ev_loop *loop;
    struct ev_io nfq_io_mnl;
    struct mnl_socket *nfq_mnl;
    struct nfq_pkt_info pkt_info;
    process_nfq_event_cb nfq_cb;
    int    nfq_fd;
    void *user_data;
    bool initialized;
} nf_queue_context;

static struct nf_queue_context *
nf_queue_get_context(void)
{
    return &nf_queue_context;
}



static int
nf_queue_parse_attr_cb(const struct nlattr *attr, void *data)
{
    const struct nlattr **tb = data;
    int type = mnl_attr_get_type(attr);
    int rc;

    /* skip unsupported attribute in user-space */
    if (mnl_attr_type_valid(attr, NFQA_MAX) < 0)
        return MNL_CB_OK;

    switch(type)
    {
    case NFQA_MARK:
    case NFQA_IFINDEX_INDEV:
    case NFQA_IFINDEX_OUTDEV:
    case NFQA_IFINDEX_PHYSINDEV:
    case NFQA_IFINDEX_PHYSOUTDEV:
        rc = mnl_attr_validate(attr, MNL_TYPE_U32);
        if (rc < 0)
        {
            LOGE("%s: mnl_attr_validate failed", __func__);
            return MNL_CB_ERROR;
        }
        break;
    case NFQA_TIMESTAMP:
        rc = mnl_attr_validate2(attr, MNL_TYPE_UNSPEC,
                                sizeof(struct nfqnl_msg_packet_timestamp));
        if (rc < 0)
        {
            LOGE("%s: mnl_attr_validate2 failed", __func__);
            return MNL_CB_ERROR;
        }
        break;
    case NFQA_HWADDR:
        rc = mnl_attr_validate2(attr, MNL_TYPE_UNSPEC,
                                sizeof(struct nfqnl_msg_packet_hw));
        if (rc < 0)
        {
            LOGE("%s: mnl_attr_validate2 failed", __func__);
            return MNL_CB_ERROR;
        }
        break;
    case NFQA_PAYLOAD:
        break;
    }

    tb[type] = attr;

    return MNL_CB_OK;
}


static int
nf_queue_cb(const struct nlmsghdr *nlh, void *data)
{
    struct nlattr *tb[NFQA_MAX+1] = {};
    struct nf_queue_context *ctxt;
    struct nfq_pkt_info *pkt_info;
    struct nfqnl_msg_packet_hdr *ph = NULL;
    struct nfqnl_msg_packet_hw  *phw = NULL;
    uint32_t id = 0;
    int ret;

    ctxt = nf_queue_get_context();
    ret = mnl_attr_parse(nlh, sizeof(struct nfgenmsg), nf_queue_parse_attr_cb, tb);
    if (ret == MNL_CB_ERROR) return MNL_CB_ERROR;

    pkt_info = &ctxt->pkt_info;
    if (tb[NFQA_PACKET_HDR])
    {
        ph = mnl_attr_get_payload(tb[NFQA_PACKET_HDR]);
        id = ntohl(ph->packet_id);
    }


    if (tb[NFQA_HWADDR]) phw = mnl_attr_get_payload(tb[NFQA_HWADDR]);

    /* Default verdict is Inspect */
    pkt_info->verdict = NF_UTIL_NFQ_INSPECT;
    pkt_info->packet_id = id;
    pkt_info->payload = mnl_attr_get_payload(tb[NFQA_PAYLOAD]);
    pkt_info->payload_len = mnl_attr_get_payload_len(tb[NFQA_PAYLOAD]);
    if (phw) pkt_info->hw_addr = phw->hw_addr;
    pkt_info->hw_protocol = ntohs(ph->hw_protocol);

    if (ctxt->nfq_cb) ctxt->nfq_cb(pkt_info, ctxt->user_data);

    return MNL_CB_OK;
}


static struct nlmsghdr *
nf_queue_set_nlh_request(char *buf, uint8_t type, int queue_num)
{
    struct nlmsghdr *nlh;
    struct nfgenmsg *nfg;

    if (!buf) return NULL;

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = (NFNL_SUBSYS_QUEUE << 8) | type;
    nlh->nlmsg_flags = NLM_F_REQUEST;

    nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));
    nfg->nfgen_family = AF_UNSPEC;
    nfg->version = NFNETLINK_V0;
    nfg->res_id = htons(queue_num);

    return nlh;
}


static void
nf_queue_send_nlh_request(struct nlmsghdr *nlh, uint8_t cfg_type, void *cmd_opts)
{
    struct nf_queue_context *ctxt;
    int ret;
    uint32_t *queue_maxlen;

    ctxt = nf_queue_get_context();

    switch (cfg_type)
    {
    case NFQA_CFG_CMD:
        mnl_attr_put(nlh, NFQA_CFG_CMD, sizeof(struct nfqnl_msg_config_cmd), cmd_opts);
        break;

    case NFQA_CFG_PARAMS:
        mnl_attr_put(nlh, NFQA_CFG_PARAMS, sizeof(struct nfqnl_msg_config_params), cmd_opts);

        /* Set flag NFQA_CFG_F_FAIL_OPEN for accepting packets by kernel when queue full*/
        mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_FAIL_OPEN));
        mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_FAIL_OPEN));

        /* Avoid receiving fragmented packets to the QUEUE*/
        mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_GSO));
        mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_GSO));

        /* Get/Set conntrack info through NFQUEUE.*/
        mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_CONNTRACK));
        mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_CONNTRACK));

        break;

    case NFQA_CFG_QUEUE_MAXLEN:
        queue_maxlen = (uint32_t *)cmd_opts;
        mnl_attr_put_u32(nlh, NFQA_CFG_QUEUE_MAXLEN, htonl(*queue_maxlen));
        break;

     default:
        break;
    }

    ret = mnl_socket_sendto(ctxt->nfq_mnl, nlh, nlh->nlmsg_len);
    if (ret == -1)
    {
        LOGE("%s: Failed to send nfq command type:[%d] ",__func__,cfg_type);
    }
    return;
}


static void
nf_queue_send_verdict(struct nlmsghdr *nlh, struct nfqnl_msg_verdict_hdr *vhdr)
{
    struct nf_queue_context *ctxt;
    struct nfq_pkt_info *pkt_info;
    struct nlattr *nest;
    int ret;
    uint32_t mark = 1;

    ctxt = nf_queue_get_context();
    pkt_info = &ctxt->pkt_info;

    switch(pkt_info->verdict)
    {
        case NF_UTIL_NFQ_ACCEPT:
                LOGT("%s: Setting mark 2, no more packets needed.",__func__);
                mark = 2;
                vhdr->verdict = htonl(NF_ACCEPT);
                break;
        case NF_UTIL_NFQ_INSPECT:
                LOGT("%s: Continue inspection, need more packets.",__func__);
                vhdr->verdict = htonl(NF_ACCEPT);
                break;
        case NF_UTIL_NFQ_DROP:
                LOGT("%s: Setting mark to 3, drop packets",__func__);
                mark = 3;
                vhdr->verdict = htonl(NF_DROP);
                break;
    }

    mnl_attr_put(nlh, NFQA_VERDICT_HDR, sizeof(struct nfqnl_msg_verdict_hdr), vhdr);

    if (mark == 2 || mark == 3)
    {
        nest = mnl_attr_nest_start(nlh, NFQA_CT);
        mnl_attr_put_u32(nlh, CTA_MARK, htonl(mark));
        mnl_attr_nest_end(nlh, nest);
    }

    ret = mnl_socket_sendto(ctxt->nfq_mnl, nlh, nlh->nlmsg_len);
    if (ret == -1)
    {
        LOGE("%s: Failed to send verdict for packet_id[%d]",__func__,ntohl(vhdr->id));
    }
    return;
}


/**
 * @brief ev callback to nfq events
 */
static void
nf_queue_read_mnl_cbk(EV_P_ ev_io *ev, int revents)
{
    struct nf_queue_context *ctxt;
    struct nfq_pkt_info *pkt_info;
    struct nfqnl_msg_verdict_hdr vhdr;
    char rcv_buf[0xFFFF];
    struct nlmsghdr *nlh;
    int portid = 0;
    int ret = 0;

    if (EV_ERROR & revents)
    {
        LOGE("%s: Invalid mnl socket event", __func__);
        return;
    }

    ctxt = nf_queue_get_context();
    pkt_info = &ctxt->pkt_info;
    pkt_info->verdict = NF_UTIL_NFQ_INSPECT;

    ret = mnl_socket_recvfrom(ctxt->nfq_mnl, rcv_buf, sizeof(rcv_buf));
    if (ret == -1)
    {
        LOGE("%s: mnl_socket_recvfrom failed: %s", __func__, strerror(errno));
        return;
    }

    portid = mnl_socket_get_portid(ctxt->nfq_mnl);
    ret = mnl_cb_run(rcv_buf, ret, 0, portid, nf_queue_cb, NULL);
    if (ret == -1)
    {
        LOGE("%s: mnl_cb_run failed [%u]", __func__, errno);
        return;
    }

    /* TODO: Sending Batch verdict is efficient */
    memset(&vhdr, 0, sizeof(struct nfqnl_msg_verdict_hdr));
    vhdr.id = htonl(pkt_info->packet_id);

    nlh = nf_queue_set_nlh_request(rcv_buf, NFQNL_MSG_VERDICT, ctxt->queue_num);
    nf_queue_send_verdict(nlh, &vhdr);

    return;
}

static bool
nf_queue_event_init(void)
{
    struct nf_queue_context *ctxt;
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct mnl_socket *nl;
    struct nlmsghdr *nlh;
    struct nfqnl_msg_config_cmd cmd;
    struct nfqnl_msg_config_params params;
    int ret;

    ctxt = nf_queue_get_context();

    LOGI("%s: Starting nfqueue ", __func__);

    nl = mnl_socket_open(NETLINK_NETFILTER);
    if (nl == NULL)
    {
        LOGE("%s: mnl_socket_open failed", __func__);
        return false;
    }

    ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
    if (ret != 0)
    {
        LOGE("%s: mnl_socket_bind failed", __func__);
        goto error;
    }
    ctxt->nfq_mnl = nl;

    nlh = nf_queue_set_nlh_request(buf, NFQNL_MSG_CONFIG, ctxt->queue_num);

    /* Bind for packets.*/
    memset(&cmd, 0, sizeof(struct nfqnl_msg_config_cmd));
    cmd.command = NFQNL_CFG_CMD_BIND;
    cmd.pf = htons(AF_INET);
    nf_queue_send_nlh_request(nlh, NFQA_CFG_CMD, &cmd);

    nlh = nf_queue_set_nlh_request(buf, NFQNL_MSG_CONFIG, ctxt->queue_num);
    /* Set config params. */
    memset(&params, 0, sizeof(struct nfqnl_msg_config_params));
    params.copy_range = htonl(0xFFFF);
    params.copy_mode = NFQNL_COPY_PACKET;
    nf_queue_send_nlh_request(nlh, NFQA_CFG_PARAMS, &params);

    ctxt->nfq_fd = mnl_socket_get_fd(ctxt->nfq_mnl);
    ev_io_init(&ctxt->nfq_io_mnl, nf_queue_read_mnl_cbk, ctxt->nfq_fd, EV_READ);
    ev_io_start(ctxt->loop, &ctxt->nfq_io_mnl);

    return true;

error:
    mnl_socket_close(nl);
    return false;

}


/**
 * @brief set max socket buffer size of netlink.
 *
 * @param sock_buff_sz
 * @return 0 if the nfqueue initialization successful, -1 otherwise
 */
bool
nf_queue_set_nlsock_buffsz(uint32_t sock_buff_sz)
{
    struct nf_queue_context *ctxt;
    int ret;

    ctxt = nf_queue_get_context();
    if (ctxt == NULL) return false;
    ret = setsockopt(ctxt->nfq_fd, SOL_SOCKET,
                     SO_RCVBUFFORCE, &sock_buff_sz,
                     sizeof(sock_buff_sz));
    if (ret == -1)
    {
        LOGE("%s: Failed to set nfq socket buff size to %u: error[%s]",
              __func__, sock_buff_sz, strerror(errno));
        return false;
    }

    return true;
}


/**
 * @brief set max length of nfqueues.
 *
 * @param queue length
 * @return 0 if the nfqueue initialization successful, -1 otherwise
 */
bool
nf_queue_set_queue_maxlen(uint32_t queue_maxlen)
{
    struct nf_queue_context *ctxt;
    struct nlmsghdr *nlh;
    char buf[MNL_SOCKET_BUFFER_SIZE];

    ctxt = nf_queue_get_context();
    if (ctxt == NULL) return false;

    nlh = nf_queue_set_nlh_request(buf, NFQNL_MSG_CONFIG, ctxt->queue_num);
    nf_queue_send_nlh_request(nlh, NFQA_CFG_QUEUE_MAXLEN, &queue_maxlen);
    return true;
}


/**
 * @brief initialize nfqueues
 *
 * @param struct nfq_settings.
 * @return 0 if the nfqueue initialization successful, -1 otherwise
 */
bool
nf_queue_init(struct nfq_settings *nfqs)
{
    struct nf_queue_context *ctxt;
    bool ret;

    ctxt = nf_queue_get_context();
    if (ctxt->initialized) return true;

    memset(ctxt, 0, sizeof(struct nf_queue_context));
    ctxt->loop = nfqs->loop;
    ctxt->nfq_cb = nfqs->nfq_cb;
    ctxt->user_data = nfqs->data;
    ctxt->queue_num = nfqs->queue_num;

    ret = nf_queue_event_init();
    if (ret == false)
    {
        LOGE("%s: nf queue event monitor init failure.", __func__);
        return false;
    }
    ctxt->initialized = true;
    return true;
}


/**
 *
 * @brief free allocated nfqueue resources
 */
void
nf_queue_exit(void)
{
    struct nlmsghdr *nlh;
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nfqnl_msg_config_cmd cmd;
    struct nf_queue_context *ctxt;;

    ctxt = nf_queue_get_context();

    if (ctxt->nfq_mnl == NULL) return;

    nlh = nf_queue_set_nlh_request(buf, NFQNL_MSG_CONFIG, ctxt->queue_num);

    /* UnBind for packets.*/
    memset(&cmd, 0, sizeof(struct nfqnl_msg_config_cmd));
    cmd.command = NFQNL_CFG_CMD_UNBIND;
    cmd.pf = htons(AF_INET);
    nf_queue_send_nlh_request(nlh, NFQA_CFG_CMD, &cmd);

    if (ev_is_active(&ctxt->nfq_io_mnl))
    {
        ev_io_stop(ctxt->loop, &ctxt->nfq_io_mnl);
    }

    mnl_socket_close(ctxt->nfq_mnl);
    ctxt->initialized = false;
    return;
}


/**
 *
 * @brief  Set verdict for given pktid.
 */
bool
nf_queue_set_verdict(uint32_t packet_id, int action)
{
    struct nf_queue_context  *ctxt;
    struct nfq_pkt_info *pkt_info;

    ctxt = nf_queue_get_context();
    pkt_info = &ctxt->pkt_info;
    if (pkt_info->packet_id != packet_id) return false;

    pkt_info->verdict = action;

    LOGD("%s: Setting verdict for packet_id[%d] to [%d/%s]",
         __func__, packet_id, pkt_info->verdict,
         pkt_info->verdict == NF_UTIL_NFQ_DROP ? "Drop" : "Accept/Inspect");
    return true;
}
