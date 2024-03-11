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

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

#include "os.h"
#include "util.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "fsm.h"
#include "fsm_internal.h"
#include "fsm_dpi_utils.h"
#include "nf_utils.h"
#include "kconfig.h"
#include "memutil.h"
#include "neigh_table.h"
#include "sockaddr_storage.h"
#include "osn_types.h"

#define MAX_BUFFER_SIZE 2048

struct msg;
typedef void (*sock_recv)(struct msg *);

struct sock_context
{
    struct ev_loop *loop;
    void *data;
    sock_recv recv_fn;
    int sock_fd;
    ev_io w_io;
    int events;
    struct fsm_session *session;
    bool initialized;
};

struct sock_context g_sock_context =
{
    .initialized = false,
    .loop = NULL,
    .sock_fd = 0,
    .session = NULL,
};


struct msg
{
    uint8_t data[MAX_BUFFER_SIZE];
    size_t len;
    size_t rcvd_len;
    uint32_t packet_id;
    uint16_t hw_protocol;
    uint8_t mac[6];
};


struct msg my_msg;


static bool
fsm_socket_mac_same(os_macaddr_t *lkp_mac, struct msg *receiver)
{
    bool rc = false;

    if (!memcmp(lkp_mac, receiver->mac, sizeof(os_macaddr_t))) return true;

    if (!osn_mac_addr_cmp(receiver->mac, &OSN_MAC_ADDR_INIT)) return true;

    LOGT("%s:%s: receiver->mac: " PRI_os_macaddr_lower_t,
         __FILE__, __func__,
         FMT_os_macaddr_pt((os_macaddr_t *)&receiver->mac));

    LOGT("%s: lkp_mac: "PRI_os_macaddr_t , __func__, FMT_os_macaddr_pt(lkp_mac));

    return rc;

}


static void
net_recv_cb(struct msg *receiver)
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
    int len = 0;

    MEMZERO(net_parser);
    net_parser.packet_id = ntohl(receiver->packet_id);
    net_parser.packet_len = receiver->len;
    net_parser.caplen = receiver->len;
    net_parser.data = receiver->data;

    net_parser.payload_updated = false;
    net_parser.start = net_parser.data;
    net_parser.parsed = 0;

    /* set packet source as NFQ */
    net_parser.source = PKT_SOURCE_SOCKET;
    net_parser.eth_header.ethertype = ntohs(receiver->hw_protocol);

    len = net_header_parse_ip(&net_parser);
    if (len == 0)
    {
        LOGE("%s: failed to parse IP for packet %u", __func__, net_parser.packet_id);
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

    LOGT("%s:%s: receiver->mac: " PRI_os_macaddr_lower_t,
         __FILE__, __func__,
         FMT_os_macaddr_pt((os_macaddr_t *)&receiver->mac));

    if (kconfig_enabled(CONFIG_FSM_ALWAYS_ADD_NEIGHBOR_INFO))
    {
        MEM_CPY(&src_mac, receiver->mac, sizeof(receiver->mac));
        rc_lookup = fsm_update_neigh_cache(src_ip, &src_mac, domain, FSM_SOCKET);
        if (!rc_lookup)
        {
            LOGT("%s: Couldn't update neighbor cache.", __func__);
        }
        else
        {
            net_parser.eth_header.srcmac = &src_mac;
        }
    }
    else
    {
        rc_lookup = neigh_table_lookup_af(domain, src_ip, &src_mac);
        if (rc_lookup)
        {
            if (fsm_socket_mac_same(&src_mac, receiver) == false)
            {
                MEM_CPY(&src_mac, receiver->mac, sizeof(receiver->mac));
                rc_lookup = fsm_update_neigh_cache(src_ip, &src_mac, domain, FSM_SOCKET);
                if (!rc_lookup) LOGT("%s: Couldn't update neighbor cache.", __func__);
            }
            if (rc_lookup) net_parser.eth_header.srcmac = &src_mac;
        }
    }

    rc_lookup = neigh_table_lookup_af(domain, dst_ip, &dst_mac);
    if (rc_lookup) net_parser.eth_header.dstmac = &dst_mac;

    ip_protocol = net_parser.ip_protocol;
    if (ip_protocol == IPPROTO_TCP) len = net_header_parse_tcp(&net_parser);
    if (ip_protocol == IPPROTO_UDP) len = net_header_parse_udp(&net_parser);
    if (ip_protocol == IPPROTO_ICMP) len = net_header_parse_icmp(&net_parser);
    if (ip_protocol == IPPROTO_ICMPV6) len = net_header_parse_icmp6(&net_parser);

    if (len == 0)
    {
        LOGE("%s: failed to parse protocol %x", __func__, ip_protocol);
        return;
    }

    session = g_sock_context.session;
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler(session, &net_parser);
}


static void
net_ev_recv_cb(EV_P_ ev_io *ev, int revents)
{
    (void)loop;
    (void)revents;

    struct sock_context *context;
    struct msg *receiver;
    struct iovec iov[4];
    struct msghdr msg;
    int rc;

    receiver = &my_msg;

    memset(&msg, 0, sizeof(msg));
    memset(iov, 0, sizeof(iov));
    context = ev->data;

    iov[0].iov_base = receiver->mac;
    iov[0].iov_len = sizeof(receiver->mac);

    iov[1].iov_base = &receiver->hw_protocol;
    iov[1].iov_len = sizeof(receiver->hw_protocol);

    iov[2].iov_base = &receiver->packet_id;
    iov[2].iov_len = sizeof(receiver->packet_id);

    iov[3].iov_base = receiver->data;
    iov[3].iov_len = sizeof(receiver->data);

    msg.msg_iov = iov;
    msg.msg_iovlen = 4;

    rc = recvmsg(context->sock_fd, &msg, 0);
    if (rc == -1)
    {
        LOGE("%s: failed to receive data: %s", __func__,
             strerror(errno));
        return;
    }

    /* Call the user receive routine */
    receiver->rcvd_len = rc;
    receiver->len = receiver->rcvd_len - (6 + 2 + 4); /* remove iov[0-2].len */
    context->recv_fn(receiver);
}

static int
fsm_dispatch_init_listener(struct fsm_session *session)
{
    unsigned char buf[sizeof(struct in6_addr)];
    struct fsm_dpi_dispatcher *dispatch;
    union fsm_dpi_context *dpi_context;
    struct sockaddr_in servaddr;
    char *portstr;
    char *ipstr;
    int sockfd;
    int domain;
    int64_t port;
    int rc;
    int s;

    dpi_context = session->dpi;
    if (dpi_context == NULL) return -1;

    dispatch = &dpi_context->dispatch;

    ipstr = dispatch->listening_ip;
    if (ipstr == NULL) return -1;

    portstr = dispatch->listening_port;
    if (portstr == NULL) return -1;

    domain = AF_INET;

    s = inet_pton(domain, ipstr, buf);
    if (s <= 0)
    {
        if (s == 0)
        {
            LOGE("%s: %s is not a valid IPv4 address", __func__,
                 ipstr);
        }
        else
        {
            LOGE("%s: inet_pton failed: %s", __func__, strerror(errno));
        }
        return -1;
    }

    errno = 0;
    port = strtol(portstr, NULL, 0);
    /* Check for various possible errors */

    if ((errno == ERANGE && (port == LONG_MAX || port == LONG_MIN))
        || (errno != 0 && port == 0))
    {
        LOGE("%s: strtol failed: %s", __func__, strerror(errno));
        return -1;
    }

    if (g_sock_context.initialized == true)
    {
        ev_io_stop(session->loop, &g_sock_context.w_io);
        close(g_sock_context.sock_fd);
        g_sock_context.initialized = false;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_addr.s_addr = inet_addr(ipstr);
    servaddr.sin_port = htons(port);
    servaddr.sin_family = AF_INET;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    errno = 0;
    rc = bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    if (rc < 0)
    {
        LOGE("%s: bind failed: %s", __func__, strerror(errno));
        return -1;
    }

    /* set socket to non blocking mode */
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    memset(&g_sock_context, 0, sizeof(g_sock_context));
    g_sock_context.sock_fd = sockfd;
    g_sock_context.recv_fn = net_recv_cb;
    g_sock_context.session = session;
    g_sock_context.loop = session->loop;
    ev_io_init(&g_sock_context.w_io, net_ev_recv_cb,
               g_sock_context.sock_fd, EV_READ);

    g_sock_context.w_io.data = (void *)&g_sock_context;

    ev_io_start(session->loop, &g_sock_context.w_io);
    g_sock_context.initialized = true;
    LOGI("%s: FSM now listening on %s::%s", __func__,
         ipstr, portstr);

    return 0;
}


/**
 * @brief update nfqueues settings for the given session
 *
 * @param session the fsm session involved
 * @return true if the nfqueue settings were successful, false otherwise
 */
bool
fsm_socket_tap_update(struct fsm_session *session)
{
    int rc;

    g_sock_context.session = session;
    if (g_sock_context.initialized == true)  return true;

    rc = fsm_dispatch_init_listener(session);

    return ((rc == 0) ? true : false);
}


void
fsm_socket_tap_close(struct fsm_session *session)
{
    if (g_sock_context.initialized == false) return;

    ev_io_stop(session->loop, &g_sock_context.w_io);
    close(g_sock_context.sock_fd);
    g_sock_context.initialized = false;

    g_sock_context.sock_fd = 0;
    g_sock_context.recv_fn = NULL;
    g_sock_context.loop = NULL;

    return;
}
