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

#include "ds_tree.h"
#include "os.h"
#include "osa_assert.h"

#if 0
/* associative map with managed allocation */
/* built on top of ds_tree */
/* template implementation */
#endif

#define DS_TMPL_VALUE(N) N

#define DS_TMPL_NOP(N)

#define DS_TMPL_SCALAR_CMP(A, B) (A > B ? 1 : A < B ? -1 : 0)

#define DS_TMPL_VLA_ARG_DECL_1D(ARG_T, NAME, NUM) ARG_T NAME[NUM]
#define DS_TMPL_VLA_ARG_DECL_2D(ARG_T, NAME, NUM) int NAME##_size, typeof(*(ARG_T)NULL) NAME[NUM][NAME##_size]

#define DS_TMPL_VLA_ARG_1D(NAME) NAME
#define DS_TMPL_VLA_ARG_2D(NAME) NAME##_size, NAME

#define DS_TMPL_ADDR_PTR(A) A
#define DS_TMPL_ADDR_VAL(A) &A

#define DS_TMPL_VALID_PTR(A) A != NULL
#define DS_TMPL_VALID_VAL(A) true

#define DS_TMPL_INVALID_PTR(A) A == NULL
#define DS_TMPL_INVALID_VAL(A) false

#define DS_TMPL_IS_PTR_PTR(A) A
#define DS_TMPL_IS_PTR_VAL(A)
#define DS_TMPL_IS_VAL_VAL(A) A
#define DS_TMPL_IS_VAL_PTR(A)

#define DS_MAP_TEMPLATE_IMPL(                                                                      \
        TYPE,        /* map type suffix */                                                         \
        INIT_MAGIC,  /* int id for integrity check */                                              \
        KEY_T,       /* key type */                                                                \
        VAL_T,       /* val type */                                                                \
        KEY_STOR,    /* key storage type: VAL or PTR */                                            \
        VAL_STOR,    /* val storage type: VAL or PTR */                                            \
        KEY_COPY,    /* key copy func */                                                           \
        VAL_COPY,    /* val copy func */                                                           \
        KEY_FREE,    /* key free func */                                                           \
        VAL_FREE,    /* val free func */                                                           \
        KEY_CMP,     /* key compare func */                                                        \
        VAL_CMP,     /* val compare func */                                                        \
        KEY_FMT,     /* key printf fmt */                                                          \
        VAL_FMT,     /* val printf fmt */                                                          \
        KEY_VLA_DIM, /* key variable length array dimension: 1D or 2D */                           \
        VAL_VLA_DIM  /* val variable length array dimension: 1D or 2D */                           \
        /*-------------------------------------------------------------------------------------*/) \
                                                                                                   \
    typedef struct ds_map_##TYPE##_node ds_map_##TYPE##_node_t;                                    \
                                                                                                   \
    struct ds_map_##TYPE##_node                                                                    \
    {                                                                                              \
        ds_tree_node_t ds_node;                                                                    \
        KEY_T key;                                                                                 \
        VAL_T val;                                                                                 \
    };                                                                                             \
                                                                                                   \
    struct ds_map_##TYPE                                                                           \
    {                                                                                              \
        uint32_t init;                                                                             \
        ds_tree_t ds_tree;                                                                         \
    };                                                                                             \
                                                                                                   \
    static inline bool _ds_map_##TYPE##_valid(ds_map_##TYPE##_t *map)                              \
    {                                                                                              \
        return map && map->init == INIT_MAGIC;                                                     \
    }                                                                                              \
                                                                                                   \
    static inline void _ds_map_##TYPE##_assert(ds_map_##TYPE##_t *map)                             \
    {                                                                                              \
        ASSERT(_ds_map_##TYPE##_valid(map), "Invalid map %p", map);                                \
    }                                                                                              \
                                                                                                   \
    void _ds_map_##TYPE##_free_node(ds_map_##TYPE##_node_t *node)                                  \
    {                                                                                              \
        if (!node) return;                                                                         \
        KEY_FREE(node->key);                                                                       \
        VAL_FREE(node->val);                                                                       \
        free(node);                                                                                \
    }                                                                                              \
                                                                                                   \
    bool _ds_map_##TYPE##_delete_node(ds_map_##TYPE##_t *map, ds_map_##TYPE##_node_t *node)        \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        ds_tree_remove(&map->ds_tree, node);                                                       \
        _ds_map_##TYPE##_free_node(node);                                                          \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* public */                                                                                   \
                                                                                                   \
    ds_map_##TYPE##_t *ds_map_##TYPE##_new()                                                       \
    {                                                                                              \
        ds_map_##TYPE##_t *map = CALLOC(sizeof(*map), 1);                                          \
        map->ds_tree = (ds_tree_t)DS_TREE_INIT(KEY_CMP, ds_map_##TYPE##_node_t, ds_node);          \
        map->init = INIT_MAGIC;                                                                    \
        return map;                                                                                \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_empty(ds_map_##TYPE##_t *map)                                             \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        return ds_tree_is_empty(&map->ds_tree);                                                    \
    }                                                                                              \
                                                                                                   \
    int ds_map_##TYPE##_size(ds_map_##TYPE##_t *map)                                               \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        return ds_tree_len(&map->ds_tree);                                                         \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_find(ds_map_##TYPE##_t *map, const KEY_T key, VAL_T *pval)                \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        ds_map_##TYPE##_node_t *node = ds_tree_find(&map->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        if (pval) *pval = node ? node->val : 0;                                                    \
        return node != NULL;                                                                       \
    }                                                                                              \
                                                                                                   \
    /* insert adds a new key value */                                                              \
    /* if it already exists it will not overwrite it */                                            \
    bool ds_map_##TYPE##_insert(ds_map_##TYPE##_t *map, const KEY_T key, const VAL_T val)          \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        if (DS_TMPL_INVALID_##VAL_STOR(val)) return false;                                         \
        ds_map_##TYPE##_node_t *node = ds_tree_find(&map->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        if (node) return false;                                                                    \
        node = CALLOC(sizeof(*node), 1);                                                           \
        node->key = KEY_COPY(key);                                                                 \
        node->val = VAL_COPY(val);                                                                 \
        ds_tree_insert(&map->ds_tree, node, DS_TMPL_ADDR_##KEY_STOR(node->key));                   \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_remove(ds_map_##TYPE##_t *map, const KEY_T key)                           \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        ds_map_##TYPE##_node_t *node = ds_tree_find(&map->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        if (!node) return false;                                                                   \
        return _ds_map_##TYPE##_delete_node(map, node);                                            \
    }                                                                                              \
                                                                                                   \
    /* set will either insert new or update existing key/value. */                                 \
    DS_TMPL_IS_PTR_##VAL_STOR(/* using val=NULL will remove the key if it exists */);              \
    /* returns status of change: */                                                                \
    /* - true if key/value changed */                                                              \
    /* - false if no change */                                                                     \
    bool ds_map_##TYPE##_set(ds_map_##TYPE##_t *map, const KEY_T key, const VAL_T val)             \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        ds_map_##TYPE##_node_t *node = ds_tree_find(&map->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        bool remove = DS_TMPL_INVALID_##VAL_STOR(val);                                             \
        if (node)                                                                                  \
        {                                                                                          \
            /* key exists, update it */                                                            \
            if (!remove)                                                                           \
            {                                                                                      \
                /* check for value change */                                                       \
                if (VAL_CMP(node->val, val) == 0)                                                  \
                {                                                                                  \
                    /* value is the same, do nothing */                                            \
                    return false;                                                                  \
                }                                                                                  \
                else                                                                               \
                {                                                                                  \
                    /* value is different */                                                       \
                    VAL_FREE(node->val);                                                           \
                    node->val = VAL_COPY(val);                                                     \
                    return true;                                                                   \
                }                                                                                  \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                /* remove key */                                                                   \
                _ds_map_##TYPE##_delete_node(map, node);                                           \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            /* key does not exist */                                                               \
            if (!remove)                                                                           \
            {                                                                                      \
                /* insert new value */                                                             \
                ds_map_##TYPE##_insert(map, key, val);                                             \
                return true;                                                                       \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                /* do nothing */                                                                   \
                return false;                                                                      \
            }                                                                                      \
        }                                                                                          \
    }                                                                                              \
                                                                                                   \
    /* clear all key/map values */                                                                 \
    bool ds_map_##TYPE##_clear(ds_map_##TYPE##_t *map)                                             \
    {                                                                                              \
        _ds_map_##TYPE##_assert(map);                                                              \
        ds_map_##TYPE##_node_t *node = NULL;                                                       \
        ds_map_##TYPE##_node_t *tmp = NULL;                                                        \
        ds_tree_foreach_safe (&map->ds_tree, node, tmp)                                            \
        {                                                                                          \
            _ds_map_##TYPE##_delete_node(map, node);                                               \
        }                                                                                          \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* delete the map container */                                                                 \
    bool ds_map_##TYPE##_delete(ds_map_##TYPE##_t **pmap)                                          \
    {                                                                                              \
        ASSERT(pmap, "pmap");                                                                      \
        if (!*pmap) return false;                                                                  \
        ds_map_##TYPE##_clear(*pmap);                                                              \
        (*pmap)->init = 0;                                                                         \
        free(*pmap);                                                                               \
        *pmap = NULL;                                                                              \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    bool _ds_map_##TYPE##_fill_iter(ds_map_##TYPE##_iter_t *iter, ds_map_##TYPE##_node_t *node)    \
    {                                                                                              \
        if (node)                                                                                  \
        {                                                                                          \
            iter->key = node->key;                                                                 \
            iter->val = node->val;                                                                 \
            iter->end = false;                                                                     \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            iter->key = 0;                                                                         \
            iter->val = 0;                                                                         \
            iter->end = true;                                                                      \
        }                                                                                          \
        return !iter->end;                                                                         \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_first(ds_map_##TYPE##_t *map, ds_map_##TYPE##_iter_t *iter)               \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        _ds_map_##TYPE##_assert(map);                                                              \
        MEMZERO(*iter);                                                                            \
        iter->_map = map;                                                                          \
        ds_map_##TYPE##_node_t *node = ds_tree_ifirst(&iter->_ds_iter, &map->ds_tree);             \
        return _ds_map_##TYPE##_fill_iter(iter, node);                                             \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_next(ds_map_##TYPE##_iter_t *iter)                                        \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        ds_map_##TYPE##_t *map = iter->_map;                                                       \
        _ds_map_##TYPE##_assert(map);                                                              \
        ds_map_##TYPE##_node_t *node = ds_tree_inext(&iter->_ds_iter);                             \
        return _ds_map_##TYPE##_fill_iter(iter, node);                                             \
    }                                                                                              \
                                                                                                   \
    ds_map_##TYPE##_node_t *_ds_map_##TYPE##_iter_curr(ds_map_##TYPE##_iter_t *iter)               \
    {                                                                                              \
        return NODE_TO_CONT(iter->_ds_iter.oti_curr, iter->_ds_iter.oti_root->ot_cof);             \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_iter_remove(ds_map_##TYPE##_iter_t *iter)                                 \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        ds_map_##TYPE##_t *map = iter->_map;                                                       \
        _ds_map_##TYPE##_assert(map);                                                              \
        if (iter->end) return false;                                                               \
        ds_map_##TYPE##_node_t *node = _ds_map_##TYPE##_iter_curr(iter);                           \
        ds_tree_iremove(&iter->_ds_iter);                                                          \
        _ds_map_##TYPE##_free_node(node);                                                          \
        iter->key = 0;                                                                             \
        iter->val = 0;                                                                             \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* insert a variable length array, */                                                          \
    /* log duplicates if log_msg specified */                                                      \
    /* returns false if any key is duplicate otherwise true */                                     \
    /* all keys except duplicates will be inserted */                                              \
    bool ds_map_##TYPE##_insert_vl_array_log(                                                      \
            ds_map_##TYPE##_t *map,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num),                                  \
            DS_TMPL_VLA_ARG_DECL_##VAL_VLA_DIM(VAL_T, values, num),                                \
            log_severity_t log_level,                                                              \
            char *log_msg)                                                                         \
    {                                                                                              \
        int i;                                                                                     \
        bool ret = true;                                                                           \
        for (i = 0; i < num; i++)                                                                  \
        {                                                                                          \
            if (!ds_map_##TYPE##_insert(map, keys[i], values[i]))                                  \
            {                                                                                      \
                ret = false;                                                                       \
                if (log_msg)                                                                       \
                {                                                                                  \
                    LOG_SEVERITY(                                                                  \
                            log_level, /**/                                                        \
                            "%s: " KEY_FMT "=" VAL_FMT "",                                         \
                            log_msg,                                                               \
                            keys[i],                                                               \
                            values[i]);                                                            \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
        return ret;                                                                                \
    }                                                                                              \
                                                                                                   \
    bool ds_map_##TYPE##_insert_vl_array(                                                          \
            ds_map_##TYPE##_t *map,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num),                                  \
            DS_TMPL_VLA_ARG_DECL_##VAL_VLA_DIM(VAL_T, values, num))                                \
    {                                                                                              \
        return ds_map_##TYPE##_insert_vl_array_log(                                                \
                map,                                                                               \
                num,                                                                               \
                DS_TMPL_VLA_ARG_##KEY_VLA_DIM(keys),                                               \
                DS_TMPL_VLA_ARG_##VAL_VLA_DIM(values),                                             \
                0,                                                                                 \
                NULL);                                                                             \
    }                                                                                              \
                                                                                                   \
    /* compare two maps, return 0 if equal */                                                      \
    int ds_map_##TYPE##_compare(ds_map_##TYPE##_t *a, ds_map_##TYPE##_t *b)                        \
    {                                                                                              \
        int sa = ds_map_##TYPE##_size(a);                                                          \
        int sb = ds_map_##TYPE##_size(b);                                                          \
        VAL_T b_val;                                                                               \
        int cmp;                                                                                   \
                                                                                                   \
        if (sa != sb) return sa - sb;                                                              \
        ds_map_##TYPE##_iter_t iter;                                                               \
        ds_map_##TYPE##_foreach(a, iter)                                                           \
        {                                                                                          \
            if (!ds_map_##TYPE##_find(b, iter.key, &b_val)) return 1;                              \
            cmp = VAL_CMP(iter.val, b_val);                                                        \
            if (cmp != 0) return cmp;                                                              \
        }                                                                                          \
        return 0;                                                                                  \
    }
