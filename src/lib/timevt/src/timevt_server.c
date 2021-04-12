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
#include <log.h>
#include <ds_dlist.h>
#include <ds.h>
#include <os_uds_link.h>
#include <os_time.h>

#include "timevt_server.h"

typedef struct timevt_msg
{
    ds_dlist_node_t node;
    size_t length; // read data length in the buffer
    uint8_t buffer[0];
} timevt_msg_t;

struct te_server
{
    uds_link_t socklink;
    struct ev_loop *ev_loop;
    size_t max_msges; // max allowed number of messages in rx buffer
    ev_timer aggr_timer; // aggregation timer
    ds_dlist_t messages; // aggregated messages for the packet
    size_t messages_cnt; // number of messages in the messages list
    uint32_t reportNo; // current report number
    tesrv_new_report_fp_t new_report_fp;
    void *subscriber;

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
        free(tmp);
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
    size_t msize = sizeof(timevt_msg_t) + mlen;
    timevt_msg_t *msg = (timevt_msg_t *)malloc(msize);
    if (msg == NULL)
    {
        LOG(ERR, "Cannot allocate %zu bytes for received message, message lost", msize);
        h->cnt_msg_lost++;
    }
    else
    {
        // storage limit reached ? remove oldest
        if (h->messages_cnt >= h->max_msges)
        {
            free(ds_dlist_remove_head(&h->messages));
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
}

static bool process_dgram(te_server_handle h, const udgram_t *dg)
{
    uint32_t crc, rxcrc;
    size_t hdrlen = strlen(TIMEVT_HEADER);
    if (dg->size < hdrlen + sizeof(crc) || 0 != strncmp(TIMEVT_HEADER, (char *)dg->data, hdrlen))
    {
        return false;
    }
    
    crc = te_crc32_compute(dg->data, dg->size - sizeof(crc));
    rxcrc = te_crc32_read(dg->data + dg->size - sizeof(crc));
    if (crc != rxcrc)
    {
        return false;
    }

    on_message_received(h, dg->data + hdrlen, dg->size - hdrlen - sizeof(crc));
    return true;
}

static void eh_on_dgram_received(uds_link_t *self, const udgram_t *dg)
{
    te_server_handle h = CONTAINER_OF(self, struct te_server, socklink);
    (void)process_dgram(h, dg);
}

static char *alloc_string(char *oldstr, const char *newstr)
{
    char *str = (char *)((newstr != NULL) ? realloc(oldstr, strlen(newstr) + 1) : (free(oldstr), NULL));
    if (str) strcpy(str, newstr);
    return str;
}

te_server_handle tesrv_open(struct ev_loop *ev, const char *sock_name, const char *sw_version,
                            ev_tstamp aggregation_period, size_t max_events)
{
    // server must use event loop
    if (ev == NULL) return NULL;
    if (max_events == 0) return NULL;

    te_server_handle h = (te_server_handle)malloc(sizeof(*h));
    if (h == NULL)
    {
        LOG(ERR, "malloc(%zu) failed in %s", sizeof(*h), __FUNCTION__);
        return NULL;
    }

    memset(h, 0, sizeof(*h));
    ds_dlist_init(&h->messages, timevt_msg_t, node);

    if (!uds_link_init(&h->socklink, sock_name ? sock_name : TESRV_SOCKET_ADDR, ev))
    {
        free(h);
        return NULL;
    }

    uds_link_subscribe_datagram_read(&h->socklink, &eh_on_dgram_received);
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
    uds_link_fini(&h->socklink);
    h->ev_loop = NULL;

    timevt_msg_t *msg;
    for (msg = ds_dlist_head(&h->messages); msg != NULL; )
    {
        timevt_msg_t *tmp = msg;
        msg = ds_dlist_next(&h->messages, msg);

        ds_dlist_remove(&h->messages, tmp);
        free(tmp);
    }

    free(h->nodeid);
    free(h->swver);
    free(h->locid);

    free(h);
}

void tesrv_set_aggregation_period(te_server_handle h, ev_tstamp period)
{
    h->aggr_timer.repeat = period;
    ev_timer_again(h->ev_loop, &h->aggr_timer);
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
    return uds_link_socket_name(&h->socklink);
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
