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

#include "fsm_internal.h"
#include "neigh_table.h"


void util_populate_sockaddr(int af, void *ip, struct sockaddr_storage *dst)
{
    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip, sizeof(in4->sin_addr));
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip, sizeof(in6->sin6_addr));
    }
    return;
}


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
    struct sockaddr_storage key;
    struct ip6_hdr *ipv6hdr;
    struct iphdr *ipv4hdr;
    os_macaddr_t src_mac;
    os_macaddr_t dst_mac;
    int ip_protocol;
    bool rc_lookup;
    void *src_ip;
    void *dst_ip;
    int domain;
    int len = 0;

    memset(&net_parser, 0, sizeof(net_parser));
    net_parser.packet_id = pkt_info->packet_id;
    net_parser.nfq_queue_num = pkt_info->queue_num;
    net_parser.packet_len = pkt_info->payload_len;
    net_parser.caplen = pkt_info->payload_len;
    net_parser.data = (uint8_t *)pkt_info->payload;

    net_parser.start = net_parser.data;
    net_parser.parsed = 0;

    /* nfqueues are L3 packets, flag eth_header_available is set false */
    net_parser.eth_header_available = false;
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

    /* fetch ethernet header details using neigh table lookup */
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(domain, src_ip, &key);
    if (!pkt_info->hw_addr)
    {
        rc_lookup = neigh_table_lookup(&key, &src_mac);
        if (rc_lookup) net_parser.eth_header.srcmac = &src_mac;
    }
    else
    {
        memcpy(&src_mac, pkt_info->hw_addr, sizeof(os_macaddr_t));
        net_parser.eth_header.srcmac = &src_mac;
    }

    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(domain, dst_ip, &key);
    rc_lookup = neigh_table_lookup(&key, &dst_mac);
    if (rc_lookup)
    {
        net_parser.eth_header.dstmac = &dst_mac;
    }

    ip_protocol = net_parser.ip_protocol;
    if (ip_protocol == IPPROTO_TCP)
    {
        len = net_header_parse_tcp(&net_parser);
        if (len == 0) return;
    }

    if (ip_protocol == IPPROTO_UDP)
    {
        len = net_header_parse_udp(&net_parser);
        if (len == 0) return;
    }

    if (ip_protocol == IPPROTO_ICMP)
    {
        len = net_header_parse_icmp(&net_parser);
        if (len == 0) return;
    }

    if (ip_protocol == IPPROTO_ICMPV6)
    {
        len = net_header_parse_icmp6(&net_parser);
        if (len == 0) return;
    }


    session = (struct fsm_session *)data;
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler(session, &net_parser);

    return;
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
    uint32_t nlbuf_sz = 3*(1024 * 1024); // 3M netlink packet buffer.
    uint32_t queue_len = 10240; // number of packets in queue.
    uint32_t queue_num = 0; // Default 0 queue for all traffic
    uint32_t num_of_queues = 1; // Default number of nfqueues
    uint32_t start_queue_num = 0;
    uint32_t end_queue_num = 0;
    size_t index;
    int ret_val;
    bool ret;

    if (session->tap_type != FSM_TAP_NFQ) return false;

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

    for (index = 0; index < num_of_queues ; index++)
    {
        nfqs.queue_num = queue_num + index;
        ret = nf_queue_open(&nfqs);
        if (ret == false)
        {
            LOGE("%s : nfqs open failed to open queue %d", __func__, nfqs.queue_num);
        }
    }

    buf_size_str = fsm_get_other_config_val(session, "nfqueue_buff_size");
    if (buf_size_str != NULL)
    {
        errno = 0;
        nlbuf_sz = strtoul(buf_size_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
    }

    ret = nf_queue_set_nlsock_buffsz(queue_num, nlbuf_sz);
    if (ret == false)
    {
        LOGE("%s: Failed to set netlink sock buf size[%u].",__func__, nlbuf_sz);
    }

    queue_len_str = fsm_get_other_config_val(session, "nfqueue_length");
    if (queue_len_str != NULL)
    {
        errno = 0;
        queue_len = strtoul(queue_len_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
    }

    ret = nf_queue_set_queue_maxlen(queue_num, queue_len);
    if (ret == false)
    {
        LOGE("%s: Failed to set default nfueue length[%u].",__func__,queue_len);
    }

    return true;
}
