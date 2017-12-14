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

#ifndef QM_CONN_H_INCLUDED
#define QM_CONN_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

// request

#define QM_REQUEST_TAG "QREQ"
#define QM_REQUEST_VER 2

enum qm_req_cmd
{
    QM_CMD_STATUS = 1,
    QM_CMD_SEND   = 2,
};


enum qm_req_compress
{
    QM_REQ_COMPRESS_IF_CFG  = 0, // enabled by ovsdb mqtt conf
    QM_REQ_COMPRESS_DISABLE = 1, // disable
    QM_REQ_COMPRESS_FORCE   = 2, // always compress
};

// informational only
enum qm_req_data_type
{
    QM_DATA_RAW = 0,
    QM_DATA_TEXT,
    QM_DATA_STATS,
    QM_DATA_BS,    // band steering
};

typedef struct qm_request
{
    char tag[4];
    uint32_t ver;
    uint32_t seq;
    uint32_t cmd;
    uint32_t flags;
    char sender[16]; // prog name

    uint8_t set_qos; // if 1 use qos_val instead of ovsdb cfg
    uint8_t qos_val;
    uint8_t compress;
    uint8_t data_type;

    uint32_t interval;
    uint32_t topic_len;
    uint32_t data_size;
    uint32_t reserved;
} qm_request_t;

// response

#define QM_RESPONSE_TAG "RESP"
#define QM_RESPONSE_VER 1

enum qm_response_type
{
    QM_RESPONSE_ERROR    = 0, // error response
    QM_RESPONSE_STATUS   = 1, // status response
    QM_RESPONSE_RECEIVED = 2, // message received confirmation
};

// error type
enum qm_res_error
{
    QM_ERROR_GENERAL     = 100, // general error
    QM_ERROR_CONNECT     = 101, // error connecting to QM
    QM_ERROR_INVALID     = 102, // invalid response
};

// status of connection from QM to the mqtt server
enum qm_res_conn_status
{
    QM_CONN_STATUS_NO_CONF      = 200,
    QM_CONN_STATUS_DISCONNECTED = 201,
    QM_CONN_STATUS_CONNECTED    = 202,
};

typedef struct qm_response
{
    char tag[4];
    uint32_t ver;
    uint32_t seq;
    uint32_t response;
    uint32_t error;
    uint32_t flags;
    uint32_t conn_status;
    uint32_t qdrop; // num queued messages dropped due to queue full
    uint32_t qlen;  // queue length - number of messages
    uint32_t qsize; // queue size - bytes
} qm_response_t;



bool qm_conn_accept(int listen_fd, int *accept_fd);
bool qm_conn_server(int *pfd);
bool qm_conn_client(int *pfd);

void qm_req_init(qm_request_t *req);
bool qm_req_valid(qm_request_t *req);
bool qm_conn_write_req(int fd, qm_request_t *req, char *topic, void *data, int data_size);
bool qm_conn_read_req(int fd, qm_request_t *req, char **topic, void **data);
bool qm_conn_parse_req(void *buf, int buf_size, qm_request_t *req, char **topic, void **data, bool *complete);

void qm_res_init(qm_response_t *res, qm_request_t *req);
bool qm_res_valid(qm_response_t *res);
bool qm_conn_write_res(int fd, qm_response_t *res);
bool qm_conn_read_res(int fd, qm_response_t *res);

bool qm_conn_get_status(qm_response_t *res);
bool qm_conn_send_req(qm_request_t *req, char *topic, void *data, int data_size, qm_response_t *res);
bool qm_conn_send_raw(void *data, int data_size, char *topic, qm_response_t *res);
bool qm_conn_send_stats(void *data, int data_size, qm_response_t *res);
bool qm_conn_send_bs(void *data, int data_size, int interval, qm_response_t *res);

#endif
