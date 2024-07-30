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

#if 0
/* associative map with managed allocation */
/* built on top of ds_tree */
/* template declaration */
#endif

#define DS_TMPL_VLA_ARG_DECL_1D(ARG_T, NAME, NUM) ARG_T NAME[NUM]

#define DS_TMPL_VLA_ARG_DECL_2D(ARG_T, NAME, NUM) int NAME##_size, typeof(*(ARG_T)NULL) NAME[NUM][NAME##_size]

#define DS_MAP_TEMPLATE_DECL(                                                                      \
        TYPE,        /* map type suffix */                                                         \
        KEY_T,       /* key type */                                                                \
        VAL_T,       /* val type */                                                                \
        KEY_STOR,    /* key storage type: VAL or PTR */                                            \
        VAL_STOR,    /* val storage type: VAL or PTR */                                            \
        KEY_VLA_DIM, /* key variable length array dimension: 1D or 2D */                           \
        VAL_VLA_DIM  /* val variable length array dimension: 1D or 2D */                           \
        /*-------------------------------------------------------------------------------------*/) \
    typedef struct ds_map_##TYPE ds_map_##TYPE##_t;                                                \
                                                                                                   \
    typedef struct ds_map_##TYPE##_iter                                                            \
    {                                                                                              \
        ds_map_##TYPE##_t *_map;                                                                   \
        ds_tree_iter_t _ds_iter;                                                                   \
        bool end;                                                                                  \
        KEY_T key;                                                                                 \
        VAL_T val;                                                                                 \
    } ds_map_##TYPE##_iter_t;                                                                      \
                                                                                                   \
    ds_map_##TYPE##_t *ds_map_##TYPE##_new();                                                      \
                                                                                                   \
    bool ds_map_##TYPE##_empty(ds_map_##TYPE##_t *map);                                            \
                                                                                                   \
    int ds_map_##TYPE##_size(ds_map_##TYPE##_t *map);                                              \
                                                                                                   \
    bool ds_map_##TYPE##_find(ds_map_##TYPE##_t *map, const KEY_T key, VAL_T *pval);               \
                                                                                                   \
    /* insert adds a new key value */                                                              \
    /* if it already exists it will not overwrite it */                                            \
    bool ds_map_##TYPE##_insert(ds_map_##TYPE##_t *map, const KEY_T key, const VAL_T val);         \
                                                                                                   \
    bool ds_map_##TYPE##_remove(ds_map_##TYPE##_t *map, const KEY_T key);                          \
                                                                                                   \
    /* set will either insert new, update existing or remove a key/value. */                       \
    /* using val=NULL will remove the key if it exists */                                          \
    /* returns status of change: */                                                                \
    /* - true if key/value changed */                                                              \
    /* - false if no change */                                                                     \
    bool ds_map_##TYPE##_set(ds_map_##TYPE##_t *map, const KEY_T key, const VAL_T val);            \
                                                                                                   \
    /* clear all key/map values */                                                                 \
    bool ds_map_##TYPE##_clear(ds_map_##TYPE##_t *map);                                            \
                                                                                                   \
    /* delete the map container */                                                                 \
    bool ds_map_##TYPE##_delete(ds_map_##TYPE##_t **pmap);                                         \
                                                                                                   \
    bool ds_map_##TYPE##_first(ds_map_##TYPE##_t *map, ds_map_##TYPE##_iter_t *iter);              \
                                                                                                   \
    bool ds_map_##TYPE##_next(ds_map_##TYPE##_iter_t *iter);                                       \
                                                                                                   \
    bool ds_map_##TYPE##_iter_remove(ds_map_##TYPE##_iter_t *iter);                                \
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
            char *log_msg);                                                                        \
                                                                                                   \
    bool ds_map_##TYPE##_insert_vl_array(                                                          \
            ds_map_##TYPE##_t *map,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num),                                  \
            DS_TMPL_VLA_ARG_DECL_##VAL_VLA_DIM(VAL_T, values, num));                               \
                                                                                                   \
    /* compare two maps, return 0 if equal */                                                      \
    int ds_map_##TYPE##_compare(ds_map_##TYPE##_t *a, ds_map_##TYPE##_t *b);
