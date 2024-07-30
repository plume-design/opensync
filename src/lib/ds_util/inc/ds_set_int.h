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

// generated from ds_set_int.tmpl.h
#ifndef DS_SET_INT_H_INCLUDED
#define DS_SET_INT_H_INCLUDED

// set of ints

#include "ds_set_cmn.h"

typedef struct ds_set_int ds_set_int_t;
typedef struct ds_set_int_iter
{
    ds_set_int_t *_set;
    ds_tree_iter_t _ds_iter;
    bool end;
    int key;
} ds_set_int_iter_t;
ds_set_int_t *ds_set_int_new();
bool ds_set_int_delete(ds_set_int_t **pset);
bool ds_set_int_empty(ds_set_int_t *set);
int ds_set_int_size(ds_set_int_t *set);
bool ds_set_int_find(ds_set_int_t *set, const int key);
bool ds_set_int_insert(ds_set_int_t *set, const int key);
bool ds_set_int_remove(ds_set_int_t *set, const int key);
bool ds_set_int_clear(ds_set_int_t *set);
bool ds_set_int_first(ds_set_int_t *set, ds_set_int_iter_t *iter);
bool ds_set_int_next(ds_set_int_iter_t *iter);
bool ds_set_int_iter_remove(ds_set_int_iter_t *iter);

/* insert a variable length array */
bool ds_set_int_insert_vl_array_log(ds_set_int_t *set, int num, int keys[num], log_severity_t log_level, char *log_msg);
bool ds_set_int_insert_vl_array(ds_set_int_t *set, int num, int keys[num]);

/* compare two sets, return 0 if equal */
int ds_set_int_compare(ds_set_int_t *a, ds_set_int_t *b);

#define ds_set_int_foreach(set, iter) ds_set_type_foreach(int, set, iter)

#define ds_set_int_insert_schema_set(SET, SCHEMA, LEVEL, MSG) \
    ds_set_int_insert_vl_array_log(SET, SCHEMA##_len, SCHEMA, LEVEL, MSG)

#endif
