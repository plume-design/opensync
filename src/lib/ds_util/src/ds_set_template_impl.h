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
/* set of keys with managed allocation */
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

#define DS_SET_TEMPLATE_IMPL(                                                                      \
        TYPE,       /* map type suffix */                                                          \
        INIT_MAGIC, /* int id for integrity check */                                               \
        KEY_T,      /* key type */                                                                 \
        KEY_STOR,   /* key storage type: VAL or PTR */                                             \
        KEY_COPY,   /* key copy func */                                                            \
        KEY_FREE,   /* key free func */                                                            \
        KEY_CMP,    /* key compare func */                                                         \
        KEY_FMT,    /* key printf fmt */                                                           \
        KEY_VLA_DIM /* key variable length array dimension: 1D or 2D */                            \
        /*-------------------------------------------------------------------------------------*/) \
                                                                                                   \
    typedef struct ds_set_##TYPE##_node ds_set_##TYPE##_node_t;                                    \
                                                                                                   \
    struct ds_set_##TYPE##_node                                                                    \
    {                                                                                              \
        ds_tree_node_t ds_node;                                                                    \
        KEY_T key;                                                                                 \
    };                                                                                             \
                                                                                                   \
    struct ds_set_##TYPE                                                                           \
    {                                                                                              \
        uint32_t init;                                                                             \
        ds_tree_t ds_tree;                                                                         \
    };                                                                                             \
                                                                                                   \
    static inline bool _ds_set_##TYPE##_valid(ds_set_##TYPE##_t *set)                              \
    {                                                                                              \
        return set && set->init == INIT_MAGIC;                                                     \
    }                                                                                              \
                                                                                                   \
    static inline void _ds_set_##TYPE##_assert(ds_set_##TYPE##_t *set)                             \
    {                                                                                              \
        ASSERT(_ds_set_##TYPE##_valid(set), "Invalid set %p", set);                                \
    }                                                                                              \
                                                                                                   \
    void _ds_set_##TYPE##_free_node(ds_set_##TYPE##_node_t *node)                                  \
    {                                                                                              \
        if (!node) return;                                                                         \
        KEY_FREE(node->key);                                                                       \
        free(node);                                                                                \
    }                                                                                              \
                                                                                                   \
    bool _ds_set_##TYPE##_delete_node(ds_set_##TYPE##_t *set, ds_set_##TYPE##_node_t *node)        \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        ds_tree_remove(&set->ds_tree, node);                                                       \
        _ds_set_##TYPE##_free_node(node);                                                          \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* public */                                                                                   \
                                                                                                   \
    ds_set_##TYPE##_t *ds_set_##TYPE##_new()                                                       \
    {                                                                                              \
        ds_set_##TYPE##_t *set = CALLOC(sizeof(*set), 1);                                          \
        set->ds_tree = (ds_tree_t)DS_TREE_INIT(KEY_CMP, ds_set_##TYPE##_node_t, ds_node);          \
        set->init = INIT_MAGIC;                                                                    \
        return set;                                                                                \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_empty(ds_set_##TYPE##_t *set)                                             \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        return ds_tree_is_empty(&set->ds_tree);                                                    \
    }                                                                                              \
                                                                                                   \
    int ds_set_##TYPE##_size(ds_set_##TYPE##_t *set)                                               \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        return ds_tree_len(&set->ds_tree);                                                         \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_find(ds_set_##TYPE##_t *set, const KEY_T key)                             \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        ds_set_##TYPE##_node_t *node = ds_tree_find(&set->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        return node != NULL;                                                                       \
    }                                                                                              \
                                                                                                   \
    /* insert adds a new key */                                                                    \
    /* if it already exists it will not overwrite it */                                            \
    bool ds_set_##TYPE##_insert(ds_set_##TYPE##_t *set, const KEY_T key)                           \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        ds_set_##TYPE##_node_t *node = ds_tree_find(&set->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        if (node) return false;                                                                    \
        node = CALLOC(sizeof(*node), 1);                                                           \
        node->key = KEY_COPY(key);                                                                 \
        ds_tree_insert(&set->ds_tree, node, DS_TMPL_ADDR_##KEY_STOR(node->key));                   \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_remove(ds_set_##TYPE##_t *set, const KEY_T key)                           \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        if (DS_TMPL_INVALID_##KEY_STOR(key)) return false;                                         \
        ds_set_##TYPE##_node_t *node = ds_tree_find(&set->ds_tree, DS_TMPL_ADDR_##KEY_STOR(key));  \
        if (!node) return false;                                                                   \
        return _ds_set_##TYPE##_delete_node(set, node);                                            \
    }                                                                                              \
                                                                                                   \
    /* clear all keys */                                                                           \
    bool ds_set_##TYPE##_clear(ds_set_##TYPE##_t *set)                                             \
    {                                                                                              \
        _ds_set_##TYPE##_assert(set);                                                              \
        ds_set_##TYPE##_node_t *node = NULL;                                                       \
        ds_set_##TYPE##_node_t *tmp = NULL;                                                        \
        ds_tree_foreach_safe (&set->ds_tree, node, tmp)                                            \
        {                                                                                          \
            _ds_set_##TYPE##_delete_node(set, node);                                               \
        }                                                                                          \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* delete the set container */                                                                 \
    bool ds_set_##TYPE##_delete(ds_set_##TYPE##_t **pset)                                          \
    {                                                                                              \
        ASSERT(pset, "pset");                                                                      \
        if (!*pset) return false;                                                                  \
        ds_set_##TYPE##_clear(*pset);                                                              \
        (*pset)->init = 0;                                                                         \
        free(*pset);                                                                               \
        *pset = NULL;                                                                              \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    bool _ds_set_##TYPE##_fill_iter(ds_set_##TYPE##_iter_t *iter, ds_set_##TYPE##_node_t *node)    \
    {                                                                                              \
        if (node)                                                                                  \
        {                                                                                          \
            iter->key = node->key;                                                                 \
            iter->end = false;                                                                     \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            iter->key = 0;                                                                         \
            iter->end = true;                                                                      \
        }                                                                                          \
        return !iter->end;                                                                         \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_first(ds_set_##TYPE##_t *set, ds_set_##TYPE##_iter_t *iter)               \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        _ds_set_##TYPE##_assert(set);                                                              \
        MEMZERO(*iter);                                                                            \
        iter->_set = set;                                                                          \
        ds_set_##TYPE##_node_t *node = ds_tree_ifirst(&iter->_ds_iter, &set->ds_tree);             \
        return _ds_set_##TYPE##_fill_iter(iter, node);                                             \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_next(ds_set_##TYPE##_iter_t *iter)                                        \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        ds_set_##TYPE##_t *set = iter->_set;                                                       \
        _ds_set_##TYPE##_assert(set);                                                              \
        ds_set_##TYPE##_node_t *node = ds_tree_inext(&iter->_ds_iter);                             \
        return _ds_set_##TYPE##_fill_iter(iter, node);                                             \
    }                                                                                              \
                                                                                                   \
    ds_set_##TYPE##_node_t *_ds_set_##TYPE##_iter_curr(ds_set_##TYPE##_iter_t *iter)               \
    {                                                                                              \
        return NODE_TO_CONT(iter->_ds_iter.oti_curr, iter->_ds_iter.oti_root->ot_cof);             \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_iter_remove(ds_set_##TYPE##_iter_t *iter)                                 \
    {                                                                                              \
        ASSERT(iter, "iter");                                                                      \
        ds_set_##TYPE##_t *set = iter->_set;                                                       \
        _ds_set_##TYPE##_assert(set);                                                              \
        if (iter->end) return false;                                                               \
        ds_set_##TYPE##_node_t *node = _ds_set_##TYPE##_iter_curr(iter);                           \
        ds_tree_iremove(&iter->_ds_iter);                                                          \
        _ds_set_##TYPE##_free_node(node);                                                          \
        iter->key = 0;                                                                             \
        return true;                                                                               \
    }                                                                                              \
                                                                                                   \
    /* insert a variable length array, */                                                          \
    /* log duplicates if log_msg specified */                                                      \
    /* returns false if any key is duplicate otherwise true */                                     \
    /* all keys except duplicates will be inserted */                                              \
    bool ds_set_##TYPE##_insert_vl_array_log(                                                      \
            ds_set_##TYPE##_t *set,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num),                                  \
            log_severity_t log_level,                                                              \
            char *log_msg)                                                                         \
    {                                                                                              \
        int i;                                                                                     \
        bool ret = true;                                                                           \
        for (i = 0; i < num; i++)                                                                  \
        {                                                                                          \
            if (!ds_set_##TYPE##_insert(set, keys[i]))                                             \
            {                                                                                      \
                ret = false;                                                                       \
                if (log_msg)                                                                       \
                {                                                                                  \
                    LOG_SEVERITY(log_level, "%s: " KEY_FMT, log_msg, keys[i]);                     \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
        return ret;                                                                                \
    }                                                                                              \
                                                                                                   \
    bool ds_set_##TYPE##_insert_vl_array(                                                          \
            ds_set_##TYPE##_t *set,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num))                                  \
    {                                                                                              \
        return ds_set_##TYPE##_insert_vl_array_log(                                                \
                set,                                                                               \
                num,                                                                               \
                DS_TMPL_VLA_ARG_##KEY_VLA_DIM(keys),                                               \
                0,                                                                                 \
                NULL /*       */);                                                                 \
    }                                                                                              \
                                                                                                   \
    int ds_set_##TYPE##_compare(ds_set_##TYPE##_t *a, ds_set_##TYPE##_t *b)                        \
    {                                                                                              \
        int sa = ds_set_##TYPE##_size(a);                                                          \
        int sb = ds_set_##TYPE##_size(b);                                                          \
                                                                                                   \
        if (sa != sb) return sa - sb;                                                              \
        ds_set_##TYPE##_iter_t iter;                                                               \
        ds_set_##TYPE##_foreach(a, iter)                                                           \
        {                                                                                          \
            if (!ds_set_##TYPE##_find(b, iter.key)) return 1;                                      \
        }                                                                                          \
        return 0;                                                                                  \
    }
