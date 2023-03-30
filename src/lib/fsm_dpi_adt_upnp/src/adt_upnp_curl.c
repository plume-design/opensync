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

#include <curl/curl.h>
#include <ev.h>
#include <mxml.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "adt_upnp_curl.h"
#include "adt_upnp_json_report.h"
#include "fsm_dpi_adt_upnp.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

static struct adt_upnp_curl conn_mgr;

struct adt_upnp_curl *get_curl_mgr(void) {
    return &conn_mgr;
}

#define NUM_OF_ELEMENTS 11

static struct adt_upnp_key_val elements[NUM_OF_ELEMENTS] =
{
    { "deviceType", NULL, FSM_UPNP_URL_MAX_SIZE },
    { "friendlyName", NULL, 64 },
    { "manufacturer", NULL, 256 },
    { "manufacturerURL", NULL, FSM_UPNP_URL_MAX_SIZE },
    { "modelDescription", NULL, 128 },
    { "modelName", NULL, 32 },
    { "modelNumber", NULL, 32 },
    { "modelURL", NULL, FSM_UPNP_URL_MAX_SIZE },
    { "serialNumber", NULL, 64 },
    { "UDN", NULL, 164 },
    { "UPC", NULL, 12 },
};

void timer_cb(EV_P_ struct ev_timer *w, int revents);


struct adt_upnp_key_val *
adt_upnp_get_elements(void)
{
    return elements;
}


int
multi_timer_cb(CURLM *multi, long timeout_ms,
               struct adt_upnp_curl *mgr)
{
    ev_timer_stop(mgr->loop, &mgr->timer_event);
    if (timeout_ms > 0)
    {
        double  t = timeout_ms / 1000;
        ev_timer_init(&mgr->timer_event, timer_cb, t, 0.);
        ev_timer_start(mgr->loop, &mgr->timer_event);
    }
    else if (timeout_ms == 0)
    {
        timer_cb(mgr->loop, &mgr->timer_event, 0);
    }
    return 0;
}


void
mcode_or_die(const char *where, CURLMcode code)
{
    const char *s;
    if (code == CURLM_OK) return;

    switch(code)
    {
        case CURLM_BAD_HANDLE:
            s = "CURLM_BAD_HANDLE";
            break;
        case CURLM_BAD_EASY_HANDLE:
            s = "CURLM_BAD_EASY_HANDLE";
            break;
        case CURLM_OUT_OF_MEMORY:
            s = "CURLM_OUT_OF_MEMORY";
            break;
        case CURLM_INTERNAL_ERROR:
            s = "CURLM_INTERNAL_ERROR";
            break;
        case CURLM_UNKNOWN_OPTION:
            s = "CURLM_UNKNOWN_OPTION";
            break;
        case CURLM_LAST:
            s = "CURLM_LAST";
            break;
        default:
            s = "CURLM_unknown";
            break;
        case CURLM_BAD_SOCKET:
            s = "CURLM_BAD_SOCKET";
    }
    LOGE("%s returns %s\n", where, s);
}


void
adt_upnp_init_elements(struct fsm_dpi_adt_upnp_root_desc *url)
{
    elements[0].value = url->dev_type;
    elements[1].value = url->friendly_name;
    elements[2].value = url->manufacturer;
    elements[3].value = url->manufacturer_url;
    elements[4].value = url->model_desc;
    elements[5].value = url->model_name;
    elements[6].value = url->model_num;
    elements[7].value = url->model_url;
    elements[8].value = url->serial_num;
    elements[9].value = url->udn;
    elements[10].value = url->upc;
};


void
upnp_scan_data(struct conn_info *conn)
{
    struct upnp_curl_buffer *data = &conn->data;
    struct fsm_dpi_adt_upnp_root_desc *url = conn->context;
    struct adt_upnp_report to_report = { 0 };
    mxml_node_t *node = NULL;
    const char *temp = NULL;
    char *report = NULL;
    mxml_node_t *tree;
    size_t i;

    tree = mxmlLoadString(NULL, data->buf, MXML_OPAQUE_CALLBACK);
    if (tree == NULL)
    {
        LOGE("%s: mxml parsing failed", __func__);
        goto fail;
    }

    adt_upnp_init_elements(url);
    for (i = 0; i < NUM_OF_ELEMENTS; i++)
    {
        if (elements[i].key[0] == '\0')
        {
            LOGT("%s: elements[%zu] key is null", __func__, i);
            continue;
        }
        LOGT("%s: looking up %s", __func__, elements[i].key);
        node = mxmlFindElement(tree, tree, elements[i].key,
                               NULL, NULL, MXML_DESCEND);
        if (node == NULL)
        {
            LOGT("%s: %s lookup failed", __func__, elements[i].key);
            continue;
        }

        temp = mxmlGetOpaque(node);
        if (temp == NULL)
        {
            LOGT("%s: %s value lookup failed", __func__, elements[i].key);
            continue;
        }

        LOGT("%s: key %s, value %s", __func__, elements[i].key, temp);
        strscpy(elements[i].value, temp, elements[i].val_max_len);
    }

    for (i = 0; i < NUM_OF_ELEMENTS; i++)
    {
        if (elements[i].key[0] == '\0') continue;

        LOGT("%s: %s set to %s", __func__,
             elements[i].key,
             (strlen(elements[i].value) != 0) ? elements[i].value : "None");
    }

    mxmlDelete(tree);

    // This is where the report gets created and sent immediately.
    to_report.first = &elements[0];
    to_report.nelems = NUM_OF_ELEMENTS;
    to_report.url = url;
    url->timestamp = time(NULL);

    report = jencode_adt_upnp_report(url->session, &to_report);
    url->session->ops.send_report(url->session, report);
    url->state = FSM_DPI_ADT_UPNP_COMPLETE;
    return;

fail:
    url->state = FSM_DPI_ADT_UPNP_INIT;
}


void
upnp_curl_process_conn(struct conn_info *conn)
{
    struct upnp_curl_buffer *data = NULL;
    struct fsm_dpi_adt_upnp_root_desc *url = NULL;

    if (conn == NULL)
    {
        LOGT("%s: conn pointer is null", __func__);
        return;
    }
    url = conn->context;
    data = &conn->data;
    LOGT("%s: data for url %s:\n%s", __func__, url->url, data->buf);
    upnp_scan_data(conn);
}


void
upnp_free_conn(struct conn_info *conn)
{
    struct adt_upnp_curl *mgr = get_curl_mgr();
    struct upnp_curl_buffer *data = &conn->data;

    FREE(data->buf);
    if (conn->easy)
    {
        curl_multi_remove_handle(mgr->multi, conn->easy);
        curl_easy_cleanup(conn->easy);
    }
    FREE(conn);
}

void
check_multi_info(struct adt_upnp_curl *mgr)
{
    struct fsm_dpi_adt_upnp_root_desc *url;
    char *eff_url;
    CURLMsg *msg;
    int msgs_left;
    struct conn_info *conn;
    CURL *easy;
    CURLcode res;

    while ((msg = curl_multi_info_read(mgr->multi, &msgs_left)))
    {
        if (msg->msg != CURLMSG_DONE) continue;

        easy = msg->easy_handle;
        res = msg->data.result;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, (char **)&conn);
        curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
        LOGT("DONE: %s => (%d) %s", eff_url, res, conn->error);
        curl_multi_remove_handle(mgr->multi, easy);
        curl_easy_cleanup(easy);
        conn->easy = NULL;
        if (res == CURLE_OK) upnp_curl_process_conn(conn);
        else
        {
            url = conn->context;
            url->state = FSM_DPI_ADT_UPNP_INIT;
        }
        upnp_free_conn(conn);
    }
}


void
event_cb(EV_P_ struct ev_io *w, int revents)
{
    struct adt_upnp_curl *mgr = (struct adt_upnp_curl *) w->data;
    CURLMcode rc;
    int action;

    LOGT("%s: w %p revents %i", __func__, w, revents);
    action  = (revents & EV_READ ? CURL_POLL_IN : 0);
    action |= (revents & EV_WRITE ? CURL_POLL_OUT : 0);
    rc = curl_multi_socket_action(mgr->multi, w->fd, action,
                                  &mgr->still_running);
    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(mgr);
    if (mgr->still_running <= 0)
    {
        LOGT("%s: last transfer done, kill timeout.", __func__);
        ev_timer_stop(mgr->loop, &mgr->timer_event);
    }
}


void
timer_cb(EV_P_ struct ev_timer *w, int revents)
{
    struct adt_upnp_curl *mgr = w->data;
    CURLMcode rc;

    rc = curl_multi_socket_action(mgr->multi, CURL_SOCKET_TIMEOUT, 0,
                                  &mgr->still_running);
    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(mgr);
}


void
remsock(struct sock_info *f, struct adt_upnp_curl *mgr)
{
    if (f == NULL) return;

    if (f->evset) ev_io_stop(mgr->loop, &f->ev);

    FREE(f);
}


void
setsock(struct sock_info *f, curl_socket_t s, CURL *e, int act,
        struct adt_upnp_curl *mgr)
{
    int kind = (act & CURL_POLL_IN ? EV_READ : 0) |
               (act&CURL_POLL_OUT ? EV_WRITE : 0);

    f->sockfd = s;
    f->action = act;
    f->easy = e;
    if (f->evset) ev_io_stop(mgr->loop, &f->ev);

    ev_io_init(&f->ev, event_cb, f->sockfd, kind);
    f->ev.data = mgr;
    f->evset = 1;
    ev_io_start(mgr->loop, &f->ev);
}


void
addsock(curl_socket_t s, CURL *easy, int action,
        struct adt_upnp_curl *mgr)
{
    struct sock_info *fdp = CALLOC(sizeof(struct sock_info), 1);

    fdp->global = mgr;
    setsock(fdp, s, easy, action, mgr);
    curl_multi_assign(mgr->multi, s, fdp);
}


int
sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
    struct adt_upnp_curl *mgr = cbp;
    struct sock_info *fdp = sockp;
    const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

    LOGT("%s: socket callback: s=%d e=%p what=%s ", __func__, s, e, whatstr[what]);
    if (what == CURL_POLL_REMOVE) remsock(fdp, mgr);
    else if (!fdp) addsock(s, e, what, mgr);
    else setsock(fdp, s, e, what, mgr);

    return 0;
}


size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    struct conn_info *conn = data;
    struct upnp_curl_buffer *upnp_data = NULL;

    if (conn == NULL) goto out;

    upnp_data = &conn->data;
    if (upnp_data->buf == NULL) goto out;

    upnp_data->buf = REALLOC(upnp_data->buf, upnp_data->size + realsize + 1);

    memcpy(&upnp_data->buf[upnp_data->size], ptr, realsize);
    upnp_data->size += realsize;
    upnp_data->buf[upnp_data->size] = 0;

out:
    return realsize;
}


void
adt_upnp_call_mcurl(struct fsm_dpi_adt_upnp_root_desc *url)
{
    struct adt_upnp_curl *mgr = get_curl_mgr();
    struct conn_info *conn;
    CURLMcode rc;

    LOGD("%s: Setting up curl", __func__);
    conn = CALLOC(1, sizeof(struct conn_info));

    conn->data.buf = MALLOC(1);
    conn->data.size = 0;
    conn->error[0]='\0';
    conn->easy = curl_easy_init();
    if (!conn->easy) goto err_free_conn;

    conn->global = mgr;
    conn->url = url->url;
    conn->context = url;

    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYHOST, 0L);

    rc = curl_multi_add_handle(mgr->multi, conn->easy);
    mcode_or_die("adt_upnp_call_mcurl: curl_multi_add_handle", rc);
    if (rc == CURLM_OK) return;

err_free_conn:
    upnp_free_conn(conn);

    return;
}


void
adt_upnp_curl_init(struct ev_loop *loop)
{
    struct adt_upnp_curl *mgr = get_curl_mgr();

    curl_global_init(CURL_GLOBAL_DEFAULT);
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->multi = curl_multi_init();

    ev_timer_init(&mgr->timer_event, timer_cb, 0., 0.);
    mgr->timer_event.data = mgr;
    mgr->fifo_event.data = mgr;
    curl_multi_setopt(mgr->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
    curl_multi_setopt(mgr->multi, CURLMOPT_SOCKETDATA, mgr);
    curl_multi_setopt(mgr->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    curl_multi_setopt(mgr->multi, CURLMOPT_TIMERDATA, mgr);
}


void
adt_upnp_curl_exit(void)
{
    struct adt_upnp_curl *mgr = get_curl_mgr();

    curl_multi_cleanup(mgr->multi);
    curl_global_cleanup();
}
