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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include <ev.h>

#include "ds_tree.h"
#include "log.h"
#include "os.h"
#include "os_socket.h"
#include "ovsdb.h"
#include "json_util.h"
#include "memutil.h"
#include "os_ev_trace.h"

#include "ovsdb_stream.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define OVSDB_SLEEP_TIME             1
#define OVSDB_WAIT_TIME              30   /* in s (0 = infinity) */

/*****************************************************************************/

/*global to avoid any potential issues with stack */
struct ev_io wovsdb;
const char *ovsdb_comment = NULL;

int json_rpc_fd = -1;

//it's should be embedded in monitor transact
static int json_update_monitor_id = 0;

/* JSON-RPC handler list */
ds_tree_t json_rpc_handler_list = DS_TREE_INIT(ds_int_cmp, struct rpc_response_handler, rrh_node);

/* JSON-RPC update handler list */
ds_tree_t json_rpc_update_handler_list = DS_TREE_INIT(ds_int_cmp, struct rpc_update_handler, rrh_node);

/* Deferred readiness reporting */
struct ovsdb_ready {
    struct ds_dlist_node node;
    ovsdb_ready_fn_t *fn;
    void *fn_priv;
};

static struct ds_dlist g_ovsdb_ready = DS_DLIST_INIT(struct ovsdb_ready, node);

/******************************************************************************
 *  PROTECTED declarations
 *****************************************************************************/

static bool ovsdb_process_recv(json_t *js);
static bool ovsdb_process_event(json_t *js);
static bool ovsdb_process_result(json_t *id, json_t *js);
static bool ovsdb_process_error(json_t *id, json_t *js);
static bool ovsdb_process_update(json_t *jsup);
static bool ovsdb_rpc_callback(int id, bool is_error, json_t *jsmsg);

static void cb_ovsdb_read(struct ev_loop *loop, struct ev_io *watcher, int revents);

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static void cb_ovsdb_read(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    struct ovsdb_stream *st = watcher->data;
    if (EV_ERROR & revents)
    {
        LOG(ERR,"cb_ovsdb_read: got invalid event");
        return;
    }

    int err = ovsdb_stream_run(st, watcher->fd, ovsdb_process_recv);
    if (err)
    {
        /* During Opensync restart OVSDB may end up getting
         * stopped before Opensync itself. In such case the
         * socket will be abruptly closed. To avoid
         * restart-within-restart wait a few seconds to give
         * Opensync itself a chance to stop its underlying
         * processes.
         */
        sleep(10);

        /* Re-connecting is pointless because any and all
         * monitor state is lost. All update notification
         * handlers would need to be able to recover from
         * this explicitly too..
         *
         * This isn't expected during normal operation so
         * just restart everything.
         */
        LOGEM("Connection to OVSDB is lost, restarting OpenSync");
        target_managers_restart();
    }
}

/**
 * Dispatch message JSON-RPC
 */
bool ovsdb_process_recv(json_t *jsrpc)
{
    json_t *jsid;
    json_t *jst;

   /*
     * 3 options remaining for this message:
     *      - synchronous method call (not supported);
     *      - synchronous method result
     *      - synchronous method error
     */

    /* 1) Check if it's a method */
    jst = json_object_get(jsrpc, "method");
    if (jst != NULL && !json_is_null(jst))
    {
        const char * method;
        method = json_string_value(jst);
        if (!strcmp(method, "update")) {
            return ovsdb_process_update(jsrpc);
        } else  {
            LOG(ERR, "Received unsupported SYNCHRONOUS method request.::method=%s", json_string_value(jst));
            return false;
        }
    }

    /* Check if we have an id */
    jsid = json_object_get(jsrpc, "id");
    if (jsid == NULL || json_is_null(jsid))
    {
        /* We dont have an ID, at this point this must be an EVENT */
        return ovsdb_process_event(jsrpc);
    }

    /* 2) Check if we have a result response */
    jst = json_object_get(jsrpc, "result");
    if (jst != NULL && !json_is_null(jst))
    {
        /* Result message */
        return ovsdb_process_result(jsid, jst);
    }

    /* 3) CHeck if we have an error response */
    jst = json_object_get(jsrpc, "error");
    if (jst != NULL && !json_is_null(jst))
    {
       /* Error message */
        return ovsdb_process_error(jsid, jst);
    }

    /* Nothing looks familiar, lets drop it */
    LOG(ERR, "Received unsupported JSON-RPC message, discarding.");
    return false;
}

/**
 * Process signle JSON-RPC "event" message (asynchronous method call)
 */
bool ovsdb_process_event(json_t *js)
{
    (void)js;
    return false;
}

/**
 * Process single JSON-RPC "result" message
 */
bool ovsdb_process_result(json_t *jsid, json_t *jsresult)
{
    int id;

    /* The current implementation supports only integer IDs */
    if (!json_is_integer(jsid))
    {
        LOG(ERR, "Received non-integer id in JSON-RPC.");
        return false;
    }

    id = json_integer_value(jsid);

    /*
     * Lookup result handler
     */
    return ovsdb_rpc_callback(id, false, jsresult);
}

/**
 * Process single JSON-RPC "error" message
 */
bool ovsdb_process_error(json_t *jsid, json_t *jserror)
{
    int id;

    /* The current implementation supports only integer IDs */
    if (!json_is_integer(jsid))
    {
        LOG(ERR, "Received non-integer id in JSON-RPC.");
        return false;
    }

    id = json_integer_value(jsid);

    /*
     * Lookup result handler
     */
    return ovsdb_rpc_callback(id, true, jserror);
}

int ovsdb_register_update_cb(ovsdb_update_process_t *fn, void *data)
{
    struct rpc_update_handler *rh;

    rh = MALLOC(sizeof(struct rpc_update_handler));

    ++json_update_monitor_id;

    /* Not thread-safe */
    rh->rrh_id = json_update_monitor_id;
    rh->rrh_callback = fn;
    rh->data = data;

    ds_tree_insert(&json_rpc_update_handler_list, rh, &rh->rrh_id);

    return json_update_monitor_id;
}

int ovsdb_unregister_update_cb(int mon_id)
{
    struct rpc_update_handler *rh;

    rh = ds_tree_find(&json_rpc_update_handler_list, &mon_id);

    if (rh)
    {
        ds_tree_remove(&json_rpc_update_handler_list, rh);
        FREE(rh);
    }

    return 0;
}

bool ovsdb_change_update_cb(int mon_id, ovsdb_update_process_t *fn, void *data)
{
    struct rpc_update_handler *rh;

    rh = ds_tree_find(&json_rpc_update_handler_list, &mon_id);
    if (rh)
    {
        rh->rrh_callback = fn;
        rh->data = data;
        return true;
    }

    return false;
}

/**
 * Process single JSON-RPC "update" message
 */
static bool ovsdb_process_update(json_t *jsup)
{
    int mon_id = 0;
    struct rpc_update_handler *rh;

    mon_id = json_integer_value(json_array_get(json_object_get(jsup,"params"),0));

    rh = ds_tree_find(&json_rpc_update_handler_list, &mon_id);
    if (rh == NULL)
    {
        LOG(NOTICE, "JSON-RPC Update: Callback not found for monitor.::mon_id=%d\n", mon_id);
        return false;
    }

    rh->rrh_callback(mon_id, jsup, rh->data);

    return true;
}

bool ovsdb_rpc_callback(int id, bool is_error, json_t *jsmsg)
{
    struct rpc_response_handler *rh;

    rh = ds_tree_find(&json_rpc_handler_list, &id);
    if (rh == NULL)
    {
        LOG(NOTICE, "JSON-RPC: Callback not found for id.::mon_id=%d\n", id);
        return false;
    }

    /*
     * Filter out empty responses from jsmsg generated by "op":"comment" requests -- it would too much hassle
     * to handle the case where the comment might be present or not in every single response parser.
     */
    if (ovsdb_comment != NULL && json_is_array(jsmsg))
    {
        json_t *jscomm = json_array_get(jsmsg, 0);

        /* If js is an object and its empty, remove it */
        if (json_is_object(jscomm) && json_object_size(jscomm) == 0)
        {
            json_array_remove(jsmsg, 0);
        }
    }

    rh->rrh_callback(id, is_error, jsmsg, rh->data);

    /* Remove callback from the tree */
    ds_tree_remove(&json_rpc_handler_list, rh);
    FREE(rh);

    return true;
}


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

static void ovsdb_ready_notify(void)
{
    struct ovsdb_ready *r;
    while ((r = ds_dlist_remove_head(&g_ovsdb_ready)) != NULL) {
        r->fn(r->fn_priv);
        FREE(r);
    }
}

void ovsdb_when_ready(ovsdb_ready_fn_t *fn, void *fn_priv)
{
    if (json_rpc_fd == -1) {
        struct ovsdb_ready *r = CALLOC(1, sizeof(*r));
        r->fn = fn;
        r->fn_priv = fn_priv;
        ds_dlist_insert_tail(&g_ovsdb_ready, r);
    }
    else {
        fn(fn_priv);
    }
}

bool ovsdb_init(const char *name)
{
    return ovsdb_init_loop(NULL, name);
}

bool ovsdb_init_loop(struct ev_loop *loop, const char *name)
{
    bool success = false;

    if (json_rpc_fd != -1) {
        if (ovsdb_comment == NULL && name != NULL) {
            ovsdb_comment = name;
        }
        return true;
    }

    if (loop == NULL) {
        loop = ev_default_loop(0);
    }

    json_rpc_fd = ovsdb_conn();

    if (json_rpc_fd > 0)
    {
        LOG(NOTICE, "OVSDB connection established");
        OS_EV_TRACE_MAP(cb_ovsdb_read);
        ev_io_init(&wovsdb, cb_ovsdb_read, json_rpc_fd, EV_READ);
        ev_io_start(loop, &wovsdb);
        wovsdb.data = ovsdb_stream_alloc();

        success = true;
        ovsdb_ready_notify();
    }
    else
    {
        LOG(ERR, "Error starting OVSDB client.::reason=%d", json_rpc_fd);
    }

    if (name != NULL)
    {
        ovsdb_comment = name;
    }

    return success;
}

bool ovsdb_init_loop_with_priority(struct ev_loop *loop, const char *name, int priority)
{
    bool success = false;

    if (json_rpc_fd != -1) {
        if (ovsdb_comment == NULL && name != NULL) {
            ovsdb_comment = name;
        }
        return true;
    }

    if (loop == NULL) {
        loop = ev_default_loop(0);
    }

    json_rpc_fd = ovsdb_conn();

    if (json_rpc_fd > 0)
    {
        LOG(NOTICE, "OVSDB connection established");
        OS_EV_TRACE_MAP(cb_ovsdb_read);
        ev_io_init(&wovsdb, cb_ovsdb_read, json_rpc_fd, EV_READ);
        ev_set_priority(&wovsdb, priority);
        LOGI("%s: Set ovsdb event priority for %s to %d", __func__,
             name, ev_priority(&wovsdb));
        ev_io_start(loop, &wovsdb);
        wovsdb.data = ovsdb_stream_alloc();

        success = true;
        ovsdb_ready_notify();
    }
    else
    {
        LOG(ERR, "Error starting OVSDB client.::reason=%d", json_rpc_fd);
    }

    if (name != NULL)
    {
        ovsdb_comment = name;
    }

    return success;
}

bool ovsdb_ready(const char *name)
{
    /* Wait for the OVSDB to initialize */
    int wait = OVSDB_WAIT_TIME;
    while (wait >= 0)
    {
        if (ovsdb_init(name)) {
            return true;
        }
        LOG(INFO, "OVSDB not ready. Need to Zzzz ...");

sleep:
        if (OVSDB_WAIT_TIME) {
            wait -= OVSDB_SLEEP_TIME;
        }

        sleep (OVSDB_SLEEP_TIME);
    };

    return false;
}

bool ovsdb_stop(void)
{
    return ovsdb_stop_loop(NULL);
}

bool ovsdb_stop_loop(struct ev_loop *loop)
{
    if (loop == NULL) {
        loop = ev_default_loop(0);
    }

    ev_io_stop(loop, &wovsdb);
    ovsdb_comment = NULL;

    close(json_rpc_fd);

    json_rpc_fd = -1;

    LOG(NOTICE, "Closing OVSDB connection.");

    return true;
}
