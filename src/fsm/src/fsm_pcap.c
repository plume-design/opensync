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

#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pcap.h>
#include <errno.h>

#include "log.h"
#include "fsm.h"
#include "json_util.h"
#include "qm_conn.h"
#include "os_types.h"
#include "dppline.h"

/* Set of default values for pcaps settings */
static int g_buf_size = 0;
static int g_cnt = 1;
static int g_immediate_mode = 1;

#if defined(CONFIG_FSM_PCAP_SNAPLEN) && (CONFIG_FSM_PCAP_SNAPLEN > 0)
static int g_snaplen = CONFIG_FSM_PCAP_SNAPLEN;
#else
static int g_snaplen = 2048;
#endif

static void
fsm_pcap_handler(uint8_t * args, const struct pcap_pkthdr *header,
                 const uint8_t *bytes)
{
    struct net_header_parser net_parser;
    struct fsm_parser_ops *parser_ops;
    struct fsm_session *session;
    size_t len;

    session = (struct fsm_session *)args;

    memset(&net_parser, 0, sizeof(net_parser));
    net_parser.packet_len = header->caplen;
    net_parser.caplen = header->caplen;
    net_parser.data = (uint8_t *)bytes;
    net_parser.pcap_datalink = session->pcaps->pcap_datalink;
    len = net_header_parse(&net_parser);
    if (len == 0) return;

    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler(session, &net_parser);
}


/**
 * @brief parse a session's pcap options from ovsdb.
 *
 * @param session the session
 * @return true if the pcap session needs to be restared, false otherwise.
 */
bool
fsm_get_pcap_options(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    char *buf_size_str;
    char *snaplen_str;
    char *mode_str;
    int prev_value;
    char *cnt_str;
    bool started;
    bool restart;
    char *iface;
    long value;

    pcaps = session->pcaps;
    if (pcaps == NULL) return false;
    started = (pcaps->started != 0);

    iface = session->conf->if_name;
    if (iface == NULL) return false;

    restart = false;

    /* Check the buffer size option */
    prev_value = (started ? pcaps->buffer_size : g_buf_size);
    if (!started) pcaps->buffer_size = g_buf_size;
    buf_size_str = fsm_get_other_config_val(session, "pcap_bsize");
    if (buf_size_str != NULL)
    {
        errno = 0;
        value = strtol(buf_size_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 buf_size_str, strerror(errno));
        }
        else pcaps->buffer_size = (int)value;
    }
    LOGD("%s: %s: buffer size: %d", __func__, iface, pcaps->buffer_size);
    restart |= (started && (prev_value != pcaps->buffer_size));
    if (restart)
    {
        LOGI("%s: %s: buf size changed from %d to %d. Will restart pcap socket",
             __func__, iface, prev_value, pcaps->buffer_size);
    }

    /* Check the snaplen option */
    prev_value = (started ? pcaps->snaplen : g_snaplen);
    if (!started) pcaps->snaplen = g_snaplen;
    snaplen_str = fsm_get_other_config_val(session, "pcap_snaplen");
    if (snaplen_str != NULL)
    {
        errno = 0;
        value = strtol(snaplen_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 snaplen_str, strerror(errno));
        }
        else pcaps->snaplen = (int)value;
    }
    LOGD("%s: %s: snaplen: %d", __func__, iface, pcaps->snaplen);
    restart |= (started && (prev_value != pcaps->snaplen));
    if (restart)
    {
        LOGI("%s: %s: snaplen changed from %d to %d. Will restart pcap socket",
             __func__, iface, prev_value, pcaps->snaplen);
    }

    /* Check the immediate mode option */
    prev_value = (started ? pcaps->immediate : g_immediate_mode);
    if (!started) pcaps->immediate = g_immediate_mode;
    mode_str = fsm_get_other_config_val(session, "pcap_immediate");
    if (mode_str != NULL)
    {
        pcaps->immediate = (strcmp(mode_str, "no") ? 1 : 0);
    }
    LOGD("%s: %s: immediate mode: %d", __func__, iface, pcaps->immediate);
    restart |= (started && (prev_value != pcaps->immediate));
    if (restart)
    {
        LOGI("%s: %s: immediate mode changed from %d to %d. "
             "Will restart pcap socket",
             __func__, iface, prev_value, pcaps->immediate);
    }

    pcaps->cnt = g_cnt;
    cnt_str = fsm_get_other_config_val(session, "pcap_cnt");
    if (cnt_str != NULL)
    {
        errno = 0;
        value = strtol(cnt_str, NULL, 10);
        if (errno != 0)
        {
            LOGD("%s: error reading value %s: %s", __func__,
                 cnt_str, strerror(errno));
        }
        else pcaps->cnt = (int)value;
    }
    LOGD("%s: %s: delivery count: %d", __func__, iface, pcaps->cnt);

    if (restart)
    {
        LOGEM("%s: detected pcap settings changes, restarting", __func__);
        exit(EXIT_SUCCESS);
    }

    return restart;
}


bool
fsm_set_pcap_options(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    pcap_t *pcap;
    char *iface;
    bool ret;
    int rc;

    pcaps = session->pcaps;
    pcap = pcaps->pcap;

    iface = session->conf->if_name;
    if (iface == NULL) return false;

    ret = true;

    /* set pcap buffer size */
    LOGI("%s: setting %s buffer size to %d",
         __func__, iface, pcaps->buffer_size);
    rc = pcap_set_buffer_size(pcap, pcaps->buffer_size);
    if (rc != 0)
    {
        LOGW("%s: Unable to set %s pcap buffer size",
             __func__, iface);
        ret = false;
    }

    /* set pcap immediate mode */
    LOGI("%s: %ssetting %s in immediate mode",
         __func__, (pcaps->immediate == 1) ? "" : "_not_ ", iface);
    rc = pcap_set_immediate_mode(pcap, pcaps->immediate);
    if (rc != 0)
    {
        LOGW("%s: Unable to set %s pcap immediate mode",
             __func__, iface);
        ret = false;
    }

    /* set pcap snap length */
    LOGI("%s: setting %s snap length to %d",
         __func__, iface, pcaps->snaplen);
    rc = pcap_set_snaplen(pcap, pcaps->snaplen);
    if (rc != 0)
    {
        LOGW("%s: unable to set %s snaplen to %d", __func__,
             iface, pcaps->snaplen);
        ret = false;
    }

    return ret;
}

bool
fsm_pcap_update(struct fsm_session *session)
{
    bool ret;

    if (!fsm_plugin_has_intf(session)) return true;

    ret = fsm_get_pcap_options(session);
    if (ret) fsm_pcap_close(session);

    ret = fsm_pcap_open(session);
    if (!ret)
    {
        LOGE("pcap open failed for handler %s",
             session->name);
        return false;
    }

    return true;
}


static void
fsm_pcap_recv_fn(EV_P_ ev_io *ev, int revents)
{
    (void)loop;
    (void)revents;

    struct fsm_session *session = ev->data;
    struct fsm_pcaps *pcaps = session->pcaps;
    pcap_t *pcap = pcaps->pcap;

    /* Ready to receive packets */
    pcap_dispatch(pcap, pcaps->cnt, fsm_pcap_handler, (void *)session);
}


bool fsm_pcap_open(struct fsm_session *session) {
    struct fsm_mgr *mgr = fsm_get_mgr();
    struct fsm_pcaps *pcaps = session->pcaps;
    pcap_t *pcap = NULL;
    char *iface = session->conf->if_name;
    struct bpf_program *bpf = pcaps->bpf;
    char *pkt_filter = session->conf->pkt_capt_filter;
    char pcap_err[PCAP_ERRBUF_SIZE];
    bool ret;
    int rc;

    if (iface == NULL) return true;

    pcaps->pcap = pcap_create(iface, pcap_err);
    if (pcaps->pcap == NULL) {
        LOGN("PCAP initialization failed for interface %s.",
             iface);
        goto error;
    }

    ret = fsm_set_pcap_options(session);
    if (!ret) goto error;

    pcap = pcaps->pcap;

    rc = pcap_setnonblock(pcap, 1, pcap_err);
    if (rc == -1) {
        LOGE("Unable to set non-blocking mode: %s", pcap_err);
        goto error;
    }

    /*
     * We do not want to block forever on receive. A timeout 0 means block
     * forever, so use 1ms for the timeout.
     */
    rc = pcap_set_timeout(pcap, 1);
    if (rc != 0)
    {
        LOGE("%s: %s: Error setting buffer timeout.", __func__,
            iface);
        goto error;
    }

    /* Activate the interface */
    rc = pcap_activate(pcap);
    if (rc != 0) {
        LOGE("Error activating interface %s: %s",
             iface, pcap_geterr(pcap));
        goto error;
    }

    if ((pcap_datalink(pcap) != DLT_EN10MB) &&
        (pcap_datalink(pcap) != DLT_LINUX_SLL))
    {
        LOGE("%s: unsupported data link layer: %d\n",
            __func__, pcap_datalink(pcap));
            goto error;
    }
    pcaps->pcap_datalink = pcap_datalink(pcap);

    rc = pcap_compile(pcap, bpf, pkt_filter, 0, PCAP_NETMASK_UNKNOWN);
    if (rc != 0) {
        LOGE("Error compiling capture filter: '%s'. PCAP error:\n>>> %s",
             pkt_filter, pcap_geterr(pcap));
        pcaps->bpf = NULL;
        goto error;
    }

    rc = pcap_setfilter(pcap, bpf);
    if (rc != 0) {
        LOGE("Error setting the capture filter, error: %s",
             pcap_geterr(pcap));
        goto error;
    }

    /* We need a selectable fd for libev */
    pcaps->pcap_fd = pcap_get_selectable_fd(pcap);
    if (pcaps->pcap_fd < 0) {
        LOGE("Error getting selectable FD (%d). PCAP error:\n>>> %s",
             pcaps->pcap_fd, pcap_geterr(pcap));
        goto error;
    }

    /* Register FD for libev events */
    ev_io_init(&pcaps->fsm_evio, fsm_pcap_recv_fn, pcaps->pcap_fd, EV_READ);
    /* Set user data */
    pcaps->fsm_evio.data = (void *)session;
    /* Start watching it on the default queue */
    ev_io_start(mgr->loop, &pcaps->fsm_evio);
    pcaps->started = 1;

    return true;

  error:
    LOGE("Interface %s registered for snooping returning error.", iface);

    return false;
}

void fsm_pcap_close(struct fsm_session *session) {
    struct fsm_mgr *mgr = fsm_get_mgr();
    struct fsm_pcaps *pcaps = session->pcaps;
    pcap_t *pcap = pcaps->pcap;

    if (ev_is_active(&pcaps->fsm_evio)) {
        ev_io_stop(mgr->loop, &pcaps->fsm_evio);
    }

    if (pcaps->bpf != NULL) {
        pcap_freecode(pcaps->bpf);
        free(pcaps->bpf);
    }

    if (pcap != NULL) {
        pcap_close(pcap);
        pcaps->pcap = NULL;
    }
    pcaps->started = 0;
}
