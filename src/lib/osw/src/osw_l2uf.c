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
#include <log.h>
#include <const.h>
#include <osn_netif.h>
#include <os_nif.h>
#include <os_types.h>

/* unit */
#include <osw_module.h>
#include <osw_l2uf.h>

#define OSW_L2UF_PCAP_SNAPLEN 200
#define OSW_L2UF_PCAP_BUFFER_SIZE (64 * 1024)
#define OSW_L2UF_BPF_FILTER "ether broadcast and ether[12]==0 and (ether[13]==6 or ether[13]==8)"
#define OSW_L2UF_IF_RETRY_SECONDS 1
#define LOG_PREFIX(i, fmt, ...) "osw: l2uf: %s: " fmt, (i)->if_name, ##__VA_ARGS__

struct osw_l2uf {
    struct ev_idle work;
    struct ds_dlist ifaces;
    struct ev_loop *loop;
};

struct osw_l2uf_if {
    struct ds_dlist_node node;
    bool pending;
    struct osw_l2uf *m;
    char *if_name;
    struct bpf_program bpf;
    struct ev_io io;
    struct ev_timer retry;
    pcap_t *pcap;
    osn_netif_t *netif;
    osw_l2uf_seen_fn_t *seen_fn;
    void *data;
};

struct eth_hdr {
    unsigned char da[6];
    unsigned char sa[6];
    unsigned short eth_type;
} __attribute__((packed));

static void
osw_l2uf_if_pending_set(struct osw_l2uf_if *i)
{
    if (i->pending == true) return;

    i->pending = true;
    ds_dlist_insert_tail(&i->m->ifaces, i);
    ev_idle_start(i->m->loop, &i->m->work);
}

static void
osw_l2uf_if_pending_unset(struct osw_l2uf_if *i)
{
    if (i->pending == false) return;

    ds_dlist_remove(&i->m->ifaces, i);
    i->pending = false;
}

static void
osw_l2uf_netif_status_fn(osn_netif_t *netif,
                         struct osn_netif_status *status)
{
    struct osw_l2uf_if *i = osn_netif_data_get(netif);
    if (WARN_ON(i == NULL))
        return;

    LOGD(LOG_PREFIX(i, "netif: exists=%d up=%d", status->ns_exists, status->ns_up));

    if (status->ns_exists && status->ns_up) {
        ev_timer_start(i->m->loop, &i->retry);
    }
    else {
        ev_timer_stop(i->m->loop, &i->retry);
        osw_l2uf_if_pending_unset(i);
    }
}

static void
osw_l2uf_pcap_recv(u_char *user,
                   const struct pcap_pkthdr *pkt,
                   const u_char *packet)
{
    struct osw_l2uf_if *i = (struct osw_l2uf_if *)user;
    const struct eth_hdr *eth = (const void *)packet;
    const struct osw_hwaddr addr = { .octet = {
        eth->sa[0],
        eth->sa[1],
        eth->sa[2],
        eth->sa[3],
        eth->sa[4],
        eth->sa[5],
    } };

    LOGD(LOG_PREFIX(i, "outbound: sa="OSW_HWADDR_FMT, OSW_HWADDR_ARG(&addr)));

    if (i->seen_fn != NULL) {
        i->seen_fn(i, &addr);
    }
}

static void
osw_l2uf_if_pcap_stop(struct osw_l2uf_if *i)
{
    const bool already_stopped = (i->pcap == NULL);
    if (already_stopped) return;

    LOGD(LOG_PREFIX(i, "stopping"));

    ev_io_stop(i->m->loop, &i->io);
    pcap_close(i->pcap);
    i->pcap = NULL;
    ev_timer_start(i->m->loop, &i->retry);
}

static void
osw_l2uf_io_cb(struct ev_loop *loop, ev_io *io, int revent)
{
    struct osw_l2uf_if *i = io->data;

    LOGT(LOG_PREFIX(i, "processing io"));

    const int rc = pcap_dispatch(i->pcap, 64, osw_l2uf_pcap_recv, (u_char *)i);
    if (rc == -1) {
        LOGI(LOG_PREFIX(i, "dispatch failed: %s", pcap_geterr(i->pcap)));
        osw_l2uf_if_pcap_stop(i);
    }
}

static void
osw_l2uf_if_pcap_start(struct osw_l2uf_if *i)
{
    bool is_up = false;
    os_nif_is_up(i->if_name, &is_up);
    if (is_up == false) return;

    const bool already_started = (i->pcap != NULL);
    if (already_started) return;

    char pcap_err[PCAP_ERRBUF_SIZE];
    int err;
    int fd;

    LOGD(LOG_PREFIX(i, "starting"));

    if (WARN_ON((i->pcap = pcap_create(i->if_name, pcap_err)) == NULL)) goto error;
    if (WARN_ON(pcap_set_immediate_mode(i->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_set_snaplen(i->pcap, OSW_L2UF_PCAP_SNAPLEN) != 0)) goto error;
    if (WARN_ON(pcap_set_buffer_size(i->pcap, OSW_L2UF_PCAP_BUFFER_SIZE) != 0)) goto error;
    if (WARN_ON(pcap_setnonblock(i->pcap, 1, pcap_err) == -1)) goto error;
    if (WARN_ON(pcap_set_timeout(i->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_activate(i->pcap) != 0)) goto error;
    if (WARN_ON(pcap_setdirection(i->pcap, PCAP_D_OUT) != 0)) goto error;
    if (WARN_ON(pcap_compile(i->pcap, &i->bpf, OSW_L2UF_BPF_FILTER, 0, PCAP_NETMASK_UNKNOWN) != 0)) goto error;
    WARN_ON((err = pcap_setfilter(i->pcap, &i->bpf)) != 0);
    pcap_freecode(&i->bpf);
    if (err != 0) goto error;
    if (WARN_ON((fd = pcap_get_selectable_fd(i->pcap)) < 0)) goto error;

    ev_io_init(&i->io, osw_l2uf_io_cb, fd, EV_READ);
    i->io.data = i;
    ev_io_start(i->m->loop, &i->io);
    ev_timer_stop(i->m->loop, &i->retry);

    LOGD(LOG_PREFIX(i, "started"));
    return;

error:
    if (i->pcap != NULL) {
        LOGW(LOG_PREFIX(i, "pcap error: %s", pcap_geterr(i->pcap)));
        pcap_close(i->pcap);
        i->pcap = NULL;
    }
}

static void
osw_l2uf_if_retry_cb(struct ev_loop *loop,
                     ev_timer *t,
                     int revent)
{
    struct osw_l2uf_if *i = t->data;
    osw_l2uf_if_pending_set(i);
}

struct osw_l2uf_if *
osw_l2uf_if_alloc(struct osw_l2uf *m,
                  const char *if_name)
{
    struct osw_l2uf_if *i = CALLOC(1, sizeof(*i));
    i->m = m;
    i->if_name = STRDUP(if_name);
    LOGD(LOG_PREFIX(i, "allocating"));
    ev_timer_init(&i->retry, osw_l2uf_if_retry_cb,
                  OSW_L2UF_IF_RETRY_SECONDS,
                  OSW_L2UF_IF_RETRY_SECONDS);
    i->retry.data = i;
    i->netif = osn_netif_new(if_name);
    osn_netif_data_set(i->netif, i);
    osn_netif_status_notify(i->netif, osw_l2uf_netif_status_fn);
    return i;
}

void
osw_l2uf_if_free(struct osw_l2uf_if *i)
{
    if (i == NULL) return;

    LOGD(LOG_PREFIX(i, "freeing"));
    osw_l2uf_if_pcap_stop(i);
    ev_timer_stop(i->m->loop, &i->retry);
    osw_l2uf_if_pending_unset(i);
    osn_netif_status_notify(i->netif, NULL);
    osn_netif_del(i->netif);
    FREE(i->if_name);
    FREE(i);
}

void
osw_l2uf_if_set_data(struct osw_l2uf_if *i,
                     void *data)
{
    i->data = data;
}

void *
osw_l2uf_if_get_data(struct osw_l2uf_if *i)
{
    return i->data;
}

void
osw_l2uf_if_set_seen_fn(struct osw_l2uf_if *i,
                        osw_l2uf_seen_fn_t *fn)
{
    i->seen_fn = fn;
}

static void
osw_l2uf_work_cb(struct ev_loop *loop,
                 ev_idle *idle,
                 int revent)
{
    struct osw_l2uf *m = idle->data;
    struct osw_l2uf_if *i = ds_dlist_head(&m->ifaces);
    ev_idle_stop(loop, &m->work);
    if (i == NULL) return;

    osw_l2uf_if_pending_unset(i);
    osw_l2uf_if_pcap_start(i);
    ev_idle_start(loop, &m->work);
}

static void
osw_l2uf_init(struct osw_l2uf *m)
{
    ds_dlist_init(&m->ifaces, struct osw_l2uf_if, node);
    ev_async_init(&m->work, osw_l2uf_work_cb);
    m->work.data = m;
}

static void
osw_l2uf_attach(struct osw_l2uf *m)
{
    m->loop = OSW_MODULE_LOAD(osw_ev);
}

OSW_MODULE(osw_l2uf)
{
    static struct osw_l2uf m;
    osw_l2uf_init(&m);
    osw_l2uf_attach(&m);
    return &m;
}
