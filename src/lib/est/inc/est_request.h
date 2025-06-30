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

#if !defined(EST_REQUEST_H_INCLUDED)
#define EST_REQUEST_H_INCLUDED

#include <stdbool.h>

#include <curl/curl.h>

#include "arena.h"

/*
 * EST request status structure
 */
struct est_request_status
{
    enum
    {
        ER_STATUS_OK = 0,
        ER_STATUS_ERROR = 1
    } status;

    union
    {
        struct
        {
            time_t retry_after; /* Retry-After header, relative to current time */
        } ER_STATUS_ERROR;
    };
};

/*
 * Structure returned as `data` when the status is ER_STATUS_ERROR (see est_request_fn_t)
 */
struct est_request_error
{
    time_t err_retry_after; /* Suggest rettry-after in seconds as received by the remote server, 0 if not present */
};

typedef void est_request_fn_t(const struct est_request_status status, const char *data, void *ctx);

/*
 * Start the cURL request in `c_req` in an asynchronous fashion
 *
 * @param[in]   arena   arena that will be used for internal allocations
 * @param[in]   loop    libev event loop used for timers and other events
 * @param[in]   c_req   the CURL request to start asynchronously
 * @param[in]   fn      completion callback function
 * @param[in]   ctx     the context passed to `fn` upon completion
 *
 * This function takes a CURL structures and wraps it around a CURL multi-object
 * (CURLM). The event handlers for CURLM are implemented using libev. When the
 * request has completed, the `fn` function is called with the status code
 * (enum est_request_status), the data received and the context `ctx`
 *
 * @return This function returns `true` on success and `false` on error.
 */
bool est_request_curl_async(arena_t *arena, struct ev_loop *loop, CURL *c_req, est_request_fn_t *fn, void *ctx);

#endif /* EST_REQUEST_H_INCLUDDE */
