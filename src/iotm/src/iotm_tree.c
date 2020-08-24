/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm_tree.h"
#include "iotm_tree_private.h"
#include "iotm_data_types.h"

static int iotm_str_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

void free_iotm_str_tree(struct iotm_tree_t *self)
{
    free_str_tree(self->items);
    free(self);
}

struct iotm_tree_t *new_empty_tree()
{
    struct iotm_tree_t *iotm_tree = calloc(1, sizeof(struct iotm_tree_t));

    // ops
    iotm_tree->free = iotm_tree_free;
    iotm_tree->add_val = iotm_tree_add;
    iotm_tree->add_val_str =  iotm_tree_add_str;
    iotm_tree->foreach = iotm_tree_foreach;
    iotm_tree->foreach_val = iotm_tree_foreach_value;
    iotm_tree->get_val = iotm_tree_get_single_str;
    iotm_tree->get_list = iotm_tree_get;
    iotm_tree->get_type = iotm_tree_get_single_type; 

    return iotm_tree;
}

struct iotm_tree_t *iotm_tree_new()
{
    struct iotm_tree_t *iotm_tree = NULL;
    ds_tree_t *tree;

    iotm_tree = new_empty_tree();
    if ( iotm_tree == NULL ) return NULL;

    tree = calloc(1, sizeof(*tree));
    ds_tree_init(tree, iotm_str_cmp, struct iotm_list_t, list_node);
    iotm_tree->init = true;
    iotm_tree->items = tree;
    iotm_tree->len = 0;
    return iotm_tree;
}

void iotm_tree_foreach(struct iotm_tree_t *self,
        void(*cb)(ds_tree_t *, struct iotm_list_t *, void*),
        void *ctx)
{
    if ( self == NULL ) return;

    struct iotm_list_t *f_last;
    struct iotm_list_t *f_iter;
    f_iter = ds_tree_head(self->items);

    while(f_iter != NULL)
    {
        f_last = f_iter;
        f_iter = ds_tree_next(self->items, f_iter);
        cb(self->items, f_last, ctx);
    }
}

struct list_iter_hlpr_t {
    void(*cb)(ds_list_t *, struct iotm_value_t *, void *);
    void *ctx;
} list_iter_hlpr_t;


void list_iter_cb(ds_tree_t *tree, struct iotm_list_t *list, void *ctx)
{
    struct list_iter_hlpr_t *hlp;

    hlp = (struct list_iter_hlpr_t *) ctx;
    iotm_list_foreach(list, hlp->cb, hlp->ctx);
}

void iotm_tree_foreach_value(struct iotm_tree_t *self,
        void(*cb)(ds_list_t *, struct iotm_value_t *, void*),
        void *ctx)
{
    struct list_iter_hlpr_t context = 
    {
        .cb = cb,
        .ctx = ctx,
    };

    iotm_tree_foreach(self, list_iter_cb, &context);
}

void type_convert_cb(ds_list_t *_, struct iotm_value_t *val, void *ctx)
{
    int err = -1;
    struct foreach_type_data_t *data = (struct foreach_type_data_t *)ctx;
    void *value = NULL;
    err = alloc_data(data->type, &value);

    if (err)
    {
        LOGE("%s: Couldn't allocate data for type [%d]", __func__, data->type);
        return;
    }
    err = convert_to_type(val->value, data->type, value);

    if (err) LOGE("%s: Error converting [%s]", __func__, val->value);
    data->cb(val->key, value, data->ctx);
    free(value);
}

void iotm_tree_foreach_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx)
{
    struct iotm_list_t *list = NULL;
    list = iotm_tree_find(self, key);
    struct foreach_type_data_t data =
    {
        .cb = cb,
        .ctx = ctx,
        .type = type,
    };

    iotm_list_foreach(list, type_convert_cb, &data);
}

void free_node(ds_tree_t *tree, struct iotm_list_t *list, void *ctx)
{
    ds_tree_remove(tree, list);
    iotm_list_free(list);
}

void iotm_tree_free(struct iotm_tree_t *self)
{
    if ( self == NULL ) return;

    iotm_tree_foreach(self, free_node, NULL);

    if (self->items) free(self->items);
    if ( self ) free(self);
}

int iotm_tree_add_list(struct iotm_tree_t *self, char *key, struct iotm_list_t *adding)
{
    ds_tree_insert(self->items, adding, key);
    return 0;
}

int iotm_tree_set_add_str(struct iotm_tree_t *self,
        char *key,
        char *value)
{
    int err = 0;
    if ( self == NULL ) return -1;

    struct iotm_list_t *list = iotm_tree_get(self, key);
    if ( list == NULL ) return -1;

    err = iotm_set_add_str(list, value);

    if ( !err )
    {
        self->len += 1;
    }
    return err;
}

int iotm_tree_add_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *value)
{
    char *str = NULL;
    int err = -1;

    // tmp hold before we put into tree
    str = alloc_type_string(type);
    err = convert_from_type(value, type, str);
    if (err)
    {
        LOGE("%s: Couldn't convert type [%d] to string.", __func__, type);
        return err;
    }
    err = iotm_tree_add_str(self, key, str);

    free(str);

    return err;
}

int iotm_tree_set_add_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *value)
{
    char *str = NULL;
    int err = -1;

    // tmp hold before we put into tree
    str = alloc_type_string(type);
    err = convert_from_type(value, type, str);
    if (err)
    {
        LOGE("%s: Couldn't convert type [%d] to string.", __func__, type);
        return err;
    }
    err = iotm_tree_set_add_str(self, key, str);

    free(str);

    return err;
}

int iotm_tree_set_add(struct iotm_tree_t *self,
        char *key,
        struct iotm_value_t *val)
{
    int err = -1;
    if ( self == NULL ) return -1;

    struct iotm_list_t *list = iotm_tree_get(self, key);
    if ( list == NULL ) return -1;

    err = iotm_set_add(list, val);

    if (err) iotm_value_free(val);
    else self->len += 1;
    return err;
}

int iotm_tree_add_str(struct iotm_tree_t *self,
        char *key,
        char *value)
{
    if ( self == NULL ) return -1;

    struct iotm_list_t *list = iotm_tree_get(self, key);
    if ( list == NULL ) return -1;
    self->len += 1;
    return iotm_list_add_str(list, value);
}

int iotm_tree_add(struct iotm_tree_t *self,
        char *key,
        struct iotm_value_t *adding)
{
    if ( self == NULL ) return -1;
    if ( adding == NULL ) return -1;

    struct iotm_list_t *list = iotm_tree_get(self, key);
    if ( list == NULL ) return -1;

    self->len += 1;
    iotm_list_add(list, adding);
    return 0;
}

struct iotm_list_t *iotm_tree_get(struct iotm_tree_t *self, char *key)
{
    struct iotm_list_t *list = NULL;
    list = ds_tree_find(self->items, key);

    if ( list == NULL )
    {
        list = iotm_list_new(key);
        iotm_tree_add_list(self, list->key, list);
    }
    return list;
}

struct iotm_list_t *iotm_tree_find(struct iotm_tree_t *self, char *key)
{
    return ds_tree_find(self->items, key);
}

struct iotm_value_t *iotm_tree_get_single(struct iotm_tree_t *self, char *key)
{
    struct iotm_list_t *list = iotm_tree_get(self, key);
    return iotm_list_get_head(list);
}

char *iotm_tree_get_single_str(struct iotm_tree_t *self, char *key)
{
    struct iotm_list_t *list = iotm_tree_get(self, key);
    return iotm_list_get_head_str(list);
}

int iotm_tree_get_single_type(
        struct iotm_tree_t *self,
        char *key,
        int type,
        void *out)
{
    int err = -1;
    char *str_input = NULL;

    str_input = iotm_tree_get_single_str(self, key);
    err = convert_to_type(str_input, type, out);
    return err;
}

/**
 * @brief context that enables concatenation of two trees
 */
struct tree_concat_t
{
    struct iotm_tree_t *dst;
};

void concat_cb(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    struct tree_concat_t *concat = (struct tree_concat_t *) ctx;
    if ( concat->dst == NULL ) return;
    iotm_tree_add_str(concat->dst, val->key, val->value);
}

int iotm_tree_concat_str(struct iotm_tree_t *dst, struct iotm_tree_t *src)
{
    if ( dst == NULL ) return -1;
    if ( src == NULL ) return -1;

    struct tree_concat_t passthrough =
    {
        .dst = dst,
    };

    iotm_tree_foreach_value(src, concat_cb, &passthrough);
    return 0;
}

/**
 * @brief : convert 2 static arrays into a dynamically allocated tree
 *
 * Takes a set of 2 arrays representing <string key, string value> pairs,
 * and creates a DS tree of one element lists where the only element is <value>
 *
 * @param elem_size provisioned size of strings in the input arrays
 * @param nelems number of actual elements in the input arrays
 * @param keys the static input array of string keys
 * @param values the static input array of string values
 * @return a pointer to a ds_tree if successful, NULL otherwise
 */
    iotm_tree_t *
schema2iotmtree(size_t key_size, size_t value_size, size_t nelems,
        char keys[][key_size],
        char values[][value_size])
{
    struct iotm_tree_t *iotm_tree;
    char *value;
    char *key;
    bool loop = true;
    size_t i;

    if (nelems == 0) return NULL;

    iotm_tree = iotm_tree_new();
    if (iotm_tree == NULL) return NULL;

    i = 0;
    do {
        key = keys[i];
        value = values[i];
        iotm_tree_add_str(iotm_tree, key, value);
        i++;
        loop &= (i < nelems);
    } while (loop);

    if (i != nelems) goto err_free_tree;

    return iotm_tree;

err_free_tree:
    iotm_tree_free(iotm_tree);
    return NULL;
}

int iotm_tree_remove_list(
        struct iotm_tree_t *self,
        char *key)
{
    struct iotm_list_t *list = ds_tree_find(self->items, key);
    if (list == NULL) return -1;

    ds_tree_remove(self->items, list);
    self->len -= list->len;
    return iotm_list_free(list);
}
