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

#include <ev.h>
#include <pcap.h>

#include "log.h"
#include "const.h"
#include "execsh.h"
#include "target.h"
#include "osn_netif.h"

#define OSP_L2UF_PCAP_SNAPLEN       1024
#define OSP_L2UF_PCAP_BUFFER_SIZE   (64*1024)

struct eth_hdr
{
    uint8_t                 eth_dst[6];         /* Hardware destination address */
    uint8_t                 eth_src[6];         /* Hardware source address */
    uint16_t                eth_type;           /* Packet type */
};


/* BPF syntax for capturing L2UF frames (LLC XID packets) */
#define L2UF_BPF_FILTER "ether broadcast and not ip and not ip6 and not arp"

static pcap_t               *l2uf_pcap = NULL;
static struct bpf_program   l2uf_bpf;
static ev_io                l2uf_watcher;
static osn_netif_t*          l2uf_netif;
static ev_timer osp_l2uf_init_timer;

static void osp_l2uf_process(struct ev_loop *loop, ev_io *watcher, int revent);
static void osp_l2uf_recv(u_char *self, const struct pcap_pkthdr *pkt, const u_char *packet);
void osp_l2uf_start(void);

static void osp_l2uf_init_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    /* Call the actual initialization function */
    osp_l2uf_start();
}

void l2uf_netif_status_fn(osn_netif_t *netif, struct osn_netif_status *status)
{
    if (status->ns_exists && status->ns_up)
    {
        ev_timer_start(EV_DEFAULT, &osp_l2uf_init_timer);
    }
    else
    {
        ev_timer_stop(EV_DEFAULT, &osp_l2uf_init_timer);
    }

}

bool osp_l2uf_init(void)
{
    ev_timer_init(&osp_l2uf_init_timer, osp_l2uf_init_fn, 1.0, 1.0);
    l2uf_netif = osn_netif_new(CONFIG_OSP_L2UF_LIBPACP_DEVICE);
    osn_netif_status_notify(l2uf_netif, l2uf_netif_status_fn);
    return true;
}

void osp_l2uf_start(void)
{
    int rc;
    char pcap_err[PCAP_ERRBUF_SIZE];

    if (l2uf_pcap != NULL) return;

    LOG(DEBUG, "l2uf: Init, interface: %s", CONFIG_OSP_L2UF_LIBPACP_DEVICE);

    /*
     * Create a PCAP instance for listening to L2UF frames
     */
    l2uf_pcap = pcap_create(CONFIG_OSP_L2UF_LIBPACP_DEVICE, pcap_err);
    if (l2uf_pcap == NULL)
    {
        LOG(ERR, "l2uf: Error creating PCAP instance: %s", pcap_err);
        goto error;
    }

    /* Set immediate mode -- this forces the packet to be pushed from the kernel as they are received (as opposed to
     * waiting for the buffer to fill up) */
    rc = pcap_set_immediate_mode(l2uf_pcap, 1);
    if (rc != 0)
    {
        LOG(NOTICE, "l2uf: Error setting pcap immediate mode.");
        goto error;
    }

    /*
     * Set the snapshot length to something sensible.
     */
    rc = pcap_set_snaplen(l2uf_pcap, OSP_L2UF_PCAP_SNAPLEN);
    if (rc != 0)
    {
        LOG(WARN, "l2uf: Unable to set snapshot length: %d",
                OSP_L2UF_PCAP_SNAPLEN);
    }

    /*
     * Set the buffer size.
     */
    rc = pcap_set_buffer_size(l2uf_pcap, OSP_L2UF_PCAP_BUFFER_SIZE);
    if (rc != 0)
    {
        LOG(WARN, "l2uf: Unable to set buffer size: %d",
                OSP_L2UF_PCAP_BUFFER_SIZE);
    }

    /*
     * Set non-blocking mode
     * XXX pcap_setnonblock() returns the current non-blocking state. However, on savefiles
     * it will always return 0. So the proper way to check for errors is to check if it returns
     * -1.
     */
    rc = pcap_setnonblock(l2uf_pcap, 1, pcap_err);
    if (rc == -1)
    {
        LOG(NOTICE, "l2uf: Error setting pcap non-blocking mode: %s", pcap_err);
        goto error;
    }

    /*
     * We do not want to block forever on receive. A timeout 0 means block
     * forever, so use 1ms for the timeout.
     */
    rc = pcap_set_timeout(l2uf_pcap, 1);
    if (rc != 0)
    {
        LOG(ERR, "l2uf: Error setting buffer timeout.");
        goto error;
    }

    /* Activate the interface -- apparently we need to do this before installing the BPF filter */
    rc = pcap_activate(l2uf_pcap);
    if (rc != 0)
    {
        LOG(TRACE, "l2uf: Error activating PCAP interface: %s",
                pcap_geterr(l2uf_pcap));
        goto error;
    }

    /* Create the BPF filter for L2UF frames, namely use "llc xid" to filter anything but XID LLC frames */
    rc = pcap_compile(l2uf_pcap, &l2uf_bpf, L2UF_BPF_FILTER, 0, PCAP_NETMASK_UNKNOWN);
    if (rc != 0)
    {
        LOG(ERR, "l2uf: Error compiling  BPF filter: %s: %s", L2UF_BPF_FILTER, pcap_geterr(l2uf_pcap));
        goto error;
    }

    /* And install the filter -- handle the error after pcap_freecode() */
    rc = pcap_setfilter(l2uf_pcap, &l2uf_bpf);
    pcap_freecode(&l2uf_bpf);
    /* Handle error from pcap_setfilter() after pcap_freecode() */
    if (rc != 0)
    {
        LOG(ERR, "l2uf: Error installing BPF filter: %s", pcap_geterr(l2uf_pcap));
        goto error;
    }

    /* Get the PCAP socket file descriptor */
    rc = pcap_get_selectable_fd(l2uf_pcap);
    if (rc < 0)
    {
        LOG(ERR, "l2uf: Error getting selectable FD, error: %s", pcap_geterr(l2uf_pcap));
        goto error;
    }

    /* Initialize a libev watcher for monitoring the PCAP socket; it should be in non-blocking mode */
    ev_io_init(&l2uf_watcher, osp_l2uf_process, rc, EV_READ);
    ev_io_start(EV_DEFAULT, &l2uf_watcher);

    LOG(NOTICE, "l2uf: client roaming mitigation initialized.");
    ev_timer_stop(EV_DEFAULT, &osp_l2uf_init_timer);
    return;

error:
    if (l2uf_pcap != NULL)
    {
        pcap_close(l2uf_pcap);
    }

    /* Retry later */
    l2uf_pcap = NULL;

}


/* This function processes a burst of packets from PCAP and calls osp_l2uf_recv() for each packet received */
void osp_l2uf_process(struct ev_loop *loop, ev_io *watcher, int revent)
{
    (void)loop;
    (void)watcher;
    (void)revent;

    int rc;

    rc = pcap_dispatch(l2uf_pcap, 64, osp_l2uf_recv, NULL);
    if (rc == -1)
    {
        /* pcap_dispatch() may return an error */
        LOG(NOTICE, "l2uf: pcap_dispatch() returned error. Interface went down?");
        /* Stop the filedescriptor watcher */
        ev_io_stop(EV_DEFAULT, &l2uf_watcher);
        /* Stop libpcap */
        pcap_close(l2uf_pcap);
        l2uf_pcap = NULL;
        ev_timer_start(EV_DEFAULT, &osp_l2uf_init_timer);
    }
}

bool osp_l2uf_acc_mac_flush(const char *mac)
{
    char om_flow[54];
    bool ret = 0;

    /*
     * WORKAROUND
     * Fake event TARGET_OM_POST_ADD with mac to trigger cache flush
     * Proper target function does not exist yet
     * Fake openflow rule "tcp,tp_dst=80,dl_src=MAC"
     */

    snprintf(om_flow, sizeof(om_flow), "tcp,tp_dst=80,dl_src=%s", mac);
    ret = target_om_hook(TARGET_OM_POST_ADD, om_flow);

    return ret;
}

/* Process a single frame from PCAP */
void osp_l2uf_recv(
        u_char *self,
        const struct pcap_pkthdr *pkt,
        const u_char *packet)
{
    (void)self;
    (void)pkt;
    char smac[32];
    struct eth_hdr *eth = (void *)packet;

    snprintf(smac, sizeof(smac), "%02X:%02X:%02X:%02X:%02X:%02X",
            eth->eth_src[0],
            eth->eth_src[1],
            eth->eth_src[2],
            eth->eth_src[3],
            eth->eth_src[4],
            eth->eth_src[5]);

    LOG(NOTICE, "l2uf: Received L2UF frame from %s", smac);

    if (osp_l2uf_acc_mac_flush(smac))
    {
        LOG(INFO, "l2uf: flushed flows for %s", smac);
    }
    else
    {
        LOG(INFO, "l2uf: Failed to flush flows for %s", smac);
    }
}
