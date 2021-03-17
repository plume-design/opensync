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

#ifndef GK_CURL_H_INCLUDED
#define GK_CURL_H_INCLUDED

#include <curl/curl.h>
#include <ev.h>

#include "ds_tree.h"
#include "os_types.h"

struct http2_curl
{
    struct ev_loop *loop;
    struct ev_io fifo_event;
    struct ev_timer timer_event;
    CURLM *multi;
    int still_running;
};

struct gk_conn_info
{
    CURL *easy;
    char *url;
    struct http2_curl *global;
    char error[CURL_ERROR_SIZE];
};

struct gk_sock_info
{
    curl_socket_t sockfd;
    CURL *easy;
    int action;
    long timeout;
    struct ev_io ev;
    int evset;
    struct http2_curl *global;
};

/**
 * @brief Create a new easy handle, and add it to the global curl_multi
 * @param url url to add.
 * @return true if the initialization succeeded,
 *         false otherwise
 */
bool
gk_new_conn(char *url);


/**
 * @brief cleans up curl library.
 *
 */
bool
gk_curl_exit(void);


/**
 * @brief initialize curl library
 * @param loop pointer to ev_loop structure
 * @return true if the initialization succeeded,
 *         false otherwise
 */
bool
gk_multi_curl_init(struct ev_loop *loop);


#endif /* GK_CURL_H_INCLUDED */
