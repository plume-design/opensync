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

#ifndef OW_CORE_THREAD_H_INCLUDED
#define OW_CORE_THREAD_H_INCLUDED

typedef void *ow_core_thread_call_fn_t(void *priv);

/**
 * Start OneWifi Core Thread
 *
 * This returns control flow as soon as the Core Thread
 * logic settles down and is ready to operate through other
 * APIs that it exposes (eg. ow_conf).
 *
 * The alternative is to integrate more tightly with libev
 * and ow_core_init() and ow_core_start().
 */
void
ow_core_thread_start(void);

/**
 * Run provided call within Core Thread context.
 *
 * This allows thread-safe interactions from a
 * foreign thread. This is expected to be called
 * whenever ow_* or osw_* interactions are done
 * from a non-core thread.
 *
 * This function blocks until after the call is
 * performed on the other end, and returns
 * whatever the provided function pointer returns.
 */
void *
ow_core_thread_call(ow_core_thread_call_fn_t *fn, void *fn_priv);

#endif /* OW_CORE_THREAD_H_INCLUDED */
