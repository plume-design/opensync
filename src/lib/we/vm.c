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

#include "vm.h"
#include "we.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "memutil.h"
#include <string.h>
#include <kconfig.h>
#include "log.h"

struct we_obj
{
    uint32_t type : 3;
    uint32_t off : 29;
    uint32_t weak : 1;
    uint32_t len : 31;
    union
    {
        int64_t val;
        uint64_t bits;
        struct we_buf *buf;
        struct we_arr *arr;
        struct we_tab *tab;
    } u;
};

struct we_buf
{
    uint32_t ref;
    uint32_t managed : 1;
    uint32_t len : 31;
    uint8_t *data;
    uint8_t mem[];
};

struct we_arr
{
    uint32_t ref;
    uint32_t size;
    uint32_t items;
    struct we_obj *data;
    struct we_arr *prev;
};

struct we_tab
{
    uint32_t ref;
    uint32_t size;
    uint32_t items;
    struct we_kvp *data;
    struct we_obj meta;
};

struct we_kvp
{
    struct we_obj key;
    struct we_obj val;
};

void *we_malloc(size_t sz);
void *we_calloc(size_t nmemb, size_t sz);
void we_free(void *p);

static struct we_obj *we_obj_acquire(struct we_obj *);
static void we_obj_release(struct we_obj *);

static struct we_mem_pool we_mem_pool_mgr = {
    .initialized = false,
};

struct we_mem_pool *we_get_mem_pool_mgr(void)
{
    return &we_mem_pool_mgr;
}

static bool we_nil(struct we_obj *a)
{
    a->u.val = 0;
    a->type = WE_NIL;
    return true;
}

static uint32_t we_nil_len(struct we_obj *a)
{
    (void)a;
    return 0;
}

static uint32_t we_nil_size(struct we_obj *a)
{
    (void)a;
    return 0;
}

static uint32_t we_nil_ref(struct we_obj *a)
{
    (void)a;
    return 0;
}

static uint32_t we_nil_hash(struct we_obj *a)
{
    (void)a;
    return 0;
}

static struct we_obj *we_nil_acquire(struct we_obj *a)
{
    return a;
}

static void we_nil_release(struct we_obj *a)
{
    (void)a;
}

static bool we_nil_equal(struct we_obj *a, struct we_obj *b)
{
    return a->type == b->type;
}

static bool we_num(struct we_obj *a, int64_t val)
{
    a->u.val = val;
    a->type = WE_NUM;
    return true;
}

static uint32_t we_num_len(struct we_obj *a)
{
    return sizeof(a->u.val);
}

static uint32_t we_num_size(struct we_obj *a)
{
    return we_num_len(a);
}

static uint32_t we_num_ref(struct we_obj *a)
{
    (void)a;
    return 1;
}

static uint32_t we_num_hash(struct we_obj *a)
{
    return a->u.val;
}

static struct we_obj *we_num_acquire(struct we_obj *a)
{
    return a;
}

static void we_num_release(struct we_obj *a)
{
    (void)a;
}

static bool we_num_equal(struct we_obj *a, struct we_obj *b)
{
    return a->type == b->type && a->u.val == b->u.val;
}

static struct we_obj *we_buf_acquire(struct we_obj *a)
{
    a->u.buf->ref++;
    return a;
}

static void we_buf_release(struct we_obj *a)
{
    if (--(a->u.buf->ref) == 0)
    {
        if (a->u.buf->managed) we_free(a->u.buf->data);
        we_free(a->u.buf);
    }
}

static void we_buf(struct we_obj *obj, uint32_t len, uint32_t off, struct we_obj *from)
{
    *obj = *we_buf_acquire(from);
    obj->off = from->off + off;
    obj->len = len;
}

static bool we_buf_ptr(struct we_obj *obj, uint32_t len, const void *data)
{
    struct we_buf *buf;
    if (!(buf = we_malloc(sizeof(*buf))))
    {
        return false;
    }
    else
    {
        buf->ref = 1;
        buf->len = len;
        buf->data = (void *)data;
        buf->managed = 0;
    }
    obj->u.buf = buf;
    obj->len = data ? len : 0;
    obj->off = 0;
    obj->type = WE_BUF;
    return true;
}

static bool we_buf_dup(struct we_obj *obj, uint32_t len, const void *data)
{
    struct we_buf *buf;
    if (!(buf = we_malloc(sizeof(*buf) + len)))
    {
        return false;
    }
    else
    {
        buf->ref = 1;
        buf->len = len;
        buf->data = buf->mem;
        buf->managed = 0;
        if (data) __builtin_memcpy(buf->data, data, len);
    }
    obj->u.buf = buf;
    obj->len = len;
    obj->off = 0;
    obj->type = WE_BUF;
    return true;
}

static uint32_t we_buf_len(struct we_obj *a)
{
    return a->len;
}

static uint32_t we_buf_size(struct we_obj *a)
{
    return a->u.buf->len;
}

static uint32_t we_buf_ref(struct we_obj *a)
{
    return a->u.buf->ref;
}

static uint32_t we_buf_hash(struct we_obj *a)
{
    const struct we_buf *buf = a->u.buf;
    const uint32_t fnv_prime = 16777619UL;
    const uint32_t e = a->off + a->len;
    uint32_t hash = 2166136261UL;
    uint32_t i = a->off;
    while (i < e)
    {
        hash ^= (uint32_t)buf->data[i++];
        hash *= fnv_prime;
    }
    return hash;
}

static inline void *we_buf_data(struct we_obj *a)
{
    return a->u.buf->data + a->off;
}

static int we_buf_cmp(struct we_obj *a, struct we_obj *b)
{
    int la = we_buf_len(a);
    int lb = we_buf_len(b);
    int cr = __builtin_memcmp(we_buf_data(a), we_buf_data(b), la < lb ? la : lb);
    if (!cr) return la - lb;
    return cr;
}

static bool we_buf_equal(struct we_obj *a, struct we_obj *b)
{
    if (we_buf_len(a) == we_buf_len(b)) return we_buf_cmp(a, b) == 0;
    return false;
}

static struct we_obj *we_arr_acquire(struct we_obj *a)
{
    if (!a->weak) a->u.arr->ref++;
    return a;
}

static void we_arr_release(struct we_obj *a)
{
    struct we_arr *arr = a->u.arr;
    if (!a->weak && --(arr->ref) == 0)
    {
        uint32_t i;
        if (arr->prev)
        {
            struct we_obj obj = {.type = WE_ARR, .u.arr = arr->prev};
            we_obj_release(&obj);
        }
        for (i = 0; i < arr->size; i++)
        {
            if (!arr->items)
                break;
            else if (!arr->data[i].type)
                continue;
            arr->items--;
            we_obj_release(&arr->data[i]);
        }
        we_free(arr->data);
        we_free(arr);
    }
}


static bool we_arr(struct we_obj *obj)
{
    if (!(obj->u.arr = we_malloc(sizeof(*obj->u.arr)))) return false;
    obj->u.arr->items = 0;
    obj->u.arr->size = 0;
    obj->u.arr->data = 0;
    obj->u.arr->ref = 1;
    obj->u.arr->prev = 0;
    obj->weak = 0;
    obj->type = WE_ARR;
    return true;
}

static bool we_arr_copy(struct we_obj *obj, struct we_arr *arr)
{
    if (arr)
    {
        obj->u.arr = arr;
        obj->type = WE_ARR;
        we_obj_acquire(obj);
        return true;
    }
    return we_arr(obj);
}

static uint32_t we_arr_len(struct we_obj *a)
{
    return a->u.arr->items;
}

static uint32_t we_arr_size(struct we_obj *a)
{
    return a->u.arr->size;
}

static uint32_t we_arr_ref(struct we_obj *a)
{
    return a->u.arr->ref;
}

static bool we_arr_equal(struct we_obj *a, struct we_obj *b)
{
    return (a->u.arr == b->u.arr);
}

static uint32_t we_arr_hash(struct we_obj *a)
{
    return (uintptr_t)a->u.arr;
}

static bool we_arr_set(struct we_obj *, struct we_obj *, struct we_obj *);
static bool we_arr_get(struct we_obj *, struct we_obj *, struct we_obj *);
static bool we_arr_resize(struct we_obj *, uint32_t);

static struct we_obj *we_tab_acquire(struct we_obj *a)
{
    if (!a->weak) a->u.tab->ref++;
    return a;
}

static void we_tab_release(struct we_obj *a)
{
    struct we_tab *tab = a->u.tab;

    if (!a->weak && --tab->ref == 0)
    {
        uint32_t i;
        for (i = 0; i < tab->size; i++)
        {
            we_obj_release(&tab->data[i].key);
            we_obj_release(&tab->data[i].val);
        }
        we_obj_release(&tab->meta);
        we_free(tab->data);
        we_free(tab);
    }
}

static bool we_tab(struct we_obj *obj)
{
    if (!(obj->u.tab = we_malloc(sizeof(*obj->u.tab)))) return false;
    we_nil(&obj->u.tab->meta);
    obj->u.tab->items = 0;
    obj->u.tab->size = 0;
    obj->u.tab->data = 0;
    obj->u.tab->ref = 1;
    obj->weak = 0;
    obj->type = WE_TAB;
    return true;
}

static bool we_tab_copy(struct we_obj *obj, struct we_tab *tab)
{
    if (tab)
    {
        obj->u.tab = tab;
        obj->type = WE_TAB;
        we_obj_acquire(obj);
        return true;
    }
    return we_tab(obj);
}

static uint32_t we_tab_len(struct we_obj *a)
{
    return a->u.tab->items;
}

static uint32_t we_tab_size(struct we_obj *a)
{
    return a->u.tab->size;
}

static uint32_t we_tab_ref(struct we_obj *a)
{
    return a->u.tab->ref;
}

static bool we_tab_equal(struct we_obj *a, struct we_obj *b)
{
    return (a->u.tab == b->u.tab);
}

static uint32_t we_tab_hash(struct we_obj *a)
{
    return (uintptr_t)a->u.tab;
}

static bool we_tab_set(struct we_obj *, struct we_obj *, struct we_obj *);
static bool we_tab_get(struct we_obj *, struct we_obj *, struct we_obj *);
static bool we_tab_resize(struct we_obj *, uint32_t);

/* Release the object */
static void (*we_release[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_release,
    [WE_NUM] = we_num_release,
    [WE_BUF] = we_buf_release,
    [WE_TAB] = we_tab_release,
    [WE_ARR] = we_arr_release};
/* Acquire the object */
static struct we_obj *(*we_acquire[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_acquire,
    [WE_NUM] = we_num_acquire,
    [WE_BUF] = we_buf_acquire,
    [WE_TAB] = we_tab_acquire,
    [WE_ARR] = we_arr_acquire};
/* Return the length of the object */
static uint32_t (*_we_len[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_len,
    [WE_NUM] = we_num_len,
    [WE_BUF] = we_buf_len,
    [WE_TAB] = we_tab_len,
    [WE_ARR] = we_arr_len};
/* Return the size of the object */
static uint32_t (*we_size[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_size,
    [WE_NUM] = we_num_size,
    [WE_BUF] = we_buf_size,
    [WE_TAB] = we_tab_size,
    [WE_ARR] = we_arr_size};
/* Return the reference count of the object */
static uint32_t (*we_ref[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_ref,
    [WE_NUM] = we_num_ref,
    [WE_BUF] = we_buf_ref,
    [WE_TAB] = we_tab_ref,
    [WE_ARR] = we_arr_ref};
/* Return the hash of the object */
static uint32_t (*we_hash[5])(struct we_obj *) = {
    [WE_NIL] = we_nil_hash,
    [WE_NUM] = we_num_hash,
    [WE_BUF] = we_buf_hash,
    [WE_TAB] = we_tab_hash,
    [WE_ARR] = we_arr_hash};
/* Return true if 2 object are equal */
static bool (*we_equal[5])(struct we_obj *, struct we_obj *) = {
    [WE_NIL] = we_nil_equal,
    [WE_NUM] = we_num_equal,
    [WE_BUF] = we_buf_equal,
    [WE_TAB] = we_tab_equal,
    [WE_ARR] = we_arr_equal};

/* Set a value */
static bool (*_we_set[5])(struct we_obj *, struct we_obj *, struct we_obj *) =
        {[WE_NIL] = NULL, [WE_NUM] = NULL, [WE_BUF] = NULL, [WE_TAB] = we_tab_set, [WE_ARR] = we_arr_set};
/* Get a value */
static bool (*_we_get[5])(struct we_obj *, struct we_obj *, struct we_obj *) =
        {[WE_NIL] = NULL, [WE_NUM] = NULL, [WE_BUF] = NULL, [WE_TAB] = we_tab_get, [WE_ARR] = we_arr_get};

static bool (*we_resize[5])(struct we_obj *, uint32_t) =
        {[WE_NIL] = NULL, [WE_NUM] = NULL, [WE_BUF] = NULL, [WE_TAB] = we_tab_resize, [WE_ARR] = we_arr_resize};

static inline struct we_obj *we_obj_acquire(struct we_obj *obj)
{
    return we_acquire[obj->type](obj);
}

static inline void we_obj_release(struct we_obj *obj)
{
    we_release[obj->type](obj);
}

static inline uint32_t we_obj_hash(struct we_obj *obj)
{
    return we_hash[obj->type](obj);
}

static inline bool we_obj_equal(struct we_obj *a, struct we_obj *b)
{
    if (a->type != b->type) return false;
    return we_equal[a->type](a, b);
}

static inline bool we_obj_set(struct we_obj *obj, struct we_obj *key, struct we_obj *val)
{
    if (obj->type < WE_TAB) return false;
    return _we_set[obj->type](obj, key, val);
}

static inline bool we_obj_get(struct we_obj *obj, struct we_obj *key, struct we_obj *def)
{
    if (obj->type < WE_TAB) return false;
    return _we_get[obj->type](obj, key, def);
}

static inline bool we_obj_resize(struct we_obj *obj, uint32_t size)
{
    if (obj->type < WE_TAB) return false;
    return we_resize[obj->type](obj, size);
}

static struct we_arr *we_gc_init(struct we_obj *meta)
{
    struct we_obj nil = {.type = WE_NIL};
    struct we_obj obj = {.type = WE_NUM, .u.val = 0};
    if (meta->type == WE_TAB)
    {
        if (we_obj_get(meta, &obj, &nil))
        {
            if (obj.type == WE_ARR)
            {
                if (we_arr_resize(&obj, 32))  // fixme
                    return obj.u.arr;
            }
        }
    }
    return NULL;
}

static inline bool we_gc_eval(struct we_arr *s, struct we_kvp *kvp)
{
    if (s)
    {
        s->data[s->items - 1] = *we_obj_acquire(&kvp->val);
        we_call(&s, NULL);
        return !!s->data[s->items - 1].u.val;
    }
    return false;
}

static inline void we_gc_done(struct we_arr *s)
{
    if (s)
    {
        struct we_obj obj = {.type = WE_ARR, .u.arr = s};
        we_obj_release(&obj);
    }
}

static struct we_kvp *we_kvp_find(struct we_tab *tab, struct we_obj *key, bool gc)
{
    struct we_arr *s = NULL;
    struct we_kvp *kvp, *res = NULL;
    uint32_t mask, idx, end;

    if (!tab->size) return NULL;

    mask = tab->size - 1;
    idx = we_obj_hash(key) & mask;
    end = idx;

    if (gc)
    {
        s = we_gc_init(&tab->meta);
    }

    do
    {
        kvp = &tab->data[idx];
        if (kvp->key.type == WE_NIL)
        {
            if (kvp->val.type == WE_NIL)
            {
                if (res == NULL) res = kvp;
                break;
            }
            else if (res == NULL)
            {
                res = kvp;
            }
        }
        else if (we_gc_eval(s, kvp))
        {
            bool done = we_obj_equal(&kvp->key, key);
            we_obj_release(&kvp->key);
            we_obj_release(&kvp->val);
            we_nil(&kvp->key);
            we_num(&kvp->val, 0);
            tab->items--;
            if (done)
            {
                if (res == NULL) res = kvp;
                break;
            }
        }
        else if (we_obj_equal(&kvp->key, key))
        {
            res = kvp;
            break;
        }
        idx = (idx + 1) & mask;
    } while (idx != end);

    we_gc_done(s);
    return res;
}

static void *we_tab_rehash(struct we_obj *obj, struct we_kvp *new_data, uint32_t new_size, uint32_t num_item)
{
    uint32_t idx, old_size = obj->u.tab->size;
    struct we_kvp *old_data = obj->u.tab->data;
    obj->u.tab->data = new_data;
    obj->u.tab->size = new_size;
    obj->u.tab->items = 0;
    for (idx = 0; idx < old_size; idx++)
    {
        if (old_data[idx].key.type != WE_NIL)
        {
            if (obj->u.tab->items < num_item) we_tab_set(obj, &old_data[idx].key, &old_data[idx].val);
            we_obj_release(&old_data[idx].key);
            we_obj_release(&old_data[idx].val);
        }
    }
    return old_data;
}

static uint32_t clp2(uint32_t n)
{
    --n;
    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    return ++n;
}

static bool we_tab_resize(struct we_obj *obj, uint32_t num_item)
{
    struct we_kvp *data = NULL;
    uint32_t size = clp2(num_item);
    if (size && !(data = we_calloc(size, sizeof(*data)))) return false;
    we_free(we_tab_rehash(obj, data, size, num_item));
    return true;
}

static bool we_tab_capacity(struct we_obj *obj)
{
    struct we_kvp *data;
    uint32_t size = obj->u.tab->size;
    if (obj->u.tab->items + 1 < (size >> 1)) return true;
    size = !size ? 2 : (size << 2);
    if (!(data = we_calloc(size, sizeof(*data)))) return false;
    we_free(we_tab_rehash(obj, data, size, obj->u.tab->items));
    return true;
}

static void we_kvp_del(struct we_obj *obj, struct we_obj *key)
{
    struct we_kvp *kvp = we_kvp_find(obj->u.tab, key, false);
    if (kvp)
    {
        if (kvp->key.type != WE_NIL) obj->u.tab->items--;
        we_obj_release(&kvp->key);
        we_obj_release(&kvp->val);
        we_nil(&kvp->key);    /* magic marker */
        we_num(&kvp->val, 0); /* magic marker */
    }
}

/* Returns false iff storage cannot be allocated for the kvp, which
 * is an unrecoverable error condition. If the item is not found, but
 * space is available, then a we_free kvp entry is returned, and the
 * caller can decide whether to initialize it. This will not increase
 * the number of items in the table, but can increase its size.
 */
static struct we_kvp *we_kvp_get(struct we_obj *obj, struct we_obj *key)
{
    struct we_kvp *kvp = we_kvp_find(obj->u.tab, key, true);
    if (!kvp)
    {
        if (!we_tab_capacity(obj)) return NULL;
        kvp = we_kvp_find(obj->u.tab, key, false);
    }
    return kvp;
}

static struct we_kvp *we_kvp_swap(struct we_kvp *a, struct we_kvp *b)
{
    struct we_kvp tmp = *a;
    *a = *b;
    *b = tmp;
    return a;
}

static bool we_tab_set(struct we_obj *obj, struct we_obj *key, struct we_obj *val)
{
    struct we_kvp *kvp;

    assert(obj != key);
    assert(obj != val);

    if (val->type == WE_NIL)
    {
        we_kvp_del(obj, key);
        return true;
    }

    if (!(kvp = we_kvp_get(obj, key))) return false;

    assert(val->type);
    if (kvp->key.type == WE_NIL)
    {
        kvp->key = *we_obj_acquire(key);
        obj->u.tab->items++;
    }

    we_obj_release(&kvp->val);
    kvp->val = *we_obj_acquire(val);

    /* For positive numeric keys, guarantee that the key position matches the
     * index. This cannot guarantee order within the entire collection, but
     * does guarantee O(1) for integers in range. */
    if (key->type == WE_NUM)
    {
        int idx = we_obj_hash(key) & (obj->u.tab->size - 1);
        if (key->u.val == idx && kvp - obj->u.tab->data != idx)
        {
            we_kvp_swap(&obj->u.tab->data[idx], kvp);
        }
    }
    return true;
}

static bool we_dyncast(struct we_obj *obj, uint8_t type);

static bool we_tab_get(struct we_obj *obj, struct we_obj *key, struct we_obj *def)
{
    struct we_kvp *kvp;

    assert(obj->type == WE_TAB);
    assert(obj != key);

    if (!(kvp = we_kvp_find(obj->u.tab, key, true)))
    {
        if (!def->type)
        {
            we_obj_release(key);
            *key = *we_obj_acquire(def);
            return true;
        }
        else if (!we_tab_capacity(obj))
        {
            return false;
        }
        else
        {
            kvp = we_kvp_find(obj->u.tab, key, false);
        }
        assert(kvp);
    }

    /* true when an item doesn't exist */
    if (kvp->key.type == WE_NIL)
    {
        /* do not add nil values */
        if (def->type == WE_NIL)
        {
            we_obj_release(key);
            *key = *we_obj_acquire(def);
            return true;
        }

        kvp->val = *we_obj_acquire(def);
        kvp->key = *key;
        *key = *we_obj_acquire(&kvp->val);
        obj->u.tab->items++;
        return true;
    }
    we_obj_release(key);
    *key = *we_obj_acquire(&kvp->val);

    /* should generate type error here while this can still happen */
    if (def->type && kvp->val.type != (uint32_t)def->type)
    {
#ifndef NDEBUG
        printf(" *** type error *** could not convert %u -> %d\n", key->type, def->type);
#endif
        if (!we_dyncast(key, def->type))
        {
            return false;
        }
    }
    return true;
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static bool we_arr_resize(struct we_obj *obj, uint32_t new_size)
{
    uint32_t i = 0, move = min(new_size, obj->u.arr->size);
    struct we_obj *new_data = NULL;

    if (new_size)
    {
        /* noop */
        if (new_size == obj->u.arr->size) return true;
        /* change */
        if (!(new_data = we_calloc(new_size, sizeof(*new_data)))) return false;
        /* move old to new */
        for (; i < move; i++)
            new_data[i] = obj->u.arr->data[i];
    }

    /* shrink */
    for (; i < obj->u.arr->size; i++)
    {
        if (obj->u.arr->data[i].type)
        {
            we_obj_release(&obj->u.arr->data[i]);
            obj->u.arr->items--;
        }
    }

    we_free(obj->u.arr->data);
    obj->u.arr->data = new_data;
    obj->u.arr->size = new_size;
    return true;
}

static bool we_push(struct we_arr *s, struct we_obj *val)
{
    if (s->items + 2 >= s->size)
    {
        struct we_obj obj = {.type = WE_ARR, .u.arr = s};
        if (!we_arr_resize(&obj, clp2(s->size * 2 + 1))) return false;
    }
    s->data[s->items++] = *val;
    return true;
}

static bool we_arr_set(struct we_obj *obj, struct we_obj *key, struct we_obj *val)
{
    assert(obj->type == WE_ARR && key->type == WE_NUM);
    if (key->u.val < 0)
    {
        key->u.val += obj->u.arr->items;
        if (key->u.val < 0) key->u.val = obj->u.arr->items;
    }
    if (key->u.val >= obj->u.arr->size)
    {
        if (!we_arr_resize(obj, clp2(key->u.val * 2 + 1))) return false;
    }
    /* insert or delete */
    if (!obj->u.arr->data[key->u.val].type)
    {
        if (val->type != WE_NIL) obj->u.arr->items++;
        /* upsert or delete */
    }
    else
    {
        we_obj_release(&obj->u.arr->data[key->u.val]);
        if (val->type == WE_NIL) obj->u.arr->items--;
    }
    obj->u.arr->data[key->u.val] = *we_obj_acquire(val);
    return true;
}

static bool we_arr_get(struct we_obj *obj, struct we_obj *key, struct we_obj *def)
{
    assert(obj->type == WE_ARR && key->type == WE_NUM);
    if (key->u.val < 0)
    {
        key->u.val += obj->u.arr->items;
        if (key->u.val < 0) key->u.val = obj->u.arr->items;
    }
    if (key->u.val >= obj->u.arr->size)
    {
        if (!def->type)
        {
            *key = *we_obj_acquire(def);
            return true;
        }
        else if (!we_arr_resize(obj, key->u.val * 2 + 1))
        {
            return false;
        }
    }
    /* Exists */
    if (obj->u.arr->data[key->u.val].type)
    {
        assert(!def->type || obj->u.arr->data[key->u.val].type == def->type);
        /* Does not exist but has a type. Insert. */
    }
    else if (def->type)
    {
        obj->u.arr->data[key->u.val] = *we_obj_acquire(def);
        obj->u.arr->items++;
    }
    *key = *we_obj_acquire(&obj->u.arr->data[key->u.val]);
    return true;
}

static int we_op_res(struct we_arr **arr, void *arg)
{
    struct we_obj obj = {.type = WE_ARR, .u.arr = *arr};
    if (!we_arr_resize(&obj, 32)) return ENOMEM;
    return -we_call(arr, arg);
}

/**
 * Extensions (WE_OP_EXT)
 *
 * An extension should return 0 to continue execution of the program. Positive
 * return values are treated as the posix errno. These errors will be
 * propagated to the host program.
 */
static int (*we_ext[256])(struct we_arr *, void *) = {};

/**
 * Internal Functions
 */

/* Return the object type identifier */
static inline void we_op_tid(struct we_obj *a)
{
    uint8_t type = a->type;
    we_obj_release(a);
    we_num(a, type);
}

/* Return the object identity (buf & tab only) */
static inline void we_op_oid(struct we_obj *a)
{
    int64_t id = a->type > 1 ? a->u.val : 0;
    we_obj_release(a);
    we_num(a, id);
}

/* Return the object offset */
static inline void we_op_off(struct we_obj *a)
{
    uint32_t off = a->type == WE_BUF ? a->off : 0;
    we_obj_release(a);
    we_num(a, off);
}

/* Return the length the object */
static inline void we_op_len(struct we_obj *a)
{
    uint32_t len = _we_len[a->type](a);
    we_obj_release(a);
    we_num(a, len);
}

/* Return the object reference count */
static inline void we_op_ref(struct we_obj *a)
{
    uint32_t ref = we_ref[a->type](a) - 1;
    we_obj_release(a);
    we_num(a, ref);
}

/* Return the size of the object */
static inline void we_op_size(struct we_obj *a)
{
    uint32_t size = we_size[a->type](a);
    we_obj_release(a);
    we_num(a, size);
}

/* Return the integer representing the character */
static inline void we_op_ord(struct we_obj *a)
{
    assert(a->type == WE_BUF);
    uint8_t val = 0;
    if (we_buf_len(a)) val = a->u.buf->data[a->off];
    we_obj_release(a);
    we_num(a, val);
}

/* Return the character represented by the integer value */
static inline bool we_op_chr(struct we_obj *a)
{
    assert(a->type == WE_NUM);
    char c = (char)a->u.val;
    if (!we_buf_dup(a, 1, &c)) return false;
    return true;
}

static uint32_t we_ref_add(struct we_obj *ref, uintptr_t val)
{
    struct we_obj k = {.type = WE_NUM, .u = {.val = val}};
    struct we_obj v = {.type = WE_NUM};
    we_obj_get(ref, &k, &v);
    v = k;
    k.u.val = val;
    v.u.val += 1;
    we_obj_set(ref, &k, &v);
    return v.u.val;
}

static uint32_t we_ref_del(struct we_obj *ref, uintptr_t val)
{
    struct we_obj k = {.type = WE_NUM, .u = {.val = val}};
    struct we_obj v = {.type = WE_NUM};
    we_obj_get(ref, &k, &v);
    v = k;
    k.u.val = val;
    --v.u.val;
    we_obj_set(ref, &k, &v);
    return v.u.val;
}

/* Return the buffer size required to stringify the object */
static uint32_t we_stringify_len(struct we_obj *obj, struct we_obj *ref)
{
    uint32_t i, n = 0, len = 0;
    struct we_tab *tab;
    struct we_arr *arr;
    switch (obj->type)
    {
        case WE_NIL:
            len = 3;
            break;
        case WE_NUM:
            len = snprintf(0, 0, "%jd", (intmax_t)obj->u.val);
            break;
        case WE_BUF:
            len = we_buf_len(obj) + 2; /* quotes */
            break;
        case WE_TAB:
            len++;
            tab = obj->u.tab;
            if (we_ref_add(ref, (uintptr_t)tab) == 1)
            {
                for (i = 0; i < tab->size; i++)
                {
                    if (tab->data[i].key.type != WE_NIL)
                    {
                        if (n++ != 0) len += 2;
                        len += we_stringify_len(&tab->data[i].key, ref);
                        len += 2;
                        len += we_stringify_len(&tab->data[i].val, ref);
                    }
                }
            }
            else
            {
                len += 3;
            }
            len++;
            we_ref_del(ref, (uintptr_t)tab);
            break;
        case WE_ARR:
            len++;
            arr = obj->u.arr;
            if (we_ref_add(ref, (uintptr_t)arr) == 1)
            {
                for (i = 0; i < arr->size; i++)
                {
                    if (arr->data[i].type != WE_NIL)
                    {
                        if (n++ != 0) len += 2;
                        len += we_stringify_len(&arr->data[i], ref);
                    }
                }
            }
            else
            {
                len += 3;
            }
            len++;
            we_ref_del(ref, (uintptr_t)arr);
            break;
    }
    return len;
}

/* Convert any object to a string */
static bool we_op_str(struct we_obj *obj, struct we_obj *buf, struct we_obj *ref)
{
    uint32_t i, n = 0, num = 0;
    struct we_tab *tab;
    struct we_arr *arr;
    struct we_obj tmp = {.type = WE_NIL}, tmp2 = {.type = WE_NIL};
    if (!buf)
    {
        if (obj->type == WE_BUF) return true;
        assert(!ref);
        if (!we_tab(&tmp2)) return false;
        ref = &tmp2;
        if (!we_buf_dup(&tmp, we_stringify_len(obj, ref) + 1, NULL))
        {
            we_obj_release(ref);
            return false;
        }
        buf = &tmp;
        buf->len = 0;
    }
    switch (obj->type)
    {
        case WE_NIL:
            buf->len += snprintf((char *)buf->u.buf->data + buf->len, we_buf_size(buf) - buf->len, "nil");
            break;
        case WE_NUM:
            buf->len += snprintf(
                    (char *)buf->u.buf->data + buf->len,
                    we_buf_size(buf) - buf->len,
                    "%jd",
                    (intmax_t)obj->u.val);
            break;
        case WE_BUF:
            buf->len += snprintf(
                    (char *)buf->u.buf->data + buf->len,
                    we_buf_size(buf) - buf->len,
                    "\"%.*s\"",
                    we_buf_len(obj),
                    (char *)we_buf_data(obj));
            break;
        case WE_TAB:
            buf->u.buf->data[buf->len++] = '{';
            tab = obj->u.tab;
            if (we_ref_add(ref, (uintptr_t)tab) == 1)
            {
                for (i = 0; i < tab->size; i++)
                {
                    if (tab->data[i].key.type != WE_NIL)
                    {
                        if (n++ != 0)
                        {
                            buf->u.buf->data[buf->len++] = ',';
                            buf->u.buf->data[buf->len++] = ' ';
                        }
                        we_op_str(&tab->data[i].key, buf, ref);
                        buf->u.buf->data[buf->len++] = ':';
                        buf->u.buf->data[buf->len++] = ' ';
                        we_op_str(&tab->data[i].val, buf, ref);
                    }
                }
            }
            else
            {
                buf->u.buf->data[buf->len++] = '.';
                buf->u.buf->data[buf->len++] = '.';
                buf->u.buf->data[buf->len++] = '.';
            }
            buf->u.buf->data[buf->len++] = '}';
            we_ref_del(ref, (uintptr_t)tab);
            break;
        case WE_ARR:
            buf->u.buf->data[buf->len++] = '[';
            arr = obj->u.arr;
            num = arr->items;
            if (we_ref_add(ref, (uintptr_t)arr) == 1)
            {
                for (i = 0; i < arr->size; i++)
                {
                    if (!num) break;
                    if (arr->data[i].type != WE_NIL)
                    {
                        if (n++ != 0)
                        {
                            buf->u.buf->data[buf->len++] = ',';
                            buf->u.buf->data[buf->len++] = ' ';
                        }
                        we_op_str(&arr->data[i], buf, ref);
                        --num;
                    }
                }
            }
            else
            {
                buf->u.buf->data[buf->len++] = '.';
                buf->u.buf->data[buf->len++] = '.';
                buf->u.buf->data[buf->len++] = '.';
            }
            buf->u.buf->data[buf->len++] = ']';
            we_ref_del(ref, (uintptr_t)arr);
            break;
    }
    if (tmp.type != WE_NIL)
    {
        we_obj_release(obj);
        *obj = *buf;
        we_obj_release(ref);
    }
    return true;
}

/* Convert buf to int */
static inline bool we_op_int(struct we_obj *a)
{
    if (a->type == WE_BUF)
    {
        const char *s = we_buf_data(a);
        int n = we_buf_len(a), b = 10, sb = 1;
        int64_t rv = 0;

        if (n && *s == '-')
        {
            sb = -1;
            s++;
            n--;
        }
        while (n && *s)
        {
            char ch = *s++;
            n--;
            if (ch >= '0' && ch <= '9')
                ch -= '0';
            else if (ch >= 'A' && ch < 'A' + b - 10)
                ch -= 'A' - 10;
            else if (ch >= 'a' && ch < 'a' + b - 10)
                ch -= 'a' - 10;
            else
                break;
            rv *= b;
            rv += ch;
        }
        we_obj_release(a);
        we_num(a, sb * rv);
    }
    assert(a->type == WE_NUM);
    return true;
}

static bool we_dyncast(struct we_obj *obj, uint8_t type)
{
    if (obj->type == type) return true;
    switch (obj->type)
    {
        case WE_NIL:
            we_obj_release(obj);
            break;
        case WE_NUM:
            if (type == WE_BUF) return we_op_str(obj, 0, 0);
            break;
        case WE_BUF:
            if (type == WE_NUM) return we_op_int(obj);
            break;
        case WE_TAB:
            if (type == WE_BUF) return we_op_str(obj, 0, 0);
            break;
        default:
            break;
    }
    we_nil(obj);
    return false;
}

/* [a,b] -> ab */
static bool we_op_tie(struct we_obj *a, struct we_obj *s)
{
    uint32_t c, i, n, z, off, len;
    bool res = true;
    struct we_obj obj = {.type = WE_NIL};

    n = we_arr_len(a);
    z = we_arr_size(a);
    for (i = 0, c = 0, len = 0; i < z; i++)
    {
        if (!we_op_str(&a->u.arr->data[i], 0, 0)) return false;
        len += we_buf_len(&a->u.arr->data[i]);
        if (++c == n) break;
        len += we_buf_len(s);
    }
    /* empty */
    if (n == 0)
    {
        res = we_buf_dup(&obj, 0, NULL);
        /* single (and already resident) */
    }
    else if (n == 1 && (a->u.arr->data[i].u.buf->data == a->u.arr->data[i].u.buf->mem))
    {
        obj = *we_obj_acquire(&a->u.arr->data[i]);
        /* allocate */
    }
    else if (!(res = we_buf_dup(&obj, len, NULL)))
    {
        ;
        /* copy n */
    }
    else
    {
        for (i = 0, c = 0, off = 0; i < z; i++)
        {
            __builtin_memcpy(&obj.u.buf->data[off], we_buf_data(&a->u.arr->data[i]), we_buf_len(&a->u.arr->data[i]));
            off += we_buf_len(&a->u.arr->data[i]);
            if (++c == n) break;
            __builtin_memcpy(&obj.u.buf->data[off], we_buf_data(s), we_buf_len(s));
            off += we_buf_len(s);
        }
    }
    we_obj_release(a);
    we_obj_release(s);
    *a = obj;
    return res;
}

/* a,b -> ab */
static bool we_op_cat(struct we_obj *a, struct we_obj *b)
{
    struct we_buf *buf;
    uint32_t len, off = 0;

    assert(a->type == WE_BUF && b->type == WE_BUF);
    if (a->type != WE_BUF || b->type != WE_BUF) return false;

    len = we_buf_len(a) + we_buf_len(b);
    /* return the empty string */
    if (len == 0)
    {
        we_obj_release(b);
        return true;
        /* lhs is empty */
    }
    else if (!we_buf_len(a))
    {
        we_obj_release(a);
        *a = *b;
        return true;
        /* rhs is empty */
    }
    else if (!we_buf_len(b))
    {
        we_obj_release(b);
        return true;
        /* lhs, rhs refer to the same buffer and concatenation is linear */
    }
    else if (a->u.buf == b->u.buf && a->off + a->len == b->off)
    {
        a->len += b->len;
        we_obj_release(b);
        return true;
    }
    else if (!(buf = we_malloc(sizeof(*buf) + len)))
    {
        return false;
    }
    else
    {
        buf->ref = 1;
        buf->len = len;
        buf->data = buf->mem;
        buf->managed = 0;
        __builtin_memcpy(buf->data, we_buf_data(a), we_buf_len(a));
        __builtin_memcpy(buf->data + we_buf_len(a), we_buf_data(b), we_buf_len(b));
    }
    we_obj_release(a);
    we_obj_release(b);
    a->u.buf = buf;
    a->len = len;
    a->off = off;
    a->type = WE_BUF;
    return true;
}

/* memcmp */
static void we_op_com(struct we_obj *a, struct we_obj *b)
{
    assert(a->type == WE_BUF);
    assert(b->type == WE_BUF);
    int res = we_buf_cmp(a, b);
    we_obj_release(a);
    we_obj_release(b);
    we_num(a, res);
}

static bool we_op_sel(struct we_obj *obj, struct we_obj *start, struct we_obj *stop, struct we_obj *step)
{
    int i = start->u.val;
    int e = stop->u.val;
    int s = step->u.val;
    int len, p = 0;
    struct we_obj r;

    if (!obj->len)
    {
        return true;
    }

    if (!start->type)
    {
        if (step->u.val < 0) i = obj->len - 1;
    }
    else if (start->u.val >= 0)
    {
        i = min(i, (int)obj->len);
    }
    else if ((i += obj->len) < 0)
    {
        i = 0;
    }

    if (!stop->type)
    {
        if (step->u.val < 0)
            e = -1;
        else
            e = obj->len;
    }
    else if (stop->u.val >= 0)
    {
        e = min(e, (int)obj->len);
    }
    else
    {
        e += obj->len;
    }

    /* If the start is larger than the stop, then the step must
     * be a negative value since we are moving right to left. */
    if (i > e)
    {
        if (s < 0)
        {
            len = (i - e);
            if (!(we_buf_dup(&r, (len + (abs(s) - 1)) / abs(s), NULL)))
            {
                obj->len = 0;
            }
            else
            {
                while (i > e)
                {
                    r.u.buf->data[p++] = *((uint8_t *)we_buf_data(obj) + i);
                    i += s;
                }
                we_obj_release(obj);
                *obj = r;
            }
        }
        else
        {
            obj->len = 0;
        }
        /* If the start is less than the stop, then the step must
         * be a positive value since we are moving left to right. */
    }
    else
    {
        if (s > 0)
        {
            len = (e - i);
            if (s == 1)
            {
                obj->off += i;
                obj->len = len;
            }
            else if (!we_buf_dup(&r, (len + (s - 1)) / s, NULL))
            {
                obj->len = 0;
            }
            else
            {
                while (i < e)
                {
                    r.u.buf->data[p++] = *((uint8_t *)we_buf_data(obj) + i);
                    i += s;
                }
                we_obj_release(obj);
                *obj = r;
            }
        }
        else
        {
            obj->len = 0;
        }
    }
    we_obj_release(step);
    we_obj_release(stop);
    we_obj_release(start);
    return true;
}

static bool we_op_gmt(struct we_obj *obj)
{
    if (obj->u.tab->meta.type == WE_NIL)
    {
        if (!we_tab(&obj->u.tab->meta)) return false;
    }
    we_obj_release(obj);
    *obj = *we_tab_acquire(&obj->u.tab->meta);
    return true;
}

static bool we_op_smt(struct we_obj *obj, struct we_obj *meta)
{
    we_obj_release(&obj->u.tab->meta);
    obj->u.tab->meta = *we_tab_acquire(meta);
    return true;
}

static void we_op_rsz(struct we_obj *obj, struct we_obj *num)
{
    bool res = false;
    assert(num->type == WE_NUM);
    if (num->u.val >= 0) res = we_obj_resize(obj, num->u.val);
    we_obj_release(obj);
    we_num(obj, res ? 1 : 0);
}

static void we_op_idx(struct we_obj *obj, struct we_obj *idx)
{
    assert(obj->type >= WE_TAB);
    assert(idx->type <= WE_NUM);

    if (obj->type == WE_TAB)
    {
        if (obj->u.tab->items)
        {
            int i = idx->type == WE_NIL ? 0 : idx->u.val + 1;
            int n = obj->u.tab->size;
            while (i < n)
            {
                if (obj->u.tab->data[i].key.type)
                {
                    we_num(idx, i);
                    return;
                }
                i++;
            }
        }
    }
    else
    {
        if (obj->u.arr->items)
        {
            int i = idx->type == WE_NIL ? 0 : idx->u.val + 1;
            int n = obj->u.arr->size;
            while (i < n)
            {
                if (obj->u.arr->data[i].type)
                {
                    we_num(idx, i);
                    return;
                }
                i++;
            }
        }
    }
    we_nil(idx);
}

static bool we_op_val(struct we_obj *obj, struct we_obj *idx, struct we_obj *dst)
{
    assert(obj->type >= WE_TAB);
    assert(idx->type <= WE_NUM);
    assert(dst->type == WE_ARR);

    struct we_obj key, val, nil;
    int i = idx->u.val, n;

    if (obj->type == WE_TAB)
        n = obj->u.tab->size;
    else
        n = obj->u.arr->size;

    if (idx->type == WE_NIL) return true;

    we_num(&key, 0);
    we_num(&val, 1);

    if (i < 0 || i >= n)
    {
        we_nil(&nil);
        return we_obj_set(dst, &key, &nil) && we_obj_set(dst, &val, &nil);
    }
    else if (obj->type == WE_TAB)
    {
        return we_obj_set(dst, &key, &obj->u.tab->data[i].key) && we_obj_set(dst, &val, &obj->u.tab->data[i].val);
    }
    else
    {
        return we_obj_set(dst, &key, idx) && we_obj_set(dst, &val, &obj->u.arr->data[i]);
    }
}

static int we_op_rusage(struct we_obj *obj)
{
    struct we_obj key, val;
    struct we_mem_pool *mem_pool;
    struct rts_memstats *stats;

    if (!we_tab(obj)) return -ENOMEM;

    mem_pool = we_get_mem_pool_mgr();
    if (!mem_pool->initialized) return 0;

    stats = &mem_pool->we_mem_pool.stats;

    we_buf_ptr(&key, 10, "curr_alloc");
    we_num(&val, stats->curr_alloc);
    we_tab_set(obj, &key, &val);
    we_obj_release(&key);
    we_obj_release(&val);

    we_buf_ptr(&key, 10, "peak_alloc");
    we_num(&val, stats->peak_alloc);
    we_tab_set(obj, &key, &val);
    we_obj_release(&key);
    we_obj_release(&val);

    we_buf_ptr(&key, 10, "fail_alloc");
    we_num(&val, stats->fail_alloc);
    we_tab_set(obj, &key, &val);
    we_obj_release(&key);
    we_obj_release(&val);

    return 0;
}

static inline int16_t read16(const unsigned char *src)
{
    return ((int16_t)src[0] << 8) | ((int16_t)src[1]);
}

static inline int32_t read32(const unsigned char *src)
{
    return ((int32_t)src[0] << 24) | ((int32_t)src[1] << 16) | ((int32_t)src[2] << 8) | ((int32_t)src[3]);
}

static inline int64_t read64(const unsigned char *src)
{
    return ((int64_t)src[0] << 56) | ((int64_t)src[1] << 48) | ((int64_t)src[2] << 40) | ((int64_t)src[3] << 32)
           | ((int64_t)src[4] << 24) | ((int64_t)src[5] << 16) | ((int64_t)src[6] << 8) | ((int64_t)src[7]);
}

int we_call(struct we_arr **state, void *arg)
{
    const uint8_t *prog = (*state)->data[0].u.buf->data + (*state)->data[0].off;
    struct we_obj *s = (*state)->data;
    int sp = (*state)->items;
    int pc = 0;
    int err;

    static const void *dispatch[] = {
        &&we_op_nil, &&we_op_num, &&we_op_buf, &&we_op_tab, &&we_op_arr, &&we_op_get, &&we_op_set, &&we_op_mov,
        &&we_op_pop, &&we_op_add, &&we_op_sub, &&we_op_mul, &&we_op_cmp, &&we_op_div, &&we_op_mod, &&we_op_and,
        &&we_op_ior, &&we_op_xor, &&we_op_shl, &&we_op_shr, &&we_op_eql, &&we_op_hlt, &&we_op_jmp, &&we_op_brz,
        &&we_op_ext, &&we_op_tid, &&we_op_len, &&we_op_ref, &&we_op_siz, &&we_op_ord, &&we_op_chr, &&we_op_int,
        &&we_op_str, &&we_op_cat, &&we_op_com, &&we_op_sel, &&we_op_idx, &&we_op_val, &&we_op_tie, &&we_op_off,
        &&we_op_oid, &&we_op_csp, &&we_op_bin, &&we_op_gmt, &&we_op_smt, &&we_op_lbf, &&we_op_lbt, &&we_op_rsz,
        &&we_op_wea, &&we_op_res, &&we_op_eva, &&we_op_rus};

    if (!s[0].len)
    {
        return -EINVAL;
    }
#define DISPATCH() goto *dispatch[prog[pc++] & 0x3f]
#define RETURN(err) \
    { \
        (*state)->items = sp; \
        return -(err); \
    }
    DISPATCH();
/* Memory */
we_op_nil:
    we_nil(&s[sp++]);
    DISPATCH();
we_op_num:
    switch ((prog[pc - 1] >> 6) & 0x3)
    {
        case 0:
            we_num(&s[sp++], (int8_t)prog[pc++]);
            DISPATCH();
        case 1:
            we_num(&s[sp++], read16(&prog[pc]));
            pc += 2;
            DISPATCH();
        case 2:
            we_num(&s[sp++], read32(&prog[pc]));
            pc += 4;
            DISPATCH();
        case 3:
            we_num(&s[sp++], read64(&prog[pc]));
            pc += 8;
            DISPATCH();
    }
we_op_buf: {
    int16_t len = read16(&prog[pc]);
    pc += 2;
    we_buf(&s[sp++], len, pc, &s[0]);
    pc += len;
    DISPATCH();
}
we_op_tab:
    if (!we_tab(&s[sp++])) RETURN(ENOMEM);
    DISPATCH();
we_op_arr:
    if (!we_arr(&s[sp++])) RETURN(ENOMEM);
    DISPATCH();
we_op_get: {
    if (!we_obj_get(&s[prog[pc++]], &s[sp - 2], &s[sp - 1])) return (EINVAL);
    we_obj_release(&s[--sp]);
    DISPATCH();
}
we_op_mov:
    assert(prog[pc] <= sp);
    we_obj_acquire(&s[prog[pc]]);
    s[sp++] = s[prog[pc++]];
    DISPATCH();
we_op_pop:
    we_obj_release(&s[prog[pc]]);
    s[prog[pc++]] = s[--sp];
    s[sp].type = 0;
    DISPATCH();
we_op_set:
    we_obj_set(&s[prog[pc++]], &s[sp - 2], &s[sp - 1]);
    we_obj_release(&s[sp - 1]);
    we_obj_release(&s[sp - 2]);
    sp -= 2;
    DISPATCH();
/* Arithmetic Logic */
we_op_add:
    s[sp - 2].u.val += s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_sub:
    s[sp - 2].u.val -= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_mul:
    s[sp - 2].u.val *= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_cmp:
    s[sp - 2].u.val = s[sp - 2].u.val < s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_div:
    if (s[sp - 1].u.val == 0) RETURN(ERANGE);
    s[sp - 2].u.val /= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_mod:
    if (s[sp - 1].u.val == 0) RETURN(ERANGE);
    s[sp - 2].u.val %= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_and:
    s[sp - 2].u.val &= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_ior:
    s[sp - 2].u.val |= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_xor:
    s[sp - 2].u.val ^= s[sp - 1].u.val;
    --sp;
    DISPATCH();
we_op_shl:
    s[sp - 2].u.bits <<= s[sp - 1].u.bits;
    --sp;
    DISPATCH();
we_op_shr:
    s[sp - 2].u.bits >>= s[sp - 1].u.bits;
    --sp;
    DISPATCH();
we_op_eql:
    s[sp - 2].u.val = s[sp - 2].u.val == s[sp - 1].u.val;
    --sp;
    DISPATCH();
/* Branch Instructions */
we_op_hlt:
    s[0].off += pc;
    s[0].len -= pc;
    (*state)->items = sp;
    /* yield (restore caller) */
    if (s[0].len)
    {
        return 0;
    }
    if ((*state)->prev)
    {
        struct we_arr *prev = (*state)->prev;
        (*state)->prev = NULL;
        *state = prev;
    }
    else
    {
        /* return to host */
        return 0;
    }
    /* resume (restore caller) */
    s = (*state)->data;
    prog = s[0].u.buf->data + s[0].off;
    sp = (*state)->items;
    pc = 0;
    DISPATCH();
we_op_jmp:
    pc += read32(&prog[pc]);
    DISPATCH();
we_op_brz:
    assert(s[sp - 1].type <= WE_NUM);
    pc += (!s[--sp].u.val) ? read32(&prog[pc]) : 4;
    DISPATCH();
we_op_ext:
    (*state)->items = sp;
    if ((err = we_ext[prog[pc++]](s[sp - 1].u.arr, arg)))
    {
        /* items is volatile because externals can modify */
        sp = (*state)->items;
        RETURN(err);
    }
    DISPATCH();
/* Builtin Functions */
we_op_tid:
    we_op_tid(&s[sp - 1]);
    DISPATCH();
we_op_len:
    we_op_len(&s[sp - 1]);
    DISPATCH();
we_op_ref:
    we_op_ref(&s[sp - 1]);
    DISPATCH();
we_op_siz:
    we_op_size(&s[sp - 1]);
    DISPATCH();
we_op_ord:
    we_op_ord(&s[sp - 1]);
    DISPATCH();
we_op_chr:
    if (!we_op_chr(&s[sp - 1])) RETURN(ENOMEM);
    DISPATCH();
we_op_str:
    if (!we_op_str(&s[sp - 1], 0, 0)) RETURN(ENOMEM);
    DISPATCH();
we_op_int:
    if (!we_op_int(&s[sp - 1])) RETURN(ENOMEM);
    DISPATCH();
we_op_tie:
    if (!we_op_tie(&s[sp - 2], &s[sp - 1])) RETURN(EINVAL);
    --sp;
    DISPATCH();
we_op_off:
    we_op_off(&s[sp - 1]);
    DISPATCH();
we_op_oid:
    we_op_oid(&s[sp - 1]);
    DISPATCH();
we_op_cat:
    if (!we_op_cat(&s[sp - 2], &s[sp - 1])) RETURN(ENOMEM);
    --sp;
    DISPATCH();
we_op_com:
    we_op_com(&s[sp - 2], &s[sp - 1]);
    --sp;
    DISPATCH();
we_op_sel:
    we_op_sel(&s[sp - 4], &s[sp - 3], &s[sp - 2], &s[sp - 1]);
    sp -= 3;
    DISPATCH();
we_op_idx:
    we_op_idx(&s[prog[pc]], &s[prog[pc + 1]]);
    pc += 2;
    DISPATCH();
we_op_val:
    if (!we_op_val(&s[prog[pc]], &s[prog[pc + 1]], &s[prog[pc + 2]])) RETURN(ENOMEM);
    pc += 3;
    DISPATCH();
we_op_csp:
    assert(prog[pc] == sp);
    pc++;
    we_num(&s[sp], sp);
    sp++;
    DISPATCH();
we_op_bin: {
    uint32_t ip = pc + 4;
    pc += read32(&prog[pc]);
    int16_t len = read16(&prog[pc]);
    pc += 2;
    we_buf(&s[sp++], len, pc, &s[0]);
    pc = ip;
    DISPATCH();
}
we_op_gmt:
    if (!we_op_gmt(&s[sp - 1])) RETURN(ENOMEM);
    DISPATCH();
we_op_smt:
    if (!we_op_smt(&s[sp - 2], &s[sp - 1])) RETURN(ENOMEM);
    we_obj_release(&s[sp - 1]);
    --sp;
    DISPATCH();
we_op_lbf:
    we_num(&s[sp++], 0);
    DISPATCH();
we_op_lbt:
    we_num(&s[sp++], 1);
    DISPATCH();
we_op_rsz:
    we_op_rsz(&s[sp - 2], &s[sp - 1]);
    --sp;
    DISPATCH();
we_op_wea:
    we_obj_release(&s[sp - 1]);
    s[sp - 1].weak = 1;
    DISPATCH();
we_op_res:
    if ((err = we_op_res(&s[sp - 1].u.arr, arg))) RETURN(err);
    DISPATCH();
we_op_eva:
    /* Fixme */
    we_arr_resize(&s[sp - 1], 32);

    s[sp - 1].u.arr->prev = *state;
    assert((int)(*state)->size > sp);

    s[0].off += pc;
    s[0].len -= pc;
    (*state)->items = sp;

    // Setup callee
    *state = s[sp - 1].u.arr;
    s = (*state)->data;
    sp = (*state)->items;
    pc = 0;
    prog = s[0].u.buf->data + s[0].off;

    DISPATCH();
we_op_rus:
    if ((err = we_op_rusage(&s[sp++])))
        RETURN(err);
    DISPATCH();
}

#define XLATE_REG_OR_EINVAL(s, r) \
    if (r < 0 && ((r += s->items) < 0)) \
    { \
        return -EINVAL; \
    } \
    else if (r >= (int)s->items) \
    { \
        return -EINVAL; \
    }

int we_pushnil(struct we_arr *s)
{
    struct we_obj val = {.type = WE_NIL};
    if (!we_push(s, &val)) return -ENOMEM;
    return we_top(s);
}

int we_pushnum(struct we_arr *s, int64_t num)
{
    struct we_obj val = {.type = WE_NUM, .u.val = num};
    if (!we_push(s, &val)) return -ENOMEM;
    return we_top(s);
}

int we_pushstr(struct we_arr *s, size_t len, const char *str)
{
    struct we_obj val;
    if (!we_buf_dup(&val, len, str)) return -ENOMEM;
    if (we_push(s, &val)) return we_top(s);
    we_obj_release(&val);
    return -ENOMEM;
}

int we_pushbuf(struct we_arr *s, size_t len, void *buf)
{
    struct we_obj val;
    if (!we_buf_ptr(&val, len, buf)) return -ENOMEM;
    if (we_push(s, &val)) return we_top(s);
    we_obj_release(&val);
    return -ENOMEM;
}

int we_pushtab(struct we_arr *s, void *tab)
{
    struct we_obj val;
    if (!we_tab_copy(&val, tab)) return -ENOMEM;
    if (we_push(s, &val)) return we_top(s);
    we_obj_release(&val);
    return -ENOMEM;
}

int we_pusharr(struct we_arr *s, void *arr)
{
    struct we_obj val;
    if (!we_arr_copy(&val, arr)) return -ENOMEM;
    if (we_push(s, &val)) return we_top(s);
    we_obj_release(&val);
    return -ENOMEM;
}

int we_trim(struct we_arr *s, int reg, int n)
{
    if (s->data[reg].type != WE_BUF)
    {
        return -EINVAL;
    }
    else if ((uint32_t)abs(n) > s->data[reg].len)
    {
        s->data[reg].len = 0;
    }
    else if (n > 0)
    {
        s->data[reg].off += n;
        s->data[reg].len -= n;
    }
    else
    {
        s->data[reg].len += n;
    }
    return s->data[reg].len;
}

int we_popr(struct we_arr *s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    we_obj_release(&s->data[reg]);
    s->data[reg] = s->data[--s->items];
    s->data[s->items].type = WE_NIL;
    return 0;
}

int we_pop(struct we_arr *s)
{
    if (!s->items) return -ENOMEM;
    we_obj_release(&s->data[--s->items]);
    s->data[s->items].type = WE_NIL;
    return 0;
}

/**
 * Initialize the WE memory pool
 *
 * This function allocates and initializes the global memory pool used by WE.
 * The pool is only initialized once, subsequent calls are ignored.
 *
 * @param mempool_size Size of the memory pool in bytes
 */
void we_init_mempool(size_t mempool_size)
{
    struct we_mem_pool *mem_pool;
    uint8_t *mem;

    mem_pool = we_get_mem_pool_mgr();
    if (mem_pool->initialized) return;

    mem = CALLOC(1, mempool_size);

    rts_pool_init(&mem_pool->we_mem_pool, mem, mempool_size);
    LOGI("%s: Successfully initialized WE memory pool with size %zu bytes", __func__, mempool_size);
    mem_pool->initialized = true;
}

int we_create(struct we_arr **s, size_t nr, size_t mempool_size)
{
    struct we_obj obj;

    if (nr > 255) return -EINVAL;

    if (kconfig_enabled(CONFIG_WE_AGENT_MEMPOOL)) {
        we_init_mempool(mempool_size);
    }

    if (we_arr(&obj))
    {
        if (we_arr_resize(&obj, nr))
        {
            *s = obj.u.arr;
            return 0;
        }
        we_obj_release(&obj);
    }
    return -ENOMEM;
}

int we_destroy(struct we_arr *s)
{
    struct we_obj obj = {.type = WE_ARR, .u.arr = s};
    we_obj_release(&obj);
    return 0;
}

int we_move(struct we_arr *s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    s->data[s->items] = *we_obj_acquire(&s->data[reg]);
    s->items++;
    return 0;
}

int we_get(struct we_arr *s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    struct we_obj def = {.type = WE_NIL};
    return we_obj_get(&s->data[reg], &s->data[s->items - 1], &def);
}

int we_set(struct we_arr *s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    if (!we_obj_set(&s->data[reg], &s->data[s->items - 2], &s->data[s->items - 1])) return -1;
    we_pop(s);
    we_pop(s);
    return 0;
}

int we_type(struct we_arr *s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    return s->data[reg].type;
}

int we_read(struct we_arr *s, int reg, we_type_t type, void *val)
{
    XLATE_REG_OR_EINVAL(s, reg);
    if (s->data[reg].type > WE_ARR) return -EINVAL;
    switch (type)
    {
        case WE_NIL:
            if (val) *(void **)val = NULL;
            return 0;
        case WE_NUM:
            if (val) *(int64_t *)val = s->data[reg].u.val;
            return sizeof(s->data[reg].u.val);
        case WE_BUF:
            if (val) *(uint8_t **)val = s->data[reg].u.buf->data + s->data[reg].off;
            return s->data[reg].len;
        case WE_TAB:
            if (val) *(void **)val = s->data[reg].u.tab;
            return s->data[reg].u.tab->items;
        case WE_ARR:
            if (val) *(void **)val = s->data[reg].u.arr;
            return s->data[reg].u.arr->items;
        default:
            return -1;
    }
}

int we_top(struct we_arr *s)
{
    return s->items - 1;
}

int we_setup(int id, int (*ext)(struct we_arr *, void *))
{
    if (id < 1 || id > 255) return -EINVAL;
    we_ext[id] = ext;
    return 0;
}

int we_clone(struct we_arr **clone, struct we_arr *from)
{
    struct we_obj state;
    if (we_arr(&state))
    {
        if (we_arr_resize(&state, from->size))
        {
            while (state.u.arr->items < from->items)
            {
                state.u.arr->data[state.u.arr->items] = *we_obj_acquire(&from->data[state.u.arr->items]);
                state.u.arr->items++;
            }
            *clone = state.u.arr;
            return 0;
        }
        we_obj_release(&state);
    }
    return -ENOMEM;
}

int we_hold(struct we_arr *s, int reg, void **val)
{
    XLATE_REG_OR_EINVAL(s, reg);
    if (s->data[reg].type != WE_BUF) return -EINVAL;
    *val = we_buf_acquire(&s->data[reg])->u.buf;
    return s->data[reg].u.buf->ref;
}

int we_sync(void *val)
{
    int sync = 0;
    struct we_obj obj = {.type = WE_BUF, .u.buf = val};

    if (obj.u.buf->ref > 1 && obj.u.buf->len)
    {
        void *dst = we_malloc(obj.u.buf->len);
        if (!dst) return -ENOMEM;
        memcpy(dst, obj.u.buf->data, obj.u.buf->len);
        obj.u.buf->data = dst;
        obj.u.buf->managed = 1;
        sync = 1;
    }
    we_buf_release(&obj);
    return sync;
}

int we_dup(we_state_t s, int reg)
{
    struct we_obj arg[] = {{.type = WE_ARR, .u.arr = s}, {.type = WE_NUM, .u.val = s->items}, s->data[reg]};
    if (!we_arr_set(&arg[0], &arg[1], &arg[2])) return -ENOMEM;
    we_obj_release(&arg[1]);
    we_obj_release(&arg[2]);
    return we_top(s);
}

int we_len(we_state_t s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    return _we_len[s->data[reg].type](&s->data[reg]);
}

int we_next(we_state_t s, int reg)
{
    XLATE_REG_OR_EINVAL(s, reg);
    we_op_idx(&s->data[reg], &s->data[s->items - 1]);
    if (s->data[s->items - 1].type != WE_NIL)
    {
        if (we_pusharr(s, NULL) >= 0)
        {
            we_op_val(&s->data[reg], &s->data[s->items - 2], &s->data[s->items - 1]);
            return s->data[s->items - 1].type;
        }
    }
    return 0;
}
