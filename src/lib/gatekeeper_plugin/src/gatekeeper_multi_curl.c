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
#include <time.h>
#include <mxml.h>

#include "gatekeeper_multi_curl.h"
#include "log.h"

static struct http2_curl curl_mgr;

struct http2_curl *get_curl_multi_mgr(void)
{
    return &curl_mgr;
}


/**
 * @brief logs error is curl operation failed.
 *
 * @param where operation that is performed
 * @param code return value (CURLMcode) of the operation.
 */
static void
mcode_or_die(const char *where, CURLMcode code)
{
    const char *s;
    if (code == CURLM_OK) return;;

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
    case CURLM_BAD_SOCKET:
        s = "CURLM_BAD_SOCKET";
        break;
    default:
        s = "CURLM_unknown";
        break;
    }
    LOGE("http2: %s() %s returns %s\n", __func__, where, s);
}


/**
 * @brief clean up curl library.
 *
 */
bool
gk_curl_exit(void)
{
    CURLMcode mret;

    struct http2_curl *cmgr = get_curl_multi_mgr();

    mret = curl_multi_cleanup(cmgr->multi);
    if (mret != CURLM_OK) return false;

    curl_global_cleanup();
    return true;
}


/**
 * @brief cleanup connection info
 *
 */
void
gk_free_conn(struct gk_conn_info *conn)
{
    struct http2_curl *mgr = get_curl_multi_mgr();
    if (conn->easy)
    {
        curl_multi_remove_handle(mgr->multi, conn->easy);
        curl_easy_cleanup(conn->easy);
    }

    free(conn->url);
    free(conn);
}


/**
 * @brief Check for completed transfers, and remove
 *        their easy handles.
 * @param mgr curl data
 */
static void
check_multi_info(struct http2_curl *mgr)
{
    struct gk_conn_info *conn;
    char *eff_url;
    CURLMsg *msg;
    int msgs_left;
    CURL *easy;
    CURLcode res;

    LOGT("http2: %s() Remaining: %d", __func__, mgr->still_running);
    while ((msg = curl_multi_info_read(mgr->multi, &msgs_left)))
    {
        LOGT("http2: %s() msg = { %d %p { %d } }, queue = %d",
                __func__, msg->msg, msg->easy_handle, msg->data.result, msgs_left);
        if (msg->msg != CURLMSG_DONE) continue;

        easy = msg->easy_handle;
        res = msg->data.result;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, (char **)&conn);
        curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
        LOGT("%s(): DONE: %s => (%d) %s", __func__, eff_url, res, conn->error);
        curl_multi_remove_handle(mgr->multi, easy);
        free(conn->url);
        curl_easy_cleanup(easy);
        free(conn);
    }
}


/**
 * @brief invoked by libevent when we get action on multi socket
 * @param w event that is received
 * @param revents type of the event
 */
void
event_cb(EV_P_ struct ev_io *w, int revents)
{
    struct http2_curl *mgr = (struct http2_curl *) w->data;
    CURLMcode rc;
    int action;

    LOGT("http2: %s  w %p revents %i", __func__, w, revents);
    action = (revents & EV_READ ? CURL_POLL_IN : 0) | (revents & EV_WRITE ?
                                                       CURL_POLL_OUT : 0);
    rc = curl_multi_socket_action(mgr->multi, w->fd, action,
                                  &mgr->still_running);
    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(mgr);
    if (mgr->still_running <= 0)
    {
        LOGT("last transfer done, kill timeout");
        ev_timer_stop(mgr->loop, &mgr->timer_event);
    }

}


/**
 * @brief invoked by libevent when timeout expires
 * @param w event timer
 * @param revents type of the event
 */
static void
timer_cb(EV_P_ struct ev_timer *w, int revents)
{
    struct http2_curl *mgr = w->data;
    CURLMcode rc;

    rc = curl_multi_socket_action(mgr->multi, CURL_SOCKET_TIMEOUT, 0,
                                  &mgr->still_running);
    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(mgr);
}


/**
 * @brief cleans up sock_info structure.
 * @param f sock_info structure to free
 * @param mgr http2_curl structure used to stop ev_io events
 */
static void
remsock(struct gk_sock_info *f, struct http2_curl *mgr)
{
    if (f == NULL) return;

    if (f->evset) ev_io_stop(mgr->loop, &f->ev);

    free(f);
}


/**
 * @brief Assign information to a sock_info structure
 * @param f pointer to sock_info structure to set
 * @param e curl easy handle
 * @param s socket descriptor
 * @param act ev action type
 * @param mgr http2_curl structure used to stop ev_io events
 */
static void
setsock(struct gk_sock_info *f, curl_socket_t s, CURL *e, int act,
        struct http2_curl *mgr)
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

/**
 * @brief Initialize a new sock_info structure
 * @param easy curl easy handle
 * @param s socket descriptor
 * @param mgr http2_curl structure used to stop ev_io events
 */
static void
addsock(curl_socket_t s, CURL *easy, int action,
        struct http2_curl *mgr)
{
    struct gk_sock_info *fdp;

    LOGT("http2: %s %p, %d, %d, %p", __func__, easy, s, action, mgr);

    fdp = calloc(1, sizeof(struct gk_sock_info));

    fdp->global = mgr;
    setsock(fdp, s, easy, action, mgr);
    curl_multi_assign(mgr->multi, s, fdp);
}


/**
 * @brief CURLMOPT_SOCKETFUNCTION callback informing about the
 *        socket status.
 * @param e curl easy handle
 * @param s socket
 * @param what status of the socket
 * @param cbp private callback pointer
 * @param sockp private socket pointer
 * @return CURLM_OK
 */
static int
curl_sock_cb(CURL *e, curl_socket_t s, int what, void *userp, void *sockp)
{
    struct http2_curl *mgr = userp;
    struct gk_sock_info *fdp = sockp;
    const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

    LOGT("http2: %s() socket callback: s=%d e=%p what=%s ",
         __func__, s, e, whatstr[what]);
    if (what == CURL_POLL_REMOVE) remsock(fdp, mgr);
    else if (!fdp) addsock(s, e, what, mgr);
    else setsock(fdp, s, e, what, mgr);

    return CURLM_OK;
}


/**
 * @brief CURLMOPT_TIMERFUNCTION callback to receive timeout values
 * @param multi multi handle
 * @param timeout_ms timeout in number of ms
 * @param mgr private callback pointer
 * @return CURLM_OK
 */
int
multi_timer_cb(CURLM *multi, long timeout_ms,
               struct http2_curl *mgr)
{
    LOGT("http2: %s() %p, %li, %p ", __func__, multi, timeout_ms, mgr);

    ev_timer_stop(mgr->loop, &mgr->timer_event);

    if (timeout_ms >= 0)
    {
        double  t = timeout_ms / 1000;
        ev_timer_init(&mgr->timer_event, timer_cb, t, 0.);
        ev_timer_start(mgr->loop, &mgr->timer_event);
    }

    return 0;
}


/**
 * @brief CURLOPT_WRITEFUNCTION callback when data is received
 * @param ptr pointer to received data
 * @param size value is always 1
 * @param nmemb size of the data received
 * @param data user data argument
 * @return realsize size that is actually read.
 */
static size_t
curl_write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize;

    realsize = size * nmemb;

    LOGT("http2: %s(): %p %zu %zu %p", __func__, ptr, size, nmemb, data);
    return realsize;
}


/**
 * @brief CURLOPT_PROGRESSFUNCTION callback to progress meter function
 * @param p is the pointer set with CURLOPT_PROGRESSDATA()
 * @param dltotal the total number of bytes libcurl expects to
 *        download in  this  transfer
 * @param dlnow number of bytes downloaded so far
 * @param ult total number of bytes libcurl expects to upload in this transfer
 * @param uln is the number of bytes uploaded so far
 * @return returns CURLE_OK
 */
static int
prog_cb(void *p, double dltotal, double dlnow, double ult,
                   double uln)
{
  struct gk_conn_info *conn = (struct gk_conn_info *)p;

  LOGT("%s() Progress: %s (%g/%g)\n", __func__, conn->url, dlnow, dltotal);
  return CURLE_OK;
}


/**
 * @brief Create a new easy handle, and add it to the global curl_multi
 * @param url url to add.
 * @return true if the initialization succeeded,
 *         false otherwise
 */
bool
gk_new_conn(char *url)
{
    struct http2_curl *cmgr;
    struct gk_conn_info *conn;
    CURLMcode rc;

    LOGN("http2: %s() adding url %s", __func__, url);
    cmgr = get_curl_multi_mgr();

    conn = calloc(1, sizeof(struct gk_conn_info));
    if (conn == NULL) return false;

    conn->error[0]='\0';
    conn->easy = curl_easy_init();
    if (!conn->easy) goto err_free_conn;

    conn->global = cmgr;
    conn->url = strdup(url);

    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYHOST, 0L);

    LOGT("http2: %s() adding easy %p to multi %p (%s)",
         __func__, conn->easy, cmgr->multi, url);

    /* add curl easy handle */
    rc = curl_multi_add_handle(cmgr->multi, conn->easy);
    mcode_or_die("gk_new_conn: curl_multi_add_handle", rc);
    if (rc == CURLM_OK) return true;

err_free_conn:
    gk_free_conn(conn);

    return false;
}


/**
 * @brief initialize curl library
 * @param loop pointer to ev_loop structure
 * @return true if the initialization succeeded,
 *         false otherwise
 */
bool
gk_multi_curl_init(struct ev_loop *loop)
{
    struct http2_curl *cmgr = get_curl_multi_mgr();
    CURLcode  rc;
    CURLMcode cmret;

    LOGT("http2: initializing curl");

    /* initialize curl library */
    rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) return false;

    memset(cmgr, 0, sizeof(struct http2_curl));
    cmgr->loop = loop;

    /* initialize multi curl handle*/
    cmgr->multi = curl_multi_init();

    /* initialize event timer callback */
    ev_timer_init(&cmgr->timer_event, timer_cb, 0.0, 0.0);
    cmgr->timer_event.data = cmgr;

    /* initialize socket callback */
    cmret = curl_multi_setopt(cmgr->multi, CURLMOPT_SOCKETFUNCTION, curl_sock_cb);
    if (cmret != CURLM_OK)
    {
        LOGE("http2: failed to initialize socket callback function");
        goto err;
    }
    cmret = curl_multi_setopt(cmgr->multi, CURLMOPT_SOCKETDATA, cmgr);
    if (cmret != CURLM_OK)
    {
        LOGE("http2: failed to initialize socket callback data");
        goto err;
    }

    /* initialize timer callback function */
    cmret = curl_multi_setopt(cmgr->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    if (cmret != CURLM_OK)
    {
        LOGE("http2: failed to initialize timer callback function");
        goto err;
    }
    cmret = curl_multi_setopt(cmgr->multi, CURLMOPT_TIMERDATA, cmgr);
    if (cmret != CURLM_OK)
    {
        LOGE("http2: failed to initialize timer callback data");
        goto err;
    }

    LOGT("http2: curl initialization successful");
    return true;

err:
    gk_curl_exit();
    return false;
}
