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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "gatekeeper_ecurl.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
/**
 * gk_curl_callback is a callback function used by libcurl as a write function.
 * This function gets called by libcurl as soon as there is data received that needs to be saved.
 *
 * @param contents Pointer to the data received.
 * @param size Size of each data element received.
 * @param nmemb Number of data elements received.
 * @param userp User-defined pointer passed from the curl request, in this case,
 *  a pointer to a gk_curl_data structure.
 */
static size_t gk_curl_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct gk_curl_data *mem;
    size_t realsize;
    char *ptr;

    mem = (struct gk_curl_data *)userp;
    realsize = size * nmemb;
    ptr = REALLOC(mem->memory, mem->size + realsize + 1);

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/**
 * @brief makes a request to gatekeeper server.
 *
 * @param conn_info pointer to gk_connection_info structure that contains
 * information about the cURL connection.
 * @param chunk memory to store the curl response
 * @return true on success, false on failure
 */
static int gk_send_curl_request(struct gk_curl_data *chunk, struct gk_connection_info *conn_info)
{
    struct gk_server_info *server_info;
    struct gk_curl_easy_info *curl_info;
    struct gk_packed_buffer *pb;
    char errbuf[CURL_ERROR_SIZE];
    CURLcode res;

    errbuf[0] = '\0';
    server_info = conn_info->server_conf;
    curl_info = conn_info->ecurl;
    pb = conn_info->pb;

    LOGT("%s(): sending request to %s using handler %p, certs path: %s, ssl cert %s, ssl key %s",
         __func__,
         server_info->gk_url,
         curl_info->curl_handle,
         server_info->cert_path != NULL ? server_info->cert_path : server_info->ca_path,
         server_info->cert_path != NULL ? "None" : server_info->ssl_cert,
         server_info->cert_path != NULL ? "None" : server_info->ssl_key);

    curl_easy_setopt(curl_info->curl_handle, CURLOPT_URL, server_info->gk_url);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDS, pb->buf);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDSIZE, (long)pb->len);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEFUNCTION, gk_curl_callback);
    if (server_info->cert_path != NULL)
    {
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_CAINFO, server_info->cert_path);
    }
    else
    {
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_CAINFO, server_info->ca_path);
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLCERT, server_info->ssl_cert);
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLKEY, server_info->ssl_key);
    }
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEDATA, chunk);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_ERRORBUFFER, errbuf);

    res = curl_easy_perform(curl_info->curl_handle);
    if (res != CURLE_OK)
    {
        LOGT("%s(): curl_easy_perform failed: ret code: %d (%s)", __func__, res, errbuf);
    }

    return res;
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
/**
 * @brief sends curl request to gatekeeper service.  Also the curl response code
 * is stored in response_code
 *
 * @param conn_info  pointer to gk_connection_info structure that contains information
 * about the cURL connection.
 * @param chunk pointer to a gk_curl_data structure that contains the data to be sent in the cURL request.
 * @response_code pointer to a long variable that will store the response code from the gatekeeper server.
 * @return gatekeeper response code.
 */
int gk_handle_curl_request(struct gk_connection_info *conn_info, struct gk_curl_data *chunk, long *response_code)
{
    struct gk_curl_easy_info *curl_info;
    CURLcode res;

    curl_info = conn_info->ecurl;

    /* update connection time */
    curl_info->ecurl_connection_time = time(NULL);

    /* send the curl request */
    res = gk_send_curl_request(chunk, conn_info);
    /* set the curl response code */
    curl_easy_getinfo(curl_info->curl_handle, CURLINFO_RESPONSE_CODE, response_code);
    if (res != CURLE_OK)
    {
        LOGN("%s(): curl request failed", __func__);
        curl_info->response = res;
        return GK_CONNECTION_ERROR;
    }

    LOGT("%s(): %zu bytes retrieved", __func__, chunk->size);
    return (chunk->size == 0) ? GK_CONNECTION_ERROR : GK_LOOKUP_SUCCESS;
}

/**
 * @brief initializes the curl handler for making HTTP requests
 *
 * @param curl_info pointer to gk_curl_easy_info structure, which
 * has the information about the cURL session
 * @return true on success, false on failure
 */
bool gk_curl_easy_init(struct gk_curl_easy_info *curl_info)
{
    if (curl_info->ecurl_connection_active == true) return true;

    curl_global_init(CURL_GLOBAL_ALL);

    curl_info->curl_handle = curl_easy_init();
    if (curl_info->curl_handle == false) goto error;

    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEFUNCTION, gk_curl_callback);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

    /* max. allowed time (2 secs) to establish the connection */
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_CONNECTTIMEOUT, 2L);

    /* max allowed time (2 secs) to get the data after connection is established */
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSL_VERIFYHOST, 1L);

    curl_info->ecurl_connection_active = true;
    curl_info->ecurl_connection_time = time(NULL);

    LOGT("%s() initialized curl handle %p ", __func__, curl_info->curl_handle);
    return true;

error:
    LOGD("%s(): failed to initialize curl handler", __func__);
    curl_global_cleanup();
    return false;
}
