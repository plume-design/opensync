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

/* std libc */

/* 3rd party */
#include <pcap.h>
#include <ev.h>

/* opensync */
#include "log.h"
#include "const.h"
#include "execsh.h"
#include "target.h"
#include "osn_netif.h"
#include "ds_dlist.h"
#include "os_types.h"
#include "os_nif.h"

#define WM2_L2UF_PCAP_SNAPLEN 200
#define WM2_L2UF_PCAP_BUFFER_SIZE (64 * 1024)
#define WM2_L2UF_BPF_FILTER "ether broadcast and ether[12]==0 and (ether[13]==6 or ether[13]==8)"
#define WM2_L2UF_IF_RETRY_SECONDS 1

struct wm2_l2uf_if {
    struct ds_dlist_node list;
    char *if_name;
    struct bpf_program bpf;
    struct ev_io io;
    struct ev_timer retry;
    pcap_t *pcap;
    osn_netif_t *netif;
    bool passive;
};

struct eth_hdr {
    unsigned char da[6];
    unsigned char sa[6];
    unsigned short eth_type;
} __attribute__((packed));

static struct ds_dlist g_wm2_l2uf_if_list = DS_DLIST_INIT(struct wm2_l2uf_if, list);

static struct wm2_l2uf_if *
wm2_l2uf_if_lookup(const char *if_name)
{
    struct wm2_l2uf_if *i;

    ds_dlist_foreach(&g_wm2_l2uf_if_list, i)
        if (strcmp(i->if_name, if_name) == 0)
            return i;

    return NULL;
}

static bool
wm2_l2uf_if_is_enabled(const char *if_name)
{
    return wm2_l2uf_if_lookup(if_name) == NULL ? false : true;
}

static void
wm2_l2uf_netif_status_fn(osn_netif_t *netif,
                         struct osn_netif_status *status)
{
    struct wm2_l2uf_if *i;

    LOGD("l2uf: netif status: if_name=%s exists=%d up=%d",
         status->ns_ifname,
         status->ns_exists,
         status->ns_up);

    i = wm2_l2uf_if_lookup(status->ns_ifname);
    if (WARN_ON(i == NULL))
        return;

    if (status->ns_exists && status->ns_up) {
        ev_timer_start(EV_DEFAULT_ &i->retry);
    }
    else {
        ev_timer_stop(EV_DEFAULT_ &i->retry);
    }
}

static void
wm2_l2uf_pcap_recv(u_char *user,
                   const struct pcap_pkthdr *pkt,
                   const u_char *packet)
{
    struct wm2_l2uf_if *i = (struct wm2_l2uf_if *)user;
    const struct eth_hdr *eth = (const void *)packet;
    const os_macaddr_t *mac_addr = (const os_macaddr_t *)eth->sa;
    char mac_str[32] = {0};
    const bsal_disc_type_t type = BSAL_DISC_TYPE_DEAUTH;
    const uint8_t unspec_deauth_reason = 1;
    const uint8_t reason = unspec_deauth_reason;

    /* This may seem abusive but trying to pair this up with
     * station connect/disconnect events is full of races.
     * Asking the driver to disconnect a non-connected
     * client is not all that bad, and these L2UF frames
     * shouldn't be coming too often to cause a real problem
     * on CPU load.
     *
     * Also using target bsal API in WM seems wrong, but
     * there's no WM specific API to disconnect a client
     * either. This is supposed to be retrofittable with
     * least amount of changes into existing systems.
     */

    os_nif_macaddr_to_str(mac_addr, mac_str, PRI_os_macaddr_lower_t);
    LOGI("l2uf: received: if_name=%s sa=%s%s",
         i->if_name, mac_str,
         i->passive ? " (passive; no disconnect)" : "");
    if (i->passive == false)
        WARN_ON(target_bsal_client_disconnect(i->if_name, eth->sa, type, reason) < 0);
}

static void
wm2_l2uf_if_pcap_stop(struct wm2_l2uf_if *i)
{
    if (i->pcap == NULL) return;

    LOGI("l2uf: pcap: stopping, if_name=%s", i->if_name);

    ev_io_stop(EV_DEFAULT_ &i->io);
    pcap_close(i->pcap);
    i->pcap = NULL;
    ev_timer_start(EV_DEFAULT_ &i->retry);
}

static void
wm2_l2uf_io_process(EV_P_ ev_io *io, int revent)
{
    struct wm2_l2uf_if *i = container_of(io, struct wm2_l2uf_if, io);
    int rc;

    LOGD("l2uf: io: processing: if_name=%s", i->if_name);

    rc = pcap_dispatch(i->pcap, 64, wm2_l2uf_pcap_recv, (u_char *)i);
    if (rc == -1) {
        LOGI("l2uf: pcap: dispatch failed");
        wm2_l2uf_if_pcap_stop(i);
    }
}

static void
wm2_l2uf_if_pcap_start(struct wm2_l2uf_if *i)
{
    char pcap_err[PCAP_ERRBUF_SIZE];
    int err;
    int fd;

    if (i->pcap != NULL) return;

    LOGD("l2uf: pcap: starting: if_name=%s", i->if_name);

    if (WARN_ON((i->pcap = pcap_create(i->if_name, pcap_err)) == NULL)) goto error;
    if (WARN_ON(pcap_set_immediate_mode(i->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_set_snaplen(i->pcap, WM2_L2UF_PCAP_SNAPLEN) != 0)) goto error;
    if (WARN_ON(pcap_set_buffer_size(i->pcap, WM2_L2UF_PCAP_BUFFER_SIZE) != 0)) goto error;
    if (WARN_ON(pcap_setnonblock(i->pcap, 1, pcap_err) == -1)) goto error;
    if (WARN_ON(pcap_set_timeout(i->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_activate(i->pcap) != 0)) goto error;
    if (WARN_ON(pcap_setdirection(i->pcap, PCAP_D_OUT) != 0)) goto error;
    if (WARN_ON(pcap_compile(i->pcap, &i->bpf, WM2_L2UF_BPF_FILTER, 0, PCAP_NETMASK_UNKNOWN) != 0)) goto error;
    WARN_ON((err = pcap_setfilter(i->pcap, &i->bpf)) != 0);
    pcap_freecode(&i->bpf);
    if (err != 0) goto error;
    if (WARN_ON((fd = pcap_get_selectable_fd(i->pcap)) < 0)) goto error;

    ev_io_init(&i->io, wm2_l2uf_io_process, fd, EV_READ);
    ev_io_start(EV_DEFAULT_ &i->io);
    ev_timer_stop(EV_DEFAULT_ &i->retry);

    LOGI("l2uf: pcap: started, if_name=%s", i->if_name);
    return;

error:
    if (i->pcap != NULL) {
        LOGW("wm2: pcap error: %s", pcap_geterr(i->pcap));
        pcap_close(i->pcap);
        i->pcap = NULL;
    }
}

static void
wm2_l2uf_if_retry_fn(EV_P_ ev_timer *t, int revent)
{
    struct wm2_l2uf_if *i = container_of(t, struct wm2_l2uf_if, retry);
    wm2_l2uf_if_pcap_start(i);
}

void
wm2_l2uf_if_enable(const char *if_name)
{
    struct wm2_l2uf_if *i;

    if (wm2_l2uf_if_is_enabled(if_name) == true)
        return;

    LOGN("l2uf: enabling: if_name=%s", if_name);

    i = CALLOC(1, sizeof(*i));
    ds_dlist_insert_tail(&g_wm2_l2uf_if_list, i);
    i->if_name = STRDUP(if_name);
    ev_timer_init(&i->retry, wm2_l2uf_if_retry_fn,
                  WM2_L2UF_IF_RETRY_SECONDS,
                  WM2_L2UF_IF_RETRY_SECONDS);
    i->netif = osn_netif_new(if_name);
    osn_netif_status_notify(i->netif, wm2_l2uf_netif_status_fn);
}

void
wm2_l2uf_if_disable(const char *if_name)
{
    struct wm2_l2uf_if *i;

    if (wm2_l2uf_if_is_enabled(if_name) == false)
        return;

    LOGN("l2uf: disabling: if_name=%s", if_name);

    i = wm2_l2uf_if_lookup(if_name);
    assert(i != NULL);

    ds_dlist_remove(&g_wm2_l2uf_if_list, i);
    wm2_l2uf_if_pcap_stop(i);
    ev_timer_stop(EV_DEFAULT_ &i->retry);
    osn_netif_status_notify(i->netif, NULL);
    osn_netif_del(i->netif);
    FREE(i->if_name);
    FREE(i);
}

void
wm2_l2uf_if_set_passive(const char *if_name, const bool enable)
{
    struct wm2_l2uf_if *i;

    i = wm2_l2uf_if_lookup(if_name);
    if (WARN_ON(i == NULL))
        return;

    if (enable != i->passive) {
        LOGN("l2uf: set passive: if_name=%s enable=%d", if_name, enable);
        i->passive = enable;
    }
}
