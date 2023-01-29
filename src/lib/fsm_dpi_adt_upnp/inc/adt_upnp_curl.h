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

#ifndef ADT_UPNP_CURL_H_INCLUDED
#define ADT_UPNP_CURL_H_INCLUDED

#include <curl/curl.h>
#include <ev.h>

#include "fsm_dpi_adt_upnp.h"

struct adt_upnp_curl
{
    struct ev_loop *loop;
    struct ev_io fifo_event;
    struct ev_timer timer_event;
    CURLM *multi;
    int still_running;
};

struct upnp_curl_buffer
{
    int size;
    char *buf;
};

struct conn_info
{
    char error[CURL_ERROR_SIZE];
    CURL *easy;
    char *url;
    struct adt_upnp_curl *global;
    struct fsm_dpi_adt_upnp_root_desc *context;
    struct upnp_curl_buffer data;
};

/* Information associated with a specific socket */
struct sock_info
{
    curl_socket_t sockfd;
    CURL *easy;
    int action;
    long timeout;
    struct ev_io ev;
    int evset;
    struct adt_upnp_curl *global;
};

void adt_upnp_curl_init(struct ev_loop *loop);
void adt_upnp_curl_exit(void);
void adt_upnp_call_mcurl(struct fsm_dpi_adt_upnp_root_desc *url);

void adt_upnp_init_elements(struct fsm_dpi_adt_upnp_root_desc *url);
struct adt_upnp_key_val *adt_upnp_get_elements(void);

#endif /* ADT_UPNP_CURL_H_INCLUDED */
