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
#include <arpa/inet.h>
#include <netinet/in.h>
#include "fsm_dpi_utils.h"
#include "fsm_internal.h"
#include "neigh_table.h"
#include "sockaddr_storage.h"


/**
 * @brief nfqueue network header parser
 *
 * @return 0 if the parsing is successful, -1 otherwise
 */
static void
fsm_nfq_net_header_parse(struct nfq_pkt_info *pkt_info, void *data)
{
    struct net_header_parser net_parser;
    struct fsm_parser_ops *parser_ops;
    struct fsm_session *session;
    struct ip6_hdr *ipv6hdr;
    struct iphdr *ipv4hdr;
    os_macaddr_t src_mac;
    os_macaddr_t dst_mac;
    int ip_protocol;
    bool rc_lookup;
    void *src_ip;
    void *dst_ip;
    int domain;
    uint16_t ethertype;
    int len = 0;

    MEMZERO(net_parser);
    net_parser.packet_id = pkt_info->packet_id;
    net_parser.nfq_queue_num = pkt_info->queue_num;
    net_parser.packet_len = pkt_info->payload_len;
    net_parser.caplen = pkt_info->payload_len;
    net_parser.data = (uint8_t *)pkt_info->payload;
    net_parser.rx_vidx = pkt_info->rx_vidx;
    net_parser.tx_vidx = pkt_info->tx_vidx;
    net_parser.rx_pidx = pkt_info->rx_pidx;
    net_parser.tx_pidx = pkt_info->tx_pidx;

    net_parser.payload_updated = false;
    net_parser.start = net_parser.data;
    net_parser.parsed = 0;

    /* set packet source as NFQ */
    net_parser.source = PKT_SOURCE_NFQ;
    net_parser.eth_header.ethertype = pkt_info->hw_protocol;

    len = net_header_parse_ip(&net_parser);
    if (len == 0)
    {
        LOGE("%s: failed to parse IP", __func__);
        return;
    }

    if (net_parser.ip_version == 4)
    {
        ipv4hdr = net_header_get_ipv4_hdr(&net_parser);
        if (ipv4hdr == NULL) return;
        src_ip = &ipv4hdr->saddr;
        dst_ip = &ipv4hdr->daddr;
        domain = AF_INET;
    }
    else
    {
        ipv6hdr = net_header_get_ipv6_hdr(&net_parser);
        if (ipv6hdr == NULL) return;
        src_ip = &ipv6hdr->ip6_src;
        dst_ip = &ipv6hdr->ip6_dst;
        domain = AF_INET6;
    }

    len = 0;
    ip_protocol = net_parser.ip_protocol;
    if (ip_protocol == IPPROTO_TCP) len = net_header_parse_tcp(&net_parser);
    if (ip_protocol == IPPROTO_UDP) len = net_header_parse_udp(&net_parser);
    if (ip_protocol == IPPROTO_ICMP) len = net_header_parse_icmp(&net_parser);
    if (ip_protocol == IPPROTO_ICMPV6) len = net_header_parse_icmp6(&net_parser);

    if (len == 0)
    {
        LOGT("%s: failed to parse protocol %x", __func__, ip_protocol);
        return;
    }

    /* Account for the ethetnet header that will be prepended */
    net_parser.start -= ETH_HLEN;
    net_parser.caplen += ETH_HLEN;
    net_parser.packet_len += ETH_HLEN;
    net_parser.parsed += ETH_HLEN;
    memset(net_parser.start, 0, ETH_HLEN);

    rc_lookup = neigh_table_lookup_af(domain, src_ip, &src_mac);
    if (rc_lookup)
    {
        if (fsm_nfq_mac_same(&src_mac, pkt_info) == false)
        {
            MEM_CPY(&src_mac, pkt_info->hw_addr, pkt_info->hw_addr_len);
            rc_lookup = fsm_update_neigh_cache(src_ip, &src_mac, domain, FSM_NFQUEUE);
            if (!rc_lookup) LOGT("%s: Couldn't update neighbor cache.",__func__);

        }
        if (rc_lookup)
        {
            net_parser.eth_header.srcmac = &src_mac;
            memcpy(&net_parser.start[6], &src_mac, ETH_ALEN);
        }
    }

    rc_lookup = neigh_table_lookup_af(domain, dst_ip, &dst_mac);
    if (rc_lookup)
    {
        net_parser.eth_header.dstmac = &dst_mac;
        memcpy(net_parser.start, &dst_mac, ETH_ALEN);
    }
    ethertype = htons(net_parser.eth_header.ethertype);
    memcpy(&net_parser.start[12], &ethertype, sizeof(ethertype));

    session = (struct fsm_session *)data;
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler(session, &net_parser);
}


/**
 * @brief update nfqueues settings for the given session
 *
 * @param session the fsm session involved
 * @return true if the nfqueue settings were successful, false otherwise
 */
bool
fsm_nfq_tap_update(struct fsm_session *session)
{
    struct nfq_settings nfqs;
    struct fsm_mgr *mgr;
    char   *buf_size_str;
    char   *queue_len_str;
    char   *queue_num_str;
    char   buf[10];
    uint32_t nlbuf_sz0 = 10*(1024 * 1024); // 10M netlink packet buffer.
    uint32_t nlbuf_szx = 6*(1024 * 1024); // 6M netlink packet buffer remaining queues.
    uint32_t queue_len0 = 10240; // number of packets in queue.
    uint32_t queue_lenx = CONFIG_FSM_NFQUEUE_LEN; // number of packets in queue for remaining queues.
    uint32_t queue_num = 0; // Default 0 queue for all traffic
    uint32_t num_of_queues = 1; // Default number of nfqueues
    uint32_t start_queue_num = 0;
    uint32_t end_queue_num = 0;
    size_t index;
    int ret_val;
    bool ret;

    LOGN("%s: setting nfqueue queue len to %u", __func__, queue_lenx);

    if ((session->tap_type & FSM_TAP_NFQ) == 0) return false;

    ret = nf_queue_init();
    if (ret == false)
    {
        LOGE("%s : nfqs init failed", __func__);
        return false;
    }

    /**
     * queue_num format :
     * default is 0
     * a single queue is represented as 'M'
     * multiple queues are represented as 'M-N'
     */
    queue_num_str = fsm_get_other_config_val(session, "queue_num");
    if (queue_num_str != NULL)
    {
        strcpy(buf, queue_num_str);
        ret_val = sscanf(buf, "%d-%d", &start_queue_num, &end_queue_num);
        if (ret_val == 2)
        {
            num_of_queues = end_queue_num - start_queue_num + 1;
        }
        queue_num = start_queue_num;
    }

    mgr = fsm_get_mgr();
    nfqs.loop = mgr->loop;
    nfqs.nfq_cb = fsm_nfq_net_header_parse;
    nfqs.data = session;

    buf_size_str = fsm_get_other_config_val(session, "nfqueue_buff_size");
    if (buf_size_str != NULL)
    {
        errno = 0;
        nlbuf_sz0 = strtoul(buf_size_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
    }

    queue_len_str = fsm_get_other_config_val(session, "nfqueue_length");
    if (queue_len_str != NULL)
    {
        errno = 0;
        queue_len0 = strtoul(queue_len_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
    }


    mgr = fsm_get_mgr();
    nfqs.loop = mgr->loop;
    nfqs.nfq_cb = fsm_nfq_net_header_parse;
    nfqs.data = session;

    for (index = 0; index < num_of_queues ; index++)
    {
        nfqs.queue_num = queue_num + index;
        ret = nf_queue_open(&nfqs);
        if (ret == false)
        {
            LOGE("%s : nfqs open failed to open queue %d", __func__, nfqs.queue_num);
        }


        ret = nf_queue_set_nlsock_buffsz(nfqs.queue_num, index == 0 ? nlbuf_sz0 : nlbuf_szx);
        if (ret == false)
        {
            LOGE("%s: Failed to set netlink sock buf size[%u].",__func__, index == 0 ? nlbuf_sz0 : nlbuf_szx);
        }

        ret = nf_queue_set_queue_maxlen(nfqs.queue_num, index == 0 ? queue_len0 : queue_lenx);
        if (ret == false)
        {
            LOGE("%s: Failed to set default nfueue length[%u].",__func__, index == 0 ? queue_len0 : queue_lenx);
        }

        nf_queue_get_nlsock_buffsz(nfqs.queue_num);
    }

    return true;
}
