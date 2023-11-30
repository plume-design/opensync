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
#include <errno.h>
#include <sys/socket.h>
#include <ev.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "assert.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "memutil.h"
#include "1035.h"

#include "fsm_dpi_mdns_responder.h"

#define MDNS_TTL 600
#define MDNS_MAX_TXT_LEN 255

/* Create multicast 224.0.0.251:5353 socket */
int
fsm_dpi_mdns_create_mcastv4_socket(void)
{
    struct sockaddr_in sin;
    struct ip_mreq mc;
    struct ip_mreqn mreqn;
    int unicast_ttl = 255;
    int sd, flag = 1;
    uint32_t ifindex = -1;
    struct dpi_mdns_resp_client *mgr = fsm_dpi_mdns_get_mgr();

    if (!mgr) return -1;

    if (mgr->srcip == NULL) return -1;

    sd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sd < 0)
        return -1;

    setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    memset(&mreqn, 0, sizeof(mreqn));

    inet_aton(mgr->srcip, &mreqn.imr_address);
    ifindex = if_nametoindex(mgr->txintf);
    mreqn.imr_ifindex = ifindex;
    /* Set interface for outbound multicast */
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF, &mreqn, sizeof(mreqn)))
    {
        LOGW("%s: mdns_daemon: Failed setting IP_MULTICAST_IF to %s: %s",
             __func__, mgr->srcip, strerror(errno));
    }

    /* mDNS also supports unicast, so we need a relevant TTL there too */
    setsockopt(sd, IPPROTO_IP, IP_TTL, &unicast_ttl, sizeof(unicast_ttl));

    /* Filter inbound traffic from anyone (ANY) to port 5353 */
    memset(&sin, 0, sizeof(sin));

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(5353);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sd, (struct sockaddr *)&sin, sizeof(sin))) {
        goto err_socket;
    }

    // Join mDNS link-local group on the given interface
    mreqn.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn, sizeof(mc)))
    {
        LOGW("%s: mdns_daemon: Failed to add multicast membership for mdns", __func__);
    }
    return sd;

err_socket:
    close(sd);
    return -1;
}

/**
 * @brief Converts a bytes array in a hex dump file wireshark can import.
 *
 * Dumps the array in a file that can then be imported by wireshark.
 * Useful to visualize the packet content.
 */
void
create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
{
    int line_number = 0;
    bool new_line = true;
    size_t i;
    FILE *f;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))  return;

    f = fopen(fname, "w+");

    if (f == NULL) return;

    for (i = 0; i < len; i++)
    {
	 new_line = (i == 0 ? true : ((i % 8) == 0));
	 if (new_line)
	 {
	      if (line_number) fprintf(f, "\n");
	      fprintf(f, "%06x", line_number);
	      line_number += 8;
	 }
         fprintf(f, " %02x", buf[i]);
    }
    fprintf(f, "\n");
    fclose(f);

    return;
}

static void
fsm_dpi_populate_txt_records(struct fsm_dpi_mdns_service *service, struct message *msg)
{
    ds_tree_t *pairs = service->txt;
    unsigned char *combined_data;
    struct str_pair *pair;
    char text_record[256];
    unsigned char *ptr;
    int data_len = 0;

    if (!pairs) return;

    combined_data  = CALLOC(service->n_txt_recs, MDNS_MAX_TXT_LEN);
    pair = ds_tree_head(pairs);
    ptr = combined_data;

    LOGT("%s(): populating TXT records:", __func__);
    while(pair != NULL)
    {
        uint8_t txt_len;

        snprintf(text_record, sizeof(text_record), "%s=%s", pair->key, pair->value);
        LOGT("%s(): processing text record %s", __func__, text_record);
        txt_len = strlen(text_record);

        MEM_CPY(ptr, &txt_len, sizeof(txt_len));
        ptr += sizeof(txt_len);
        MEM_CPY(ptr, text_record, txt_len);
        ptr += txt_len;

        data_len += txt_len;
        pair = ds_tree_next(pairs, pair);
    }
    message_rdata_raw(msg, combined_data, data_len + 1);

    FREE(combined_data);
}

static int
fsm_dpi_populate_mdns_reply(struct fsm_dpi_mdns_service *service, struct message *msg)
{
    struct dpi_mdns_resp_client *mgr;
    struct mdns_record *rec;

    mgr = fsm_dpi_mdns_get_mgr();
    if (!mgr->initialized) return false;

    rec = &mgr->curr_mdns_rec_processed;

    LOGT("%s():%d populating mdns reply pkts with rec name: %s", __func__, __LINE__, rec->qname);
    memset(msg, 0, sizeof(*msg));

    /* populate header */
    msg->header.qr = 1;
    msg->header.aa = 1;
    msg->id = 0;

    /* populate question header */
    message_qd(msg, rec->qname, QTYPE_TXT, QTYPE_A);
    /* populate answers header */
    message_an(msg, rec->qname, QTYPE_TXT, QTYPE_A, MDNS_TTL);
    /* populate text records */
    fsm_dpi_populate_txt_records(service, msg);

    return 0;
}

static void
fsm_dpi_mdns_set_ip(struct sockaddr_in *to, bool unicast, struct net_header_parser *net_parser)
{
    struct iphdr * iphdr;
    struct in_addr ip;

    memset(to, 0, sizeof(*to));

    to->sin_family = AF_INET;
    to->sin_port = htons(5353);

    iphdr = net_header_get_ipv4_hdr(net_parser);
    if (iphdr == NULL) return;

    ip.s_addr = iphdr->saddr;

    if (unicast) ip.s_addr = iphdr->saddr;
    else ip.s_addr = inet_addr("224.0.0.251");

    to->sin_addr = ip;
}

bool
fsm_dpi_mdns_send_response(struct fsm_dpi_mdns_service *service, bool unicast, struct net_header_parser *net_parser)
{
    struct dpi_mdns_resp_client *mgr;
    struct sockaddr_in to;
    struct message msg;
    unsigned char *buf;
    ssize_t len;

    /* populate address to send */
    fsm_dpi_mdns_set_ip(&to, unicast, net_parser);

    mgr = fsm_dpi_mdns_get_mgr();
    if (!mgr->initialized) return false;

    LOGT("%s: Send mdns %scast response for qname %s", __func__, unicast ? "uni": "multi", service->name);

    fsm_dpi_populate_mdns_reply(service, &msg);

    /* copy msg struct to buffer */
    len = message_packet_len(&msg);
    buf = message_packet(&msg);

    LOGT("%s(): sending mDNS reply", __func__);
    ssize_t n = sendto(mgr->mcast_fd, buf, len, 0, (struct sockaddr *)&to, sizeof(to));
    if (n < 0) {
        perror("sendto");
        return false;
    }
    return true;
}
