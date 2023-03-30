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

struct osw_token_pool_key {
    struct osw_ifname vif_name;
    struct osw_hwaddr sta_addr;
};

struct osw_token_pool {
    struct osw_token_pool_key key;
    int last_token;
    struct ds_dlist token_list; /* struct osw_token_dialog_token */
    struct ds_dlist ref_list; /* struct osw_token_pool_reference */
    struct ds_tree_node node;
};

struct osw_token_pool_reference {
    struct osw_token_pool *pool;
    struct ds_dlist_node node;
};

struct osw_token_dialog_token {
    int val;
    struct ds_dlist_node node;
};

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
    pool->last_token = OSW_TOKEN_MIN;
    ds_dlist_init(&pool->token_list, struct osw_token_dialog_token, node);
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
    size_t token_count = ds_dlist_len(&pool->token_list);
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

void
osw_token_pool_ref_free(struct osw_token_pool_reference *pool_ref)
{
    if (pool_ref == NULL) return;

    struct osw_token_pool *pool = pool_ref->pool;
    if (WARN_ON(pool == NULL)) return;

    ds_dlist_remove(&pool->ref_list, pool_ref);
    FREE(pool_ref);

    size_t ref_count = ds_dlist_len(&pool->ref_list);
    if (ref_count == 0) osw_token_pool_free(pool);
}

static int
osw_token_get_next(const int last_token)
{
    /* upper half, going down from OSW_TOKEN_MAX to OSW_TOKEN_MIN */
    if (last_token <= OSW_TOKEN_MIN) return OSW_TOKEN_MAX;
    return last_token - 1;
}

static bool
osw_token_is_present_in_list(struct osw_token_pool *pool,
                             const int token)
{
    ASSERT(pool != NULL, "");

    bool present = false;
    struct osw_token_dialog_token *dt;
    ds_dlist_foreach(&pool->token_list, dt) {
        if (dt->val == token) {
            present = true;
        }
    }
    return present;
}

int
osw_token_pool_fetch_token(const struct osw_token_pool_reference *pool_ref)
{
    if (WARN_ON(pool_ref == NULL)) return OSW_TOKEN_INVALID;
    struct osw_token_pool *pool = pool_ref->pool;
    if (WARN_ON(pool == NULL)) return OSW_TOKEN_INVALID;

    /* look for unused token */
    int new_token = OSW_TOKEN_INVALID;
    uint8_t t;
    for (t = osw_token_get_next(pool->last_token);
         t != pool->last_token;
         t = osw_token_get_next(t)) {
        const bool is_token_taken = osw_token_is_present_in_list(pool, t);
        if (is_token_taken == false) {
            new_token = t;
            break;
        }
    }

    if (new_token == OSW_TOKEN_INVALID) {
        LOGN("osw: token: pool_fetch_token: unique dialog token pool exhausted"
             " vif_name: %s"
             " sta_addr: "OSW_HWADDR_FMT,
             pool->key.vif_name.buf,
             OSW_HWADDR_ARG(&pool->key.sta_addr));
        return new_token;
    }

    /* append list with reserved token */
    struct osw_token_dialog_token *dialog_token = CALLOC(1, sizeof(*dialog_token));
    dialog_token->val = new_token;
    ds_dlist_insert_tail(&pool->token_list, dialog_token);
    pool->last_token = new_token;

    LOGT("osw: token: pool_fetch_token: generated new dialog token,"
         " token: %d"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT,
         pool->last_token,
         pool->key.vif_name.buf,
         OSW_HWADDR_ARG(&pool->key.sta_addr));

    return pool->last_token;
}

void
osw_token_pool_free_token(const struct osw_token_pool_reference *pool_ref,
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

    /* free single dialog token reservation if present */
    struct osw_token_dialog_token *dt;
    struct osw_token_dialog_token *dt_tmp;
    ds_dlist_foreach_safe(&pool->token_list, dt, dt_tmp) {
        if (dt->val == token) {
            ds_dlist_remove(&pool->token_list, dt);
            FREE(dt);
            break;
        }
    }
}

OSW_MODULE(osw_token)
{
    return NULL;
}

#include "osw_token_ut.c"
