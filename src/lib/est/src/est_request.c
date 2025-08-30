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

#include <ev.h>

#include "memutil.h"
#include "os_util.h"
#include "osa_assert.h"

#include "est_request.h"

struct est_request_ctx
{
    arena_t *er_arena;                   /* Arena associated with the request */
    struct ev_loop *er_loop;             /* The event loop to use */
    CURLM *er_cmulti;                    /* CURL multi object */
    char *er_data;                       /* Current data */
    size_t er_datasz;                    /* Current size of data */
    est_request_fn_t *er_fn;             /* Callback */
    void *er_fn_ctx;                     /* Callback context */
    struct est_request_status er_status; /* Status */
};

/* cURL multi socket callback */
static int est_request_cmulti_socket_fn(CURL *easy, curl_socket_t s, int what, void *clientp, void *socketp);
static int est_request_cmulti_timer_fn(CURLM *multi, long timeout_msg, void *data);
static size_t est_request_curl_write_fn(char *data, size_t sz, size_t nmemb, void *ctx);
static size_t est_request_curl_header_fn(char *data, size_t ssz, size_t nmemb, void *ctx);

static void est_request_ev_io_fn(struct ev_loop *loop, ev_io *ev, int revent);
static void est_request_ev_timer_fn(struct ev_loop *loop, ev_timer *ev, int revent);
static void est_request_process_msg(struct est_request_ctx *er);
static void defer_CURLM_fn(void *cmulti);
static bool defer_curl_remove_handle(arena_t *arena, CURLM *c_multi, CURL *c_easy);
static void defer_ev_timer_fn(void *data);
static bool defer_ev_timer(arena_t *a, struct ev_loop *loop, ev_timer *evt);

/*
 * Dispatch an async cURL request on the libev loop `loop`. When the request
 * is completed, `fn` is called with the request data and error code. The
 * request data is allocated in memory using arena 'arena'.
 *
 * It seems the only way to do async in cURL is to allocate a CURLM (cuRL multi)
 * object and wrap a CURLE (cURL easy) object inside it. But in order to create
 * a functioning CURLM object, we need the following:
 *
 *  - a socket function (manage I/O on osckets)
 *  - I/O watchers dispatched by the socket function
 *  - message processing function (manage completion messages of CURLE objects)
 *  - a CURLM timer function, that schedules a timer callback
 *
 * The request can be cancelled at any time simply by destroying the arena
 * region. Note that in in this case, `fn` won't be called.
 *
 * On error, this function returns false.
 */
bool est_request_curl_async(arena_t *arena, struct ev_loop *loop, CURL *c_req, est_request_fn_t *fn, void *fn_ctx)
{
    struct est_request_ctx *er;

    arena_frame_auto_t af = arena_save(arena);

    /* Allocate the context */
    er = arena_malloc(arena, sizeof(*er));
    if (er == NULL)
    {
        LOG(ERR, "est_request: Error creating context.");
        return false;
    }
    er->er_arena = arena;
    er->er_loop = loop;
    er->er_data = NULL;
    er->er_datasz = 0;
    er->er_fn = fn;
    er->er_fn_ctx = fn_ctx;
    memset(&er->er_status, 0, sizeof(er->er_status));
    er->er_status.status = ER_STATUS_ERROR;

    /*
     * cURL multi initialization: cURL multi is is the only way to have a custom
     * event loop with cURL.
     */
    er->er_cmulti = curl_multi_init();
    if (er->er_cmulti == NULL || !arena_defer(arena, defer_CURLM_fn, er->er_cmulti))
    {
        LOG(ERR, "est: Error initializing cURL multi-handle.");
        return false;
    }

    if (curl_multi_setopt(er->er_cmulti, CURLMOPT_SOCKETFUNCTION, est_request_cmulti_socket_fn) != CURLM_OK)
    {
        LOG(ERR, "est: Error setting CURLMOP_SOCKETFUNCTION.");
        return false;
    }
    if (curl_multi_setopt(er->er_cmulti, CURLMOPT_SOCKETDATA, er) != CURLM_OK)
    {
        LOG(ERR, "est: Error setting CURLMOP_SOCKETDATA.");
        return false;
    }

    ev_timer *evt = arena_malloc(arena, sizeof(*evt));
    if (evt == NULL || !defer_ev_timer(arena, loop, evt))
    {
        LOG(ERR, "est: Error allocating timer");
        return false;
    }
    memset(evt, 0, sizeof(*evt));
    evt->data = er;

    if (curl_multi_setopt(er->er_cmulti, CURLMOPT_TIMERFUNCTION, est_request_cmulti_timer_fn) != CURLM_OK)
    {
        LOG(ERR, "est: Error setting timer function.");
        return false;
    }
    if (curl_multi_setopt(er->er_cmulti, CURLMOPT_TIMERDATA, evt) != CURLM_OK)
    {
        LOG(ERR, "est: Error setting timer data.");
        return false;
    }

    /*
     * Plant custom header function -- currently this is required only for
     * catching the "retry-after" header
     */
    if (curl_easy_setopt(c_req, CURLOPT_HEADERFUNCTION, est_request_curl_header_fn) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting CURL header function.");
        return false;
    }
    if (curl_easy_setopt(c_req, CURLOPT_HEADERDATA, er) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL header data.");
        return false;
    }

    /*
     * Patch the cURL easy handle with our own writer functions
     */
    if (curl_easy_setopt(c_req, CURLOPT_WRITEFUNCTION, est_request_curl_write_fn) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL write function.");
        return false;
    }
    if (curl_easy_setopt(c_req, CURLOPT_WRITEDATA, er) != CURLE_OK)
    {
        LOG(ERR, "est: Error setting cURL write data.");
        return false;
    }

    /*
     * Add the easy handle to a multi handle
     */
    if (curl_multi_add_handle(er->er_cmulti, c_req) != CURLM_OK)
    {
        LOG(ERR, "est: Error adding cURL handle.");
        return false;
    }

    if (!defer_curl_remove_handle(arena, er->er_cmulti, c_req))
    {
        LOG(ERR, "est: Error deferring cURL remove handle.");
        return false;
    }

    af = arena_save(arena);

    return true;
}

/*
 * =============================================================================
 * cURL callbacks
 * ============================================================================
 */

/*
 * CURLM socket function -- used to manage I/O watchers on sockets
 */
int est_request_cmulti_socket_fn(CURL *easy, curl_socket_t s, int what, void *data, void *sock_data)
{
    struct est_request_ctx *er = data;
    ev_io *evio = sock_data;

    ASSERT(data != NULL, "est_request_cmulti_socket_fn() data is NULL");

    if (what == CURL_POLL_REMOVE)
    {
        if (evio != NULL)
        {
            ev_io_stop(er->er_loop, evio);
            FREE(evio);
        }
        return 0;
    }

    if (evio == NULL)
    {
        evio = CALLOC(1, sizeof(*evio));
        evio->data = er;

        /* Assign the evio object to the cURL multi object */
#if 0
        /*
         * cURL seems to bail out if this function fails. Even worse, it reports
         * error code == CURLE_OK, which means success.
         */
        if (FT(curl_multi_assign(er->er_cmulti, s, evio), CURLM_LAST) != CURLM_OK)
        {
            LOG(ERR, "est_request: Error assigning value to cURL multi.");
            FREE(evio);
            return -1;
        }
#else
        curl_multi_assign(er->er_cmulti, s, evio);
#endif
    }

    ev_io_stop(er->er_loop, evio);

    int revent = 0;
    switch (what)
    {
        case CURL_POLL_IN:
            revent = EV_READ;
            break;

        case CURL_POLL_OUT:
            revent = EV_WRITE;
            break;

        case CURL_POLL_INOUT:
            revent = EV_READ | EV_WRITE;
            break;

        default:
            return 0;
    }
    ev_io_init(evio, est_request_ev_io_fn, s, revent);
    ev_io_start(er->er_loop, evio);

    return 0;
}

/* CURLM I/O callback */
void est_request_ev_io_fn(struct ev_loop *loop, ev_io *w, int revents)
{
    struct est_request_ctx *er = w->data;
    ASSERT(er != NULL, "est_request_ev_io_fn passed NULL context");

    int evb = 0;
    if (revents & EV_READ)
    {
        evb |= CURL_CSELECT_IN;
    }

    if (revents & EV_WRITE)
    {
        evb |= CURL_CSELECT_OUT;
    }

    int running;
    /* Process the socket */
    if (curl_multi_socket_action(er->er_cmulti, w->fd, evb, &running) != CURLM_OK)
    {
        /*
         * It seems curl_multi_socket_action() doesn't generate any messages
         * for curl_multi_info_read() on error. So we need to call the handler
         * here
         */
        LOG(ERR, "est_request: Error processing cURL multi socket (I/O).");
        running = 0;
    }

    est_request_process_msg(er);
    if (running == 0)
    {
        /* Forward the error code */
        er->er_fn(er->er_status, (er->er_status.status == ER_STATUS_OK) ? er->er_data : NULL, er->er_fn_ctx);
    }
}

/* CURLM timer function */
int est_request_cmulti_timer_fn(CURLM *multi, long timeout, void *data)
{
    ev_timer *evt = data;
    struct est_request_ctx *er = evt->data;

    ev_timer_stop(er->er_loop, evt);

    if (timeout < 0) return 0;

    ev_timer_init(evt, est_request_ev_timer_fn, (double)timeout / 1000.0, 0.0);
    ev_timer_start(er->er_loop, evt);

    return 0;
}

void est_request_ev_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    struct est_request_ctx *er = w->data;

    int running;
    if (curl_multi_socket_action(er->er_cmulti, CURL_SOCKET_TIMEOUT, 0, &running) != CURLM_OK)
    {
        LOG(ERR, "est_request: Error processing cURL multi socket (timeout).");
        running = 0;
    }

    est_request_process_msg(er);
    if (running == 0)
    {
        er->er_fn(er->er_status, (er->er_status.status == ER_STATUS_OK ? er->er_data : NULL), er->er_fn_ctx);
    }
}

/*
 * CURLM message processing function
 */
void est_request_process_msg(struct est_request_ctx *er)
{
    CURLMsg *msg;

    while ((msg = curl_multi_info_read(er->er_cmulti, &(int){0})) != NULL)
    {
        if (msg->msg != CURLMSG_DONE)
        {
            LOG(DEBUG, "est_request: Received message that is not CURLMSG_DONE.");
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        if (msg->data.result != CURLE_OK)
        {
            LOG(DEBUG, "est_request: Easy socket failed with: %s", curl_easy_strerror(msg->data.result));
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        long http_code;
        if (curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK)
        {
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        if (er->er_data == NULL)
        {
            LOG(DEBUG, "est_request: No data received.");
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        /* Pad the data buffer with a \0 */
        er->er_data = arena_memcat(er->er_arena, er->er_data, er->er_datasz, "\0", sizeof(char));
        if (er->er_data == NULL)
        {
            LOG(DEBUG, "est_request: Error appending null-terminator to buffer.");
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        /* Treat all HTTP codes as errors, except for 200. */
        if (http_code != 200)
        {
            LOG(DEBUG, "est: Request returned http code: %ld", http_code);
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        if (er->er_data == NULL)
        {
            LOG(DEBUG, "est: Request data was empty.");
            er->er_status.status = ER_STATUS_ERROR;
            continue;
        }

        er->er_status.status = ER_STATUS_OK;
    }
}

/*
 * CURLE header callback -- parse the "retry-after" header.
 *
 * Note: data is not guaranteed to be null terminated and it might be padded
 * with \r\n
 *
 * Note:: This function must return sz*nmemb
 */
size_t est_request_curl_header_fn(char *data, size_t sz, size_t nmemb, void *ctx)
{
    struct est_request_ctx *er = ctx;

    ARENA_SCRATCH(scratch);

    /* Create a copy of the header, pad it with \0 */
    char *buf = arena_malloc(scratch, sz * nmemb + 1);
    if (buf == NULL)
    {
        LOG(ERR, "est: Error allocating data for parsing HTTP header.");
        goto out;
    }
    memcpy(buf, data, sz * nmemb);
    buf[sz * nmemb] = '\0';

    char *hdr = strsep(&buf, ":");
    char *val = strsep(&buf, "\r\n");
    if (hdr == NULL || val == NULL) goto out;

    LOG(DEBUG, "est: Received HTTP header: %s: %s", hdr, val);

    if (strcasecmp(hdr, "Retry-After") != 0) goto out;
    LOG(INFO, "est: Received '%s' header: %s", hdr, val);

    time_t retry = -1;
    long lval = -1;
    /*
     * Try to parse the date using curl_getdate() in case it is in in an
     * absolute RFC1123 format and convert it to a relative time_t. If that
     * fails, check if it is a simple integer. In this case the timestamp is
     * already relative and there's no need for conversion.
     */
    if ((retry = curl_getdate(val, NULL)) >= 0)
    {
        LOG(DEBUG, "est: Date conversion: curl_getdate() success: %s -> %jd", val, (intmax_t)retry);
        /* curl_getdate() returns an absolute date, return a relative one */
        retry = retry - time(NULL);
    }
    else if (os_strtoul(val, &lval, 0))
    {
        LOG(DEBUG, "est: Date conversion: os_strtoul() success: %s -> %ld", val, lval);
        retry = lval;
    }
    else
    {
        LOG(ERR, "est: Error converting Retry-After value to integer: %s", val);
        goto out;
    }

    if (retry < 0)
    {
        LOG(ERR, "est: Invalid Retry-After value after conversion. Ignoring.");
        goto out;
    }

    er->er_status.ER_STATUS_ERROR.retry_after = retry;

out:
    return sz * nmemb;
}

/*
 * CURLM data callback -- called by CURLM every time there's new data available.
 * This function simply extends the arena and copies the new data to memory.
 */
size_t est_request_curl_write_fn(char *data, size_t sz, size_t nmemb, void *ctx)
{
    struct est_request_ctx *er = ctx;

    if ((SIZE_MAX / sz) <= nmemb)
    {
        LOG(ERR, "est_request: Multiply overflow during est_request_curl_write_fn().");
        return 0;
    }
    size_t datasz = sz * nmemb;

    ASSERT(er != NULL, "No context passed to est_request_curl_write_fn()");

    if (er->er_data == NULL)
    {
        er->er_data = arena_push(er->er_arena, 0);
        if (er->er_data == NULL)
        {
            LOG(ERR, "est_request: Error allocating data in write callback.");
            return 0;
        }
    }

    if (er->er_datasz > (SIZE_MAX - datasz))
    {
        LOG(ERR, "est_request: Data size overflow in write callback.");
        return 0;
    }

    er->er_data = arena_memcat(er->er_arena, er->er_data, er->er_datasz, data, datasz);
    if (er->er_data == NULL)
    {
        LOG(ERR, "est_request: Error appending data to write buffer.");
        return 0;
    }

    er->er_datasz += datasz;
    return datasz;
}

/*
 * =============================================================================
 * Arena support functions
 * =============================================================================
 */
void defer_CURLM_fn(void *cmulti)
{
    LOG(DEBUG, "est_request: Delete CURLM handle.");
    curl_multi_cleanup(cmulti);
}

struct remove_handle_ctx
{
    CURLM *c_multi;
    CURL *c_easy;
};

void arena_defer_curl_remove_handle_fn(void *data)
{
    struct remove_handle_ctx *r = data;

    LOG(DEBUG, "est_request: cURL multi remove easy handle.");

    if (curl_multi_remove_handle(r->c_multi, r->c_easy) != CURLM_OK)
    {
        LOG(ERR, "est_request: Error removing cURL handle.");
    }
}

bool defer_curl_remove_handle(arena_t *arena, CURLM *c_multi, CURL *c_easy)
{
    struct remove_handle_ctx r = {.c_multi = c_multi, .c_easy = c_easy};
    return arena_defer_copy(arena, arena_defer_curl_remove_handle_fn, &r, sizeof(r));
}

struct arena_defer_ev_timer_stop_ctx
{
    struct ev_loop *evt_loop;
    ev_timer *evt_timer;
};

void defer_ev_timer_fn(void *data)
{
    struct arena_defer_ev_timer_stop_ctx *ctx = data;
    ev_timer_stop(ctx->evt_loop, ctx->evt_timer);
}

bool defer_ev_timer(arena_t *a, struct ev_loop *loop, ev_timer *evt)
{
    struct arena_defer_ev_timer_stop_ctx ctx = {.evt_loop = loop, .evt_timer = evt};
    return arena_defer_copy(a, defer_ev_timer_fn, &ctx, sizeof(ctx));
}
