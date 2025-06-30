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

#ifndef NFE_H
#define NFE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfe_types.h"

int nfe_conntrack_dump(nfe_conntrack_t conntrack, nfe_get_conntrack_cb_t cb, void *data);

/* nfe_conntrack_create()
 *
 * Create a conntrack for use by a thread, where @param size is the number of
 * hash buckets to use, increased to the next power of 2 when necessary. 
 *
 * This operation will allocate memory through nfe_ext_alloc().
 * 
 * Returns 0 on success or a negative error code on failure.
 *
 * An error code can have the following value:
 *
 * -ENOMEM
 *     There was no space to allocate state tables.
 */
int nfe_conntrack_create(nfe_conntrack_t *conntrack, uint32_t size);

/* nfe_conntrack_destroy()
 *
 * Destroy a conntrack previously created with nfe_conntrack_create().
 *
 * This operation will release memory through nfe_ext_free().
 *
 * Returns 0 on success or a negative error code on failure.
 */
int nfe_conntrack_destroy(nfe_conntrack_t conntrack);

/* nfe_packet_hash()
 *
 * Compute the hash for @param packet and fills the ntuple header. The
 * @param timestamp is in milliseconds.
 *
 * If @param data points to an ethernet frame, then @param ethertype is 0;
 * otherwise, ethertype must correspond to the protocol being passed in.
 *
 * Only one thread may hash a given packet.
 *
 * This operation will not allocate memory.
 *
 * Returns 0 on success or -1 on failure. The computed hash is
 * available in packet->hash.
 */
int nfe_packet_hash(struct nfe_packet *packet, uint16_t ethertype,
    const uint8_t *data, size_t len, uint64_t timestamp);

/* nfe_conn_lookup()
 *
 * Lookup the nfe_conn for @param packet.
 *
 * Only one thread may lookup a connection for a given conntrack.
 *
 * This operation may allocate memory through nfe_ext_conn_alloc().
 *
 * Returns a nfe_conn handle if the object exists or was created, otherwise
 * returns NULL. The caller must call nfe_conn_release() to release their reference.
 *
 * See nfe_ext_conn_alloc for further details.
 */
nfe_conn_t nfe_conn_lookup(nfe_conntrack_t conntrack, struct nfe_packet *packet);

/* nfe_conn_release()
 *
 * Release a nfe_conn_t.
 *
 * This operation may deallocate memory through nfe_ext_conn_free().
 */
void nfe_conn_release(nfe_conn_t conn);


int nfe_conntrack_update_timeouts(nfe_conntrack_t ct);

/* Optionally, an integration may intercept these functions to control the
 * allocation strategy.
 */
void *nfe_ext_alloc(size_t size);
void  nfe_ext_free(void *p);

/* Below are connection connection specific functions, called when a protocol
 * wants to create or destroy a connection. For context, the tuple associated
 * with the event is provided.
 */
void *nfe_ext_conn_alloc(size_t size, const struct nfe_tuple *tuple);
void nfe_ext_conn_free(void *p, const struct nfe_tuple *tuple);

#ifdef __cplusplus
}
#endif
#endif
