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

// QM CONN OSBUS

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "log.h"
#include "os.h"
#include "os_time.h"
#include "util.h"
#include "memutil.h"
#include "osbus.h"
#include "qm_conn.h"
#include "qm_conn_osbus.h"
#include "const.h"

#define MODULE_ID LOG_MODULE_ID_OSBUS

extern double qm_conn_default_timeout;

const char *qm_conn_osbus_known_topics[] = {
    "other", "SM", "BM", "OW", "FSM", "FCM"
};
int qm_conn_osbus_known_topics_n = ARRAY_LEN(qm_conn_osbus_known_topics);

osbus_msg_policy_t qm_osbus_policy_send[QM_OSBUS_POLICY_SEND_NUM] = {
    { .name = "from",       .type = OSBUS_DATA_TYPE_STRING },
    { .name = "topic",      .type = OSBUS_DATA_TYPE_STRING },
    { .name = "type",       .type = OSBUS_DATA_TYPE_STRING },
    { .name = "compress",   .type = OSBUS_DATA_TYPE_STRING },
    { .name = "direct",     .type = OSBUS_DATA_TYPE_BOOL   },
    { .name = "data",       .type = OSBUS_DATA_TYPE_BINARY },
};

bool qm_conn_osbus_map_response_data(bool set, qm_response_t *res, osbus_msg_t *d, bool stats)
{
    osbus_msg_map_prop_int(set, d, "rc",       (int*)&res->response);
    osbus_msg_map_prop_int(set, d, "error",    (int*)&res->error);
    osbus_msg_map_prop_int(set, d, "conn",     (int*)&res->conn_status);
    if (stats) {
        osbus_msg_map_prop_int(set, d, "qlen",     (int*)&res->qlen);
        osbus_msg_map_prop_int(set, d, "qsize",    (int*)&res->qsize);
        osbus_msg_map_prop_int(set, d, "qdrop",    (int*)&res->qdrop);
        osbus_msg_map_prop_int(set, d, "nsent",    (int*)&res->num_sent);
        osbus_msg_map_prop_int64(set, d, "bytes",  (int64_t*)&res->num_bytes);
        osbus_msg_map_prop_int(set, d, "nerrors",  (int*)&res->num_errors);
        osbus_msg_map_prop_int(set, d, "runtime",  (int*)&res->runtime);
    }
    return true;
}

bool qm_conn_osbus_convert_response_short_to_data(qm_response_t *res, osbus_msg_t **data)
{
    if (!res || !data) return false;
    *data = osbus_msg_new_object();
    if (!*data) return false;
    return qm_conn_osbus_map_response_data(true, res, *data, false);
}

bool qm_conn_osbus_convert_response_stats_to_data(qm_response_t *res, osbus_msg_t **data)
{
    if (!res || !data) return false;
    *data = osbus_msg_new_object();
    if (!*data) return false;
    return qm_conn_osbus_map_response_data(true, res, *data, true);
}

bool qm_conn_osbus_convert_data_to_response(osbus_msg_t *data, qm_response_t *res)
{
    if (!res || !data) return false;
    return qm_conn_osbus_map_response_data(false, res, data, true);
}

enum qm_req_data_type qm_data_type_from_str(const char *s_type)
{
    if (s_type) {
        if (!strcmp(s_type, "raw")) {
            return QM_DATA_RAW;
        } else if (!strcmp(s_type, "stats")) {
            return QM_DATA_STATS;
        } else if (!strcmp(s_type, "text")) {
            return QM_DATA_TEXT;
        } else if (!strcmp(s_type, "log")) {
            return QM_DATA_LOG;
        }
    }
    return QM_DATA_RAW;
}

char* qm_req_compress_to_str(enum qm_req_compress compress)
{
    switch (compress) {
        case QM_REQ_COMPRESS_DISABLE:   return "disable";
        case QM_REQ_COMPRESS_FORCE:     return "force";
        default:                        return "default";
    }
}

enum qm_req_compress qm_req_compress_from_str(const char *s_compress)
{
    if (s_compress) {
        if (!strcmp(s_compress, "disable")) {
            return QM_REQ_COMPRESS_DISABLE;
        } else if (!strcmp(s_compress, "force")) {
            return QM_REQ_COMPRESS_FORCE;
        }
    }
    return QM_REQ_COMPRESS_IF_CFG;
}


bool qm_conn_map_req_data_type(bool set, const char **str, void *val)
{
    qm_request_t *req = val;
    if (set) {
        *str = qm_data_type_str(req->data_type);
        return (*str != NULL);
    } else {
        req->data_type = qm_data_type_from_str(*str);
        return true;
    }
}

bool qm_conn_map_req_compress(bool set, const char **str, void *val)
{
    qm_request_t *req = val;
    if (set) {
        *str = qm_req_compress_to_str(req->compress);
        return (*str != NULL);
    } else {
        req->compress = qm_req_compress_from_str(*str);
        return true;
    }
}

bool qm_conn_map_req_direct(bool set, bool *bool_val, void *fn_val)
{
    qm_request_t *req = fn_val;
    if (set) {
        *bool_val = req->flags & QM_REQ_FLAG_SEND_DIRECT ? true : false;
    } else {
        if (*bool_val) req->flags |= QM_REQ_FLAG_SEND_DIRECT;
    }
    return true;
}

void qm_conn_osbus_log_req(bool set, qm_request_t *req, char *topic, void *buf, int buf_size)
{
    LOGT("%s type=%d compress=%d flags=%d buf=%p size=%d topic=%s", set ? "set" : "get",
            req->data_type, req->compress, req->flags, buf, buf_size, topic);
}

bool qm_conn_osbus_map_req_to_data(bool set, qm_request_t *req, char **topic, void **buf, int *buf_size, osbus_msg_t *d)
{
    bool ret = true;
    char *empty = "";
    if (set) {
        if (!*topic) topic = &empty;
        qm_conn_osbus_log_req(set, req, *topic, *buf, *buf_size);
    } else {
        *topic = NULL;
        *buf = NULL;
        *buf_size = 0;
    }
    if (ret) ret = osbus_msg_map_prop_string_fixed(set, d, "from",     req->sender, sizeof(req->sender));
    if (ret) ret = osbus_msg_map_prop_string_alloc(set, d, "topic",    topic);
    if (ret) ret = osbus_msg_map_prop_string_fn   (set, d, "type",     req, qm_conn_map_req_data_type);
    if (ret) ret = osbus_msg_map_prop_string_fn   (set, d, "compress", req, qm_conn_map_req_compress);
    if (ret) ret = osbus_msg_map_prop_bool_fn     (set, d, "direct",   req, qm_conn_map_req_direct);
    if (ret) ret = osbus_msg_map_prop_binary_alloc(set, d, "data",     (uint8_t**)buf, buf_size);
    if (!set) {
        qm_conn_osbus_log_req(set, req, *topic, *buf, *buf_size);
    }
    return ret;
}

bool qm_conn_osbus_convert_req_to_data(qm_request_t *req, char *topic, void *buf, int buf_size, osbus_msg_t **data)
{
    *data = osbus_msg_new_object();
    if (!*data) return false;
    return qm_conn_osbus_map_req_to_data(true, req, &topic, &buf, &buf_size, *data);
}

bool qm_conn_osbus_convert_data_to_req(osbus_msg_t *data, qm_request_t *req, char **topic, void **buf)
{
    return qm_conn_osbus_map_req_to_data(false, req, topic, buf, (int*)&req->data_size, data);
}

const char *qm_conn_get_topic_name(const char *mgr_name)
{
    if (mgr_name && *mgr_name) {
        if (is_inarray(mgr_name, qm_conn_osbus_known_topics_n,
                       (char**)qm_conn_osbus_known_topics))
        {
            return mgr_name;
        }
    }
    return qm_conn_osbus_known_topics[0]; // default: "other"
}

bool qm_conn_osbus_send_req(qm_request_t *req, char *topic, void *data, int data_size, qm_response_t *res)
{
    LOGT("%s data:%p size:%d topic:%s", __func__, data, data_size, topic);
    bool result = false;
    char *cmd_str = NULL;
    osbus_msg_t *d = NULL;
    osbus_msg_t *reply = NULL;
    qm_response_t res1;
    int ll = LOG_SEVERITY_DEBUG;

    if (!res) res = &res1;
    MEMZERO(*res);

    if (!req || !qm_req_valid(req)) goto invalid_req;
    if (req->cmd == QM_CMD_STATUS) {
        cmd_str = "status";
    } else if (req->cmd == QM_CMD_SEND) {
        cmd_str = "send";
        req->data_size = data_size;
        if (!qm_conn_osbus_convert_req_to_data(req, topic, data, data_size, &d)) {
            goto out;
        }
    } else {
invalid_req:
        LOG(ERR, "%s: invalid req", __func__);
        goto out;
    }

    if (!osbus_default_handle()) {
        // auto init osbus if not already initialized
        if (!osbus_init()) goto out;
    }

    LOGT("%s invoke '%s'", __func__, cmd_str);

    if (req->cmd == QM_CMD_STATUS || (req->flags & QM_REQ_FLAG_ACK_RCPT)) {
        // use method invoke for status request
        // or if acknowledge receipt is explicitly requested
        if (!osbus_method_invoke(OSBUS_DEFAULT, osbus_path_os("QM", cmd_str), d, &reply)) {
            LOGE("osbus_method_invoke(%s)", cmd_str);
            res->error = QM_ERROR_CONNECT;
            goto out;
        }
        result = qm_conn_osbus_convert_data_to_response(reply, res);
    } else if (req->cmd == QM_CMD_SEND) {
        // use topic to send report (method also works)
        // benefit of using topic is multiple consumer can listen to it
        // drawback is a reply is not received on topic (usually not needed)
        char topic[OSBUS_NAME_SIZE];
        const char *sub = qm_conn_get_topic_name(log_get_name());
        snprintf(topic, sizeof(topic), "topic.%s", sub);
        if (!osbus_topic_send(OSBUS_DEFAULT, osbus_path_os("QM", topic), d)) {
            LOGE("osbus_topic_send(%s)", topic);
            res->error = QM_ERROR_CONNECT;
            goto out;
        }
        // note, using topic does not provide us with a reply
        res->response = QM_RESPONSE_IGNORED;
        res->error = QM_ERROR_NONE;
        result = true;
    }

out:
    osbus_msg_free(d);
    osbus_msg_free(reply);
    if (!result || res->response == QM_RESPONSE_ERROR) {
        // on either error set both return value and response type to error
        result = false;
        res->response = QM_RESPONSE_ERROR;
        if (!res->error) res->error = QM_ERROR_GENERAL;
        if (req->cmd != QM_CMD_STATUS) {
            // elevate log to error unless cmd is status request
            ll = LOG_SEVERITY_ERROR;
        }
    }

    LOG_SEVERITY(ll, "%s: req c:%d dt:%d ds:%d to:%s result:%d response:%d err:%d", __FUNCTION__,
            req->cmd, req->data_type, req->data_size, topic ? topic : "null",
            result, res->response, res->error);

    if (result && req->cmd == QM_CMD_SEND) {
        LOG(DEBUG, "Sent message to QM (size: %d type: %s)",
                data_size, qm_data_type_str(req->data_type));
    }

    return result;
}

bool qm_conn_osbus_init(void)
{
    return osbus_init();
}

