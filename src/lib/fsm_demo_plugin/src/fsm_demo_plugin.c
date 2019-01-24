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
#include <stddef.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include "const.h"
#include "log.h"
#include "fsm_demo_plugin.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"

static struct fsm_demo_plugin_cache cache_mgr = { 0 };

static struct fsm_demo_plugin_cache *get_cache_mgr(void) {
    return &cache_mgr;
}

uint32_t parse_eth_header(const struct pcap_pkthdr *h, const uint8_t *bytes,
                          struct fsm_demo_plugin_parser *parser) {
    struct eth_header *eth_header = &parser->eth_header;
    uint32_t parsed = 0;
    int i;

    if (h->len < ETH_HLEN) {
        LOGD("%s: captured length %u smaller than %u", __func__, h->len, ETH_HLEN);
        return 0;
    }

    for (i = 0; i < ETH_ALEN; i++) {
        eth_header->dstmac.addr[i] = bytes[i];
        eth_header->srcmac.addr[i + ETH_ALEN] = bytes[i + ETH_ALEN];
    }
    parsed = 2 * ETH_ALEN;

    if (parser->pcap_datalink == DLT_LINUX_SLL) {
        parsed += 2;
    }

    eth_header->ethertype = ntohs(*(uint16_t *)(bytes + parsed));
    if (eth_header->ethertype == ETH_P_8021Q) {
        parsed += 4;
        eth_header->ethertype = ntohs(*(uint16_t *)(bytes + parsed));
    }
    parsed += 2;

    return parsed;
}

/**
 * fsm_demo_plugin_init: dso initialization entry point
 * @session: session pointer provided by fsm
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap ahdler and the periodic routines called
 * by fsm.
 */
int fsm_demo_plugin_init(struct fsm_session *session) {
    struct fsm_demo_plugin_parser *parser = NULL;
    struct fsm_pcaps *pcaps = session->pcaps;
    pcap_t *pcap = pcaps->pcap;
    struct fsm_demo_plugin_cache *mgr = get_cache_mgr();

    session->handler = fsm_demo_plugin_handler;
    session->periodic = fsm_demo_plugin_periodic;
    session->exit = fsm_demo_plugin_exit;
    session->handler_ctxt = calloc(sizeof(struct fsm_demo_plugin_parser), 1);
    parser = session->handler_ctxt;
    if (parser == NULL) {
        LOGE("could not allocate fsm_demo parser");
        return -1;
    }

    parser->pcap_datalink = pcap_datalink(pcap);
    if (parser->pcap_datalink != DLT_EN10MB &&
        parser->pcap_datalink != DLT_LINUX_SLL) {
        LOGE("%s: unsupported data link layer: %d\n",
             __func__, parser->pcap_datalink);
        goto error;
    }

    memset(&cache_mgr, 0, sizeof(cache_mgr));
    parser->fsm_context = session;
    mgr->session = session;
    mgr->initialized = 1;
    LOGI("%s: plugin %s initialized", __func__, session->conf->handler);
    return 0;

  error:
    free(parser);
    return -1;
}

/**
 * fsm_demo_plugin_handler: pcap handler for this fsm plugin
 * @args: plugin context passed to the the pcap framework
 * @orig_header: pcap header
 * @orig_packet: captured packet
 *
 * Conforms to the pcap_dispatch() pcap_handler callback argument
 */
void fsm_demo_plugin_handler(uint8_t *args,
                             const struct pcap_pkthdr *h,
                             const uint8_t *bytes) {
    LOGI("%s triggered", __func__);
    struct fsm_demo_plugin_parser *parser =
        (struct fsm_demo_plugin_parser *)args;
    struct iphdr *iphdr;
    struct udphdr *udphdr;
    uint32_t parsed;
    char ip_buf[128];
    struct fsm_demo_plugin_cache *mgr = get_cache_mgr();

    parsed = parse_eth_header(h, bytes, parser);
    if (parsed == 0) {
        return;
    }

    if (parser->eth_header.ethertype != ETH_P_IP) {
        LOGE("%s: unsupported ethertype: %04x\n",
             __func__, parser->eth_header.ethertype);
        return;
    }

    if ((h->len - parsed) < sizeof(struct iphdr)) {
        LOGD("%s: ip header too short %u", __func__, (h->len - parsed));
        return;
    }

    iphdr = (struct iphdr *)(bytes + parsed);
    if (iphdr->protocol != IPPROTO_UDP) {
        LOGE("Transport %u not yet supported", iphdr->protocol);
        return;
    }
    parsed += (iphdr->ihl * 4);

    if ((h->len - parsed) < sizeof(struct udphdr)) {
        LOGD("%s: udp header too short %u", __func__, (h->len - parsed));
        return;
    }

    udphdr = (struct udphdr *)(bytes + parsed);
    parsed += (sizeof(struct udphdr));

    LOGD("%s: source mac: " PRI(os_macaddr_t) ", dst mac: " PRI(os_macaddr_t),
         __func__,
         FMT(os_macaddr_t, parser->eth_header.srcmac),
         FMT(os_macaddr_t, parser->eth_header.dstmac));

    if (iphdr->version == 4) {
        inet_ntop(AF_INET, &iphdr->daddr, ip_buf, sizeof(ip_buf));
        LOGD("%s: dst address %s, dst port %d",
             __func__, ip_buf, ntohs(udphdr->dest));

        inet_ntop(AF_INET, &iphdr->saddr, ip_buf, sizeof(ip_buf));
        LOGD("%s: src address %s, src port %d",
             __func__, ip_buf, ntohs(udphdr->source));

    }
    mgr->pkt_count++;
}

/**
 * fsm_demo_plugin_periodic: periodic handler for this fsm plugin
 * @session: session pointer provided by fsm
 *
 * Called periodically by fsm
 */
void fsm_demo_plugin_periodic(struct fsm_session *session) {
    struct fsm_demo_plugin_cache *mgr = get_cache_mgr();

    if (mgr->initialized == 0) {
        LOGT("%s: fsm demo session not initialized", __func__);
        return;
    }

    LOGI("%s: handled packet count: %d", __func__, mgr->pkt_count);
}

/**
 * fsm_demo_plugin_exit: exit handler for this fsm plugin
 * @session: session pointer provided by fsm
 *
 * Called by fsm when the plugin is removed from the fsm configuration
 */
void fsm_demo_plugin_exit(struct fsm_session *session) {
    struct fsm_demo_plugin_cache *mgr = get_cache_mgr();

    if (mgr->initialized == 0) {
        LOGT("%s: fsm demo session not initialized", __func__);
        return;
    }
    mgr->initialized = 0;
    free(session->handler_ctxt);
    LOGI("%s: done with exit callback", __func__);
}
