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

#ifndef GK_ECURL_H_INCLUDED
#define GK_ECURL_H_INCLUDED

#include <curl/curl.h>
#include <stdbool.h>
#include "gatekeeper_msg.h"

#define MAX_PATH_LEN   256
#define MAX_GK_URL_LEN 1024

enum gk_response_code
{
    GK_LOOKUP_SUCCESS = 0,
    GK_LOOKUP_FAILURE,
    GK_CONNECTION_ERROR,
    GK_SERVICE_ERROR
};

/**
 * @brief required info for connecting to the
 * gatekeeper server
 */

struct gk_curl_easy_info
{
    CURL *curl_handle;
    CURLcode response;
    bool ecurl_connection_active;
    time_t ecurl_connection_time;
};

struct gk_curl_data
{
    char *memory;
    size_t size;
};

struct gk_connection_info
{
    struct gk_curl_easy_info *ecurl;
    struct gk_server_info *server_conf;
    struct gk_packed_buffer *pb;
};

struct gk_server_info
{
    char gk_url[MAX_GK_URL_LEN];
    char *server_url;
    char ca_path[MAX_PATH_LEN];
    char ssl_cert[MAX_PATH_LEN];
    char ssl_key[MAX_PATH_LEN];
    char *cert_path;
};

int gk_handle_curl_request(struct gk_connection_info *conn_info, struct gk_curl_data *chunk, long *response_code);

bool gk_curl_easy_init(struct gk_curl_easy_info *curl_info);

#endif /* GK_ECURL_H_INCLUDED */
