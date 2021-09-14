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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MODULE_NAME "te-server"
#include "ds_dlist.h"
#include "ds.h"
#include "os_time.h"
#include "memutil.h"

#include "timevt_server.h"
#include "timevt_msg_link.h"

// default te local logs severity
#ifndef TELOG_SEVERITY
#define TELOG_SEVERITY LOG_SEVERITY_INFO
#endif

typedef struct timevt_msg
{
    ds_dlist_node_t node;
    size_t length; // read data length in the buffer
    uint8_t buffer[0];
} timevt_msg_t;

struct te_server
{
    ipc_msg_link_t *msglink;
    struct ev_loop *ev_loop;
    size_t max_msges; // max allowed number of messages in rx buffer
    ev_timer aggr_timer; // aggregation timer
    ds_dlist_t messages; // aggregated messages for the packet
    size_t messages_cnt; // number of messages in the messages list
    uint32_t reportNo; // current report number
    tesrv_new_report_fp_t new_report_fp;
    void *subscriber;
    log_severity_t log_sev;

    char *locid;
    char *swver;
    char *nodeid;

    /* stats for messages */
    size_t cnt_msg_ack;
    size_t cnt_msg_recv;
    size_t cnt_msg_lost;
    /* stats for reports */
    size_t cnt_reports;
};

static void log_event_local(log_severity_t sev, const uint8_t *mbuf, size_t mlen)
{
    if (sev == LOG_SEVERITY_DISABLED) return;

    Sts__TimeEvent *pte = sts__time_event__unpack(NULL, mlen, mbuf);
    if (pte == NULL) return;

    const char *source = pte->source;
    const char *cat_str = pte->cat;
    const char *subject = pte->subject ? pte->subject : "time-event";
    const char *seq = pte->seq ? pte->seq : "UNI";
    const char *msg = pte->msg ? pte->msg : "";

    char monotime[256];
    (void)print_mono_time(monotime, sizeof(monotime), pte->time);

    // time skipped because provided by logging engine
    mlog(sev, LOG_MODULE_ID_TELOG, "%s [%s] %s: %s: [%s] %s", source, monotime, cat_str, subject, seq, msg);

    sts__time_event__free_unpacked(pte, NULL);
}

void tesrv_subscribe_new_report(te_server_handle h, void *subscriber, tesrv_new_report_fp_t pfn)
{
    h->subscriber = subscriber;
    h->new_report_fp = pfn;
}

static bool send_report(te_server_handle h)
{
    if (ds_dlist_is_empty(&h->messages)) return false;

    Sts__TimeEventsReport report;
    sts__time_events_report__init(&report);

    Sts__DeviceID dev_id;
    sts__device_id__init(&dev_id);

    dev_id.node_id = h->nodeid ? h->nodeid : "(null)";
    dev_id.firmware_version = h->swver ? h->swver : "(null)";
    dev_id.location_id = h->locid ? h->locid : "(null)";

    report.deviceid = &dev_id;

    h->reportNo += 1;
    report.seqno = h->reportNo;
    report.realtime = clock_real_ms();
    report.monotime = clock_mono_ms();

    /* deserialize received messages and add it to the report
     * one after another */

    Sts__TimeEvent *events[h->messages_cnt];
    report.events = events;

    timevt_msg_t *msg;
    for (msg = ds_dlist_head(&h->messages); msg != NULL; )
    {
        timevt_msg_t *tmp = msg;
        msg = ds_dlist_next(&h->messages, msg);

        Sts__TimeEvent *te_msg = sts__time_event__unpack(NULL, tmp->length, tmp->buffer);
        if (te_msg == NULL)
        {
            h->cnt_msg_lost++;
            LOG(ERR, "time-event message unpacking failed, size=%zu", tmp->length);
        }
        else
        {
            report.events[report.n_events] = te_msg;
            report.n_events++;
        }

        ds_dlist_remove(&h->messages, tmp);
        FREE(tmp);
    }

    size_t report_len = sts__time_events_report__get_packed_size(&report);
    uint8_t outbuf[report_len];
    report_len = sts__time_events_report__pack(&report, outbuf);

    size_t n;
    for (n = 0; n < report.n_events; ++n)
    {
        sts__time_event__free_unpacked(report.events[n], NULL);
    }

    // reset counter for next report
    h->messages_cnt = 0;

    // notify subscriber about new report prepared
    if (h->new_report_fp && h->new_report_fp(h->subscriber, h, outbuf, report_len))
    {
        h->cnt_msg_ack += report.n_events;
        return true;
    }
    else
    {
        h->cnt_msg_lost += report.n_events;
        return false;
    }
}

static void eh_on_aggr_time_expired(struct ev_loop *loop, ev_timer *w, int revents)
{
    te_server_handle h = CONTAINER_OF(w, struct te_server, aggr_timer);

    if (send_report(h))
    {
        h->cnt_reports++;
    }
}

static void on_message_received(te_server_handle h, const uint8_t *mbuf, size_t mlen)
{
    void *temp = NULL;

    log_event_local(h->log_sev, mbuf, mlen);

    size_t msize = sizeof(timevt_msg_t) + mlen;
    timevt_msg_t *msg = (timevt_msg_t *)MALLOC(msize);
    // storage limit reached ? remove oldest
    if (h->messages_cnt >= h->max_msges)
    {
        temp = ds_dlist_remove_head(&h->messages);
        FREE(temp);
        h->messages_cnt--;
        h->cnt_msg_lost++;
        h->cnt_msg_recv--;
    }
    // add new
    msg->length = mlen;
    memcpy(msg->buffer, mbuf, mlen);

    ds_dlist_insert_tail(&h->messages, msg);
    h->messages_cnt++;
    h->cnt_msg_recv++;
}

static bool process_read_msg(te_server_handle h, const ipc_msg_t *msg)
{
    uint32_t crc, rxcrc;
    size_t hdrlen = strlen(TIMEVT_HEADER);
    if (msg->size < hdrlen + sizeof(crc) || 0 != strncmp(TIMEVT_HEADER, (char *)msg->data, hdrlen))
    {
        return false;
    }

    crc = te_crc32_compute(msg->data, msg->size - sizeof(crc));
    rxcrc = te_crc32_read(msg->data + msg->size - sizeof(crc));
    if (crc != rxcrc)
    {
        return false;
    }

    on_message_received(h, msg->data + hdrlen, msg->size - hdrlen - sizeof(crc));
    return true;
}

static void eh_on_msg_received(ipc_msg_link_t *link, void *subscr, const ipc_msg_t *msg)
{
    (void)link;
    te_server_handle h = (te_server_handle)subscr;
    (void)process_read_msg(h, msg);
}

static char *alloc_string(char *oldstr, const char *newstr)
{
    if (newstr != NULL)
    {
        return strcpy(REALLOC(oldstr, strlen(newstr) + 1), newstr);
    }
    else
    {
        FREE(oldstr);
        return NULL;
    }
}

te_server_handle tesrv_open(struct ev_loop *ev, const char *addr, const char *sw_version,
                            ev_tstamp aggregation_period, size_t max_events)
{
    // server must use event loop
    if (ev == NULL) return NULL;
    if (max_events == 0) return NULL;

    te_server_handle h = (te_server_handle)MALLOC(sizeof(*h));

    ipc_msg_link_t *ml = ipc_msg_link_open(addr, ev, TELOG_SERVER_MSG_LINK_ID);
    if (ml == NULL) return NULL;

    memset(h, 0, sizeof(*h));
    ds_dlist_init(&h->messages, timevt_msg_t, node);
    h->msglink = ml;
    h->log_sev = TELOG_SEVERITY;
    ipc_msg_link_subscribe_receive(h->msglink, h, &eh_on_msg_received);
    h->ev_loop = ev;
    h->max_msges = max_events;
    h->swver = alloc_string(h->swver, sw_version);
    ev_tstamp delay = (aggregation_period < 0.001) ? TIMEVT_AGGR_PERIOD : aggregation_period;
    ev_timer_init(&h->aggr_timer, &eh_on_aggr_time_expired, delay, delay);
    ev_timer_start(h->ev_loop, &h->aggr_timer);

    return h;
}

void tesrv_close(te_server_handle h)
{
    if (h == NULL || h->ev_loop == NULL) return;

    ev_timer_stop(h->ev_loop, &h->aggr_timer);
    ipc_msg_link_close(h->msglink);
    h->ev_loop = NULL;

    timevt_msg_t *msg;
    for (msg = ds_dlist_head(&h->messages); msg != NULL; )
    {
        timevt_msg_t *tmp = msg;
        msg = ds_dlist_next(&h->messages, msg);

        ds_dlist_remove(&h->messages, tmp);
        FREE(tmp);
    }

    FREE(h->nodeid);
    FREE(h->swver);
    FREE(h->locid);

    FREE(h);
}

void tesrv_set_aggregation_period(te_server_handle h, ev_tstamp period)
{
    h->aggr_timer.repeat = period;
    ev_timer_again(h->ev_loop, &h->aggr_timer);
}

bool tesrv_set_log_severity(te_server_handle h, log_severity_t lsev)
{
    if ((int)lsev < 0 || lsev >= LOG_SEVERITY_LAST) return false;
    h->log_sev = lsev;
    return true;
}

void tesrv_set_identity(te_server_handle h, const char *location_id, const char *node_id)
{
    h->locid = alloc_string(h->locid, location_id);
    h->nodeid = alloc_string(h->nodeid, node_id);
}

const char *tesrv_get_location_id(te_server_handle h)
{
    return h->locid;
}

const char *tesrv_get_node_id(te_server_handle h)
{
    return h->nodeid;
}

const char *tesrv_get_name(te_server_handle h)
{
    return ipc_msg_link_addr(h->msglink);
}

size_t tesrv_get_msg_ack(te_server_handle h)
{
    return h->cnt_msg_ack;
}

size_t tesrv_get_msg_received(te_server_handle h)
{
    return h->cnt_msg_recv;
}

size_t tesrv_get_msg_lost(te_server_handle h)
{
    return h->cnt_msg_lost;
}

size_t tesrv_get_reports(te_server_handle h)
{
    return h->cnt_reports;
}
