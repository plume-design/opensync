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

#ifndef RTS_TYPES_H
#define RTS_TYPES_H
#ifndef RTS_H
# error "Do not include rts_types.h directly; use rts.h instead."
#endif

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

/* Communication domain for use in rts_stream_init() */
#define RTS_AF_NONE  0
#define RTS_AF_INET  1
#define RTS_AF_INET6 2

/* These are the type provided in rts_subscribe() callback. */
#define RTS_TYPE_NUMBER 1
#define RTS_TYPE_STRING 2
#define RTS_TYPE_BINARY 3

/* A handle is required for scanning.
 *
 * The integration MUST ensure that a handle is not shared among threads 
 * unless access is protected with a mutex.
 */
typedef struct rts_handle *rts_handle_t;

/* A stream is required for scanning and is created with rts_stream_create().
 *
 * An integration may cancel scanning and free all associated resources using
 * rts_stream_destroy().
 */
typedef struct rts_stream *rts_stream_t;

/* Resource Usage - for use in rts_handle_rusage() */
struct rts_rusage {
    unsigned curr_alloc;      /* current bytes allocated */
    unsigned peak_alloc;      /* peak bytes allocated    */
    unsigned fail_alloc;      /* allocation failures     */
    unsigned mpmc_events;     /* shared events processed */
    unsigned scan_started;    /* number of scans started */
    unsigned scan_stopped;    /* number of scans stopped */
    unsigned scan_bytes;      /* number of bytes scanned */
};

#endif
