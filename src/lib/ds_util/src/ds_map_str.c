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

// generated from ds_map_str.tmpl.c
// map of strings

#include "ds_map_str.h"
#include "ds_tree.h"
#include "os.h"
#include "osa_assert.h"

#define DS_MAP_STR_MAGIC 0x4d415053

typedef struct ds_map_str_node ds_map_str_node_t;
struct ds_map_str_node
{
    ds_tree_node_t ds_node;
    char *key;
    char *val;
};

struct ds_map_str
{
    uint32_t init;
    ds_tree_t ds_tree;
};

static inline bool _ds_map_str_valid(ds_map_str_t *map)
{
    return map && map->init == DS_MAP_STR_MAGIC;
}

static inline void _ds_map_str_assert(ds_map_str_t *map)
{
    ASSERT(_ds_map_str_valid(map), "Invalid map %p", map);
}

void _ds_map_str_free_node(ds_map_str_node_t *node)
{
    if (!node) return;
    FREE(node->key);
    FREE(node->val);
    free(node);
}

bool _ds_map_str_delete_node(ds_map_str_t *map, ds_map_str_node_t *node)
{
    _ds_map_str_assert(map);
    ds_tree_remove(&map->ds_tree, node);
    _ds_map_str_free_node(node);
    return true;
}

/* public */
ds_map_str_t *ds_map_str_new()
{
    ds_map_str_t *map = CALLOC(sizeof(*map), 1);
    map->ds_tree = (ds_tree_t)DS_TREE_INIT(ds_str_cmp, ds_map_str_node_t, ds_node);
    map->init = DS_MAP_STR_MAGIC;
    return map;
}

bool ds_map_str_empty(ds_map_str_t *map)
{
    _ds_map_str_assert(map);
    return ds_tree_is_empty(&map->ds_tree);
}

int ds_map_str_size(ds_map_str_t *map)
{
    _ds_map_str_assert(map);
    return ds_tree_len(&map->ds_tree);
}

bool ds_map_str_find(ds_map_str_t *map, const char *key, char **pval)
{
    _ds_map_str_assert(map);
    if (key == NULL) return false;
    ds_map_str_node_t *node = ds_tree_find(&map->ds_tree, key);
    if (pval) *pval = node ? node->val : 0;
    return node != NULL;
}

/* insert adds a new key value */
/* if it already exists it will not overwrite it */
bool ds_map_str_insert(ds_map_str_t *map, const char *key, const char *val)
{
    _ds_map_str_assert(map);
    if (key == NULL) return false;
    if (val == NULL) return false;
    ds_map_str_node_t *node = ds_tree_find(&map->ds_tree, key);
    if (node) return false;
    node = CALLOC(sizeof(*node), 1);
    node->key = STRDUP(key);
    node->val = STRDUP(val);
    ds_tree_insert(&map->ds_tree, node, node->key);
    return true;
}

bool ds_map_str_remove(ds_map_str_t *map, const char *key)
{
    _ds_map_str_assert(map);
    if (key == NULL) return false;
    ds_map_str_node_t *node = ds_tree_find(&map->ds_tree, key);
    if (!node) return false;
    return _ds_map_str_delete_node(map, node);
}

/* set will either insert new or update existing key/value. */
/* using val=NULL will remove the key if it exists */
/* returns status of change: */
/* - true if key/value changed */
/* - false if no change */
bool ds_map_str_set(ds_map_str_t *map, const char *key, const char *val)
{
    _ds_map_str_assert(map);
    ds_map_str_node_t *node = ds_tree_find(&map->ds_tree, key);
    bool remove = val == NULL;
    if (node)
    {
        /* key exists, update it */
        if (!remove)
        {
            /* check for value change */
            if (strcmp(node->val, val) == 0)
            {
                /* value is the same, do nothing */
                return false;
            }
            else
            {
                /* value is different */
                FREE(node->val);
                node->val = STRDUP(val);
                return true;
            }
        }
        else
        {
            /* remove key */
            _ds_map_str_delete_node(map, node);
            return true;
        }
    }
    else
    {
        /* key does not exist */
        if (!remove)
        {
            /* insert new value */
            ds_map_str_insert(map, key, val);
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
bool ds_map_str_clear(ds_map_str_t *map)
{
    _ds_map_str_assert(map);
    ds_map_str_node_t *node = NULL;
    ds_map_str_node_t *tmp = NULL;
    ds_tree_foreach_safe (&map->ds_tree, node, tmp)
    {
        _ds_map_str_delete_node(map, node);
    }
    return true;
}

/* delete the map container */
bool ds_map_str_delete(ds_map_str_t **pmap)
{
    ASSERT(pmap, "pmap");
    if (!*pmap) return false;
    ds_map_str_clear(*pmap);
    (*pmap)->init = 0;
    free(*pmap);
    *pmap = NULL;
    return true;
}

bool _ds_map_str_fill_iter(ds_map_str_iter_t *iter, ds_map_str_node_t *node)
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

bool ds_map_str_first(ds_map_str_t *map, ds_map_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    _ds_map_str_assert(map);
    MEMZERO(*iter);
    iter->_map = map;
    ds_map_str_node_t *node = ds_tree_ifirst(&iter->_ds_iter, &map->ds_tree);
    return _ds_map_str_fill_iter(iter, node);
}

bool ds_map_str_next(ds_map_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_map_str_t *map = iter->_map;
    _ds_map_str_assert(map);
    ds_map_str_node_t *node = ds_tree_inext(&iter->_ds_iter);
    return _ds_map_str_fill_iter(iter, node);
}

ds_map_str_node_t *_ds_map_str_iter_curr(ds_map_str_iter_t *iter)
{
    return NODE_TO_CONT(iter->_ds_iter.oti_curr, iter->_ds_iter.oti_root->ot_cof);
}

bool ds_map_str_iter_remove(ds_map_str_iter_t *iter)
{
    ASSERT(iter, "iter");
    ds_map_str_t *map = iter->_map;
    _ds_map_str_assert(map);
    if (iter->end) return false;
    ds_map_str_node_t *node = _ds_map_str_iter_curr(iter);
    ds_tree_iremove(&iter->_ds_iter);
    _ds_map_str_free_node(node);
    iter->key = 0;
    iter->val = 0;
    return true;
}

/* insert a variable length array, */
/* log duplicates if log_msg specified */
/* returns false if any key is duplicate otherwise true */
/* all keys except duplicates will be inserted */
bool ds_map_str_insert_vl_array_log(
        ds_map_str_t *map,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        int values_size,
        typeof(*(char *)NULL) values[num][values_size],
        log_severity_t log_level,
        char *log_msg)
{
    int i;
    bool ret = true;
    for (i = 0; i < num; i++)
    {
        if (!ds_map_str_insert(map, keys[i], values[i]))
        {
            ret = false;
            if (log_msg)
            {
                LOG_SEVERITY(log_level, "%s: %s=%s", log_msg, keys[i], values[i]);
            }
        }
    }
    return ret;
}

bool ds_map_str_insert_vl_array(
        ds_map_str_t *map,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        int values_size,
        typeof(*(char *)NULL) values[num][values_size])
{
    return ds_map_str_insert_vl_array_log(map, num, keys_size, keys, values_size, values, 0, NULL);
}

/* compare two maps, return 0 if equal */
int ds_map_str_compare(ds_map_str_t *a, ds_map_str_t *b)
{
    int sa = ds_map_str_size(a);
    int sb = ds_map_str_size(b);
    char *b_val;
    int cmp;
    if (sa != sb) return sa - sb;
    ds_map_str_iter_t iter;
    ds_map_str_foreach(a, iter)
    {
        if (!ds_map_str_find(b, iter.key, &b_val)) return 1;
        cmp = strcmp(iter.val, b_val);
        if (cmp != 0) return cmp;
    }
    return 0;
}
