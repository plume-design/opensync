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

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "assert.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "mdnsd.h"
#include "sdtxt.h"

#include "mdns_plugin.h"

static void
mdnsd_record_received(const struct resource *r, void *data)
{
    char ipinput[INET_ADDRSTRLEN];

    switch(r->type) {
    case QTYPE_A:
        inet_ntop(AF_INET, &(r->known.a.ip), ipinput, INET_ADDRSTRLEN);
        LOGT("Got %s: A %s->%s", r->name, r->known.a.name, ipinput);
        break;

    case QTYPE_NS:
        LOGT("Got %s: NS %s", r->name, r->known.ns.name);
        break;

    case QTYPE_CNAME:
        LOGT("Got %s: CNAME %s", r->name, r->known.cname.name);
        break;

    case QTYPE_PTR:
        LOGT("Got %s: PTR %s", r->name, r->known.ptr.name);
        break;

    case QTYPE_TXT:
        LOGT("Got %s: TXT %s", r->name, r->rdata);
        break;

    case QTYPE_SRV:
        LOGT("Got %s: SRV %d %d %d %s", r->name, r->known.srv.priority,
            r->known.srv.weight, r->known.srv.port, r->known.srv.name);
        break;

    default:
        LOGT("Got %s: unknown", r->name);

    }
}

void
mdnsd_ctxt_update(struct mdns_session *md_session)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt  = mgr->ctxt;
    bool rc = false;

    if (!md_session || !pctxt) return;

    // Check for loglevel changes to update daemon.
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG))
    {
        LOGD("mdns_plugin: Setting daemon's log level to Debug");
        mdnsd_log_level("debug");
    } else {
        LOGD("mdns_plugin: Setting daemon's log level to Notice");
        mdnsd_log_level("notice");
    }

    // Get the latest mdns sip.
    rc = mdnsd_ctxt_set_srcip(md_session);
    // Get the latest mdns txintf.
    rc |= mdnsd_ctxt_set_txintf(md_session);

    if (!rc)
    {
        LOGD("mdns_plugin: No change in the ip/txintf");
        return;
    }
    // Close the current socket and open another one.
    close(pctxt->mcast_fd);
    pctxt->mcast_fd = pctxt->dmn_get_mcast_sock();

    return;
}

void
mdnsd_record_conflict(char *name, int type, void *arg)
{
    LOGE("mdnsd_daemon: Conflicting name detected: %s for type %d dropping record.", name, type);
    return;
}

void
mdnsd_remove_record(struct mdnsd_service *service)
{
    mdns_record_t *r;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt  = mgr->ctxt;
    char   hlocal[256] = {0};

    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", service->name, service->type);

    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    if (!r) return;

    // Remove it from the daemon records.
    mdnsd_done(pctxt->dmn, r);
    return;
}

bool
mdnsd_ctxt_start(struct mdnsd_context *pctxt)
{
    if (pctxt->enabled) return true;

    //create the socket fd.
    if ((pctxt->mcast_fd = pctxt->dmn_get_mcast_sock()) < 0) return false;

    // Initialize the evtimer.
    pctxt->dmn_ev_timer_init();
    //Initlaize the ev io.
    pctxt->dmn_ev_io_init();

    /**
      * create the handle for dmn, with internet class
      * and frame size 1400.
      */
    pctxt->dmn = mdnsd_new(QCLASS_IN, 1400);
    if (!pctxt->dmn) return false;
    // Register callback to read the rcvd records.
    mdnsd_register_receive_callback(pctxt->dmn, mdnsd_record_received, NULL);

    pctxt->enabled = true;
    return true;
}

void
mdnsd_ctxt_stop(struct mdnsd_context *pctxt)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    if (!pctxt->enabled) return;

    // close the mcast fd
    close(pctxt->mcast_fd);
    // stop the ev timer.
    ev_timer_stop(mgr->loop, &pctxt->timer);
    // stop the ev io.
    ev_io_stop(mgr->loop, &pctxt->read);
    // shutdown mdns daemon.
    mdnsd_shutdown(pctxt->dmn);
    mdnsd_free(pctxt->dmn);
    return;
}

bool
mdnsd_update_record(struct mdnsd_service *service)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt = mgr->ctxt;
    char hlocal[256] = {0};
    mdns_record_t *r;
    unsigned char *packet;
    xht_t *h;
    int len = 0;
    ds_tree_t *txt;

    if (!service) return false;

    // start the daemon if its not yet started.
    if (!mdnsd_ctxt_start(pctxt))
    {
        LOGE("mdnsd_daemon: Couldn't start the mdnsd daemon.");
        return false;
    }

    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", service->name, service->type);

    /**
      * Announce that we have a TXT service with record.
      * ttl for the TXT record is 600 seconds.
     */
    r = mdnsd_set_record(pctxt->dmn, 0, NULL, hlocal, QTYPE_TXT, 600, mdnsd_record_conflict, NULL);

    if (!r)
    {
        LOGE("mdnsd_daemon: Couldn't create mdnsd record.");
        return false;
    }


    txt = service->txt;
    if (!txt) return true;

    h = xht_new(11);
    struct str_pair *pair = ds_tree_head(txt);
    while(pair != NULL)
    {
        LOGT("mdns_daemon: %s=%s", pair->key, pair->value);
        xht_set(h, pair->key, pair->value);
        pair = ds_tree_next(txt, pair);
    }
    packet = sd2txt(h, &len);
    xht_free(h);
    mdnsd_set_raw(pctxt->dmn, r, (char *)packet, len);
    free(packet);
    return true;
}

void
mdnsd_recv_cb(EV_P_ ev_io *r, int revents)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;
    struct timeval       *tv = &pctxt->sleep_tv;
    int    rc;

    rc = mdnsd_step(pctxt->dmn, pctxt->mcast_fd, true, true, tv);

    if (rc == 2)
    {
        LOGE("mdns_daemon: Failed to read from the socket: %s", strerror(errno));
    }
    return;
}

void
mdnsd_timer_cb(EV_P_ struct ev_timer *w, int revents)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;
    struct timeval       *tv = &pctxt->sleep_tv;
    int    rc;

    loop  = mgr->loop;
    rc = mdnsd_step(pctxt->dmn, pctxt->mcast_fd, false, true, tv);

    if (rc == 1)
    {
        LOGE("Failed writing to socket: %s", strerror(errno));
    }

    LOGD("mdns_daemon: Going to sleep for %lu",tv->tv_sec);
    ev_timer_stop(loop, &pctxt->timer);
    ev_timer_set(&pctxt->timer, tv->tv_sec, 0.);
    ev_timer_start(loop, &pctxt->timer);

    return;
}


void
mdnsd_ev_io_init(void)
{
    struct mdnsd_context *pctxt = NULL;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct ev_loop *loop;

    pctxt = mgr->ctxt;

    loop = mgr->loop;

    ev_io_init(&pctxt->read, pctxt->dmn_rcvcb, pctxt->mcast_fd, EV_READ);
    ev_io_start(loop, &pctxt->read);
    return;
}

void
mdnsd_ev_timer_init(void)
{
    struct mdnsd_context *pctxt = NULL;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct ev_loop       *loop;

    pctxt = mgr->ctxt;

    loop  = mgr->loop;
    // Starts with time out zero.
    ev_timer_init(&pctxt->timer, pctxt->dmn_timercb, 0, 0);
    ev_timer_start(loop, &pctxt->timer);

    return;
}

/* Create multicast 224.0.0.251:5353 socket */
static int
mdnsd_create_mcastv4_socket(void)
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
        LOGW("mdns_daemon: Failed setting IP_MULTICAST_IF to %s: %s",
             pctxt->srcip, strerror(errno));
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
        LOGW("mdns_daemon: Failed to add multicast membership for mdns.");
    }
    return sd;

err_socket:
    close(sd);
    return -1;
}

bool
mdnsd_ctxt_set_txintf(struct mdns_session *md_session)
{
    struct fsm_session  *session = NULL;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt =  mgr->ctxt;
    char   *tx_if;

    if (!md_session || !pctxt) return false;

    session = md_session->session;

    if (pctxt->txintf && !strcmp(pctxt->txintf, session->tx_intf))
    {
        LOGD("mdns_daemon: No change in the interface.");
        return false;
    }

    tx_if = strdup(session->tx_intf);
    pctxt->txintf = tx_if;
    return true;
}

bool
mdnsd_ctxt_set_srcip(struct mdns_session *md_session)
{
    char *srcip = NULL;
    struct fsm_session  *session = NULL;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt =  mgr->ctxt;

    if (!md_session || !pctxt) return false;


    session = md_session->session;
    if (session->ops.get_config != NULL)
    {
        srcip = session->ops.get_config(session, "mdns_src_ip");
    }

    if (!srcip) return false;

    if (pctxt->srcip && !strcmp(pctxt->srcip, srcip))
    {
        LOGD("mdns_daemon: No change in the sip.");
        return false;
    }
    pctxt->srcip = srcip;
    return true;
}

int
mdnsd_ctxt_get_mcast_fd(void)
{
    return mdnsd_create_mcastv4_socket();
}

bool
mdnsd_ctxt_init(struct mdns_session *md_session)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;

    if (pctxt) return true;

    pctxt = calloc(1, sizeof(struct mdnsd_context));
    if (!pctxt)
    {
        LOGE("mdns_plugin: Couldn't allocate mdnsd context");
        return false;
    }

    mgr->ctxt = pctxt;

    // Set callbacks.
    pctxt->dmn_ev_io_init = mdnsd_ev_io_init;
    pctxt->dmn_ev_timer_init = mdnsd_ev_timer_init;
    pctxt->dmn_get_mcast_sock = mdnsd_ctxt_get_mcast_fd;
    pctxt->dmn_rcvcb = mdnsd_recv_cb;
    pctxt->dmn_timercb = mdnsd_timer_cb;
    ds_tree_init(&pctxt->services, mdnsd_service_cmp,
                 struct mdnsd_service, service_node);

    if(!mdnsd_ctxt_set_srcip(md_session))
    {
        LOGW("mdns_daemon: Couldn't set src ip.");
    }
    if(!mdnsd_ctxt_set_txintf(md_session))
    {
        LOGW("mdns_daemon: Couldn't set the outgoing if.");
    }
    return true;
}

void
mdnsd_ctxt_exit(void)
{
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;

    if (!pctxt) return;

    mdnsd_ctxt_stop(pctxt);
    mdnsd_free_services();
    free(pctxt->txintf);
    free(pctxt);
    mgr->ctxt = NULL;
    return;
}
