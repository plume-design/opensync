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
#include <net/if.h>
#include <sys/ioctl.h>

#include "fsm_packet_reinject_utils.h"
#include "log.h"
#include "nf_utils.h"
#include "os.h"

/**
 * @brief initialize forward context
 *
 * @param session the dpi_dispatcher session
 */
static bool
fsm_init_forward_context(struct fsm_session *session)
{
    struct ifreq ifreq_c;
    struct ifreq ifr_i;
    int sockfd;

    if (session == NULL) return false;

    MEMZERO(session->forward_ctx);

    sockfd = socket(AF_PACKET, SOCK_RAW, 0);

    if (sockfd == -1)
    {
        LOGE("%s: failed to open socket (%s)", __func__, strerror(errno));
        return false;
    }

    MEMZERO(ifr_i);
    STRSCPY(ifr_i.ifr_name, session->tx_intf);

    if ((ioctl(sockfd, SIOCGIFINDEX, &ifr_i)) < 0)
    {
        LOGE("%s: error in index ioctl reading (%s)", __func__, strerror(errno));
        goto err_sockfd;
    }

    session->forward_ctx.sock_fd = sockfd;
    session->forward_ctx.raw_dst.sll_family = PF_PACKET;
    session->forward_ctx.raw_dst.sll_ifindex = ifr_i.ifr_ifindex;
    session->forward_ctx.raw_dst.sll_halen = ETH_ALEN;

    MEMZERO(ifreq_c);
    STRSCPY(ifreq_c.ifr_name, session->tx_intf);

    if ((ioctl(sockfd, SIOCGIFHWADDR, &ifreq_c)) < 0)
    {
        LOGE("%s: error in SIOCGIFHWADDR ioctl reading (%s)", __func__, strerror(errno));
        goto err_sockfd;
    }

    memcpy(session->forward_ctx.src_eth_addr.addr, ifreq_c.ifr_hwaddr.sa_data,
           sizeof(session->forward_ctx.src_eth_addr.addr));
    session->forward_ctx.initialized = true;
    return true;

err_sockfd:
    close(sockfd);
    return false;
}


static bool
fsm_net_parser_forward(struct net_header_parser *net_parser)
{
    uint8_t *packet;
    int rc, len;

    packet = (uint8_t *)net_parser->start;
    len = net_parser->caplen;

    rc = sendto(net_parser->sock_fd, packet, len, 0,
                (struct sockaddr *)net_parser->raw_dst,
                sizeof(*net_parser->raw_dst));
    if (rc < 0)
    {
        LOGE("%s: failed to forward the packet (%s)", __func__, strerror(errno));
        return false;
    }

    return true;
}


/**
 * @brief forward packet to datapath
 *
 * @param session the dpi_dispatcher session
 * @param net_parser modified packet
 */
static bool
fsm_forward(struct fsm_session *session,
            struct net_header_parser *net_parser)
{
    uint8_t *packet;
    int rc, len;

    if (session == NULL) return false;
    if (net_parser == NULL) return false;

    if (net_parser->raw_dst != NULL)
    {
        /* The net parser is explicitly providing the TX interface to use */
        return fsm_net_parser_forward(net_parser);
    }

    if (!session->forward_ctx.initialized) return false;

    packet = (uint8_t *)net_parser->start;
    len = net_parser->caplen;

    rc = sendto(session->forward_ctx.sock_fd, packet, len, 0,
                (struct sockaddr *)&session->forward_ctx.raw_dst,
                sizeof(session->forward_ctx.raw_dst));
    if (rc < 0)
    {
        LOGE("%s: failed to forward the packet (%s)", __func__, strerror(errno));
        return false;
    }

    return true;
}

static bool
fsm_prepare_net_parser_forward(struct net_header_parser *net_parser)
{
    struct eth_header *eth_header;
    uint8_t *packet;

    packet = net_parser->start;

    eth_header = net_header_get_eth(net_parser);
    LOGD("%s: source mac: " PRI_os_macaddr_lower_t ", dst mac: " PRI_os_macaddr_lower_t,
         __func__,
         FMT_os_macaddr_pt(eth_header->srcmac),
         FMT_os_macaddr_pt(eth_header->dstmac));

    memcpy(net_parser->raw_dst->sll_addr, eth_header->dstmac,
           sizeof(os_macaddr_t));
    memcpy(packet + 6, net_parser->src_eth_addr->addr,
           sizeof(net_parser->src_eth_addr->addr));

    return true;
}


/**
 * @brief prepare packet forward
 *
 * Initialise forward cts if it is not initilaized
 * - replace smac with pod mac address.
 * @param session the dpi_dispatcher session
 * @param net_parser modified packet
 */
static bool
fsm_prepare_forward(struct fsm_session *session,
                    struct net_header_parser *net_parser)
{
    struct eth_header *eth_header;
    uint8_t *packet;
    bool rc;

    if (session == NULL) return false;
    if (net_parser == NULL) return false;

    if (net_parser->raw_dst != NULL)
    {
        /* The net parser is explicitly providing the TX interface to use */
        return fsm_prepare_net_parser_forward(net_parser);
    }

    packet = net_parser->start;

    eth_header = net_header_get_eth(net_parser);
    if (IS_NULL_PTR(eth_header->srcmac))
    {
        LOGD("%s: no source mac", __func__);
        return false;
    }
    if (IS_NULL_PTR(eth_header->dstmac))
    {
        LOGD("%s: no destination mac", __func__);
        return false;
    }

    LOGD("%s: source mac: " PRI_os_macaddr_lower_t ", dst mac: " PRI_os_macaddr_lower_t,
         __func__,
         FMT_os_macaddr_pt(eth_header->srcmac),
         FMT_os_macaddr_pt(eth_header->dstmac));

    if (!session->forward_ctx.initialized)
    {
        /* initialize forward context*/
        rc = fsm_init_forward_context(session);
        if (!rc)
        {
            LOGE("%s: failed initialize forward context", __func__);
            return rc;
        }
    }

    memcpy(session->forward_ctx.raw_dst.sll_addr, eth_header->dstmac,
           sizeof(os_macaddr_t));
    memcpy(packet + 6, session->forward_ctx.src_eth_addr.addr,
           sizeof(session->forward_ctx.src_eth_addr.addr));

    return true;
}


/**
 * @brief forward updated packet
 *
 * @param session the dpi_dispatcher session
 * @param net_parser modified packet
 */
void
fsm_forward_pkt(struct fsm_session *session,
                struct net_header_parser *net_parser)
{
    bool rc;

    if (net_parser->source == PKT_SOURCE_NFQ)
    {
        size_t len;

        len = net_parser->caplen - net_parser->offset;
        rc = nf_queue_update_payload(net_parser->packet_id, net_parser->nfq_queue_num, len);
        if (!rc)
        {
            LOGE("%s: Failed to update nfqueue payload", __func__);
        }
    }
    else if ((net_parser->source == PKT_SOURCE_PCAP) || (net_parser->source == PKT_SOURCE_SOCKET))
    {
        rc = fsm_prepare_forward(session, net_parser);
        if (!rc)
        {
            LOGE("%s: failed prepare forward ctx", __func__);
            return;
        }

        rc = fsm_forward(session, net_parser);
        if (!rc)
        {
            LOGE("%s: failed to forward packet", __func__);
        }
    }
}

