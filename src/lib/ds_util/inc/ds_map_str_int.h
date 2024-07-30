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

// generated from ds_map_str_int.tmpl.h
#ifndef DS_MAP_STR_INT_H_INCLUDED
#define DS_MAP_STR_INT_H_INCLUDED

// map of strings

#include "ds_map_cmn.h"

typedef struct ds_map_str_int ds_map_str_int_t;
typedef struct ds_map_str_int_iter
{
    ds_map_str_int_t *_map;
    ds_tree_iter_t _ds_iter;
    bool end;
    char *key;
    int val;
} ds_map_str_int_iter_t;
ds_map_str_int_t *ds_map_str_int_new();
bool ds_map_str_int_empty(ds_map_str_int_t *map);
int ds_map_str_int_size(ds_map_str_int_t *map);
bool ds_map_str_int_find(ds_map_str_int_t *map, const char *key, int *pval);

/* insert adds a new key value */
/* if it already exists it will not overwrite it */
bool ds_map_str_int_insert(ds_map_str_int_t *map, const char *key, const int val);
bool ds_map_str_int_remove(ds_map_str_int_t *map, const char *key);

/* set will either insert new, update existing or remove a key/value. */
/* using val=NULL will remove the key if it exists */
/* returns status of change: */
/* - true if key/value changed */
/* - false if no change */
bool ds_map_str_int_set(ds_map_str_int_t *map, const char *key, const int val);

/* clear all key/map values */
bool ds_map_str_int_clear(ds_map_str_int_t *map);

/* delete the map container */
bool ds_map_str_int_delete(ds_map_str_int_t **pmap);
bool ds_map_str_int_first(ds_map_str_int_t *map, ds_map_str_int_iter_t *iter);
bool ds_map_str_int_next(ds_map_str_int_iter_t *iter);
bool ds_map_str_int_iter_remove(ds_map_str_int_iter_t *iter);

/* insert a variable length array, */
/* log duplicates if log_msg specified */
/* returns false if any key is duplicate otherwise true */
/* all keys except duplicates will be inserted */
bool ds_map_str_int_insert_vl_array_log(
        ds_map_str_int_t *map,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        int values[num],
        log_severity_t log_level,
        char *log_msg);
bool ds_map_str_int_insert_vl_array(
        ds_map_str_int_t *map,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        int values[num]);

/* compare two maps, return 0 if equal */
int ds_map_str_int_compare(ds_map_str_int_t *a, ds_map_str_int_t *b);

#define ds_map_str_int_foreach(map, iter) ds_map_type_foreach(str_int, map, iter)

#define ds_map_str_int_insert_schema_map(MAP, SCHEMA, LEVEL, MSG) \
    ds_map_str_int_insert_vl_array_log(MAP, SCHEMA##_len, sizeof(SCHEMA##_keys[0]), SCHEMA##_keys, SCHEMA, LEVEL, MSG)

#endif