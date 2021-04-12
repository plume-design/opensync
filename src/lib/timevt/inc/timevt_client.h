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

#ifndef TIMEVT_CLIENT_H_INCLUDED
#define TIMEVT_CLIENT_H_INCLUDED

#include <stdbool.h>
#include <stdarg.h>

#include "timevt.h"

/**
 * @brief time-event client object 
 */
struct te_client;

/**
 * @brief Handle to time-event client object
 */
typedef struct te_client *te_client_handle;

/**
 * @brief Opens time-event client for events generation. By default client is
 * not enabled - sent events are only logged locally. You need to enable the client
 * to begin remote transmission of events to the server.
 * 
 * @param server_sock_name server socket name to sent event messages to or NULL to use default addr
 * @param procname process name this client belongs to, if NULL then process ID will be used instead
 * @return te_client_handle or NULL in case of failure
 */
te_client_handle tecli_open(const char *server_sock_name, const char *procname);

/**
 * @brief Closes time-event client, releases all used resources
 * 
 * @param h handle to open time-event client
 */
void tecli_close(te_client_handle h);

/**
 * @brief Logs time-event in the log and sends the event to the local
 * server when client is enabled
 * 
 * @param h handle to time-event client
 * @param cat event category
 * @param subject event subject (module / interface) or NULL when undefined
 * @param step step for category (start, stop, other) or NULL for single events
 * @param msg_fmt printf() like event message format or NULL when message not provided
 * @param ap list of message parameters
 * @return true when event logged succesfully, false otherwise
 */
bool tecli_log_event(te_client_handle h, const char *cat, 
                    const char *subject, const char *step, const char *msg_fmt, va_list ap);

/**
 * @brief Enables / disables remote event logging on the server. When disabled (by default)
 * events are only logged in the default syslog
 * 
 * @param h handle to time-event client
 * @param enable remote logging on/off flag
 */
void tecli_enable(te_client_handle h, bool enable);

#endif
