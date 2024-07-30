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

// generated from ds_set_str.tmpl.c
// set of strings

#include "ds_set_str.h"
#include "ds_tree.h"
#include "os.h"
#include "osa_assert.h"

#define DS_SET_STR_MAGIC 0x53455453

typedef struct ds_set_str_node ds_set_str_node_t;
struct ds_set_str_node
{
    ds_tree_node_t ds_node;
    char *key;
};

struct ds_set_str
{
    uint32_t init;
    ds_tree_t ds_tree;
};

static inline bool _ds_set_str_valid(ds_set_str_t *set)
{
    return set && set->init == DS_SET_STR_MAGIC;
}

static inline void _ds_set_str_assert(ds_set_str_t *set)
{
    ASSERT(_ds_set_str_valid(set), "Invalid set %p", set);
}

void _ds_set_str_free_node(ds_set_str_node_t *node)
{
    if (!node) return;
    FREE(node->key);
    free(node);
}

bool _ds_set_str_delete_node(ds_set_str_t *set, ds_set_str_node_t *node)
{
    _ds_set_str_assert(set);
    ds_tree_remove(&set->ds_tree, node);
    _ds_set_str_free_node(node);
    return true;
}

/* public */
ds_set_str_t *ds_set_str_new()
{
    ds_set_str_t *set = CALLOC(sizeof(*set), 1);
    set->ds_tree = (ds_tree_t)DS_TREE_INIT(ds_str_cmp, ds_set_str_node_t, ds_node);
    set->init = DS_SET_STR_MAGIC;
    return set;
}

bool ds_set_str_empty(ds_set_str_t *set)
{
    _ds_set_str_assert(set);
    return ds_tree_is_empty(&set->ds_tree);
}

int ds_set_str_size(ds_set_str_t *set)
{
    _ds_set_str_assert(set);
    return ds_tree_len(&set->ds_tree);
}

bool ds_set_str_find(ds_set_str_t *set, const char *key)
{
    _ds_set_str_assert(set);
    if (key == NULL) return false;
    ds_set_str_node_t *node = ds_tree_find(&set->ds_tree, key);
    return node != NULL;
}

/* insert adds a new key */
/* if it already exists it will not overwrite it */
bool ds_set_str_insert(ds_set_str_t *set, const char *key)
{
    _ds_set_str_assert(set);
    if (key == NULL) return false;
    ds_set_str_node_t *node = ds_tree_find(&set->ds_tree, key);
    if (node) return false;
    node = CALLOC(sizeof(*node), 1);
    node->key = STRDUP(key);
    ds_tree_insert(&set->ds_tree, node, node->key);
    return true;
}

bool ds_set_str_remove(ds_set_str_t *set, const char *key)
{
    _ds_set_str_assert(set);
    if (key == NULL) return false;
    ds_set_str_node_t *node = ds_tree_find(&set->ds_tree, key);
    if (!node) return false;
    return _ds_set_str_delete_node(set, node);
}

/* clear all keys */
bool ds_set_str_clear(ds_set_str_t *set)
{
    _ds_set_str_assert(set);
    ds_set_str_node_t *node = NULL;
    ds_set_str_node_t *tmp = NULL;
    ds_tree_foreach_safe (&set->ds_tree, node, tmp)
    {
        _ds_set_str_delete_node(set, node);
    }
    return true;
}

/* delete the set container */
bool ds_set_str_delete(ds_set_str_t **pset)
{
    ASSERT(pset, "pset");
    if (!*pset) return false;
    ds_set_str_clear(*pset);
    (*pset)->init = 0;
    free(*pset);
    *pset = NULL;
    return true;
}

bool _ds_set_str_fill_iter(ds_set_str_iter_t *iter, ds_set_str_node_t *node)
{
    if (node)
    {
        iter->key = node->key;
        iter->end = false;
    }
    else
    {
        iter->key = 0;
        iter->end = true;
    }
    return !iter->end;
}

bool ds_set_str_first(ds_set_str_t *set, ds_set_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    _ds_set_str_assert(set);
    MEMZERO(*iter);
    iter->_set = set;
    ds_set_str_node_t *node = ds_tree_ifirst(&iter->_ds_iter, &set->ds_tree);
    return _ds_set_str_fill_iter(iter, node);
}

bool ds_set_str_next(ds_set_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_set_str_t *set = iter->_set;
    _ds_set_str_assert(set);
    ds_set_str_node_t *node = ds_tree_inext(&iter->_ds_iter);
    return _ds_set_str_fill_iter(iter, node);
}

ds_set_str_node_t *_ds_set_str_iter_curr(ds_set_str_iter_t *iter)
{
    return NODE_TO_CONT(iter->_ds_iter.oti_curr, iter->_ds_iter.oti_root->ot_cof);
}

bool ds_set_str_iter_remove(ds_set_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_set_str_t *set = iter->_set;
    _ds_set_str_assert(set);
    if (iter->end) return false;
    ds_set_str_node_t *node = _ds_set_str_iter_curr(iter);
    ds_tree_iremove(&iter->_ds_iter);
    _ds_set_str_free_node(node);
    iter->key = 0;
    return true;
}

/* insert a variable length array, */
/* log duplicates if log_msg specified */
/* returns false if any key is duplicate otherwise true */
/* all keys except duplicates will be inserted */
bool ds_set_str_insert_vl_array_log(
        ds_set_str_t *set,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        log_severity_t log_level,
        char *log_msg)
{
    int i;
    bool ret = true;
    for (i = 0; i < num; i++)
    {
        if (!ds_set_str_insert(set, keys[i]))
        {
            ret = false;
            if (log_msg)
            {
                LOG_SEVERITY(log_level, "%s: %s", log_msg, keys[i]);
            }
        }
    }
    return ret;
}

bool ds_set_str_insert_vl_array(ds_set_str_t *set, int num, int keys_size, typeof(*(char *)NULL) keys[num][keys_size])
{
    return ds_set_str_insert_vl_array_log(set, num, keys_size, keys, 0, NULL);
}

int ds_set_str_compare(ds_set_str_t *a, ds_set_str_t *b)
{
    int sa = ds_set_str_size(a);
    int sb = ds_set_str_size(b);
    if (sa != sb) return sa - sb;
    ds_set_str_iter_t iter;
    ds_set_str_foreach(a, iter)
    {
        if (!ds_set_str_find(b, iter.key)) return 1;
    }
    return 0;
}
