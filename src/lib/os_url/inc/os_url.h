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

#ifndef OS_URL_H_INCLUDED
#define OS_URL_H_INCLUDED

#include <stdbool.h>

/**
 * @file
 *
 * Simple download library based around cURL
 */

typedef size_t                      os_url_get_fn(char* ptr, size_t size, size_t nmemb, void *userdata);
typedef size_t                      os_url_put_fn(char* ptr, size_t size, size_t nmemb, void *userdata);

/**
 * URL statistics, this is returned by os_url_get_ex() after a transfer
 */
struct os_url_stat
{
    long        ous_rc;             /**< HTTP/FTP response code, CURLINFO_RESPONSE_CODE         */
    double      ous_bytes_rx;       /**< Total bytes received (warning, it's a double!)         */
    double      ous_bytes_tx;       /**< Total bytes sent (warning, it's a double!)             */
    double      ous_time_total;     /**< Total time of transfer                                 */
    double      ous_time_trans;     /**< Total time spent transmitting data w/o other overhead  */
};


extern bool             os_url_get_ex(char* url, int timeout, os_url_get_fn *get_fn, void *get_ctx, struct os_url_stat *stat, long resumefrom, bool insecure);
extern bool             os_url_get_ex_gzip(char* url, int timeout, os_url_get_fn *get_fn, void *get_ctx, struct os_url_stat *stat, long resumefrom, bool insecure, bool gzip);
extern bool             os_url_put_ex(char* url, int timeout, os_url_put_fn *put_fn, void *put_ctx, size_t size, struct os_url_stat *stat);
extern bool             os_url_get(char *url, os_url_get_fn *get_fn, void *get_ctx);
extern bool             os_url_get_file(char* url, FILE* f);
extern bool             os_url_get_file_insecure(char* url, FILE* f);
extern bool             os_url_get_file_insecure_gzip(char* url, FILE* f);
extern os_url_get_fn    os_url_get_null_fn;
extern os_url_get_fn    os_url_get_file_fn;
extern bool             os_url_continue(void);

#endif /* OS_URL_H_INCLUDED */
