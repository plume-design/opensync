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
#include <syslog.h>
#include <getopt.h>

#include "ds_tree.h"
#include "log.h"
#include "os.h"
#include "os_socket.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"
#include "qm.h"
#include "osbus.h"
#include "qm_conn_osbus.h"

#define MODULE_ID LOG_MODULE_ID_OSBUS

static osbus_msg_policy_t qm_osbus_policy_ping[] = {};
static osbus_msg_policy_t qm_osbus_policy_status[] = {};


static bool qm_osbus_method_ping(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    LOGD(__func__);
    *reply = osbus_msg_new_object();
    osbus_msg_set_prop_int64(*reply, "pong", 0);
    return true;
}

static bool qm_osbus_method_status(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    LOGD(__func__);
    qm_request_t req = { .cmd = QM_CMD_STATUS };
    qm_response_t res = {0};
    qm_res_init(&res, &req);
    qm_res_status(&res);
    qm_conn_osbus_convert_response_stats_to_data(&res, reply);
    return true;
}

static bool qm_osbus_handle_send_req_msg(
        osbus_handle_t handle,
        osbus_msg_t *msg,
        qm_response_t *res)
{
    bool ret_val = false;
    qm_item_t *qi = NULL;
    qm_request_t *req = NULL;

    LOGT("%s:%d", __func__, __LINE__);
    MEMZERO(*res);

    qi = CALLOC(sizeof(*qi), 1);
    req = &qi->req;
    req->cmd = QM_CMD_SEND;
    qm_res_init(res, req);

    if (!qm_conn_osbus_convert_data_to_req(msg, req, &qi->topic, &qi->buf)) {
        LOGE("%s:%d convert_to_req", __func__, __LINE__);
        // TODO map to RBUS_ERROR_INVALID_INPUT
        // / UBUS_STATUS_INVALID_ARGUMENT
        goto error;
    }

    qi->size = req->data_size;

    LOGD("%s:%d size=%d buf=%p topic='%s'", __func__, __LINE__, (int)qi->size, qi->buf, qi->topic);
    qm_enqueue_or_send(qi, res);
    qi = NULL; // above takes over the qi
    qm_res_status(res);
    ret_val = true;
    goto out;

error:
    res->response = QM_RESPONSE_ERROR;
    res->error = QM_ERROR_GENERAL;
    if (qi) qm_queue_item_free(qi);

out:
    LOGT("%s:%d rc=%d resp=%d err=%d", __func__, __LINE__, ret_val, res->response, res->error);

    return ret_val;
}

static bool qm_osbus_method_send(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    LOGT("%s(%s)", __func__, method_name);
    qm_response_t res = {0};
    bool ret_val = qm_osbus_handle_send_req_msg(handle, msg, &res);
    //if (!(req->flags & QM_REQ_FLAG_NO_RESPONSE)) {
    // bus method invoke always sends a reply back
    // so ignore the NO_RESPONSE flag here
    qm_conn_osbus_convert_response_short_to_data(&res, reply);
    return ret_val;
}


static bool qm_osbus_topic_handler(
        osbus_handle_t handle,
        char *topic_path,
        osbus_msg_t *msg,
        void *user_data)
{
    LOGT("%s(%s)", __func__, topic_path);
    qm_response_t res = {0};
    bool ret_val = qm_osbus_handle_send_req_msg(handle, msg, &res);
    return ret_val;
}

static osbus_method_t qm_osbus_methods[] = {
    OSBUS_METHOD_ENTRY(qm_osbus, ping),
    OSBUS_METHOD_ENTRY(qm_osbus, send),
    OSBUS_METHOD_ENTRY(qm_osbus, status),
};

static bool qm_osbus_register_object(void)
{
    int i;
    char topic[OSBUS_NAME_SIZE];
    if (!osbus_method_register(OSBUS_DEFAULT, qm_osbus_methods, ARRAY_LEN(qm_osbus_methods))) {
        LOGE("Failed to register osbus methods");
        return false;
    }
    // TODO: use wildcard here
    // at the moment rbus wildcard listen seems to be broken in the current rbus version
    for (i = 0; i < qm_conn_osbus_known_topics_n; i++) {
        snprintf(topic, sizeof(topic), "topic.%s", qm_conn_osbus_known_topics[i]);
        osbus_topic_listen(OSBUS_DEFAULT, osbus_path_os(NULL, topic), qm_osbus_topic_handler, NULL);
    }
    return true;
}

bool qm_osbus_init(void)
{
    if (!osbus_init()) return false;
    if (!qm_osbus_register_object()) return false;
    return true;
}


