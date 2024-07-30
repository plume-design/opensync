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
/* set of keys with managed allocation */
/* built on top of ds_tree */
/* template declaration */
#endif

#define DS_TMPL_VLA_ARG_DECL_1D(ARG_T, NAME, NUM) ARG_T NAME[NUM]

#define DS_TMPL_VLA_ARG_DECL_2D(ARG_T, NAME, NUM) int NAME##_size, typeof(*(ARG_T)NULL) NAME[NUM][NAME##_size]

#define DS_SET_TEMPLATE_DECL(                                                                      \
        TYPE,       /* set type suffix */                                                          \
        KEY_T,      /* key type */                                                                 \
        KEY_STOR,   /* key storage type: VAL or PTR */                                             \
        KEY_VLA_DIM /* key variable length array dimension: 1D or 2D */                            \
        /*-------------------------------------------------------------------------------------*/) \
                                                                                                   \
    typedef struct ds_set_##TYPE ds_set_##TYPE##_t;                                                \
                                                                                                   \
    typedef struct ds_set_##TYPE##_iter                                                            \
    {                                                                                              \
        ds_set_##TYPE##_t *_set;                                                                   \
        ds_tree_iter_t _ds_iter;                                                                   \
        bool end;                                                                                  \
        KEY_T key;                                                                                 \
    } ds_set_##TYPE##_iter_t;                                                                      \
                                                                                                   \
    ds_set_##TYPE##_t *ds_set_##TYPE##_new();                                                      \
    bool ds_set_##TYPE##_delete(ds_set_##TYPE##_t **pset);                                         \
    bool ds_set_##TYPE##_empty(ds_set_##TYPE##_t *set);                                            \
    int ds_set_##TYPE##_size(ds_set_##TYPE##_t *set);                                              \
    bool ds_set_##TYPE##_find(ds_set_##TYPE##_t *set, const KEY_T key);                            \
    bool ds_set_##TYPE##_insert(ds_set_##TYPE##_t *set, const KEY_T key);                          \
    bool ds_set_##TYPE##_remove(ds_set_##TYPE##_t *set, const KEY_T key);                          \
    bool ds_set_##TYPE##_clear(ds_set_##TYPE##_t *set);                                            \
    bool ds_set_##TYPE##_first(ds_set_##TYPE##_t *set, ds_set_##TYPE##_iter_t *iter);              \
    bool ds_set_##TYPE##_next(ds_set_##TYPE##_iter_t *iter);                                       \
    bool ds_set_##TYPE##_iter_remove(ds_set_##TYPE##_iter_t *iter);                                \
                                                                                                   \
    /* insert a variable length array */                                                           \
    bool ds_set_##TYPE##_insert_vl_array_log(                                                      \
            ds_set_##TYPE##_t *set,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num),                                  \
            log_severity_t log_level,                                                              \
            char *log_msg);                                                                        \
                                                                                                   \
    bool ds_set_##TYPE##_insert_vl_array(                                                          \
            ds_set_##TYPE##_t *set,                                                                \
            int num,                                                                               \
            DS_TMPL_VLA_ARG_DECL_##KEY_VLA_DIM(KEY_T, keys, num));                                 \
                                                                                                   \
    /* compare two sets, return 0 if equal */                                                      \
    int ds_set_##TYPE##_compare(ds_set_##TYPE##_t *a, ds_set_##TYPE##_t *b);
