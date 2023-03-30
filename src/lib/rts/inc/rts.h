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

#ifndef RTS_H
#define RTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rts_types.h"

/* rts_handle_create()
 *
 * Initialize a handle for scanning.
 *
 * This operation will allocate memory through rts_ext_alloc().
 *
 * @param handle is an out parameter. This is required and must point to a
 * valid rts_handle_t.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -ENOMEM
 *     There was no space to allocate the handle.
 *
 * -EINVAL
 *     The handle pointer is invalid.
 */
int rts_handle_create(rts_handle_t *handle);

/* rts_handle_destroy()
 *
 * Release a handle previously initialized with rts_handle_create().
 *
 * This operation will release memory.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -EINVAL
 *     The handle is not initialized or the pointer is invalid.
 */
int rts_handle_destroy(rts_handle_t handle);

/* rts_handle_rusage()
 *
 * This function returns resource usage about an rts_handle. Details regarding
 * the rusage fields are documented in the rts_rusage struct definition.
 *
 * It is expected that the rts_rusage structure is zeroed for first use. Note
 * also that internal counters are reset with each call to rts_handle_rusage().
 * If you wish to accumulate, do not reset/zero your rusage structure between
 * calls.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -EINVAL
 *     The handle is not initialized or the pointer is invalid.
 */

int rts_handle_rusage(rts_handle_t handle, struct rts_rusage *);

/* rts_stream_create()
 *
 * Initialize a stream for scanning.
 *
 * @param stream   A pointer to a valid rts_stream_t.
 * @param handle   The handle this stream will be attached to.
 * @param domain   RTS_AF_NONE, RTS_AF_INET, or RTS_AF_INET6.
 * @param proto    The IP protocol identifier
 * @param saddr    The source ip address
 * @param sport    The source port
 * @param daddr    The destination ip address
 * @param dport    The destination port
 * @param user     An opaque pointer to some data. This will be relayed
 *                 for context in rts_subscribe callbacks.
 *
 * Notes:
 * - The values in saddr/sport/daddr/dport are expected in network order
 * - The length of saddr/daddr are interpreted according to the domain, i.e.
 *   4 for RTS_AF_INET, 16 for RTS_AF_INET6. For RTS_AF_NONE, NULL address
 *   may be passed.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -ENOMEM
 *     There was no space to allocate the stream.
 *
 * -EINVAL
 *     The stream or handle pointer is invalid, the domain is invalid, or the
 *     protocol is unsupported.
 */
int rts_stream_create(rts_stream_t *stream, rts_handle_t handle, uint8_t domain,
    uint8_t proto, const void *saddr, uint16_t sport, const void *daddr, uint16_t dport, void *user);

/* rts_stream_destroy()
 *
 * Release a stream previously initialized with rts_stream_create.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -EINVAL
 *     The stream is invalid.
 */
int rts_stream_destroy(rts_stream_t stream);

/* rts_stream_scan()
 *
 * Scan @param len bytes in @param buf. Use 0 for @param dir if data was sent
 * by the client (connection initiator), non-zero otherwise. The @param
 * timestamp for this data is in milliseconds. It is used to update internal
 * data and for garbage collection.
 *
 * Returns the number of bytes consumed during the scan or a negative error code
 * on failure. If the stream is already classified, rts_stream_scan() returns 0.
 *
 * An error code can have the following value:
 *
 * -ENOMEM
 *     Out of handle memory. To increase the handle pool, increase the value of
 *     rts_thread_memory_size _before_ creating the handle.
 *
 * -EINVAL
 *     The stream is invalid.
 */
int rts_stream_scan(rts_stream_t stream, const void *buf, uint16_t len, int dir,
    uint64_t timestamp);

/* rts_stream_matching()
 *
 * Determine whether a stream is actively matching or complete. If a stream is
 * no longer matching, the caller is free to release the stream with
 * rts_stream_destroy(), after which they MUST not access the rts_stream_t.
 *
 * Returns 0 when the stream is done matching, 1 when the stream can continue
 * matching or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -EINVAL
 *     The stream is invalid.
 */
int rts_stream_matching(rts_stream_t stream);

/* rts_lookup()
 *
 * Lookup a string by its index.
 *
 * @param index    A numeric value provided in a subscription callback for an
 *                 element that supports this operation.
 * @param value    An out parameter returning a pointer to a string.
 * @param stream   An optional parameter providing context for the lookup. If
 *                 an integration updates signatures with rts_load() while an
 *                 existing stream is in use, the value dispatched on that
 *                 stream will reflect the previous load, thus string lookups
 *                 would need that context.
 *
 *
 * This function can also be called to determine the string table size by
 * passing an index value of -1.
 *
 * When called with a NULL stream, the string table for the current load will
 * be used.
 *
 * Returns 0 on success or a negative error code on failure, unless the index
 * is -1. In that case the return value is a positive integer representing the
 * number of services available.
 *
 * An error code can have the following value:
 *
 * -EINVAL
 *     The index is out of range.
 *
 */
int rts_lookup(int index, const char **value, rts_stream_t stream);

/* rts_load()
 *
 * Load new signatures
 *
 * This operation will allocate memory. It should be noted that if existing
 * signatures are loaded, they will be unloaded, but only after all handles
 * referencing them release their hold.
 *
 * If desired, an explicit unload is possible by calling rts_load() with a
 * NULL and 0 length.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -ENOMEM
 *     The external memory allocation calls were unsuccessful.
 *
 * -EINVAL
 *     The signatures pointed to by @param mp is either corrupt or incompatible
 *     with this version of the library.
 *
 * Note:
 *
 * To ease portability, rts makes no provision for loading a signature file from
 * disk. It is expected that the integrating product can handle this task. For
 * additional information and examples, refer to the sample applications included
 * with the SDK.
 */
int rts_load(const void *mp, size_t ml);

/* rts_subscribe()
 *
 * Subscribe to a key exported by the loaded signatures. The key names are
 * published with signature release documentation. Values are provided via
 * callback as key criteria are met.
 *
 * In the @param callback, the value of @param type indicates how to interpret
 * @param value: RTS_TYPE_NUMBER, RTS_TYPE_STRING or RTS_TYPE_BINARY.
 *
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -EBUSY
 *     The request cannot be processed because handles are in use.
 * -EINVAL
 *     No signatures are loaded or the requested key is not defined.
 *
 *  EXAMPLE:
 *
 *  The following example would print every time a server hostname was found.
 *  In current versions, this would be called for http.host, tls.host,
 *  quic.host, etc.
 *
 *  static void
 *  handle_site_host(rts_stream_t stream, void *user, const char *key,
 *        uint8_t type, uint16_t length, const void *value)
 *  {
 *      assert(type == RTS_TYPE_STRING);
 *      printf("%s = %.*s\n", key, length, value);
 *  }
 *
 *  rts_subscribe("site.host", handle_site_host);
 */

int rts_subscribe(const char *key,
    void (*callback)(rts_stream_t stream, void *user, const char *key,
        uint8_t type, uint16_t length, const void *value));


/* Optionally, an integration may intercept these functions to control the
 * allocation strategy.
 */
void *rts_ext_alloc(size_t size);
void  rts_ext_free(void *p);

#ifdef __cplusplus
}
#endif
#endif
