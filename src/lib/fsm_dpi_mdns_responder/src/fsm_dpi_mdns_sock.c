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
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt =  mgr->ctxt;

    if (!pctxt) return -1;

    sd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sd < 0)
        return -1;

    setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    memset(&mreqn, 0, sizeof(mreqn));

    inet_aton(pctxt->srcip, &mreqn.imr_address);
    ifindex = if_nametoindex(pctxt->txintf);
    mreqn.imr_ifindex = ifindex;
    /* Set interface for outbound multicast */
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF, &mreqn, sizeof(mreqn)))
    {
        LOGW("%s: mdns_daemon: Failed setting IP_MULTICAST_IF to %s: %s",
             __func__, pctxt->srcip, strerror(errno));
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

int
fsm_dpi_mdns_send_response(struct fsm_dpi_mdns_service *record)
{

}
