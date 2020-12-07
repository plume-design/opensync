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

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <ev.h>
#include <pcap.h>

#include "os_time.h"
#include "osn_types.h"
#include "osn_dhcp.h"

#include "wano.h"
#include "module.h"

/** PCAP Snapshot length */
#define WANP_ETHCLIENT_PCAP_SNAPLEN     512
/** PCAP rule for sniffing DHCP packets */
#define WANP_ETHCLIENT_PCAP_FILTER      "inbound and udp and (port bootpc or port bootps)"

struct wanp_ethclient
{
    wano_plugin_handle_t        ec_handle;          /** Plug-in handle */
    wano_plugin_status_fn_t    *ec_status_fn;       /** Plug-in status callback */
    wano_ppline_event_t         ec_ppline_event;    /** WANO pipeline event watcher */
    pcap_t                     *ec_pcap;            /** pcap handle */
    ev_io                       ec_pcap_evio;       /** pcap I/O watcher */
    osn_mac_addr_t              ec_client_mac;      /** Client MAC address */
    int                         ec_client_discnum;  /** Number discover messages from client detected */
    ev_timer                    ec_client_timer;    /** Client timer, started when the first DHCP packet is seen */
};

#define DHCP_MAGIC              0x63825363      /* Magic number */
struct dhcp_hdr
{
    uint8_t                 dhcp_op;            /* Operation ID */
    uint8_t                 dhcp_htype;         /* Hardware address type */
    uint8_t                 dhcp_hlen;          /* Hardware address length */
    uint8_t                 dhcp_hops;          /* Hops */
    uint32_t                dhcp_xid;           /* Transaction ID */
    uint16_t                dhcp_secs;          /* Time */
    uint16_t                dhcp_flags;         /* Flags */
    struct in_addr          dhcp_ciaddr;        /* Client IP address */
    struct in_addr          dhcp_yiaddr;        /* Your IP address */
    struct in_addr          dhcp_siaddr;        /* Server IP address */
    struct in_addr          dhcp_giaddr;        /* Gateway IP address */
    union
    {
        uint8_t             dhcp_chaddr[6];     /* Client hardware address */
        uint8_t             dhcp_padaddr[16];
    };
    uint8_t                 dhcp_server[64];    /* Server name */
    uint8_t                 dhcp_boot_file[128];/* Boot filename */
    uint32_t                dhcp_magic;         /* Magic */
    uint8_t                 dhcp_options[0];    /* DHCP options */
};

static void wanp_ethclient_module_start(void);
static void wanp_ethclient_module_stop(void);
static wano_plugin_ops_init_fn_t wanp_ethclient_init;
static wano_plugin_ops_run_fn_t wanp_ethclient_run;
static wano_plugin_ops_fini_fn_t wanp_ethclient_fini;
static wano_ppline_event_fn_t wanp_ethclient_ppline_event_fn;
static bool wanp_ethclient_pcap_open(struct wanp_ethclient *self);
static bool wanp_ethclient_pcap_close(struct wanp_ethclient *self);
static void wanp_ethclient_pcap_evio_fn(struct ev_loop *loop, ev_io *w, int revent);
static void wanp_ethclient_pcap_fn(u_char *data, const struct pcap_pkthdr *h, const u_char *pkt);

static void wanp_ethclient_dhcp_process(
        struct wanp_ethclient *self,
        bool is_discover,
        osn_mac_addr_t *client_mac);

static void wanp_ethclient_dhcp_timeout(struct ev_loop *loop, ev_timer *ev, int revent);

static struct wano_plugin wanp_ethclient_module = WANO_PLUGIN_INIT(
        "ethclient",
        0,
        WANO_PLUGIN_MASK_IPV4 | WANO_PLUGIN_MASK_ALL,
        wanp_ethclient_init,
        wanp_ethclient_run,
        wanp_ethclient_fini);


wano_plugin_handle_t *wanp_ethclient_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    struct wanp_ethclient *self;

    self = calloc(1, sizeof(struct wanp_ethclient));
    ASSERT(self != NULL, "Error allocating ethclient object")

    self->ec_client_mac = OSN_MAC_ADDR_INIT;
    self->ec_handle.wh_plugin = wp;
    STRSCPY(self->ec_handle.wh_ifname, ifname);
    self->ec_status_fn = status_fn;

    ev_timer_init(
            &self->ec_client_timer,
            wanp_ethclient_dhcp_timeout,
            (double)CONFIG_MANAGER_WANO_PLUGIN_ETHCLIENT_DHCP_WAIT,
            0.0);

    return &self->ec_handle;
}

void wanp_ethclient_run(wano_plugin_handle_t *wh)
{
    wano_ppline_t *wpl;

    struct wanp_ethclient *self = CONTAINER_OF(wh, struct wanp_ethclient, ec_handle);

    /* Register to the pipeline events */
    wpl = wano_ppline_from_plugin_handle(wh);
    if (wpl != NULL)
    {
        wano_ppline_event_init(&self->ec_ppline_event, wanp_ethclient_ppline_event_fn);
        wano_ppline_event_start(&self->ec_ppline_event, wpl);
    }
    else
    {
        LOG(ERR, "ethclient: %s: Error acquiring pipeline object.", wh->wh_ifname);
    }

    /* Detach the plug-in immediately, if succesfull */
    if (wanp_ethclient_pcap_open(self))
    {
        self->ec_status_fn(&self->ec_handle, &WANO_PLUGIN_STATUS(WANP_DETACH));
    }
    else
    {
        LOG(ERR, "ethclient: %s: Error initializing PCAP.",
                self->ec_handle.wh_ifname);
        self->ec_status_fn(&self->ec_handle, &WANO_PLUGIN_STATUS(WANP_ERROR));
    }
}

void wanp_ethclient_fini(wano_plugin_handle_t *wh)
{
    struct wanp_ethclient *self = CONTAINER_OF(wh, struct wanp_ethclient, ec_handle);

    wanp_ethclient_pcap_close(self);
    wano_ppline_event_stop(&self->ec_ppline_event);

    free(wh);
}

void wanp_ethclient_ppline_event_fn(wano_ppline_event_t *wpe, enum wano_ppline_status status)
{
    struct wanp_ethclient *self = CONTAINER_OF(wpe, struct wanp_ethclient, ec_ppline_event);

    switch (status)
    {
        case WANO_PPLINE_OK:
            wanp_ethclient_pcap_close(self);
            break;

        case WANO_PPLINE_IDLE:
        case WANO_PPLINE_RESTART:
            break;
    }
}

/*
 * ===========================================================================
 *  DHCP sniffing implementation
 * ===========================================================================
 */

/*
 * Create a PCAP interface for the interface that the plug-in is handling,
 * install a DHCP filter rule and kick-off the fd watcher
 */
bool wanp_ethclient_pcap_open(struct wanp_ethclient *self)
{
    char pcap_err[PCAP_ERRBUF_SIZE];
    int pcap_fd;

    if (self->ec_pcap != NULL) return true;

    /*
     * Create the PCAP instance and set various options
     */
    self->ec_pcap = pcap_create(self->ec_handle.wh_ifname, pcap_err);
    if (self->ec_pcap == NULL)
    {
        LOG(ERR, "ethclient: %s: pcap: Error creating instance: %s",
                self->ec_handle.wh_ifname, pcap_err);
        goto error;
    }

    if (pcap_set_immediate_mode(self->ec_pcap, 1) != 0)
    {
        LOG(WARN, "ethclient: %s: pcap: Error enabling immediate mode.",
                self->ec_handle.wh_ifname);
    }

    if (pcap_set_snaplen(self->ec_pcap, WANP_ETHCLIENT_PCAP_SNAPLEN) != 0)
    {
        LOG(WARN, "ethclient: %s: pcap: Unable to set snapshot length to %d. Error: %s",
                self->ec_handle.wh_ifname, WANP_ETHCLIENT_PCAP_SNAPLEN, pcap_geterr(self->ec_pcap));
    }

    if (pcap_setnonblock(self->ec_pcap, 1, pcap_err) == -1)
    {
        LOG(ERR, "ethclient: %s: pcap: Error setting non-blocking mode: %s",
                self->ec_handle.wh_ifname, pcap_err);
        goto error;
    }

    /* We do not want to block forever on receive, set an arbitrary low timeout */
    if (pcap_set_timeout(self->ec_pcap, 1) != 0)
    {
        LOG(WARN, "ethclient: %s: pcap: Error setting timeout. Error: %s",
                self->ec_handle.wh_ifname, pcap_geterr(self->ec_pcap));
    }

    if (pcap_activate(self->ec_pcap) != 0)
    {
        LOG(ERR, "ethclient: %s: pcap: Error activating interface. Error: %s",
                self->ec_handle.wh_ifname, pcap_geterr(self->ec_pcap));
        goto error;
    }

    /*
     * Create a BPF filter for DHCP traffic and assign it to the PCAP instance
     */

    static struct bpf_program bpf;

    if (pcap_compile(self->ec_pcap, &bpf, WANP_ETHCLIENT_PCAP_FILTER, 0, PCAP_NETMASK_UNKNOWN) != 0)
    {
        LOG(ERR, "ethclient: %s: pcap: Error creating BPF filter: '%s'. Error: %s",
                self->ec_handle.wh_ifname, WANP_ETHCLIENT_PCAP_FILTER, pcap_geterr(self->ec_pcap));
        goto error;
    }

    if (pcap_setfilter(self->ec_pcap, &bpf) != 0)
    {
        LOG(WARN, "ethclient: %s: pcap: Error installing BPF filter: %s. Error: %s",
                self->ec_handle.wh_ifname, WANP_ETHCLIENT_PCAP_FILTER, pcap_geterr(self->ec_pcap));
    }

    /* The bpf structure can be freed after the filter has been set */
    pcap_freecode(&bpf);

    /*
     * Get the PCAP file descriptor and kick-off the event watcher
     */

    /* Obtain the PCAP file descriptor */
    pcap_fd = pcap_get_selectable_fd(self->ec_pcap);
    if (pcap_fd < 0)
    {
        LOG(ERR, "ethclient: %s: pcap: Error obtaining file descriptor. Error: %s",
                self->ec_handle.wh_ifname, pcap_geterr(self->ec_pcap));
    }

    /* Register handler */
    ev_io_init(&self->ec_pcap_evio, wanp_ethclient_pcap_evio_fn, pcap_fd, EV_READ);
    ev_io_start(EV_DEFAULT, &self->ec_pcap_evio);

    LOG(INFO, "ethclient: %s: Enabled DHCP ethernet client detection.",
            self->ec_handle.wh_ifname);

    return true;

error:
    if (self->ec_pcap != NULL) pcap_close(self->ec_pcap);
    self->ec_pcap = NULL;

    return false;
}

void wanp_ethclient_pcap_evio_fn(struct ev_loop *loop, ev_io *w, int revent)
{
    (void)loop;
    (void)revent;

    struct wanp_ethclient *self = CONTAINER_OF(w, struct wanp_ethclient, ec_pcap_evio);

    pcap_dispatch(self->ec_pcap, 0, wanp_ethclient_pcap_fn, (void *)self);
}

bool wanp_ethclient_pcap_close(struct wanp_ethclient *self)
{
    if (self->ec_pcap == NULL) return true;

    ev_timer_stop(EV_DEFAULT, &self->ec_client_timer);
    ev_io_stop(EV_DEFAULT, &self->ec_pcap_evio);
    pcap_close(self->ec_pcap);

    self->ec_pcap = NULL;

    LOG(INFO, "ethclient: %s: Disabled DHCP ethernet client detection.",
            self->ec_handle.wh_ifname);

    return true;
}

void wanp_ethclient_pcap_fn(u_char *data, const struct pcap_pkthdr *h, const u_char *pkt)
{
    struct wanp_ethclient *self = (void *)data;
    const u_char *pend = pkt + h->caplen;

    /*
     * Parse the DHCP packet
     */

    /* Figure out the L2 offset */
    if (pcap_datalink(self->ec_pcap) == DLT_LINUX_SLL) pkt += 2;

    /* Skip ethernet header -- it's guaranteed to be IPv4 by the PCAP rules */
    if (pkt + sizeof(struct ethhdr) > pend)
    {
        LOG(DEBUG, "ethclient: %s: dhcp: Error parsing ethernet header.",
                self->ec_handle.wh_ifname);
        return;
    }

    pkt += sizeof(struct ethhdr);
    if (pkt + sizeof(struct ip) > pend)
    {
        LOG(DEBUG, "ethclient: %s: dhcp: Error parsing IPv4 header.",
                self->ec_handle.wh_ifname);
        return;
    }

    /* Skip the IP header, but we need to at least get the header length variable */
    struct ip *iph = (void *)pkt;
    pkt += iph->ip_hl << 2;

    /* Skip the UDP header */
    if (pkt + sizeof(struct udphdr) > pend)
    {
        LOG(DEBUG, "ethclient: %s: dhcp: Error parsing UDP header.",
                self->ec_handle.wh_ifname);
        return;
    }

    pkt += sizeof(struct udphdr);

    /*
     * Now comes the juicy part -- the DHCP header
     */
    if (pkt + sizeof(struct dhcp_hdr) > pend)
    {
        LOG(DEBUG, "ethclient: %s: dhcp: Error parsing DHCP header.",
                self->ec_handle.wh_ifname);
        return;
    }

    struct dhcp_hdr dh;
    memcpy(&dh, pkt, sizeof(dh));

    if (ntohl(dh.dhcp_magic) != DHCP_MAGIC)
    {
        LOG(DEBUG, "ethclient: %s: dhcp: Invalid DHCP header magic 0x%08x.",
                self->ec_handle.wh_ifname, dh.dhcp_magic);
        return;
    }

    /*
     * Parse the options to figure out the packet type
     */
    pkt += sizeof(struct dhcp_hdr);
    while (pkt + 2 <= pend)
    {
        u_char opt_id;
        u_char opt_len;
        osn_mac_addr_t client_mac;

        /* End of options */
        if (*pkt == 255) break;

        /* Padding */
        if (*pkt == 0)
        {
            pkt++;
            continue;
        }

        opt_id = *pkt++;
        opt_len = *pkt++;
        if (pkt + opt_len > pend)
        {
            break;
        }

        switch (opt_id)
        {
            case DHCP_OPTION_MSG_TYPE:
                /*
                 * There are various DHCP message types, but we care only about
                 * 1, and that's the message with type 1 (DHCP_DISCOVER)
                 */
                memset(&client_mac, 0, sizeof(client_mac));
                memcpy(client_mac.ma_addr, dh.dhcp_chaddr, sizeof(client_mac.ma_addr));
                wanp_ethclient_dhcp_process(self, *pkt == 1, &client_mac);
                break;
        }

        pkt += opt_len;
    }
}

void wanp_ethclient_dhcp_process(
        struct wanp_ethclient *self,
        bool is_discover,
        osn_mac_addr_t *client_mac)
{
    LOG(DEBUG, "ethclient: %s: Received DHCP packet: discover=%d, mac="PRI_osn_mac_addr,
            self->ec_handle.wh_ifname,
            is_discover,
            FMT_osn_mac_addr(*client_mac));

    /* Remember the first seen MAC, start the timer */
    if (osn_mac_addr_cmp(&self->ec_client_mac, &OSN_MAC_ADDR_INIT) == 0)
    {
        self->ec_client_mac = *client_mac;
        ev_timer_start(EV_DEFAULT, &self->ec_client_timer);
    }

    /*
     * Check MAC address of the DHCP packet. If there's another client on the
     * network, we're assuming it's a LAN netowrk
     */
    if (osn_mac_addr_cmp(&self->ec_client_mac, client_mac) != 0)
    {
        LOG(NOTICE, "ethclient: %s: Multiple DHCP clients detected.",
                self->ec_handle.wh_ifname);
        goto abort;
    }

    /*
     * If the message type is not DISCOVER, it means there's a DHCP server
     * on the network.
     */
    if (!is_discover)
    {
        LOG(NOTICE, "ethclient: %s: Non-DISCOVER DHCP message detected.",
                self->ec_handle.wh_ifname);
        goto abort;
    }

    self->ec_client_discnum++;
    if (self->ec_client_discnum < CONFIG_MANAGER_WANO_PLUGIN_ETHCLIENT_DHCP_NUM)
    {
        return;
    }

    LOG(NOTICE, "ethclient: %s: Ethernet client detected: "PRI_osn_mac_addr,
            self->ec_handle.wh_ifname, FMT_osn_mac_addr(self->ec_client_mac));

    self->ec_status_fn(&self->ec_handle, &WANO_PLUGIN_STATUS(WANP_ABORT));
    return;

abort:
    ev_timer_stop(EV_DEFAULT, &self->ec_client_timer);
    self->ec_status_fn(&self->ec_handle, &WANO_PLUGIN_STATUS(WANP_SKIP));
    return;
}

void wanp_ethclient_dhcp_timeout(struct ev_loop *loop, ev_timer *ev, int revent)
{
    (void)loop;
    (void)revent;

    struct wanp_ethclient *self = CONTAINER_OF(ev, struct wanp_ethclient, ec_client_timer);

    LOG(NOTICE, "ethclient: %s: Ethernet client detected via timeout: "PRI_osn_mac_addr,
            self->ec_handle.wh_ifname, FMT_osn_mac_addr(self->ec_client_mac));

    self->ec_status_fn(&self->ec_handle, &WANO_PLUGIN_STATUS(WANP_ABORT));
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_ethclient_module_start(void)
{
    wano_plugin_register(&wanp_ethclient_module);
}

void wanp_ethclient_module_stop(void)
{
    wano_plugin_unregister(&wanp_ethclient_module);
}

MODULE(wanp_ethclient_module, wanp_ethclient_module_start, wanp_ethclient_module_stop)
