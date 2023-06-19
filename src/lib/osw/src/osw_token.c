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

#include <ev.h>
#include <log.h>
#include <const.h>
#include <util.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_module.h>
#include <osw_util.h>
#include <osw_token.h>

#define OSW_TOKEN_COUNT_ONE_BITS(array) osw_token_count_one_bits(array, ARRAY_SIZE(array))
#define DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))
#define BITS_PER_BYTE 8
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)
#define BITS_TO_LONG(bits) DIV_ROUND_UP(bits, BITS_PER_LONG)
#define OSW_TOKEN_COUNT (OSW_TOKEN_MAX - OSW_TOKEN_MIN + 1)
#define SUB_CLAMP(x, min, max) (((x) <= min) ? ((max) - 1) : ((x) - 1))

struct osw_token_pool_key {
    struct osw_ifname vif_name;
    struct osw_hwaddr sta_addr;
};

struct osw_token_pool {
    struct osw_token_pool_key key;
    unsigned int last_bit_used;
    long tokens[BITS_TO_LONG(OSW_TOKEN_COUNT)];
    struct ds_dlist ref_list; /* struct osw_token_pool_reference */
    struct ds_tree_node node;
};

struct osw_token_pool_reference {
    struct osw_token_pool *pool;
    struct ds_dlist_node node;
    long tokens[BITS_TO_LONG(OSW_TOKEN_COUNT)];
};

static size_t
osw_token_count_one_bits(const long *words, const size_t len)
{
    size_t n = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        n += __builtin_popcountl(words[i]);
    }
    return n;
}

static int
osw_token_pool_key_cmp(const void *a,
                       const void *b)
{
    ASSERT(a != NULL, "");
    ASSERT(b != NULL, "");
    const struct osw_token_pool *x = a;
    const struct osw_token_pool *y = b;

    const int vif_cmp = strncmp(x->key.vif_name.buf, y->key.vif_name.buf, sizeof(x->key.vif_name.buf));
    if (vif_cmp != 0) return vif_cmp;
    const int addr_cmp = osw_hwaddr_cmp(&x->key.sta_addr, &y->key.sta_addr);
    if (addr_cmp != 0) return addr_cmp;
    return 0;
}

static struct ds_tree g_pool_tree = DS_TREE_INIT((ds_key_cmp_t *) osw_token_pool_key_cmp, struct osw_token_pool, node);

static struct osw_token_pool *
osw_token_pool_alloc_new(const struct osw_token_pool_key *key)
{
    struct osw_token_pool *pool = CALLOC(1, sizeof(*pool));
    memcpy(&pool->key, key, sizeof(pool->key));
    pool->last_bit_used = 0;
    ds_dlist_init(&pool->ref_list, struct osw_token_pool_reference, node);
    ds_tree_insert(&g_pool_tree, pool, &pool->key);
    return pool;
}

static struct osw_token_pool *
osw_token_pool_lookup(const struct osw_token_pool_key *key)
{
    if (WARN_ON(key == NULL)) return NULL;
    struct osw_token_pool *pool = ds_tree_find(&g_pool_tree, key);
    return pool;
}

static struct osw_token_pool *
osw_token_pool_get_internal(const struct osw_token_pool_key *key)
{
    if (WARN_ON(key == NULL)) return NULL;
    struct osw_token_pool *pool = osw_token_pool_lookup(key);
    if (pool == NULL) pool = osw_token_pool_alloc_new(key);
    return pool;
}

static void
osw_token_vif_sta_to_key(const struct osw_ifname *vif_name,
                         const struct osw_hwaddr *sta_addr,
                         struct osw_token_pool_key *key)
{
    ASSERT(vif_name != NULL, "");
    ASSERT(sta_addr != NULL, "");
    STRSCPY_WARN(key->vif_name.buf, vif_name->buf);
    memcpy(&key->sta_addr, sta_addr, sizeof(key->sta_addr));
}

struct osw_token_pool_reference *
osw_token_pool_ref_get(const struct osw_ifname *vif_name,
                       const struct osw_hwaddr *sta_addr)
{
    ASSERT(vif_name != NULL, "");
    ASSERT(sta_addr != NULL, "");

    struct osw_token_pool_key key;
    osw_token_vif_sta_to_key(vif_name,
                             sta_addr,
                             &key);
    struct osw_token_pool *pool = osw_token_pool_get_internal(&key);

    struct osw_token_pool_reference *pool_ref = CALLOC(1, sizeof(*pool_ref));
    pool_ref->pool = pool;
    ds_dlist_insert_tail(&pool->ref_list, pool_ref);

    return pool_ref;
}


static void
osw_token_pool_free(struct osw_token_pool *pool)
{
    if (WARN_ON(pool == NULL)) return;

    size_t ref_count = ds_dlist_len(&pool->ref_list);
    size_t token_count = OSW_TOKEN_COUNT_ONE_BITS(pool->tokens);
    if (WARN_ON(ref_count != 0)) return;
    if (WARN_ON(token_count != 0)) return;

    LOGT("osw: token: free pool,"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT,
         pool->key.vif_name.buf,
         OSW_HWADDR_ARG(&pool->key.sta_addr));

    ds_tree_remove(&g_pool_tree, pool);
    FREE(pool);
}

static void
osw_token_pool_ref_drop_tokens(struct osw_token_pool_reference *pool_ref)
{
    struct osw_token_pool *pool = pool_ref->pool;
    assert(pool != NULL);

    size_t word;
    for (word = 0; word < ARRAY_SIZE(pool_ref->tokens); word++) {
        pool->tokens[word] &= ~pool_ref->tokens[word];
    }
}

void
osw_token_pool_ref_free(struct osw_token_pool_reference *pool_ref)
{
    if (pool_ref == NULL) return;

    struct osw_token_pool *pool = pool_ref->pool;
    if (WARN_ON(pool == NULL)) return;

    osw_token_pool_ref_drop_tokens(pool_ref);
    ds_dlist_remove(&pool->ref_list, pool_ref);
    FREE(pool_ref);

    size_t ref_count = ds_dlist_len(&pool->ref_list);
    if (ref_count == 0) osw_token_pool_free(pool);
}

int
osw_token_pool_fetch_token(struct osw_token_pool_reference *pool_ref)
{
    if (WARN_ON(pool_ref == NULL)) return OSW_TOKEN_INVALID;
    struct osw_token_pool *pool = pool_ref->pool;
    if (WARN_ON(pool == NULL)) return OSW_TOKEN_INVALID;

    /* look for unused token */
    int new_token = OSW_TOKEN_INVALID;
    const unsigned int started_with = pool->last_bit_used;
    do {
        pool->last_bit_used = SUB_CLAMP(pool->last_bit_used, 0, OSW_TOKEN_COUNT);
        const size_t word = pool->last_bit_used / BITS_PER_LONG;
        const long mask = 1L << (pool->last_bit_used % BITS_PER_LONG);
        if ((pool->tokens[word] & mask) == 0) {
            pool->tokens[word] |= mask;
            pool_ref->tokens[word] |= mask;
            new_token = pool->last_bit_used + OSW_TOKEN_MIN;
            break;
        }
    } while (started_with != pool->last_bit_used);

    if (new_token == OSW_TOKEN_INVALID) {
        LOGN("osw: token: pool_fetch_token: unique dialog token pool exhausted"
             " vif_name: %s"
             " sta_addr: "OSW_HWADDR_FMT,
             pool->key.vif_name.buf,
             OSW_HWADDR_ARG(&pool->key.sta_addr));
        return new_token;
    }

    LOGT("osw: token: pool_fetch_token: generated new dialog token,"
         " token: %d"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT,
         new_token,
         pool->key.vif_name.buf,
         OSW_HWADDR_ARG(&pool->key.sta_addr));

    return new_token;
}

void
osw_token_pool_free_token(struct osw_token_pool_reference *pool_ref,
                          int token)
{
    if (WARN_ON(pool_ref == NULL)) return;
    struct osw_token_pool *pool = pool_ref->pool;
    if (WARN_ON(pool == NULL)) return;
    if (WARN_ON(token == OSW_TOKEN_INVALID)) return;

    LOGT("osw: token: pool_free_token: free dialog token,"
         " token: %d"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT,
         token,
         pool->key.vif_name.buf,
         OSW_HWADDR_ARG(&pool->key.sta_addr));

    if (token < OSW_TOKEN_MIN) return;
    if (token > OSW_TOKEN_MAX) return;

    /* free single dialog token reservation if present */
    const long bit = token - OSW_TOKEN_MIN;
    const size_t word = bit / BITS_PER_LONG;
    const long mask = 1L << (bit % BITS_PER_LONG);
    if (WARN_ON((pool_ref->tokens[word] & mask) == 0)) return;
    WARN_ON((pool->tokens[word] & mask) == 0);
    pool->tokens[word] &= ~mask;
    pool_ref->tokens[word] &= ~mask;
}

OSW_MODULE(osw_token)
{
    return NULL;
}

#include "osw_token_ut.c"
