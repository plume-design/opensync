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

#ifndef TIMEVT_SERVER_H_INCLUDED
#define TIMEVT_SERVER_H_INCLUDED

#include <stdbool.h>

#include <ev.h>
#include <sys/un.h>

#include "timevt.h"

struct te_server;
/**
 * @brief Handle to time-event server collecting time-events from clients
 * and creating time-events reports every time aggregation period expires
 */
typedef struct te_server *te_server_handle;

// Default aggregation period in seconds
#define TIMEVT_AGGR_PERIOD (5.0)

/**
 * @brief Opens time-event server for receiving of time-event messages from
 * time-event clients and creating aggregated reports in protobuf serialized format
 * 
 * @param ev event loop to work with
 * @param sock_name server socket name in abstract namespace or NULL to use default one defined in TESRV_SOCKET_ADDR
 * @param sw_version software version to be included in the time-event reports
 * @param aggregation_period how long messages are aggregated before creating report or 0 to use TIMEVT_AGGR_PERIOD
 * @param max_events maximal number of time-events collected for single report
 * @return handle to the server object or NULL on failure
 */
te_server_handle tesrv_open(struct ev_loop *ev, const char *sock_name,
    const char *sw_version, ev_tstamp aggregation_period, size_t max_events);

/**
 * @brief Closes time-event server
 * @param h handle to time-event server
 */
void tesrv_close(te_server_handle h);

/**
 * @brief Gets server socket name / path
 * 
 * @param h handle to time-event server
 * @return server socket address (Unix datagram)
 */
const char *tesrv_get_name(te_server_handle h);

/**
 * @brief Sets new report aggregation period in the server
 * 
 * @param h handle to time-event server
 * @param period new aggr period in seconds
 */
void tesrv_set_aggregation_period(te_server_handle h, ev_tstamp period);

/**
 * @brief Sets target identity data to be used in time-event reports
 * 
 * @param h handle to time-event server
 * @param location_id POD location id string or NULL
 * @param node_id node ID string or NULL
 */
void tesrv_set_identity(te_server_handle h, const char *location_id, const char *node_id);

/**
 * @brief Gets configured server location ID or NULL when not set
 * @param h handle to time-event server
 * @return ptr to location ID string
 */
const char *tesrv_get_location_id(te_server_handle h);

/**
 * @brief Gets configured node ID or NULL when not set
 * @param h handle to time-event server
 * @return ptr to node ID string
 */
const char *tesrv_get_node_id(te_server_handle h);

/**
 * @brief Pointer to time-event new report created handler function, invoked each time server
 * creates new report. Buffer contains protobuf packed structure of Sts__TimeEventsReport type
 * @param subscriber ptr to subscriber object
 * @param srv handle to server which created this report
 * @param buffer ptr to byte buffer containing protobuf packed Sts__TimeEventsReport structure
 * @param length length of the contents in the buffer in bytes
 * @return handler shall return 'true' to inform the server, that report was consumed OR 'false'
 * no inform the server, that report was rejected - for statistical purposes
 */
typedef bool (*tesrv_new_report_fp_t)(void *subscriber, te_server_handle srv, const uint8_t *buffer, size_t length);

/**
 * @brief Subscription for new reports or cancellation
 * @param h handle to time-event server
 * @param subscriber ptr to subscriber object (NULL is allowed)
 * @param pfn ptr to new subscription handler or NULL to unsubscribe
 */
void tesrv_subscribe_new_report(te_server_handle h, void *subscriber, tesrv_new_report_fp_t pfn);

/**
 * @brief Gets number of messages succesfully published to subscriber
 * @param h handle to server object
 * @return number of published time-event messages
 */
size_t tesrv_get_msg_ack(te_server_handle h);

/**
 * @brief Gets number of time-event messages received from the clients
 * @param h handle to server object
 * @return number of received time-event messages
 */
size_t tesrv_get_msg_received(te_server_handle h);

/**
 * @brief Gets number of lost time-event messages (not consumed by subscriber)
 * @param h handle to server object
 * @return number of lost messages
 */
size_t tesrv_get_msg_lost(te_server_handle h);

/**
 * @brief Gets number of reports succesfully published to subscriber
 * @param h handle to server object
 * @return number of published reports
 */
size_t tesrv_get_reports(te_server_handle h);

#endif
