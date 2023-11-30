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

#include <log.h>
#include <const.h>
#include <memutil.h>

#include <ev.h>
#include <pcap.h>
#include <os_pcap.h>
#include <osn_netif.h>

#define OS_PCAP_BUFFER_SIZE (64 * 1024)
#define LOG_PREFIX(pcap, fmt, ...) "os_pcap: %s: %s: " fmt, (pcap)->if_name, (pcap)->filter, ##__VA_ARGS__

struct os_pcap {
    /* configuration */
    struct ev_loop *loop;
    char *if_name;
    char *filter;
    pcap_direction_t direction;
    int snap_len;
    os_pcap_rx_fn_t *rx_fn;
    void *rx_fn_priv;

    /* runtime */
    struct bpf_program bpf;
    bool runnable;
    pcap_t *pcap;
    ev_io io;
    ev_timer retry;
    osn_netif_t *netif;
};

static void
os_pcap_retry_recalc(struct os_pcap *pcap)
{
    if (pcap->pcap != NULL) {
        ev_timer_stop(pcap->loop, &pcap->retry);
        return;
    }

    if (pcap->runnable == false) {
        ev_timer_stop(pcap->loop, &pcap->retry);
        return;
    }

    ev_timer_start(pcap->loop, &pcap->retry);
}

static void
os_pcap_open_try(struct os_pcap *pcap)
{
    const bool already_started = (pcap->pcap != NULL);
    if (already_started) return;

    char pcap_err[PCAP_ERRBUF_SIZE];
    int err;
    int fd;

    LOGD(LOG_PREFIX(pcap, "starting"));

    if (WARN_ON((pcap->pcap = pcap_create(pcap->if_name, pcap_err)) == NULL)) goto error;
    if (WARN_ON(pcap_set_immediate_mode(pcap->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_set_snaplen(pcap->pcap, pcap->snap_len) != 0)) goto error;
    if (WARN_ON(pcap_set_buffer_size(pcap->pcap, OS_PCAP_BUFFER_SIZE) != 0)) goto error;
    if (WARN_ON(pcap_setnonblock(pcap->pcap, 1, pcap_err) == -1)) goto error;
    if (WARN_ON(pcap_set_timeout(pcap->pcap, 1) != 0)) goto error;
    if (WARN_ON(pcap_activate(pcap->pcap) != 0)) goto error;
    if (WARN_ON(pcap_setdirection(pcap->pcap, pcap->direction) != 0)) goto error;
    if (WARN_ON(pcap_compile(pcap->pcap, &pcap->bpf, pcap->filter, 0, PCAP_NETMASK_UNKNOWN) != 0)) goto error;
    WARN_ON((err = pcap_setfilter(pcap->pcap, &pcap->bpf)) != 0);
    pcap_freecode(&pcap->bpf);
    if (err != 0) goto error;
    if (WARN_ON((fd = pcap_get_selectable_fd(pcap->pcap)) < 0)) goto error;

    pcap->io.fd = fd;
    ev_io_start(pcap->loop, &pcap->io);

    LOGD(LOG_PREFIX(pcap, "started"));
    return;

error:
    if (pcap->pcap != NULL) {
        LOGW(LOG_PREFIX(pcap, "error: %s", pcap_geterr(pcap->pcap)));
        pcap_close(pcap->pcap);
        pcap->pcap = NULL;
    }
}

static void
os_pcap_close(struct os_pcap *pcap)
{
    if (pcap == NULL) return;
    if (pcap->pcap == NULL) return;

    ev_io_stop(pcap->loop, &pcap->io);
    pcap_close(pcap->pcap);
    pcap->pcap = NULL;
    os_pcap_retry_recalc(pcap);
}

static void
os_pcap_recv_cb(u_char *priv,
                const struct pcap_pkthdr *info,
                const u_char *data)
{
    struct os_pcap *pcap = (void *)priv;
    const size_t data_len = info->caplen;

    if (pcap->rx_fn == NULL) return;
    pcap->rx_fn(pcap->rx_fn_priv, data, data_len);
}

static void
os_pcap_io_cb(struct ev_loop *loop,
              ev_io *w,
              int events)
{
    struct os_pcap *pcap = w->data;

    (void)loop;
    (void)events;

    const int rv = pcap_dispatch(pcap->pcap, 64, os_pcap_recv_cb, (u_char *)pcap);
    if (rv == -1) {
        os_pcap_close(pcap);
    }
}

static void
os_pcap_retry_cb(struct ev_loop *loop,
                 ev_timer *t,
                 int event)
{
    struct os_pcap *pcap = t->data;
    if (WARN_ON(pcap == NULL)) return;

    (void)loop;
    (void)event;

    os_pcap_open_try(pcap);
    os_pcap_retry_recalc(pcap);
}

static void
os_pcap_netif_status_cb(osn_netif_t *netif,
                        struct osn_netif_status *status)
{
    struct os_pcap *pcap = osn_netif_data_get(netif);
    pcap->runnable = (status->ns_exists && status->ns_up);
    os_pcap_retry_recalc(pcap);
}

os_pcap_t *
os_pcap_new(const struct os_pcap_arg *arg)
{
    struct os_pcap *pcap = CALLOC(1, sizeof(*pcap));
    pcap->loop = arg->loop ?: EV_DEFAULT;
    pcap->rx_fn = arg->rx_fn;
    pcap->rx_fn_priv = arg->rx_fn_priv;
    pcap->snap_len = arg->snap_len;
    pcap->direction = arg->direction;
    pcap->filter = STRDUP(arg->filter);
    pcap->if_name = STRDUP(arg->if_name);
    pcap->netif = osn_netif_new(arg->if_name);
    ev_timer_init(&pcap->retry, os_pcap_retry_cb, 1.0, 1.0);
    ev_io_init(&pcap->io, os_pcap_io_cb, 0, EV_READ);
    pcap->retry.data = pcap;
    pcap->io.data = pcap;
    osn_netif_data_set(pcap->netif, pcap);
    osn_netif_status_notify(pcap->netif, os_pcap_netif_status_cb);
    return pcap;
}

void
os_pcap_drop(os_pcap_t *pcap)
{
    if (pcap == NULL) return;

    osn_netif_status_notify(pcap->netif, NULL);
    pcap->runnable = false;
    os_pcap_close(pcap);
    osn_netif_del(pcap->netif);
    FREE(pcap->if_name);
    FREE(pcap->filter);
    FREE(pcap);
}

void
os_pcap_drop_safe(os_pcap_t **pcap)
{
    if (pcap == NULL) return;
    os_pcap_drop(*pcap);
    *pcap = NULL;
}
