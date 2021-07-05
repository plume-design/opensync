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

#include "gatekeeper_single_curl.h"
#include "gatekeeper_multi_curl.h"
#include "gatekeeper.pb-c.h"
#include "memutil.h"
#include "log.h"

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
gk_curl_multi_cleanup(struct fsm_gk_session *fsm_gk_session)
{
    struct gk_curl_multi_info *mcurl_info;
    CURLMcode mret;

    LOGN("http2: cleaning up multi curl");

    mcurl_info = &fsm_gk_session->mcurl;

    if (mcurl_info->mcurl_handle == NULL) return false;

    mret = curl_multi_cleanup(mcurl_info->mcurl_handle);
    if (mret != CURLM_OK) return false;

    curl_global_cleanup();
    return true;
}


/**
 * @brief cleanup connection info
 *
 */
void
gk_free_conn(struct gk_conn_info *conn, struct gk_curl_multi_info *mcurl_info)
{
    struct gk_mcurl_buffer *data = &conn->data;

    free(data->buf);
    if (conn->easy)
    {
        curl_multi_remove_handle(mcurl_info->mcurl_handle, conn->easy);
        curl_easy_cleanup(conn->easy);
    }
    free(conn);
}

/**
 * @brief prints the contents of mcurl_data_tree
 *
 */
void
gk_dump_tree(ds_tree_t *tree)
{
    struct gk_mcurl_data *mcurl_data;

    mcurl_data = ds_tree_head(tree);
    while (mcurl_data != NULL)
    {
        LOGT("http2: request type %d: request id %d, policy_req ==  %p",
             mcurl_data->req_type,
             mcurl_data->req_id,
             mcurl_data->gk_verdict->policy_req);
        mcurl_data = ds_tree_next(tree, mcurl_data);
    }
}

/**
 * @brief returns the reply header for the given request type.
 * @param response - unpacked response from the gk service
 * @param req_type - request type to check for
 * Returns the header if the reply is present for the given
 * request type, else returns NULL.
 */
Gatekeeper__Southbound__V1__GatekeeperCommonReply *
gk_get_reply_header(Gatekeeper__Southbound__V1__GatekeeperReply *response, int req_type)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header = NULL;

    switch (req_type)
    {
        case FSM_FQDN_REQ:
            if (response->reply_fqdn != NULL)
            {
                header = response->reply_fqdn->header;
            }
            break;

        case FSM_SNI_REQ:
            if (response->reply_https_sni != NULL)
            {
                header = response->reply_https_sni->header;
            }
            break;

        case FSM_HOST_REQ:
            if (response->reply_http_host != NULL)
            {
                header = response->reply_http_host->header;
            }
            break;

        case FSM_URL_REQ:
            if (response->reply_http_url != NULL)
            {
                header = response->reply_http_url->header;
            }
            break;

        case FSM_APP_REQ:
            if (response->reply_app != NULL)
            {
                header = response->reply_app->header;
            }
            break;

        case FSM_IPV4_REQ:
            if (response->reply_ipv4 != NULL)
            {
                header = response->reply_ipv4->header;
            }
            break;

        case FSM_IPV6_REQ:
            if (response->reply_ipv6 != NULL)
            {
                header = response->reply_ipv6->header;
            }
            break;

        case FSM_IPV4_FLOW_REQ:
            if (response->reply_ipv4_tuple != NULL)
            {
                header = response->reply_ipv4_tuple->header;
            }
            break;

        case FSM_IPV6_FLOW_REQ:
            if (response->reply_ipv6_tuple != NULL)
            {
                header = response->reply_ipv6_tuple->header;
            }
            break;

        default:
            LOGN("http2: %s() invalid request type %d", __func__, req_type);
            break;
    }

    return header;

}

/**
 * @brief looks up the tree mcurl_data_tree for the given key value.
 * @param tree - pointer to mcurl_data_tree
 * @param req_type - request type to check for
 * @param req_id - request id to check for
 * Returns mcurl data if it is found in the tree else returns NULL
 */
static struct gk_mcurl_data *
gk_find_curl_data(ds_tree_t *tree, int req_type, int req_id)
{
    struct gk_mcurl_data mcurl_data;
    struct gk_mcurl_data *res;

    if(tree == NULL) return NULL;

    mcurl_data.req_id = req_id;
    mcurl_data.req_type = req_type;

    res = ds_tree_find(tree, &mcurl_data);

    return res;
}

/**
 * @brief returns the stores request for the received reply message
 * @param gk_session - pointer gatekeeper session
 * @param response - unpacked response received from gatekeeper
 * Returns the request data if found else returns NULL
 */
static struct gk_mcurl_data *
gk_lookup_curl_data(struct fsm_gk_session *gk_session,
                    Gatekeeper__Southbound__V1__GatekeeperReply *response)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header = NULL;
    struct gk_mcurl_data *mcurl_data;
    int req_type;
    int req_id;
    int i;

    for (i = FSM_FQDN_REQ; i <= FSM_IPV6_FLOW_REQ; i++)
    {
        req_type = i;
        header = gk_get_reply_header(response, i);
        if (header) break;
    }

    /* check if header info found */
    if (header == NULL) return NULL;
    req_id = header->request_id;

    mcurl_data = gk_find_curl_data(&gk_session->mcurl_data_tree, req_type, req_id);
    if (mcurl_data == NULL)
    {
        LOGT("%s(): mcurl_data is NULL", __func__);
        return mcurl_data;
    }

    LOGT("http2: curl data found for request id %d request type %d ", req_id, req_type);


    return mcurl_data;
}

void
gk_process_fail_response(struct gk_conn_info *conn)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_gk_session *gk_session;
    struct gk_mcurl_data *mcurl_data;
    int type;
    int id;

    if (conn == NULL) return;

    type = conn->req_key.req_type;
    id = conn->req_key.req_id;

    LOGN("processing failed response of request type/id %d/%d", type, id);

    gk_session = conn->context;
    gk_dump_tree(&gk_session->mcurl_data_tree);

    /* get the curl data associated with this request */
    mcurl_data = gk_find_curl_data(&gk_session->mcurl_data_tree, type, id);
    if (mcurl_data == NULL || mcurl_data->gk_verdict == NULL)
    {
        LOGD("%s(): curl data lookup failed", __func__);
        return;
    }

    if (mcurl_data->gk_verdict->policy_reply == NULL)
    {
        LOGD("%s(): policy reply is NULL", __func__);
    }

    policy_reply = mcurl_data->gk_verdict->policy_reply;

    if (policy_reply->policy_response == NULL)
    {
        LOGD("%s(): policy response is NULL", __func__);
        return;
    }
    policy_reply->policy_response(mcurl_data->gk_verdict->policy_req, policy_reply);

}

/**
 * @brief processes the response received from the gatekeeper
 * @param conn - pointer containing the response
 * Returns true if processing was successful else false
 */
bool
gk_process_response(struct gk_conn_info *conn)
{
    Gatekeeper__Southbound__V1__GatekeeperReply *unpacked_data;
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_req *policy_request;
    struct fsm_gk_session *gk_session;
    struct gk_mcurl_buffer *data = NULL;
    struct gk_mcurl_data *mcurl_data;
    bool ret = true;

    if (conn == NULL)
    {
        LOGN("http2: conn pointer is NULL");
        return false;
    }

    gk_session = conn->context;

    gk_dump_tree(&gk_session->mcurl_data_tree);

    data = &conn->data;
    LOGT("http2: response received for url %s, data size: %d",
         conn->url, data->size);

    unpacked_data = gatekeeper__southbound__v1__gatekeeper_reply__unpack(NULL,
                                                                        data->size,
                                                                        (uint8_t *)data->buf);
    if (unpacked_data == NULL)
    {
        LOGN("http2: error in unpacking data");
        return false;
    }

    /* get the curl data for this reponse */
    mcurl_data = gk_lookup_curl_data(gk_session, unpacked_data);
    if (mcurl_data == NULL || mcurl_data->gk_verdict == NULL)
    {
        LOGD("%s(): mcurl data is NULL, cannot process multi curl response", __func__);
        ret = false;
        goto error;
    }

    /* set the response */
    policy_request = mcurl_data->gk_verdict->policy_req;
    if (policy_request == NULL)
    {
        LOGD("%s(): policy request is NULL, cannot process multi curl response", __func__);
        ret = false;
        goto error;
    }

    LOGT("%s(): setting policy for request == %p", __func__, policy_request);
    ret = gk_set_policy(unpacked_data, mcurl_data->gk_verdict);
    if (ret == false)
    {
        LOGD("%s(): failed to set policy response for policy req = %p", __func__, policy_request);
        goto error;
    }

    policy_reply = mcurl_data->gk_verdict->policy_reply;
    if (policy_reply->gatekeeper_response == NULL)
    {
        LOGT("%s(): gatekeeper_response callback is not set", __func__);
        ret = false;
        goto error;
    }

    LOGT("%s(): adding result to gatekeeper cache", __func__);
    gk_add_policy_to_cache(mcurl_data->gk_verdict->policy_req, policy_reply);

    policy_reply->gatekeeper_response(mcurl_data->gk_verdict->policy_req, policy_reply);

error:
    gatekeeper__southbound__v1__gatekeeper_reply__free_unpacked(unpacked_data, NULL);

    return ret;
}

/**
 * @brief Check for completed transfers, and remove
 *        their easy handles.
 * @param mcurl_info curl data
 */
static void
check_multi_info(struct gk_curl_multi_info *mcurl_info)
{
    struct gk_conn_info *conn;
    char *eff_url;
    int msgs_left;
    CURLMsg *msg;
    CURLcode res;
    CURL *easy;

    while ((msg = curl_multi_info_read(mcurl_info->mcurl_handle, &msgs_left)))
    {
        LOGT("http2: %s() msg = { %d %p { %d } }, queue = %d",
                __func__, msg->msg, msg->easy_handle, msg->data.result, msgs_left);
        if (msg->msg != CURLMSG_DONE) continue;

        easy = msg->easy_handle;
        res = msg->data.result;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, (char **)&conn);
        curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
        LOGT("http2: DONE: %s => (%d) %s", eff_url, res, conn->error);
        curl_multi_remove_handle(mcurl_info->mcurl_handle, easy);
        curl_easy_cleanup(easy);
        conn->easy = NULL;
        if (res == CURLE_OK)
        {
            /* process response received from gatekeeper service */
            gk_process_response(conn);
        }
        else
        {
            gk_process_fail_response(conn);
        }
        gk_free_conn(conn, mcurl_info);
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
    struct gk_curl_multi_info *mcurl_info = (struct gk_curl_multi_info *) w->data;
    CURLMcode rc;
    int action;

    LOGT("http2: %s  w %p revents %i", __func__, w, revents);
    action = (revents & EV_READ ? CURL_POLL_IN : 0) | (revents & EV_WRITE ?
                                                       CURL_POLL_OUT : 0);
    rc = curl_multi_socket_action(mcurl_info->mcurl_handle, w->fd, action,
                                  &mcurl_info->still_running);
    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(mcurl_info);
    if (mcurl_info->still_running <= 0)
    {
        LOGN("last transfer done, kill timeout");
        ev_timer_stop(mcurl_info->loop, &mcurl_info->timer_event);
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
    struct gk_curl_multi_info *mcurl_info = w->data;
    CURLMcode rc;

    rc = curl_multi_socket_action(mcurl_info->mcurl_handle, CURL_SOCKET_TIMEOUT, 0,
                                  &mcurl_info->still_running);
    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(mcurl_info);
}


/**
 * @brief cleans up sock_info structure.
 * @param f sock_info structure to free
 * @param mcurl_info gk_curl_multi_info structure used to stop ev_io events
 */
static void
remsock(struct gk_sock_info *f, struct gk_curl_multi_info *mcurl_info)
{
    if (f == NULL) return;

    if (f->evset) ev_io_stop(mcurl_info->loop, &f->ev);

    FREE(f);
}


/**
 * @brief Assign information to a sock_info structure
 * @param f pointer to sock_info structure to set
 * @param e curl easy handle
 * @param s socket descriptor
 * @param act ev action type
 * @param mcurl_info gk_curl_multi_info structure used to stop ev_io events
 */
static void
setsock(struct gk_sock_info *f, curl_socket_t s, CURL *e, int act,
        struct gk_curl_multi_info *mcurl_info)
{
    int kind = (act & CURL_POLL_IN ? EV_READ : 0) |
               (act & CURL_POLL_OUT ? EV_WRITE : 0);

    f->sockfd = s;
    f->action = act;
    f->easy = e;
    if (f->evset) ev_io_stop(mcurl_info->loop, &f->ev);

    ev_io_init(&f->ev, event_cb, f->sockfd, kind);
    f->ev.data = mcurl_info;
    f->evset = 1;
    ev_io_start(mcurl_info->loop, &f->ev);
}

/**
 * @brief Initialize a new sock_info structure
 * @param easy curl easy handle
 * @param s socket descriptor
 * @param mcurl_info gk_curl_multi_info structure used to stop ev_io events
 */
static void
addsock(curl_socket_t s, CURL *easy, int action,
        struct gk_curl_multi_info *mcurl_info)
{
    struct gk_sock_info *fdp;

    LOGT("http2: %s %p, %d, %d, %p", __func__, easy, s, action, mcurl_info);

    fdp = CALLOC(1, sizeof(struct gk_sock_info));

    fdp->global = mcurl_info;
    setsock(fdp, s, easy, action, mcurl_info);
    curl_multi_assign(mcurl_info->mcurl_handle, s, fdp);
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
    struct gk_curl_multi_info *mcurl_info = userp;
    struct gk_sock_info *fdp = sockp;
    const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

    LOGT("http2: %s() socket callback: s=%d e=%p what=%s ",
         __func__, s, e, whatstr[what]);
    if (what == CURL_POLL_REMOVE) remsock(fdp, mcurl_info);
    else if (!fdp) addsock(s, e, what, mcurl_info);
    else setsock(fdp, s, e, what, mcurl_info);

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
               struct gk_curl_multi_info *mcurl_info)
{
    ev_timer_stop(mcurl_info->loop, &mcurl_info->timer_event);
    if (timeout_ms >= 0)
    {
        double  t = timeout_ms / 1000;
        ev_timer_init(&mcurl_info->timer_event, timer_cb, t, 0.);
        ev_timer_start(mcurl_info->loop, &mcurl_info->timer_event);
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
    struct gk_mcurl_buffer *mcurl_data = NULL;
    struct gk_conn_info *conn = data;
    size_t realsize = size * nmemb;

    if (conn == NULL) goto out;

    mcurl_data = &conn->data;
    if (mcurl_data->buf == NULL) goto out;

    mcurl_data->buf = REALLOC(mcurl_data->buf, mcurl_data->size + realsize + 1);

    memcpy(&mcurl_data->buf[mcurl_data->size], ptr, realsize);
    mcurl_data->size += realsize;
    mcurl_data->buf[mcurl_data->size] = 0;

    LOGT("http2: %s(): %p %zu %zu %p", __func__, ptr, size, nmemb, data);

out:
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
gk_send_mcurl_request(struct fsm_gk_session *fsm_gk_session, struct gk_mcurl_data *mcurl_data)
{
    struct gk_curl_multi_info *mcurl_info;
    struct gk_server_info *server_info;
    struct fsm_gk_verdict *gk_verdict;
    struct gk_conn_info *conn;
    char errbuf[CURL_ERROR_SIZE];
    CURLMcode rc;

    server_info = &fsm_gk_session->gk_server_info;
    mcurl_info = &fsm_gk_session->mcurl;
    gk_verdict = mcurl_data->gk_verdict;

    conn = CALLOC(1, sizeof(struct gk_conn_info));
    if (conn == NULL) return false;

    conn->data.buf = MALLOC(1);

    conn->error[0]='\0';
    conn->easy = curl_easy_init();
    if (!conn->easy) goto err_free_conn;

    conn->global = mcurl_info;
    conn->url = server_info->server_url;
    conn->context = fsm_gk_session;

    conn->req_key.req_id = mcurl_data->req_id;
    conn->req_key.req_type = mcurl_data->req_type;

    LOGT("http2: sending request to %s using handler %p, certs path: %s, ssl cert %s, ssl key %s",
         conn->url,
         mcurl_info->mcurl_handle,
         server_info->ca_path,
         server_info->ssl_cert,
         server_info->ssl_key);

    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDS, gk_verdict->gk_pb->buf);
    curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDSIZE, (long)gk_verdict->gk_pb->len);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
    curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
    /* max. allowed time (2 secs) to establish the connection */
    curl_easy_setopt(conn->easy, CURLOPT_CONNECTTIMEOUT, 2L);
    /* max allowed time (2 secs) to get the data after connection is established */
    curl_easy_setopt(conn->easy, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(conn->easy, CURLOPT_CAINFO, server_info->ca_path);
    curl_easy_setopt(conn->easy, CURLOPT_SSLCERT, server_info->ssl_cert);
    curl_easy_setopt(conn->easy, CURLOPT_SSLKEY, server_info->ssl_key);

    LOGT("http2: %s() adding easy %p to multi %p (%s)",
         __func__, conn->easy, mcurl_info->mcurl_handle, conn->url);

    /* add curl easy handle */
    rc = curl_multi_add_handle(mcurl_info->mcurl_handle, conn->easy);
    mcode_or_die("gk_send_mcurl_request: curl_multi_add_handle", rc);
    if (rc != CURLM_OK)
    {
        LOGN("http2: curl_easy_perform failed: ret code: %d (%s)",
             rc,
             errbuf);
        return false;
    }
    return true;

err_free_conn:
    gk_free_conn(conn, mcurl_info);

    return false;
}


/**
 * @brief initialize curl library
 * @param loop pointer to ev_loop structure
 * @return true if the initialization succeeded,
 *         false otherwise
 */
bool
gk_multi_curl_init(struct fsm_gk_session *fsm_gk_session, struct ev_loop *loop)
{
    struct gk_curl_multi_info *mcurl_info;
    CURLcode  rc;
    CURLMcode cmret;

    mcurl_info = &fsm_gk_session->mcurl;

    LOGN("http2: initializing multi curl");

    /* initialize curl library */
    rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) return false;

    mcurl_info->loop = loop;

    /* initialize multi curl handle*/
    mcurl_info->mcurl_handle = curl_multi_init();

    /* initialize event timer callback */
    ev_timer_init(&mcurl_info->timer_event, timer_cb, 0.0, 0.0);
    mcurl_info->timer_event.data = mcurl_info;

    /* initialize socket callback */
    cmret = curl_multi_setopt(mcurl_info->mcurl_handle, CURLMOPT_SOCKETFUNCTION, curl_sock_cb);
    if (cmret != CURLM_OK)
    {
        LOGN("http2: failed to initialize socket callback function");
        goto err;
    }
    cmret = curl_multi_setopt(mcurl_info->mcurl_handle, CURLMOPT_SOCKETDATA, mcurl_info);
    if (cmret != CURLM_OK)
    {
        LOGN("http2: failed to initialize socket callback data");
        goto err;
    }

    /* initialize timer callback function */
    cmret = curl_multi_setopt(mcurl_info->mcurl_handle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    if (cmret != CURLM_OK)
    {
        LOGN("http2: failed to initialize timer callback function");
        goto err;
    }
    cmret = curl_multi_setopt(mcurl_info->mcurl_handle, CURLMOPT_TIMERDATA, mcurl_info);
    if (cmret != CURLM_OK)
    {
        LOGN("http2: failed to initialize timer callback data");
        goto err;
    }

    LOGN("http2: curl initialization successful");

    return true;

err:
    gk_curl_multi_cleanup(fsm_gk_session);
    return false;
}
