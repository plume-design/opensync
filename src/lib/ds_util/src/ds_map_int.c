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

// generated from ds_map_int.tmpl.c
// map of ints

#include "ds_map_int.h"
#include "ds_tree.h"
#include "os.h"
#include "osa_assert.h"

#define DS_MAP_INT_MAGIC 0x4d415049

typedef struct ds_map_int_node ds_map_int_node_t;
struct ds_map_int_node
{
    ds_tree_node_t ds_node;
    int key;
    int val;
};

struct ds_map_int
{
    uint32_t init;
    ds_tree_t ds_tree;
};

static inline bool _ds_map_int_valid(ds_map_int_t *map)
{
    return map && map->init == DS_MAP_INT_MAGIC;
}

static inline void _ds_map_int_assert(ds_map_int_t *map)
{
    ASSERT(_ds_map_int_valid(map), "Invalid map %p", map);
}

void _ds_map_int_free_node(ds_map_int_node_t *node)
{
    if (!node) return;
    free(node);
}

bool _ds_map_int_delete_node(ds_map_int_t *map, ds_map_int_node_t *node)
{
    _ds_map_int_assert(map);
    ds_tree_remove(&map->ds_tree, node);
    _ds_map_int_free_node(node);
    return true;
}

/* public */
ds_map_int_t *ds_map_int_new()
{
    ds_map_int_t *map = CALLOC(sizeof(*map), 1);
    map->ds_tree = (ds_tree_t)DS_TREE_INIT(ds_int_cmp, ds_map_int_node_t, ds_node);
    map->init = DS_MAP_INT_MAGIC;
    return map;
}

bool ds_map_int_empty(ds_map_int_t *map)
{
    _ds_map_int_assert(map);
    return ds_tree_is_empty(&map->ds_tree);
}

int ds_map_int_size(ds_map_int_t *map)
{
    _ds_map_int_assert(map);
    return ds_tree_len(&map->ds_tree);
}

bool ds_map_int_find(ds_map_int_t *map, const int key, int *pval)
{
    _ds_map_int_assert(map);
    if (false) return false;
    ds_map_int_node_t *node = ds_tree_find(&map->ds_tree, &key);
    if (pval) *pval = node ? node->val : 0;
    return node != NULL;
}

/* insert adds a new key value */
/* if it already exists it will not overwrite it */
bool ds_map_int_insert(ds_map_int_t *map, const int key, const int val)
{
    _ds_map_int_assert(map);
    if (false) return false;
    if (false) return false;
    ds_map_int_node_t *node = ds_tree_find(&map->ds_tree, &key);
    if (node) return false;
    node = CALLOC(sizeof(*node), 1);
    node->key = key;
    node->val = val;
    ds_tree_insert(&map->ds_tree, node, &node->key);
    return true;
}

bool ds_map_int_remove(ds_map_int_t *map, const int key)
{
    _ds_map_int_assert(map);
    if (false) return false;
    ds_map_int_node_t *node = ds_tree_find(&map->ds_tree, &key);
    if (!node) return false;
    return _ds_map_int_delete_node(map, node);
}

/* set will either insert new or update existing key/value. */
/* returns status of change: */
/* - true if key/value changed */
/* - false if no change */
bool ds_map_int_set(ds_map_int_t *map, const int key, const int val)
{
    _ds_map_int_assert(map);
    ds_map_int_node_t *node = ds_tree_find(&map->ds_tree, &key);
    bool remove = false;
    if (node)
    {
        /* key exists, update it */
        if (!remove)
        {
            /* check for value change */
            if ((node->val > val ? 1 : node->val < val ? -1 : 0) == 0)
            {
                /* value is the same, do nothing */
                return false;
            }
            else
            {
                /* value is different */
                node->val = val;
                return true;
            }
        }
        else
        {
            /* remove key */
            _ds_map_int_delete_node(map, node);
            return true;
        }
    }
    else
    {
        /* key does not exist */
        if (!remove)
        {
            /* insert new value */
            ds_map_int_insert(map, key, val);
            return true;
        }
        else
        {
            /* do nothing */
            return false;
        }
    }
}

/* clear all key/map values */
bool ds_map_int_clear(ds_map_int_t *map)
{
    _ds_map_int_assert(map);
    ds_map_int_node_t *node = NULL;
    ds_map_int_node_t *tmp = NULL;
    ds_tree_foreach_safe (&map->ds_tree, node, tmp)
    {
        _ds_map_int_delete_node(map, node);
    }
    return true;
}

/* delete the map container */
bool ds_map_int_delete(ds_map_int_t **pmap)
{
    ASSERT(pmap, "pmap");
    if (!*pmap) return false;
    ds_map_int_clear(*pmap);
    (*pmap)->init = 0;
    free(*pmap);
    *pmap = NULL;
    return true;
}

bool _ds_map_int_fill_iter(ds_map_int_iter_t *iter, ds_map_int_node_t *node)
{
    if (node)
    {
        iter->key = node->key;
        iter->val = node->val;
        iter->end = false;
    }
    else
    {
        iter->key = 0;
        iter->val = 0;
        iter->end = true;
    }
    return !iter->end;
}

bool ds_map_int_first(ds_map_int_t *map, ds_map_int_iter_t *iter)
{
    ASSERT(iter, "iter");
    _ds_map_int_assert(map);
    MEMZERO(*iter);
    iter->_map = map;
    ds_map_int_node_t *node = ds_tree_ifirst(&iter->_ds_iter, &map->ds_tree);
    return _ds_map_int_fill_iter(iter, node);
}

bool ds_map_int_next(ds_map_int_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_map_int_t *map = iter->_map;
    _ds_map_int_assert(map);
    ds_map_int_node_t *node = ds_tree_inext(&iter->_ds_iter);
    return _ds_map_int_fill_iter(iter, node);
}

ds_map_int_node_t *_ds_map_int_iter_curr(ds_map_int_iter_t *iter)
{
    return NODE_TO_CONT(iter->_ds_iter.oti_curr, iter->_ds_iter.oti_root->ot_cof);
}

bool ds_map_int_iter_remove(ds_map_int_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_map_int_t *map = iter->_map;
    _ds_map_int_assert(map);
    if (iter->end) return false;
    ds_map_int_node_t *node = _ds_map_int_iter_curr(iter);
    ds_tree_iremove(&iter->_ds_iter);
    _ds_map_int_free_node(node);
    iter->key = 0;
    iter->val = 0;
    return true;
}

/* insert a variable length array, */
/* log duplicates if log_msg specified */
/* returns false if any key is duplicate otherwise true */
/* all keys except duplicates will be inserted */
bool ds_map_int_insert_vl_array_log(
        ds_map_int_t *map,
        int num,
        int keys[num],
        int values[num],
        log_severity_t log_level,
        char *log_msg)
{
    int i;
    bool ret = true;
    for (i = 0; i < num; i++)
    {
        if (!ds_map_int_insert(map, keys[i], values[i]))
        {
            ret = false;
            if (log_msg)
            {
                LOG_SEVERITY(log_level, "%s: %d=%d", log_msg, keys[i], values[i]);
            }
        }
    }
    return ret;
}

bool ds_map_int_insert_vl_array(ds_map_int_t *map, int num, int keys[num], int values[num])
{
    return ds_map_int_insert_vl_array_log(map, num, keys, values, 0, NULL);
}

/* compare two maps, return 0 if equal */
int ds_map_int_compare(ds_map_int_t *a, ds_map_int_t *b)
{
    int sa = ds_map_int_size(a);
    int sb = ds_map_int_size(b);
    int b_val;
    int cmp;
    if (sa != sb) return sa - sb;
    ds_map_int_iter_t iter;
    ds_map_int_foreach(a, iter)
    {
        if (!ds_map_int_find(b, iter.key, &b_val)) return 1;
        cmp = (iter.val > b_val ? 1 : iter.val < b_val ? -1 : 0);
        if (cmp != 0) return cmp;
    }
    return 0;
}
