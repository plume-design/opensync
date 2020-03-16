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

#include "xht.h"
#include <string.h>
#include <stdlib.h>

typedef struct xhn {
    char flag;
    struct xhn *next;
    union {
        char *key;
        const char *ckey;
    } u;
    void *val;
} xhn_t;

struct xht {
    int prime;
    xhn_t *zen;
};

/* Generates a hash code for a string.
 * This function uses the ELF hashing algorithm as reprinted in 
 * Andrew Binstock, "Hashing Rehashed," Dr. Dobb's Journal, April 1996.
 */
static int _xhter(const char *s)
{
    /* ELF hash uses unsigned chars and unsigned arithmetic for portability */
    const unsigned char *name = (const unsigned char *)s;
    unsigned long h = 0, g;

    while (*name) {     /* do some fancy bitwanking on the string */
        h = (h << 4) + (unsigned long)(*name++);
        if ((g = (h & 0xF0000000UL)) != 0)
            h ^= (g >> 24);
        h &= ~g;

    }

    return (int)h;
}


static xhn_t *_xht_node_find(xhn_t *n, const char *key)
{
    for (; n != 0; n = n->next)
        if (n->u.ckey && strcmp(key, n->u.ckey) == 0)
            return n;
    return 0;
}


xht_t *xht_new(int prime)
{
    xht_t *xnew;

    xnew = malloc(sizeof(struct xht));
    xnew->prime = prime;
    xnew->zen = calloc(1, sizeof(struct xhn) * prime);  /* array of xhn_t size of prime */

    return xnew;
}

/* does the set work, used by xht_set and xht_store */
static xhn_t *_xht_set(xht_t *h, const char *key, void *val, char flag)
{
    int i;
    xhn_t *n;

    /* get our index for this key */
    i = _xhter(key) % h->prime;

    /* check for existing key first, or find an empty one */
    n = _xht_node_find(&h->zen[i], key);
    if (n == NULL) {
        for (n = &h->zen[i]; n != 0; n = n->next) {
            if (n->val == NULL)
                break;
        }
    }

    /* if none, make a new one, link into this index */
    if (n == NULL) {
        n = malloc(sizeof(struct xhn));
        n->next = h->zen[i].next;
        h->zen[i].next = n;
    }

    /* When flag is set, we manage their mem and free em first */
    if (n->flag) {
        free(n->u.key);
        free(n->val);
    }

    n->flag = flag;
    n->u.ckey = key;
    n->val = val;

    return n;
}

void xht_set(xht_t *h, const char *key, void *val)
{
    if (h == NULL || key == NULL)
        return;
    _xht_set(h, key, val, 0);
}

void xht_store(xht_t *h, const char *key, int klen, void *val, int vlen)
{
    char *ckey, *cval;

    if (h == NULL || key == NULL || klen == 0)
        return;

    ckey = malloc(klen + 1);
    memcpy(ckey, key, klen);
    ckey[klen] = '\0';
    cval = malloc(vlen + 1);
    memcpy(cval, val, vlen);
    cval[vlen] = '\0';  /* convenience, in case it was a string too */
    _xht_set(h, ckey, cval, 1);
}


void *xht_get(xht_t *h, const char *key)
{
    xhn_t *n;

    if (h == NULL || key == NULL)
        return NULL;

    n = _xht_node_find(&h->zen[_xhter(key) % h->prime], key);
    if (n == NULL)
        return NULL;

    return n->val;
}


void xht_free(xht_t *h)
{
    int i;
    xhn_t *n, *f;

    if (h == NULL)
        return;

    for (i = 0; i < h->prime; i++) {
        if ((n = (&h->zen[i])) == NULL)
            continue;
        if (n->flag) {
            free(n->u.key);
            free(n->val);
        }
        for (n = (&h->zen[i])->next; n != 0;) {
            f = n->next;
            if (n->flag) {
                free(n->u.key);
                free(n->val);
            }
            free(n);
            n = f;
        }
    }

    free(h->zen);
    free(h);
}

void xht_walk(xht_t *h, xht_walker w, void *arg)
{
    int i;
    xhn_t *n;

    if (h == NULL || w == NULL)
        return;

    for (i = 0; i < h->prime; i++) {
        for (n = &h->zen[i]; n != 0; n = n->next) {
            if (n->u.ckey != 0 && n->val != 0)
                (*w)(h, n->u.ckey, n->val, arg);
        }
    }
}
