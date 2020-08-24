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

#include "iotm_list.h"
#include "iotm_list_private.h"



struct iotm_value_t *alloc_iotm_value(char *key, char *val)
{
    if ( key == NULL ) return NULL;
    if ( val == NULL ) return NULL;

    struct iotm_value_t *iotm_val = (struct iotm_value_t *)calloc(1, sizeof(struct iotm_value_t));
    iotm_val->key = strdup(key);
    iotm_val->value = strdup(val);
    return iotm_val;
}

struct iotm_list_t *alloc_iotm_list(char *key)
{
    if (key == NULL) return NULL;

    struct iotm_list_t *list = (struct iotm_list_t *)calloc(1, sizeof(struct iotm_list_t));
    list->key = strdup(key);
    return list;
}

struct iotm_list_t *iotm_list_new(char *key)
{
    struct iotm_list_t *lists = alloc_iotm_list(key);
    if ( lists == NULL ) return NULL;

    ds_list_init(&lists->items, struct iotm_value_t, list_node);

    lists->free = iotm_list_free;
    lists->foreach = iotm_list_foreach;
    lists->get_head_str = iotm_list_get_head_str;
    lists->add_str = iotm_list_add_str;
    lists->len = 0;
    return lists;
}


int iotm_list_add(struct iotm_list_t *list, struct iotm_value_t *val)
{
    if ( list == NULL ) return -1;

    ds_list_insert_head(&list->items, val);
    list->len += 1;
    return 0;
}

bool str_eql(char *first, char *sec)
{
    return (strcmp(first, sec) == 0);
}

bool is_in_list_str(struct iotm_list_t *self, char *val)
{
    struct iotm_value_t iotm_val =
    {
        .key = self->key,
        .value = val,
    };
    return is_in_list(self, &iotm_val);
}

bool is_in_list(struct iotm_list_t *self, struct iotm_value_t *val)
{
    struct iotm_value_t *iter = NULL;
    ds_list_foreach(&self->items, iter){
        if (str_eql(iter->value, val->value)
                && str_eql(iter->key, val->key)) return true;
    }
    return false;
}


int iotm_set_add(struct iotm_list_t *list, struct iotm_value_t *val)
{
    if ( is_in_list(list, val) ) return -1;
    return iotm_list_add(list, val);
}

int iotm_set_add_str(struct iotm_list_t *self, char *val)
{
    int err;
    struct iotm_value_t *add_val = calloc(1, sizeof(struct iotm_value_t));
    add_val->key = strdup(self->key);
    add_val->value = strdup(val);
    err = iotm_set_add(self, add_val);

    if ( err )
    {
        free(add_val->key);
        free(add_val->value);
        free(add_val);
    }
    return err;
}

int iotm_list_add_str(struct iotm_list_t *self, char *val)
{
    if ( self == NULL ) return -1;

    struct iotm_value_t *adding = alloc_iotm_value(self->key, val);

    ds_list_insert_head(&self->items, adding);
    self->len += 1;
    return 0;
}

struct update_context {
    char *newkey;
};

void iotm_print_value(ds_list_t *dl, struct iotm_value_t *val, void *context)
{
    printf("[%s]->[%s]\n", val->key, val->value);
}

void iotm_list_update_key_cb(ds_list_t *dl, struct iotm_value_t *val, void *context)
{
    struct update_context *ctx = (struct update_context *) context;
    free(val->key);
    val->key = strdup(ctx->newkey);
}
void iotm_list_update_key(struct iotm_list_t *self, char *newkey)
{
    struct update_context ctx =
    {
        .newkey = newkey,
    };
    free(self->key);
    self->key = strdup(newkey);
    iotm_list_foreach(self, iotm_list_update_key_cb, &ctx);
}

void iotm_list_print(struct iotm_list_t *self)
{
    iotm_list_foreach(self, iotm_print_value, NULL);
}

void iotm_list_foreach(struct iotm_list_t *lists, void(*cb)(ds_list_t *, struct iotm_value_t *, void*), void *ctx)
{
    if (lists == NULL) return;
    ds_list_iter_t iter;
    ds_list_t list = lists->items;
    struct iotm_value_t *last = NULL;
    struct iotm_value_t *data = NULL;

    data = ds_list_ifirst(&iter, &list);
    while (data)
    {
        last = data;
        data = ds_list_inext(&iter);
        cb(&lists->items, last, ctx);
    }
}

void iotm_value_free(struct iotm_value_t *self)
{
    free(self->value);
    free(self->key);
    if ( self->free_other ) self->free_other(self->other);
    free(self);
}

void free_item(ds_list_t *dlist, struct iotm_value_t *val, void *ctx)
{
    if ( dlist != NULL ) ds_list_remove_head(dlist);
    iotm_value_free(val);
}

int iotm_list_free(struct iotm_list_t *self)
{
    if ( self == NULL ) return -1;

    iotm_list_foreach(self, free_item, NULL);
    free(self->key);
    free(self);
    return 0;
}

struct iotm_value_t *iotm_list_get_head(struct iotm_list_t *list)
{
    return (struct iotm_value_t *)ds_list_head(&list->items);
}

char *iotm_list_get_head_str(struct iotm_list_t *list)
{
    struct iotm_value_t *head = ds_list_head(&list->items);
    return head->value;
}
