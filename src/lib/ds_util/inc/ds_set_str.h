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

// generated from ds_set_str.tmpl.h
#ifndef DS_SET_STR_H_INCLUDED
#define DS_SET_STR_H_INCLUDED

// set of strings

#include "ds_set_cmn.h"

typedef struct ds_set_str ds_set_str_t;
typedef struct ds_set_str_iter
{
    ds_set_str_t *_set;
    ds_tree_iter_t _ds_iter;
    bool end;
    char *key;
} ds_set_str_iter_t;
ds_set_str_t *ds_set_str_new();
bool ds_set_str_delete(ds_set_str_t **pset);
bool ds_set_str_empty(ds_set_str_t *set);
int ds_set_str_size(ds_set_str_t *set);
bool ds_set_str_find(ds_set_str_t *set, const char *key);
bool ds_set_str_insert(ds_set_str_t *set, const char *key);
bool ds_set_str_remove(ds_set_str_t *set, const char *key);
bool ds_set_str_clear(ds_set_str_t *set);
bool ds_set_str_first(ds_set_str_t *set, ds_set_str_iter_t *iter);
bool ds_set_str_next(ds_set_str_iter_t *iter);
bool ds_set_str_iter_remove(ds_set_str_iter_t *iter);

/* insert a variable length array */
bool ds_set_str_insert_vl_array_log(
        ds_set_str_t *set,
        int num,
        int keys_size,
        typeof(*(char *)NULL) keys[num][keys_size],
        log_severity_t log_level,
        char *log_msg);
bool ds_set_str_insert_vl_array(ds_set_str_t *set, int num, int keys_size, typeof(*(char *)NULL) keys[num][keys_size]);

/* compare two sets, return 0 if equal */
int ds_set_str_compare(ds_set_str_t *a, ds_set_str_t *b);

#define ds_set_str_foreach(set, iter) ds_set_type_foreach(str, set, iter)

#define ds_set_str_insert_schema_set(SET, SCHEMA, LEVEL, MSG) \
    ds_set_str_insert_vl_array_log(SET, SCHEMA##_len, sizeof(SCHEMA[0]), SCHEMA, LEVEL, MSG)

#endif
