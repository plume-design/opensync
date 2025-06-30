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

#ifndef EST_CLIENT_H_INCLUDED
#define EST_CLIENT_H_INCLUDED

#include "arena.h"
#include "est_request.h"

/*
 * Issue an asynchronous "cacerts" request to the EST server defined by
 * `set_server`. When the request is complete, `cacerts_fn` is called with
 * the returned error code and data from the request.
 *
 * @param[in]   arena           arena used for allocations
 * @param[in]   loop            libev loop used for callback registration
 * @param[in]   est_server      URL of the EST server
 * @param[in]   cacerts_fn      function callback
 * @param[in]   ctx             `cacerts_fn` context
 *
 * This function issues an asynchronous cURL request to
 * "https://est_server/.well-known/est/cacaerts". All data pertaining the
 * request is allocated on `arena`. Simply destroying/freeing the arena
 * will stop all timers and async function.
 *
 * @return This function returns `true` whether the request was successfully
 * scheduled or `false` if it was not.
 *
 * Note that even if the function returns `true`, the request may still fail
 * and the error code will be passed down to the callback.
 *
 */
bool est_client_cacerts(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        est_request_fn_t *cacerts_fn,
        void *ctx);

/*
 * Issue an asynchronous "simpleenroll" request to the EST server defined by
 * `set_server`. When the request is complete, `enroll_fn` is called with
 * the returned error code and data from the request. You need to pass a valid
 * certificate signing request in the `csr` variable.
 *
 * @param[in]   arena           arena used for allocations
 * @param[in]   loop            libev loop used for callback registration
 * @param[in]   est_server      URL of the EST server
 * @param[in]   csr             Certificate signing request
 * @param[in]   enroll_fn       function callback
 * @param[in]   ctx             `enroll_fn` context
 *
 * This function issues an asynchronous cURL request to
 * "https://est_server/.well-known/est/simpleenroll". All data pertaining the
 * request is allocated on `arena`. Simply destroying/freeing the arena
 * will stop all timers and async function.
 *
 * @return This function returns `true` whether the request was successfully
 * scheduled or `false` if it was not.
 *
 * Note that even if the function returns `true`, the request may still fail
 * and the error code will be passed down to the callback.
 *
 */
bool est_client_simple_enroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx);

/*
 * Issue an asynchronous "simplereenroll" request to the EST server defined by
 * `set_server`. When the request is complete, `enroll_fn` is called with
 * the returned error code and data from the request. You need to pass a valid
 * certificate signing request in the `csr` variable.
 *
 * @param[in]   arena           arena used for allocations
 * @param[in]   loop            libev loop used for callback registration
 * @param[in]   est_server      URL of the EST server
 * @param[in]   csr             Certificate signing request
 * @param[in]   enroll_fn       function callback
 * @param[in]   ctx             `enroll_fn` context
 *
 * This function issues an asynchronous cURL request to
 * "https://est_server/.well-known/est/simplereenroll". All data pertaining the
 * request is allocated on `arena`. Simply destroying/freeing the arena
 * will stop all timers and async function.
 *
 * @return This function returns `true` whether the request was successfully
 * scheduled or `false` if it was not.
 *
 * Note that even if the function returns `true`, the request may still fail
 * and the error code will be passed down to the callback.
 *
 */
bool est_client_simple_reenroll(
        arena_t *arena,
        struct ev_loop *loop,
        const char *est_server,
        const char *csr,
        est_request_fn_t *enroll_fn,
        void *ctx);

#endif /* EST_CLIENT_H_INCLUDED */
