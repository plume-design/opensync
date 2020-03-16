/* Simple string->void* hashtable, very static and bare minimal, but efficient
 *
 * Copyright (c) 2003  Jeremie Miller <jer@jabber.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MDNS_XHT_H_
#define MDNS_XHT_H_

typedef struct xht xht_t;

/**
 * must pass a prime#
 */
xht_t *xht_new(int prime);

/**
 * caller responsible for key storage, no copies made
 *
 * set val to NULL to clear an entry, memory is reused but never free'd
 * (# of keys only grows to peak usage)
 *
 * Note: don't free it b4 xht_free()!
 */
void xht_set(xht_t *h, const char *key, void *val);

/**
 * Unlike xht_set() where key/val is in caller's mem, here they are
 * copied into xht and free'd when val is 0 or xht_free()
 */
void xht_store(xht_t *h, const char *key, int klen, void *val, int vlen);

/**
 * returns value of val if found, or NULL
 */
void *xht_get(xht_t *h, const char *key);

/**
 * free the hashtable and all entries
 */
void xht_free(xht_t *h);

/**
 * pass a function that is called for every key that has a value set
 */
typedef void (*xht_walker)(xht_t *h, const char *key, void *val, void *arg);
void xht_walk(xht_t *h, xht_walker w, void *arg);

#endif  /* MDNS_XHT_H_ */
