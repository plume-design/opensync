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

#include "execsh.h"
#include "memutil.h"
#include "module.h"
#include "os_time.h"
#include "osn_dhcp.h"
#include "osn_types.h"
#include "osn_types.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "kconfig.h"
#include "ovsdb_bridge.h"

#include "wano.h"

/** PCAP Snapshot length */
#define WANP_ETHCLIENT_PCAP_SNAPLEN     512
/** PCAP rule for sniffing DHCP packets */
#define WANP_ETHCLIENT_PCAP_FILTER      "inbound and udp and (port bootpc or port bootps)"
/* Ethernet re-injection interface */
#define WANP_ETHCLIENT_INJECT_IF        CONFIG_TARGET_LAN_BRIDGE_NAME".ethc"

struct wanp_ethclient
{
    wano_plugin_handle_t        ec_handle;          /** Plug-in handle */
    wano_plugin_status_fn_t    *ec_status_fn;       /** Plug-in status callback */
    struct wano_plugin_status   ec_status;          /** The current plug-in status */
    bool                        ec_is_running;      /** True if wanp_ethclient_run() was called */
    wano_ppline_event_t         ec_ppline_event;    /** WANO pipeline event watcher */
    pcap_t                     *ec_pcap;            /** pcap handle */
    ev_io                       ec_pcap_evio;       /** pcap I/O watcher */
    osn_mac_addr_t              ec_client_mac;      /** Client MAC address */
    int                         ec_client_discnum;  /** Number discover messages from client detected */
    ev_timer                    ec_client_timer;    /** Client timer, started when the first DHCP packet is seen */
    bool                        ec_detected;        /** True if a client was detected and processed */
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

/*
 * DHCP message type (option 53).  There are 18 DHCP message types, but we're
 * interested only in these 2
 */
#define DHCP_TYPE_DISCOVER  1
#define DHCP_TYPE_REQUEST   3

/*
 * List of re-injection packets. These are live captured packets from clients
 * but the Ethernet client plug-in needs to re-inject them into the datapath
 * when the interface is added to the bridge (pipeline frozen).
 */
struct wanp_ethclient_inject
{
    char            ci_ifname[C_IFNAME_LEN];    /* Interface name length */
    uint8_t        *ci_pkt;                     /* Original packet including the L2 header */
    ssize_t         ci_pkt_len;                 /* Original packet length */
    ds_tree_node_t  ci_tnode;                   /* Tree node structure */
};

static void wanp_ethclient_module_start(void *data);
static void wanp_ethclient_module_stop(void *data);
static wano_plugin_ops_init_fn_t wanp_ethclient_init;
static wano_plugin_ops_run_fn_t wanp_ethclient_run;
static wano_plugin_ops_fini_fn_t wanp_ethclient_fini;
static wano_ppline_event_fn_t wanp_ethclient_ppline_event_fn;
static void wanp_ethclient_set_status(struct wanp_ethclient *self, struct wano_plugin_status *status);
static bool wanp_ethclient_pcap_open(struct wanp_ethclient *self);
static bool wanp_ethclient_pcap_close(struct wanp_ethclient *self);
static void wanp_ethclient_pcap_evio_fn(struct ev_loop *loop, ev_io *w, int revent);
static void wanp_ethclient_pcap_fn(u_char *data, const struct pcap_pkthdr *h, const u_char *pkt);
static void wanp_ethclient_handle_detected(struct wanp_ethclient *self);
static bool wanp_ethclient_inject(const char *ifname);
static void wanp_ethclient_inject_add(const char *ifname, const u_char *pkt, ssize_t pkt_len);
static void wanp_ethclient_inject_clear(const char *ifname);
static bool wanp_ethclient_inject_packet(const char *ifname, const u_char *pkt, ssize_t pkt_len);
static bool wanp_ethclient_mac_learning_update(const char *brname, const char *ifname, osn_mac_addr_t mac);
static bool wanp_ethclient_mac_learning_delete(const char *ifname);

static void wanp_ethclient_dhcp_process(
        struct wanp_ethclient *self,
        uint8_t dhcp_type,
        bool is_os_node,
        osn_mac_addr_t *client_mac,
        const u_char *pkt_l2,
        ssize_t pkt_len);

static void wanp_ethclient_dhcp_timeout(struct ev_loop *loop, ev_timer *ev, int revent);

static struct wano_plugin wanp_ethclient_module = WANO_PLUGIN_INIT(
        "ethclient",
        0,
        WANO_PLUGIN_MASK_ALL,
        wanp_ethclient_init,
        wanp_ethclient_run,
        wanp_ethclient_fini);


static ds_tree_t g_wanp_ethclient_inject_list = DS_TREE_INIT(
        ds_str_cmp,
        struct wanp_ethclient_inject,
        ci_tnode);

wano_plugin_handle_t *wanp_ethclient_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    struct wanp_ethclient *self;

    self = CALLOC(1, sizeof(struct wanp_ethclient));

    self->ec_client_mac = OSN_MAC_ADDR_INIT;
    self->ec_handle.wh_plugin = wp;
    STRSCPY(self->ec_handle.wh_ifname, ifname);
    self->ec_status_fn = status_fn;

    if (!wanp_ethclient_mac_learning_delete(ifname))
    {
        LOG(WARN, "ethclient: %s: Error deleting OVS_MAC_Learning entries", ifname);
    }

    ev_timer_init(
            &self->ec_client_timer,
            wanp_ethclient_dhcp_timeout,
            (double)CONFIG_MANAGER_WANO_PLUGIN_ETHCLIENT_DHCP_WAIT,
            0.0);

    if (!wanp_ethclient_pcap_open(self))
    {
        LOG(ERR, "ethclient: %s: Error initializing PCAP.",
                self->ec_handle.wh_ifname);
        FREE(self);
        return NULL;
    }

    wanp_ethclient_set_status(self, &WANO_PLUGIN_STATUS(WANP_DETACH));

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

    /*
     * Force a status update -- the init function sets this to WANP_DETACH, but
     * the DHCP engine might set it to other values before this point is reached.
     * And since it is illegal to send a status update before the
     * wanp_ethclient_run() function is called, do it here.
     */
    self->ec_is_running = true;
    self->ec_status_fn(&self->ec_handle, &self->ec_status);
}

void wanp_ethclient_fini(wano_plugin_handle_t *wh)
{
    struct wanp_ethclient *self = CONTAINER_OF(wh, struct wanp_ethclient, ec_handle);

    wanp_ethclient_pcap_close(self);
    wano_ppline_event_stop(&self->ec_ppline_event);

    FREE(self);
}

void wanp_ethclient_ppline_event_fn(wano_ppline_event_t *wpe, enum wano_ppline_status status)
{
    struct wanp_ethclient *self = CONTAINER_OF(wpe, struct wanp_ethclient, ec_ppline_event);

    switch (status)
    {
        case WANO_PPLINE_OK:
            wanp_ethclient_pcap_close(self);
            break;

        case WANO_PPLINE_FREEZE:
            wanp_ethclient_inject(self->ec_handle.wh_ifname);
            break;

        case WANO_PPLINE_IDLE:
        case WANO_PPLINE_RESTART:
        case WANO_PPLINE_ABORT:
            break;
    }
}

void wanp_ethclient_set_status(struct wanp_ethclient *self, struct wano_plugin_status *status)
{
    self->ec_status = *status;

    if (!self->ec_is_running) return;

    self->ec_status_fn(&self->ec_handle, &self->ec_status);
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
    const u_char *pkt_l2;

    uint8_t dhcp_msg_type = 0;
    bool dhcp_osync_swver = false;
    bool dhcp_osync_profile = false;
    bool dhcp_osync_serial_opt = false;
    osn_mac_addr_t dhcp_client_mac = OSN_MAC_ADDR_INIT;

    /*
     * Parse the DHCP packet
     */

    /* Figure out the L2 offset */
    if (pcap_datalink(self->ec_pcap) == DLT_LINUX_SLL)
    {
        pkt += 2;
    }

    /* Save the L2 packet for later */
    pkt_l2 = pkt;

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
                dhcp_msg_type = *pkt;
                memcpy(dhcp_client_mac.ma_addr, dh.dhcp_chaddr, sizeof(dhcp_client_mac.ma_addr));
                break;

            /*
             * Check for OpenSync vendor specific options
             */
            case DHCP_OPTION_OSYNC_SWVER:
                dhcp_osync_swver = true;
                break;

            case DHCP_OPTION_OSYNC_PROFILE:
                dhcp_osync_profile = true;
                break;

            case DHCP_OPTION_OSYNC_SERIAL_OPT:
                dhcp_osync_serial_opt = true;
                break;
        }

        pkt += opt_len;
    }

    bool is_os_node = dhcp_osync_swver && dhcp_osync_profile && dhcp_osync_serial_opt;
    wanp_ethclient_dhcp_process(self, dhcp_msg_type, is_os_node, &dhcp_client_mac, pkt_l2, pend - pkt_l2);
}

/*
 * Return true if we have a controller connection, false otherwise or if
 * unknown.
 */
void wanp_ethclient_handle_detected(struct wanp_ethclient *self)
{
    if (self->ec_detected) return;

    LOG(NOTICE, "ethclient: %s: Ethernet client "PRI_osn_mac_addr" detected.",
            self->ec_handle.wh_ifname,
            FMT_osn_mac_addr(self->ec_client_mac));

    /*
     * The ethernet client plug-in will abort the current pipeline, which causes
     * the Connection_Manager_Uplink table to not be updated. Since the Ethernet
     * client requires rather specific settings (namely, loop = false and
     * eth_client = true), update it here.
     */
    if (!WANO_CONNMGR_UPLINK_UPDATE(
                self->ec_handle.wh_ifname,
                .has_L3 = WANO_TRI_FALSE,
                .eth_client = WANO_TRI_TRUE,
                .loop = WANO_TRI_FALSE))
    {
        LOG(WARN, "ethclient: %s: Error updating Connection_Manager_Uplink table",
                self->ec_handle.wh_ifname);
    }

    /*
     * Due to packet injection, the client might be incorrectly shown as being
     * connected to one of the parent nodes instead of the current one.
     *
     * This happens because the DHCP packet is re-injected with the the FLOOD
     * action, which bypasses the FDB. This also means that the FDB database
     * (and consequentially the OVS_MAC_Learning table) is not updated until
     * the client actually sends a real packet.
     *
     * The cloud assumes that the client is connected to the first node that
     * updates its OVS_MAC_Learning table with the client's MAC address, so
     * force an update before packet re-injection (below).
     */
    if (!wanp_ethclient_mac_learning_update(
            CONFIG_TARGET_LAN_BRIDGE_NAME,
            self->ec_handle.wh_ifname,
            self->ec_client_mac))
    {
        LOG(WARN, "ethclient: %s: Error updating OVS_MAC_Learning table for client "PRI_osn_mac_addr,
                self->ec_handle.wh_ifname,
                FMT_osn_mac_addr(self->ec_client_mac));
    }

    wanp_ethclient_set_status(self, &WANO_PLUGIN_STATUS(WANP_ABORT));

    self->ec_detected = true;
}

void wanp_ethclient_dhcp_process(
        struct wanp_ethclient *self,
        uint8_t dhcp_type,
        bool is_os_node,
        osn_mac_addr_t *client_mac,
        const u_char *pkt_l2,
        ssize_t pkt_len)
{
    LOG(INFO, "ethclient: %s: Received DHCP packet: type=%d, is_os_node=%d, mac="PRI_osn_mac_addr,
            self->ec_handle.wh_ifname,
            dhcp_type,
            is_os_node,
            FMT_osn_mac_addr(*client_mac));

    /* Check if the client is an OpenSync device */
    if (is_os_node)
    {
        /* In case the remote end is an OpenSync device, abort client detection */
        LOG(NOTICE, "ethclient: %s: Remote OpenSync device detected. Disabling fast client detection.",
                self->ec_handle.wh_ifname);
        wanp_ethclient_set_status(self, &WANO_PLUGIN_STATUS(WANP_SKIP));
        if (!WANO_CONNMGR_UPLINK_UPDATE(
                self->ec_handle.wh_ifname,
                .loop = WANO_TRI_TRUE))
                {
                    LOG(WARN, "wano: %s: Error updating the Connection_Manager_Uplink table (loop = true)",
                            self->ec_handle.wh_ifname);
                }
        goto abort;
    }

    /*
     * If the message type is not DISCOVER or REQUEST, it means there's a
     * DHCP server on the network.
     */
    if (dhcp_type != DHCP_TYPE_DISCOVER && dhcp_type != DHCP_TYPE_REQUEST)
    {
        LOG(NOTICE, "ethclient: %s: Non-DISCOVER/REQUEST DHCP message detected.",
                self->ec_handle.wh_ifname);
        goto abort;
    }

    if (!WANO_CONNMGR_UPLINK_UPDATE(
            self->ec_handle.wh_ifname,
            .loop = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error updating the Connection_Manager_Uplink table (loop = false)",
                        self->ec_handle.wh_ifname);
            }

    /* Remember the first seen MAC, start the timer */
    if (osn_mac_addr_cmp(&self->ec_client_mac, &OSN_MAC_ADDR_INIT) == 0)
    {
        LOG(NOTICE, "ethclient: %s: DHCP detected: "PRI_osn_mac_addr,
                self->ec_handle.wh_ifname,
                FMT_osn_mac_addr(*client_mac));
        self->ec_client_mac = *client_mac;
        ev_timer_start(EV_DEFAULT, &self->ec_client_timer);
    }

    /*
     * Check MAC address of the DHCP packet. If there's another client on the
     * network, we're assuming it's a LAN network
     */
    if (osn_mac_addr_cmp(&self->ec_client_mac, client_mac) != 0)
    {
        LOG(NOTICE, "ethclient: %s: Multiple DHCP clients detected.",
                self->ec_handle.wh_ifname);
        goto abort;
    }

    /* Add the last seen DHCP packet to the re-injection list */
    wanp_ethclient_inject_add(self->ec_handle.wh_ifname, pkt_l2, pkt_len);

    self->ec_client_discnum++;
    if (self->ec_client_discnum < CONFIG_MANAGER_WANO_PLUGIN_ETHCLIENT_DHCP_NUM)
    {
        return;
    }

    LOG(NOTICE, "ethclient: %s: Ethernet client detected: "PRI_osn_mac_addr,
                self->ec_handle.wh_ifname,
                FMT_osn_mac_addr(self->ec_client_mac));

    wanp_ethclient_handle_detected(self);
    return;

abort:
    ev_timer_stop(EV_DEFAULT, &self->ec_client_timer);
    wanp_ethclient_set_status(self, &WANO_PLUGIN_STATUS(WANP_SKIP));

    /* Clear the re-injection list */
    wanp_ethclient_inject_clear(self->ec_handle.wh_ifname);
    return;
}

void wanp_ethclient_dhcp_timeout(struct ev_loop *loop, ev_timer *ev, int revent)
{
    (void)loop;
    (void)revent;

    struct wanp_ethclient *self = CONTAINER_OF(ev, struct wanp_ethclient, ec_client_timer);

    LOG(NOTICE, "ethclient: %s: Ethernet client detected via timeout: "PRI_osn_mac_addr,
            self->ec_handle.wh_ifname,
            FMT_osn_mac_addr(self->ec_client_mac));

    wanp_ethclient_handle_detected(self);
}

/*
 * ===========================================================================
 *  Packet re-injection
 * ===========================================================================
 */

/*
 * Schedule packet for re-injection; currently there can be only one packet
 * scheduled per interface. If there's a packet already scheduled, overwrite it
 * with the most recent packet.
 *
 * The `pkt` parameter should contain the original DHCP client packet including
 * the L2 header (ethernet).
 *
 * The packet re-injection list is not tied to any ethernet client plug-in
 * instance because the plug-in can be restarted multiple times before the
 * interface is finally added to a bridge (this is when the re-injection should
 * happen). Caching this data as part of the plug-in instance may delete it
 * prematurely.
 */
void wanp_ethclient_inject_add(const char *ifname, const u_char *pkt, ssize_t pkt_len)
{
    struct wanp_ethclient_inject *pci;

    if (pkt_len < 0)
    {
        LOG(WARN, "ethclient: %s: Packet size is negative, ignoring.", ifname);
        return;
    }

    pci = ds_tree_find(&g_wanp_ethclient_inject_list, (void *)ifname);
    if (pci == NULL)
    {
        pci = CALLOC(1, sizeof(*pci));
        STRSCPY(pci->ci_ifname, ifname);
        ds_tree_insert(&g_wanp_ethclient_inject_list, pci, pci->ci_ifname);
    }
    else
    {
        FREE(pci->ci_pkt);
    }

    /* Update the structure with the current (most recent) packet */
    pci->ci_pkt = MALLOC(pkt_len);
    memcpy(pci->ci_pkt, pkt, pkt_len);
    pci->ci_pkt_len = pkt_len;
}

/*
 * Clear any pending packets that are scheduled for reinjection
 */
void wanp_ethclient_inject_clear(const char *ifname)
{
    struct wanp_ethclient_inject *pci;

    pci = ds_tree_find(&g_wanp_ethclient_inject_list, (void *)ifname);
    if (pci == NULL)
    {
        return;
    }

    ds_tree_remove(&g_wanp_ethclient_inject_list, pci);
    FREE(pci->ci_pkt);
    FREE(pci);
}

/*
 * Re-inject DHCP client packet
 */
bool wanp_ethclient_inject(const char *ifname)
{
    struct wanp_ethclient_inject *pci;

    pci = ds_tree_find(&g_wanp_ethclient_inject_list, (void *)ifname);
    if (pci == NULL) return true;

    LOG(NOTICE, "ethclient: %s: Performing DHCP client packet re-injection.",
            ifname);

    if (!wanp_ethclient_inject_packet(
            WANP_ETHCLIENT_INJECT_IF,
            pci->ci_pkt,
            pci->ci_pkt_len))
    {
        LOG(ERR, "ethclient: %s: Error re-injecting packet.",
                ifname);
        return false;
    }

    return true;
}

/*
 * Inject pending packets for an interface
 *
 * This function injects the packet that was captured on `ifname` and sends it
 * to the home bridge. This pokes the DHCP server to send an early reply.
 *
 * The only modification we need to do is to change the source MAC address
 * so that we don't mess up the OVS FDB table. This works because the the DHCP
 * server is obligated to reply to the MAC address present in the DHCP header
 * (not to the one in the Ethernet header).
 *
 * The source MAC address can be anything, as long as it doesn't conflict with
 * any existing client or interface. Use a locally administered fake address
 * for this.
 */
bool wanp_ethclient_inject_packet(const char *ifname, const u_char *pkt, ssize_t pkt_len)
{
    char perr[PCAP_ERRBUF_SIZE];

    pcap_t *pin = NULL;
    bool retval = false;

    pin = pcap_open_live(ifname, 1500, 0, 0, perr);
    if (pin == NULL)
    {
        LOG(ERR, "ethclient: %s: Error opening interface for pcap injection: %s",
                ifname,
                perr);
        goto error;
    }

    if (pcap_inject(pin, pkt, pkt_len) != pkt_len)
    {
        LOG(ERR, "ethclient: %s: Error injecting packet.", ifname);
        goto error;
    }

    retval = true;
error:
    if (pin != NULL) pcap_close(pin);
    return retval;
}

/*
 * Update the OVS_MAC_Learning table
 */
bool wanp_ethclient_mac_learning_update(
        const char *brname,
        const char *ifname,
        osn_mac_addr_t mac)
{
    struct schema_OVS_MAC_Learning ml;
    ovsdb_table_t table_OVS_MAC_Learning;
    char smac[C_MACADDR_LEN];

    snprintf(smac, sizeof(smac), PRI_osn_mac_addr, FMT_osn_mac_addr(mac));
    memset(&ml, 0, sizeof(ml));
    SCHEMA_SET_STR(ml.brname, brname);
    SCHEMA_SET_STR(ml.ifname, ifname);
    SCHEMA_SET_STR(ml.hwaddr, smac);

    OVSDB_TABLE_INIT(OVS_MAC_Learning, hwaddr);

    return ovsdb_table_upsert_simple(
            &table_OVS_MAC_Learning,
            "hwaddr",
            (char *)smac,
            &ml,
            false);
}

/*
 * Delete from OVS_MAC_Learning table where ifname
 */
bool wanp_ethclient_mac_learning_delete(const char *ifname)
{
    int rc;
    ovsdb_table_t table_OVS_MAC_Learning;
    OVSDB_TABLE_INIT(OVS_MAC_Learning, ifname);
    rc = ovsdb_table_delete_simple(
            &table_OVS_MAC_Learning,
            "ifname",
            ifname);
    return rc >= 0;  // rc: number of deleted items or -1 if error
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_ethclient_module_start(void *data)
{
    (void)data;

    /*
     * This rule sets the default action to "flood" for every DHCP packet that
     * is injected into the br-home.etch interface. The "flood" action bypasses
     * the FDB of the bridge so the source MAC address of the packet is not
     * assigned to br-home.ethc.
     *
     * The side effect of the flood action is that it floods all ports in the
     * bridge.
     */
    static char ethclient_tap_create[] = SHELL
    (
        if_brlan="$1";
        if_ethc="$2";

        [ -e "/sys/class/net/$if_ethc" ] && exit 0;

        ip link add "$if_ethc" type dummy;
        ip link set "$if_ethc" up;
        brctl addif "$if_brlan" "$if_ethc"

    );

    /*
     * Create the packet re-injection interface.
     */
    if (EXECSH_LOG(
                DEBUG,
                ethclient_tap_create,
                CONFIG_TARGET_LAN_BRIDGE_NAME,
                WANP_ETHCLIENT_INJECT_IF) != 0)
    {
        LOG(ERR, "eth_client: Error creating injection interface '%s'. Ethernet client detection will not be available.",
                WANP_ETHCLIENT_INJECT_IF);
        return;
    }

    ovsdb_bridge_manage_port(WANP_ETHCLIENT_INJECT_IF, CONFIG_TARGET_LAN_BRIDGE_NAME, true);

    wano_plugin_register(&wanp_ethclient_module);
}

void wanp_ethclient_module_stop(void *data)
{
    (void)data;

    static char ethclient_tap_delete[] = SHELL
    (
        if_brlan="$1";
        if_ethc="$2";

        [ ! -e "/sys/class/net/$if_ethc" ] && exit 0;

        brctl delif "$if_brlan" "$if_ethc";
    );

    ds_tree_iter_t iter;
    struct wanp_ethclient_inject *pci;

    ds_tree_foreach_iter(&g_wanp_ethclient_inject_list, pci, &iter)
    {
        ds_tree_iremove(&iter);
        FREE(pci->ci_pkt);
        FREE(pci);
    }

    wano_plugin_unregister(&wanp_ethclient_module);

    /*
     * Delete the packet re-injection interface.
     */
    if (EXECSH_LOG(
                DEBUG,
                ethclient_tap_delete,
                CONFIG_TARGET_LAN_BRIDGE_NAME,
                WANP_ETHCLIENT_INJECT_IF) != 0)
    {
        LOG(ERR, "eth_client: Error creating injection interface '%s'. Ethernet client detection will not be available.",
                WANP_ETHCLIENT_INJECT_IF);
        return;
    }

    ovsdb_bridge_manage_port(WANP_ETHCLIENT_INJECT_IF, CONFIG_TARGET_LAN_BRIDGE_NAME, false);
}

MODULE(wanp_ethclient_module, wanp_ethclient_module_start, wanp_ethclient_module_stop)
