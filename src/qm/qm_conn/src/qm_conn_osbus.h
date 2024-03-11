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

#ifndef QM_CONN_OSBUS_H_INCLUDED
#define QM_CONN_OSBUS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include "osbus.h"

#define QM_OSBUS_POLICY_SEND_NUM 6
extern osbus_msg_policy_t qm_osbus_policy_send[QM_OSBUS_POLICY_SEND_NUM];
extern const char *qm_conn_osbus_known_topics[];
extern int qm_conn_osbus_known_topics_n;

bool qm_conn_osbus_init(void);
bool qm_conn_osbus_convert_response_stats_to_data(qm_response_t *res, osbus_msg_t **data);
bool qm_conn_osbus_convert_response_short_to_data(qm_response_t *res, osbus_msg_t **data);
bool qm_conn_osbus_convert_data_to_response(osbus_msg_t *data, qm_response_t *res);
bool qm_conn_osbus_convert_req_to_data(qm_request_t *req, char *topic, void *buf, int buf_size, osbus_msg_t **data);
bool qm_conn_osbus_convert_data_to_req(osbus_msg_t *data, qm_request_t *req, char **topic, void **buf);
bool qm_conn_osbus_send_req(qm_request_t *req, char *topic, void *buf, int buf_size, qm_response_t *res);

#endif /* QM_CONN_OSBUS_H_INCLUDED */
