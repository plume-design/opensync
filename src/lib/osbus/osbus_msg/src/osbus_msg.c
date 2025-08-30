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

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <jansson.h>
#include <string.h>

#include "log.h"
#include "os.h"
#include "os_time.h"
#include "util.h"
#include "memutil.h"
#include "const.h"
#include "osbus_msg.h"
#include "json_util.h"

#define MODULE_ID LOG_MODULE_ID_OSBUS

static bool osbus_msg_set_name(osbus_msg_t *data, const char *name);

char* osbus_msg_type_str(osbus_msg_type type)
{
    static char *type_str[] = {
        [OSBUS_DATA_TYPE_NULL]   = "NULL",
        [OSBUS_DATA_TYPE_OBJECT] = "OBJECT",
        [OSBUS_DATA_TYPE_ARRAY]  = "ARRAY",
        [OSBUS_DATA_TYPE_BOOL]   = "BOOL",
        [OSBUS_DATA_TYPE_INT]    = "INT",
        [OSBUS_DATA_TYPE_INT64]  = "INT64",
        [OSBUS_DATA_TYPE_DOUBLE] = "DOUBLE",
        [OSBUS_DATA_TYPE_STRING] = "STRING",
        [OSBUS_DATA_TYPE_BINARY] = "BINARY",
    };
    if (type < 0 || type >= ARRAY_LEN(type_str)) return NULL;
    return type_str[type];
}

osbus_msg_t* osbus_msg_new_null(void)
{
    return osbus_msg_new_type(OSBUS_DATA_TYPE_NULL);
}

osbus_msg_t* osbus_msg_new_object(void)
{
    return osbus_msg_new_type(OSBUS_DATA_TYPE_OBJECT);
}

osbus_msg_t* osbus_msg_new_array(void)
{
    return osbus_msg_new_type(OSBUS_DATA_TYPE_ARRAY);
}

static inline void _osbus_msg_tree_init(osbus_msg_t *d)
{
    ds_tree_init(&d->tree, ds_str_cmp, osbus_msg_t, node);
}

osbus_msg_t* osbus_msg_new_type(osbus_msg_type type)
{
    osbus_msg_t *d = CALLOC(sizeof(osbus_msg_t), 1);
    d->type = type;
    _osbus_msg_tree_init(d);
    return d;
}

#define _OSBUS_NEW_VAL2(TYPE, _METHOD) \
    osbus_msg_t *d = osbus_msg_new_type(TYPE); \
    if (!d) return NULL; \
    if (!osbus_msg_set##_METHOD) { \
        osbus_msg_free(d); \
        return NULL; \
    } \
    return d;

#define _OSBUS_NEW_VAL(TYPE, _METHOD) \
    _OSBUS_NEW_VAL2(TYPE, _METHOD(d, val))

osbus_msg_t* osbus_msg_new_bool(bool val)
{
    _OSBUS_NEW_VAL(OSBUS_DATA_TYPE_BOOL, _bool);
}

osbus_msg_t* osbus_msg_new_int(int val)
{
    _OSBUS_NEW_VAL(OSBUS_DATA_TYPE_INT, _int);
}

osbus_msg_t* osbus_msg_new_int64(int64_t val)
{
    _OSBUS_NEW_VAL(OSBUS_DATA_TYPE_INT64, _int64);
}

osbus_msg_t* osbus_msg_new_double(double val)
{
    _OSBUS_NEW_VAL(OSBUS_DATA_TYPE_DOUBLE, _double);
}

osbus_msg_t* osbus_msg_new_string(const char *val)
{
    _OSBUS_NEW_VAL(OSBUS_DATA_TYPE_STRING, _string);
}

osbus_msg_t* osbus_msg_new_binary(const uint8_t *buf, int size)
{
    _OSBUS_NEW_VAL2(OSBUS_DATA_TYPE_BINARY, _binary(d, buf, size));
}


void osbus_msg_free_value(osbus_msg_t *data)
{
    if (!data) return;
    osbus_msg_t *e = NULL;
    switch(data->type) {
        case OSBUS_DATA_TYPE_OBJECT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_ARRAY:
            osbus_msg_foreach(data, e) {
                osbus_msg_free(e);
            }
            free(data->val.list);
            data->val.list = NULL;
            data->num = 0;
            _osbus_msg_tree_init(data);
            break;
        case OSBUS_DATA_TYPE_STRING:
            free(data->val.v_string);
            data->val.v_string = NULL;
            break;
        case OSBUS_DATA_TYPE_BINARY:
            free(data->val.v_buf);
            data->val.v_buf = NULL;
            data->num = 0;
            break;
        default:
            memset(&data->val, 0, sizeof(data->val));
            break;
    }
    data->type = OSBUS_DATA_TYPE_NULL;
}

void osbus_msg_free(osbus_msg_t *data)
{
    if (!data) return;
    osbus_msg_free_value(data);
    free(data->name);
    free(data);
}


bool osbus_msg_is_container(const osbus_msg_t *data)
{
    if (!data) return false;
    switch(data->type) {
        case OSBUS_DATA_TYPE_OBJECT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_ARRAY:
            return true;
        default:
            return false;
    }
}

bool osbus_msg_is_number(const osbus_msg_t *data)
{
    if (!data) return false;
    switch(data->type) {
        case OSBUS_DATA_TYPE_INT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_INT64: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_DOUBLE:
            return true;
        default:
            return false;
    }
}

bool osbus_msg_is_integer(const osbus_msg_t *data)
{
    if (!data) return false;
    switch(data->type) {
        case OSBUS_DATA_TYPE_INT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_INT64:
            return true;
        default:
            return false;
    }
}


#define _OSBUS_TYPE_CHECK(DATA, TYPE) \
    if (!DATA || DATA->type != TYPE) return false

bool osbus_msg_set_bool(osbus_msg_t *data, bool val)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_BOOL);
    data->val.v_bool = val;
    return true;
}

bool osbus_msg_set_int(osbus_msg_t *data, int val)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_INT);
    data->val.v_int = val;
    return true;
}

bool osbus_msg_set_int64 (osbus_msg_t *data, int64_t val)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_INT64);
    data->val.v_int64 = val;
    return true;
}

bool osbus_msg_set_double(osbus_msg_t *data, double val)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_DOUBLE);
    data->val.v_double = val;
    return true;
}

bool osbus_msg_set_string(osbus_msg_t *data, const char *val)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_STRING);
    // treat NULL string value as empty string
    char *copy = STRDUP(val ?: "");
    free(data->val.v_string);
    data->val.v_string = copy;
    return true;
}

bool osbus_msg_set_binary(osbus_msg_t *data, const uint8_t *buf, int size)
{
    _OSBUS_TYPE_CHECK(data, OSBUS_DATA_TYPE_BINARY);
    free(data->val.v_buf);
    data->val.v_buf = NULL;
    data->num = 0;
    if (size > 0) {
        data->val.v_buf = MEMNDUP(buf, size);
        data->num = size;
    }
    return true;
}

#define _OSBUS_DEF_VAL(VAL, DEF_VAL) \
    bool ret = false; \
    if (!VAL) return false; \
    *VAL = DEF_VAL; \
    if (!data) return false

#define _OSBUS_GET_VAL(VAL, TYPE, FIELD) \
    if (data->type == TYPE) { \
        *VAL = data->val.FIELD; \
        ret = true; \
    }

osbus_msg_type osbus_msg_get_type(const osbus_msg_t *data)
{
    return data->type;
}

char* osbus_msg_get_name(const osbus_msg_t *data)
{
    if (!data) return NULL;
    return data->name;
}

bool osbus_msg_get_bool(const osbus_msg_t *data, bool *val)
{
    _OSBUS_DEF_VAL(val, false);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_BOOL, v_bool);
    return ret;
}

bool osbus_msg_get_int(const osbus_msg_t *data, int *val)
{
    _OSBUS_DEF_VAL(val, 0);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT,    v_int);
    // implicit cast
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT64,  v_int64);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_DOUBLE, v_double);
    return ret;
}

bool osbus_msg_get_int64(const osbus_msg_t *data, int64_t *val)
{
    _OSBUS_DEF_VAL(val, 0);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT64,  v_int64)
    // implicit cast
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT,    v_int);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_DOUBLE, v_double);
    return ret;
}

bool osbus_msg_get_double(const osbus_msg_t *data, double *val)
{
    _OSBUS_DEF_VAL(val, 0);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_DOUBLE, v_double);
    // implicit cast
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT,    v_int);
    _OSBUS_GET_VAL(val, OSBUS_DATA_TYPE_INT64,  v_int64);
    return ret;
}

// returns a reference to string held by data, must not be freed
bool osbus_msg_get_string(const osbus_msg_t *data, const char **str)
{
    _OSBUS_DEF_VAL(str, NULL);
    _OSBUS_GET_VAL(str, OSBUS_DATA_TYPE_STRING, v_string);
    return ret;
}

bool osbus_msg_get_string_alloc(const osbus_msg_t *data, char **str)
{
    const char *tmp_str = NULL;
    *str = NULL;
    if (!osbus_msg_get_string(data, &tmp_str)) return false;
    *str = STRDUP(tmp_str ?: "");
    return true;
}

bool osbus_msg_get_string_fixed(const osbus_msg_t *data, char *str, int size)
{
    const char *tmp_str = NULL;
    *str = 0;
    if (!osbus_msg_get_string(data, &tmp_str)) return false;
    if (strscpy(str, tmp_str ?: "", size) < 0) return false;
    return true;
}

// returns a reference to buffer held by data, must not be freed
bool osbus_msg_get_binary(const osbus_msg_t *data, const uint8_t **buf, int *size)
{
    _OSBUS_DEF_VAL(buf, NULL);
    if (!size) return false;
    *size = 0;
    _OSBUS_GET_VAL(buf, OSBUS_DATA_TYPE_BINARY, v_buf);
    if (ret) *size = data->num;
    return ret;
}

bool osbus_msg_get_binary_alloc(const osbus_msg_t *data, uint8_t **buf, int *size)
{
    const uint8_t *src_buf = NULL;
    int src_size = 0;
    *size = 0;
    *buf = NULL;
    if (!osbus_msg_get_binary(data, &src_buf, &src_size)) return false;
    if (src_size > 0) {
        *buf = MEMNDUP(src_buf, src_size);
        *size = src_size;
    }
    return true;
}

bool osbus_msg_get_binary_fixed(const osbus_msg_t *data, uint8_t *buf, int max_buf_size, int *size)
{
    const uint8_t *src_buf = NULL;
    int src_size = 0;
    *size = 0;
    if (!osbus_msg_get_binary(data, &src_buf, &src_size)) return false;
    if (src_size > max_buf_size) return false;
    if (src_size > 0) {
        memcpy(buf, src_buf, src_size);
        *size = src_size;
    }
    return true;
}

// arrays contain unnamed value items
// items are just unnamed properties
// append existing msg (e) to a container (data)
bool _osbus_msg_append_item(osbus_msg_t *data, osbus_msg_t *e)
{
    if (!data || !e) return false;
    if (!osbus_msg_is_container(data)) return false;
    // val.list array is used also for object properties, so that
    // the order is maintained - same behaviour as in jansson
    // resize
    data->val.list = REALLOC(data->val.list, sizeof(osbus_msg_t*) * (data->num + 1));
    // append item
    data->val.list[data->num] = e;
    e->idx = data->num;
    data->num++;
    // for object types insert key in tree
    if (data->type == OSBUS_DATA_TYPE_OBJECT) {
        ds_tree_insert(&data->tree, e, e->name);
    }
    return true;
}

// add existing `e` to data, ownership is taken over
// if adding fails `e` is freed, return NULL
// on success returns `e`
osbus_msg_t* osbus_msg_add_item(osbus_msg_t *data, osbus_msg_t *e)
{
    if (!data || data->type != OSBUS_DATA_TYPE_ARRAY || !e) goto error;
    osbus_msg_set_name(e, NULL);
    if (!_osbus_msg_append_item(data, e)) goto error;
    return e;
error:
    osbus_msg_free(e);
    return NULL;
}

osbus_msg_t* osbus_msg_add_item_type(osbus_msg_t *data, osbus_msg_type type)
{
    osbus_msg_t *e = osbus_msg_new_type(type);
    if (osbus_msg_add_item(data, e)) {
        return e;
    }
    return NULL;
}

osbus_msg_t* osbus_msg_add_item_object(osbus_msg_t *data)
{
    return osbus_msg_add_item_type(data, OSBUS_DATA_TYPE_OBJECT);
}

osbus_msg_t* osbus_msg_add_item_array(osbus_msg_t *data)
{
    return osbus_msg_add_item_type(data, OSBUS_DATA_TYPE_ARRAY);
}

#define _OSBUS_ADD_ITEM_VAL1(_TYPE) \
    return osbus_msg_add_item(data, osbus_msg_new##_TYPE(val))

#define _OSBUS_ADD_ITEM_VAL2(_METHOD) \
    return osbus_msg_add_item(data, osbus_msg_new##_METHOD)

osbus_msg_t* osbus_msg_add_item_null(osbus_msg_t *data)
{
    _OSBUS_ADD_ITEM_VAL2(_null());
}

osbus_msg_t* osbus_msg_add_item_bool(osbus_msg_t *data, bool val)
{
    _OSBUS_ADD_ITEM_VAL1(_bool);
}

osbus_msg_t* osbus_msg_add_item_int(osbus_msg_t *data, int val)
{
    _OSBUS_ADD_ITEM_VAL1(_int);
}

osbus_msg_t* osbus_msg_add_item_int64(osbus_msg_t *data, int64_t val)
{
    _OSBUS_ADD_ITEM_VAL1(_int64);
}

osbus_msg_t* osbus_msg_add_item_double(osbus_msg_t *data, double val)
{
    _OSBUS_ADD_ITEM_VAL1(_double);
}

osbus_msg_t* osbus_msg_add_item_string(osbus_msg_t *data, const char *val)
{
    _OSBUS_ADD_ITEM_VAL1(_string);
}

osbus_msg_t* osbus_msg_add_item_binary(osbus_msg_t *data, const uint8_t *buf, int size)
{
    _OSBUS_ADD_ITEM_VAL2(_binary(buf, size));
}

osbus_msg_t* osbus_msg_get_item(const osbus_msg_t *data, int i)
{
    if (!data) return NULL;
    if (!osbus_msg_is_container(data)) return NULL;
    if (i < 0 || i >= data->num) return NULL;
    return data->val.list[i];
}

int osbus_msg_item_count(const osbus_msg_t *data)
{
    if (!osbus_msg_is_container(data)) return 0;
    return data->num;
}


// objects contain named key/value properties
static bool osbus_msg_set_name(osbus_msg_t *data, const char *name)
{
    if (!data) return false;
    free(data->name);
    data->name = NULL;
    if (name) {
        data->name = STRDUP(name);
    }
    return true;
}

// add existing `e` to data, ownership is taken over
// if adding fails `e` is freed, return NULL
// on success returns `e`
osbus_msg_t* osbus_msg_set_prop(osbus_msg_t *data, const char *name, osbus_msg_t *e)
{
    if (!e) return NULL;
    if (!data) goto error;
    if (data->type == OSBUS_DATA_TYPE_ARRAY && name == NULL) {
        // accept data type array with name==NULL
        if (!osbus_msg_add_item(data, e)) goto error;
        return e;
    }
    if (data->type != OSBUS_DATA_TYPE_OBJECT || !name) goto error;
    if (!osbus_msg_set_name(e, name)) goto error;
    // check for existing key
    int i;
    osbus_msg_t *x;
    x = osbus_msg_get_prop(data, name);
    if (x != NULL) {
        // overwrite existing key
        i = x->idx;
        ASSERT(data->val.list[i] == x, "idx %d", i);
        osbus_msg_free(x);
        data->val.list[i] = e;
    } else {
        // append new item
        if (!_osbus_msg_append_item(data, e)) goto error;
    }
    return e;
error:
    osbus_msg_free(e);
    return NULL;
}

osbus_msg_t* osbus_msg_set_prop_type(osbus_msg_t *data, const char *name, osbus_msg_type type)
{
    osbus_msg_t *e = osbus_msg_new_type(type);
    if (osbus_msg_set_prop(data, name, e)) {
        return e;
    }
    return NULL;
}

osbus_msg_t* osbus_msg_set_prop_object(osbus_msg_t *data, const char *name)
{
    return osbus_msg_set_prop_type(data, name, OSBUS_DATA_TYPE_OBJECT);
}

osbus_msg_t* osbus_msg_set_prop_array(osbus_msg_t *data, const char *name)
{
    return osbus_msg_set_prop_type(data, name, OSBUS_DATA_TYPE_ARRAY);
}

#define _OSBUS_ADD_PROP_VAL1(_TYPE) \
    return osbus_msg_set_prop(data, name, osbus_msg_new##_TYPE(val))

#define _OSBUS_ADD_PROP_VAL2(_METHOD) \
    return osbus_msg_set_prop(data, name, osbus_msg_new##_METHOD)

osbus_msg_t* osbus_msg_set_prop_null(osbus_msg_t *data, const char *name)
{
    _OSBUS_ADD_PROP_VAL2(_null());
}

osbus_msg_t* osbus_msg_set_prop_bool(osbus_msg_t *data, const char *name, bool val)
{
    _OSBUS_ADD_PROP_VAL1(_bool);
}

osbus_msg_t* osbus_msg_set_prop_int(osbus_msg_t *data, const char *name, int val)
{
    _OSBUS_ADD_PROP_VAL1(_int);
}

osbus_msg_t* osbus_msg_set_prop_int64 (osbus_msg_t *data, const char *name, int64_t val)
{
    _OSBUS_ADD_PROP_VAL1(_int64);
}

osbus_msg_t* osbus_msg_set_prop_double(osbus_msg_t *data, const char *name, double val)
{
    _OSBUS_ADD_PROP_VAL1(_double);
}

osbus_msg_t* osbus_msg_set_prop_string(osbus_msg_t *data, const char *name, const char *val)
{
    _OSBUS_ADD_PROP_VAL1(_string);
}

osbus_msg_t* osbus_msg_set_prop_binary(osbus_msg_t *data, const char *name, const uint8_t *buf, int size)
{
    _OSBUS_ADD_PROP_VAL2(_binary(buf, size));
}


osbus_msg_t* osbus_msg_get_prop(const osbus_msg_t *data, const char *name)
{
    if (!data || data->type != OSBUS_DATA_TYPE_OBJECT) return NULL;
    osbus_msg_t *e = ds_tree_find((ds_tree_t*)&data->tree, name);
    return e;
}

bool osbus_msg_get_prop_bool(const osbus_msg_t *data, const char *name, bool *val)
{
    *val = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_bool(e, val);
}

bool osbus_msg_get_prop_int(const osbus_msg_t *data, const char *name, int *val)
{
    *val = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_int(e, val);
}

bool osbus_msg_get_prop_int64(const osbus_msg_t *data, const char *name, int64_t *val)
{
    *val = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_int64(e, val);
}

bool osbus_msg_get_prop_double(const osbus_msg_t *data, const char *name, double *val)
{
    *val = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_double(e, val);
}

bool osbus_msg_get_prop_string(const osbus_msg_t *data, const char *name, const char **str)
{
    *str = NULL;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_string(e, str);
}

bool osbus_msg_get_prop_string_alloc(const osbus_msg_t *data, const char *name, char **str)
{
    *str = NULL;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_string_alloc(e, str);
}

bool osbus_msg_get_prop_string_fixed(const osbus_msg_t *data, const char *name, char *str, int size)
{
    *str = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_string_fixed(e, str, size);
}

bool osbus_msg_get_prop_binary(const osbus_msg_t *data, const char *name, const uint8_t **buf, int *size)
{
    *buf = NULL;
    *size = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_binary(e, buf, size);
}

bool osbus_msg_get_prop_binary_alloc(const osbus_msg_t *data, const char *name, uint8_t **buf, int *size)
{
    *buf = NULL;
    *size = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_binary_alloc(e, buf, size);
}

bool osbus_msg_get_prop_binary_fixed(const osbus_msg_t *data, const char *name, uint8_t *buf, int max_buf_size, int *size)
{
    *size = 0;
    osbus_msg_t *e = osbus_msg_get_prop(data, name);
    return osbus_msg_get_binary_fixed(e, buf, max_buf_size, size);
}

// map

#define _OSBUS_MAP_VAL1(_METHOD) \
    if (set) return osbus_msg_set_prop##_METHOD(data, name, *val);\
    else     return osbus_msg_get_prop##_METHOD(data, name, val)

#define _OSBUS_MAP_VAL2(_METHOD, A, B) \
    if (set) return osbus_msg_set_prop##_METHOD(data, name, *A, *B);\
    else     return osbus_msg_get_prop##_METHOD(data, name, A, B)

bool osbus_msg_map_prop_bool(bool set, osbus_msg_t *data, const char *name, bool *val)
{
    _OSBUS_MAP_VAL1(_bool);
}

bool osbus_msg_map_prop_int(bool set, osbus_msg_t *data, const char *name, int *val)
{
    _OSBUS_MAP_VAL1(_int);
}

bool osbus_msg_map_prop_int64(bool set, osbus_msg_t *data, const char *name, int64_t *val)
{
    _OSBUS_MAP_VAL1(_int64);
}

bool osbus_msg_map_prop_double(bool set, osbus_msg_t *data, const char *name, double *val)
{
    _OSBUS_MAP_VAL1(_double);
}

bool osbus_msg_map_prop_string(bool set, osbus_msg_t *data, const char *name, const char **val)
{
    _OSBUS_MAP_VAL1(_string);
}

bool osbus_msg_map_prop_string_alloc(bool set, osbus_msg_t *data, const char *name, char **val)
{
    if (set) return osbus_msg_set_prop_string(data, name, *val);
    else     return osbus_msg_get_prop_string_alloc(data, name, val);
}

bool osbus_msg_map_prop_string_fixed(bool set, osbus_msg_t *data, const char *name, char *str, int size)
{
    if (set) return osbus_msg_set_prop_string(data, name, str);
    else     return osbus_msg_get_prop_string_fixed(data, name, str, size);
}

bool osbus_msg_map_prop_binary(bool set, osbus_msg_t *data, const char *name, const uint8_t **buf, int *size)
{
    _OSBUS_MAP_VAL2(_binary, buf, size);
}

bool osbus_msg_map_prop_binary_alloc(bool set, osbus_msg_t *data, const char *name, uint8_t **buf, int *size)
{
    if (set) return osbus_msg_set_prop_binary(data, name, *buf, *size);
    else     return osbus_msg_get_prop_binary_alloc(data, name, buf, size);
}

bool osbus_msg_map_prop_binary_fixed(bool set, osbus_msg_t *data, const char *name, uint8_t *buf, int max_buf_size, int *size)
{
    if (set) return osbus_msg_set_prop_binary(data, name, buf, *size);
    else     return osbus_msg_get_prop_binary_fixed(data, name, buf, max_buf_size, size);
}


bool osbus_msg_map_prop_bool_fn(bool set, osbus_msg_t *data, const char *name, void *fn_val, osbus_msg_map_bool_fn_t *map_fn)
{
    bool bool_val = NULL;
    if (set) {
        if (!map_fn(set, &bool_val, fn_val)) return false;
    }
    if (!osbus_msg_map_prop_bool(set, data, name, &bool_val)) return false;
    if (!set) {
        if (!map_fn(set, &bool_val, fn_val)) return false;
    }
    return true;
}

bool osbus_msg_map_prop_string_fn(bool set, osbus_msg_t *data, const char *name, void *fn_val, osbus_msg_map_string_fn_t *map_fn)
{
    const char *str = NULL;
    if (set) {
        if (!map_fn(set, &str, fn_val)) return false;
    }
    if (!osbus_msg_map_prop_string(set, data, name, &str)) return false;
    if (!set) {
        if (!map_fn(set, &str, fn_val)) return false;
    }
    return true;
}

// conversion


osbus_msg_t* osbus_msg_copy(const osbus_msg_t *data)
{
    if (!data) return false;
    osbus_msg_t *c = osbus_msg_new_type(data->type);
    osbus_msg_t *e = NULL;
    osbus_msg_t *tmp = NULL;
    char *key = NULL;
    switch (data->type) {
        case OSBUS_DATA_TYPE_OBJECT:
            osbus_msg_foreach_prop(data, key, e) {
                tmp = osbus_msg_copy(e);
                osbus_msg_set_prop(c, key, tmp);
            }
            break;
        case OSBUS_DATA_TYPE_ARRAY:
            osbus_msg_foreach(data, e) {
                tmp = osbus_msg_copy(e);
                osbus_msg_add_item(c, tmp);
            }
            break;
        case OSBUS_DATA_TYPE_STRING:
            {
                const char *str = NULL;
                osbus_msg_get_string(data, &str);
                osbus_msg_set_string(c, str);
            }
            break;
        case OSBUS_DATA_TYPE_BINARY:
            {
                const uint8_t *buf = NULL;
                int size = 0;
                osbus_msg_get_binary(data, &buf, &size);
                osbus_msg_set_binary(c, buf, size);
            }
            break;
        default:
            c->val = data->val;
            break;
    }
    return c;
}

bool osbus_msg_set_type(osbus_msg_t *msg, osbus_msg_type type)
{
    if (!msg) return false;
    osbus_msg_free_value(msg);
    msg->type = type;
    return true;
}

bool osbus_msg_array_resize(osbus_msg_t *msg, int num)
{
    int i;
    if (!msg || msg->type != OSBUS_DATA_TYPE_ARRAY) return false;
    if (num == msg->num) return true;
    if (num > msg->num) {
        msg->val.list = REALLOC(msg->val.list, sizeof(osbus_msg_t*) * num);
        for (i = msg->num; i < num; i++) {
            msg->val.list[i] = osbus_msg_new_null();
        }
    } else {
        for (i = num; i < msg->num; i++) {
            osbus_msg_free(msg->val.list[i]);
            msg->val.list[i] = NULL;
        }
        msg->val.list = REALLOC(msg->val.list, sizeof(osbus_msg_t*) * num);
    }
    msg->num = num;
    return true;
}

// copy value of src to dest, src is freed
bool osbus_msg_assign(osbus_msg_t *dest, osbus_msg_t *src)
{
    if (!dest) {
        if (src) osbus_msg_free(src);
        return false;
    }
    if (!src) {
        src = osbus_msg_new_null();
    }
    osbus_msg_free_value(dest);
    // swap values
    dest->type = src->type;
    dest->val = src->val;
    dest->num = src->num;
    src->type = OSBUS_DATA_TYPE_NULL;
    MEMZERO(src->val);
    src->num = 0;
    osbus_msg_free(src);
    return true;
}

// return 0 if equal
int osbus_msg_compare(osbus_msg_t *a, osbus_msg_t *b)
{
    osbus_msg_t *e = NULL;
    osbus_msg_t *f = NULL;
    int64_t ai, bi;
    double ad, bd;
    char *as, *bs;
    char *key;
    int ret;
    int i;

    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    if (osbus_msg_is_integer(a) && osbus_msg_is_integer(b)) {
        osbus_msg_get_int64(a, &ai);
        osbus_msg_get_int64(b, &bi);
        return (ai == bi) ? 0 : ((ai < bi) ? -1 : 1);
    }
    if (a->type != b->type) {
        return a->type - b->type;
    }
    switch (a->type) {
        case OSBUS_DATA_TYPE_OBJECT:
            if (a->num != b->num) {
                return a->num - b->num;
            }
            osbus_msg_foreach_prop(a, key, e) {
                f = osbus_msg_get_prop(b, key);
                ret = osbus_msg_compare(e, f);
                if (ret != 0) return ret;
            }
            return 0;
        case OSBUS_DATA_TYPE_ARRAY:
            if (a->num != b->num) {
                return a->num - b->num;
            }
            osbus_msg_foreach_item(a, i, e) {
                f = osbus_msg_get_item(b, i);
                ret = osbus_msg_compare(e, f);
                if (ret != 0) return ret;
            }
            return 0;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            return 0;
        case OSBUS_DATA_TYPE_BOOL:
            return (int)a->val.v_bool - (int)a->val.v_bool;
        case OSBUS_DATA_TYPE_DOUBLE:
            ad = a->val.v_double;
            bd = a->val.v_double;
            return (ad == bd) ? 0 : ((ad < bd) ? -1 : 1);
        case OSBUS_DATA_TYPE_STRING:
            as = a->val.v_string;
            bs = b->val.v_string;
            if (!as && !bs) return 0;
            if (!as) return -1;
            if (!bs) return 1;
            return strcmp(as, bs);
        case OSBUS_DATA_TYPE_BINARY:
            if (a->num != b->num) {
                return a->num - b->num;
            }
            if (a->num == 0) {
                return 0;
            }
            return memcmp(a->val.v_buf, b->val.v_buf, a->num);
        default:
            LOGD("%s: unk type: %d", __func__, a->type);
            break;
    }
    return -1;
}


size_t base64_encode_size(size_t input_size)
{
    // +1 for nul string termination
    return (((input_size + 2) / 3) * 4) + 1;
}

/* json base64 encoded binary type:
{
	"#_type": "bin",
	"#_enc": "base64",
	"#_data": "c2FtcGxlCg=="
}
*/

bool osbus_msg_encode_binary_obj(const osbus_msg_t *data, osbus_msg_t **encoded)
{
    if (!data || !encoded) return false;
    if (data->type != OSBUS_DATA_TYPE_BINARY) return false;
    bool retval = false;
    osbus_msg_t *e = NULL;
    int esize = base64_encode_size(data->num);
    char *estr = malloc(esize);
    if (estr == NULL) goto out;
    if (base64_encode(estr, esize, data->val.v_buf, data->num) < 0) goto out;
    e = osbus_msg_new_object();
    if (!e) goto out;
    if (!osbus_msg_set_prop_string(e, "#_type", "bin")) goto out;
    if (!osbus_msg_set_prop_string(e, "#_enc", "base64")) goto out;
    if (!osbus_msg_set_prop_string(e, "#_data", estr)) goto out;
    retval = true;
out:
    if (!retval) {
        osbus_msg_free(e);
        e = NULL;
    }
    *encoded = e;
    free(estr);
    return retval;
}

bool osbus_msg_decode_binary_obj(const osbus_msg_t *data, osbus_msg_t **decoded)
{
    if (!data || !decoded) return false;
    bool retval = false;
    osbus_msg_t *e = NULL;
    uint8_t *buf = NULL;
    if (data->type != OSBUS_DATA_TYPE_OBJECT) goto out;
    if (data->num != 3) goto out;
    const char *s_type = NULL;
    const char *s_enc = NULL;
    const char *s_data = NULL;
    if (!osbus_msg_get_prop_string(data, "#_type", &s_type)) goto out;
    if (strcmp(s_type, "bin") != 0) goto out;
    if (!osbus_msg_get_prop_string(data, "#_enc", &s_enc)) goto out;
    if (strcmp(s_enc, "base64") != 0) goto out;
    if (!osbus_msg_get_prop_string(data, "#_data", &s_data)) goto out;
    int s_data_size = strlen(s_data);
    int buf_size;
    buf = malloc(s_data_size);
    buf_size = base64_decode(buf, s_data_size, (char*)s_data);
    if (buf_size < 0) goto out;
    e = osbus_msg_new_binary(buf, buf_size);
    if (!e) goto out;
    *decoded = e;
    retval = true;
out:
    free(buf);
    return retval;
}


struct _osbus_msg_strbuf {
    bool fixed;
    char *str;
    int len;
    int size;
    bool truncated;
};

static bool strbuf_printf(struct _osbus_msg_strbuf *sb, const char  *fmt, ...)
{
    va_list args;
    int len;
    int size;
    char *p;
    if (sb->truncated) return false;
    if (sb->fixed) {
        size = sb->size - sb->len;
    } else {
        va_start(args, fmt);
        len = vsnprintf(NULL, 0, fmt, args);
        va_end(args);
        size = len + 1;
        p = REALLOC(sb->str, sb->len + size);
        sb->str = p;
    }
    va_start(args, fmt);
    len = vsnprintf(sb->str + sb->len, size, fmt, args);
    va_end(args);
    if (len >= size) {
        sb->truncated = true;
        return false;
    }
    sb->len += len;
    return true;
}

#define STRBUF_PRINTF_RET(...) ({ if (!strbuf_printf(__VA_ARGS__)) return false; true; })


static bool osbus_msg_to_dbg_strbuf(const osbus_msg_t *d, struct _osbus_msg_strbuf *sb, osbus_msg_to_dbg_str_flags_t flags)
{
    if (!sb) return false;
    if (!d) {
        STRBUF_PRINTF_RET(sb, "(NULL)");
        return false;
    }
    osbus_msg_type type = d->type;
    osbus_msg_t *e;
    bool retval = true;

    switch (type) {
        // container:
        case OSBUS_DATA_TYPE_OBJECT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_ARRAY:
            STRBUF_PRINTF_RET(sb, "%s", type == OSBUS_DATA_TYPE_OBJECT ? "{" : "[");
            if (flags.indent) STRBUF_PRINTF_RET(sb, "\n");
            osbus_msg_to_dbg_str_flags_t f2 = flags;
            f2.cur_indent += flags.indent;
            osbus_msg_foreach(d, e) {
                if (flags.indent) STRBUF_PRINTF_RET(sb, "%*.s", f2.cur_indent, "");
                if (type == OSBUS_DATA_TYPE_OBJECT) {
                    char *name = osbus_msg_get_name(e);
                    if (name) {
                        STRBUF_PRINTF_RET(sb, "\"%s\":", name);
                        if (!flags.compact) STRBUF_PRINTF_RET(sb, " ");
                    }
                }
                if (!osbus_msg_to_dbg_strbuf(e, sb, f2)) {
                    retval = false;
                }
                if (_i < d->num - 1) {
                    STRBUF_PRINTF_RET(sb, ",");
                    if (!flags.indent && !flags.compact) STRBUF_PRINTF_RET(sb, " ");
                }
                if (flags.indent) STRBUF_PRINTF_RET(sb, "\n");
            }
            if (flags.indent) STRBUF_PRINTF_RET(sb, "%*.s", flags.cur_indent, "");
            STRBUF_PRINTF_RET(sb, "%s", type == OSBUS_DATA_TYPE_OBJECT ? "}" : "]");
            break;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            STRBUF_PRINTF_RET(sb, "null");
            break;
        case OSBUS_DATA_TYPE_INT:
            STRBUF_PRINTF_RET(sb, "%d", d->val.v_int);
            break;
        case OSBUS_DATA_TYPE_INT64:
            STRBUF_PRINTF_RET(sb, "%"PRId64, d->val.v_int64);
            break;
        case OSBUS_DATA_TYPE_BOOL:
            STRBUF_PRINTF_RET(sb, "%s", d->val.v_bool ? "true" : "false");
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%.17g", d->val.v_double);
                STRBUF_PRINTF_RET(sb, "%s", tmp);
                if (!strchr(tmp, '.')) {
                    // append .0 to distinguish float from int
                    STRBUF_PRINTF_RET(sb, ".0");
                }
            }
            break;
        case OSBUS_DATA_TYPE_STRING:
            STRBUF_PRINTF_RET(sb, "\"%s\"", d->val.v_string);
            break;
        case OSBUS_DATA_TYPE_BINARY:
            {
                if (sb->fixed && (d->num >= (sb->size - sb->len))) {
                    // if the content does not fit into the string buffer
                    // print just the binary buffer size.
                    // note, this does not need to be proper json
                    // as it is only used for debug logs
                    STRBUF_PRINTF_RET(sb, "BIN[%d]", d->num);
                    break;
                }
                osbus_msg_t *encoded = NULL;
                if (!osbus_msg_encode_binary_obj(d, &encoded)) {
                    retval = false;
                } else {
                    retval = osbus_msg_to_dbg_strbuf(encoded, sb, flags);
                    osbus_msg_free(encoded);
                }
            }
            break;
        default:
            STRBUF_PRINTF_RET(sb, "UNK:%d", type);
            retval = false;
            break;
    }
    return retval;
}

char* osbus_msg_to_dbg_str_flags(const osbus_msg_t *d, osbus_msg_to_dbg_str_flags_t flags)
{
    struct _osbus_msg_strbuf sb = {0};
    if (!osbus_msg_to_dbg_strbuf(d, &sb, flags)) {
        free(sb.str);
        sb.str = NULL;
    }
    return sb.str;
}

char* osbus_msg_to_dbg_str_indent(const osbus_msg_t *d, int indent)
{
    osbus_msg_to_dbg_str_flags_t flags = {.indent = indent};
    return osbus_msg_to_dbg_str_flags(d, flags);
}

char* osbus_msg_to_dbg_str_compact(const osbus_msg_t *d)
{
    osbus_msg_to_dbg_str_flags_t flags = {.compact = 1};
    return osbus_msg_to_dbg_str_flags(d, flags);
}

char* osbus_msg_to_dbg_str(const osbus_msg_t *d)
{
    osbus_msg_to_dbg_str_flags_t flags = {0};
    return osbus_msg_to_dbg_str_flags(d, flags);
}

bool osbus_msg_to_dbg_str_fixed(const osbus_msg_t *d, char *str, int size)
{
    if (!str || size == 0) return false;
    osbus_msg_to_dbg_str_flags_t flags = {.compact = 1};
    struct _osbus_msg_strbuf sb = {0};
    sb.fixed = true;
    sb.str = str;
    sb.size = size;
    *str = 0;
    return osbus_msg_to_dbg_strbuf(d, &sb, flags);
}

json_t* osbus_msg_to_json(const osbus_msg_t *data)
{
    if (!data) return NULL;
    osbus_msg_type type = data->type;
    osbus_msg_t *e = NULL;
    json_t *j = NULL;
    json_t *v = NULL;

    switch (type) {
        // container:
        case OSBUS_DATA_TYPE_OBJECT:
            j = json_object();
            osbus_msg_foreach(data, e) {
                if ((v = osbus_msg_to_json(e))) {
                    json_object_set_new(j, e->name, v);
                } else {
                    json_decref(j);
                    j = NULL;
                    break;
                }
            }
            break;
        case OSBUS_DATA_TYPE_ARRAY:
            j = json_array();
            osbus_msg_foreach(data, e) {
                if ((v = osbus_msg_to_json(e))) {
                    json_array_append_new(j, v);
                } else {
                    json_decref(j);
                    j = NULL;
                    break;
                }
            }
            break;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            j = json_null();
            break;
        case OSBUS_DATA_TYPE_BOOL:
            j = json_boolean(data->val.v_bool);
            break;
        case OSBUS_DATA_TYPE_INT:
            j = json_integer(data->val.v_int);
            break;
        case OSBUS_DATA_TYPE_INT64:
            j = json_integer(data->val.v_int64);
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            j = json_real(data->val.v_double);
            break;
        case OSBUS_DATA_TYPE_STRING:
            j = json_string(data->val.v_string);
            break;
        case OSBUS_DATA_TYPE_BINARY:
            {
                osbus_msg_t *encoded = NULL;
                if (osbus_msg_encode_binary_obj(data, &encoded)) {
                    j = osbus_msg_to_json(encoded);
                    osbus_msg_free(encoded);
                }
            }
            break;
        default:
            LOGD("%s: unk type: %d", __func__, type);
            break;
    }

    return j;
}

osbus_msg_t* osbus_msg_from_json(const json_t *json)
{
    if (!json) return false;

    osbus_msg_t *d = NULL;
    osbus_msg_t *e = NULL;
    const char *key = NULL;
    const json_t *jval = NULL;
    int jtype = json_typeof(json);
    size_t ji;
    json_int_t jint;

    switch (jtype) {
        case JSON_OBJECT:
            d = osbus_msg_new_object();
            json_object_foreach((json_t*)json, key, jval) {
                if ((e = osbus_msg_from_json(jval))) {
                    osbus_msg_set_prop(d, (char*)key, e);
                } else {
                    osbus_msg_free(d);
                    d = NULL;
                    break;
                }
            }
            // try binary decode
            osbus_msg_t *decoded = NULL;
            if (osbus_msg_decode_binary_obj(d, &decoded)) {
                osbus_msg_free(d);
                d = decoded;
            }
            break;
        case JSON_ARRAY:
            d = osbus_msg_new_array();
            json_array_foreach((json_t*)json, ji, jval) {
                if ((e = osbus_msg_from_json(jval))) {
                    osbus_msg_add_item(d, e);
                } else {
                    osbus_msg_free(d);
                    d = NULL;
                    break;
                }
            }
            break;
        case JSON_NULL:
            d = osbus_msg_new_null();
            break;
        case JSON_TRUE:
            d = osbus_msg_new_bool(true);
            break;
        case JSON_FALSE:
            d = osbus_msg_new_bool(false);
            break;
        case JSON_INTEGER:
            jint = json_integer_value(json);
            if (jint >= INT_MIN && jint <= INT_MAX) {
                d = osbus_msg_new_int(jint);
            } else {
                d = osbus_msg_new_int64(jint);
            }
            break;
        case JSON_REAL:
            d = osbus_msg_new_double(json_real_value(json));
            break;
        case JSON_STRING:
            d = osbus_msg_new_string(json_string_value(json));
            break;
        default:
            LOGD("%s: unk jtype: %d", __func__, jtype);
            break;
    }

    return d;
}

char* osbus_msg_to_json_string_flags(const osbus_msg_t *data, size_t jansson_dumps_flags)
{
    if (!data) return NULL;
    char *str = NULL;
    json_t *json = NULL;
    char *json_str = NULL;
    if (!(json = osbus_msg_to_json(data))) return false;
    json_str = json_dumps(json, jansson_dumps_flags);
    json_decref(json);
    if (!json_str) return NULL;
    // make a copy, so that normal free() can be used
    // on the resulting string instead of json_free()
    str = STRDUP(json_str);
    json_free(json_str);
    return str;
}

char* osbus_msg_to_json_string(const osbus_msg_t *data)
{
    return osbus_msg_to_json_string_flags(data, JSON_COMPACT | JSON_ENCODE_ANY);
}

osbus_msg_t* osbus_msg_from_json_string(const char *str)
{
    if (!str) return NULL;
    return osbus_msg_from_json_string_buf(str, strlen(str));
}

osbus_msg_t* osbus_msg_from_json_string_buf(const char *str, int size)
{
    if (!str) return NULL;
    osbus_msg_t *msg = NULL;
    json_error_t json_error = {0};
    json_t *json = json_loadb(str, size, JSON_DECODE_ANY | JSON_ALLOW_NUL, &json_error);
    if (!json) {
        LOGD("%s: %s", __func__, json_error.text);
        return NULL;
    }
    /*
    char *ref = json_dumps(json, JSON_ENCODE_ANY | JSON_COMPACT);
    LOGT("%s json_dump: %s", __func__, ref);
    json_free(ref);
    */
    if (!(msg = osbus_msg_from_json(json))) {
        LOGD("%s: msg_from_json", __func__);
    }
    json_decref(json);
    return msg;
}

static bool _osbus_msg_token_is_arr_idx(const char *token, const char *darr, int *idx)
{
    // check if token string is an array index eg "[123]"
    char *a = NULL;
    char *b = NULL;
    char *end = NULL;
    int len = strlen(token);
    int i;
    if (len == 0) return false;
    if (len < 3) return false;
    if ((a = strchr(darr, *token)) == NULL) return false;
    if ((b = strchr(darr, token[len - 1])) == NULL) return false;
    i = strtol(token + 1, &end, 10);
    if (end != token + len - 1) return false;
    if (i < 0) return false;
    // token is an array index
    *idx = i;
    return true;
}

osbus_msg_t* osbus_msg_lookup_ex(const osbus_msg_t *msg, const char *path, const char *dobj, const char *darr)
{
    if (!msg || !path || !dobj || !darr) return NULL;
    osbus_msg_t *m = (osbus_msg_t*)msg;
    osbus_msg_t *ret = NULL;
    char *pstr = strdupa(path);
    ASSERT(pstr, "strdupa");
    char *token = pstr;
    int i;
    while ((token = strsep(&pstr, dobj)) != NULL) {
        if (!*token) return NULL; // empty token
        if (_osbus_msg_token_is_arr_idx(token, darr, &i)) {
            // token is array index
            if (m->type != OSBUS_DATA_TYPE_ARRAY) {
                LOGT("%s: %s not array", __func__, token);
                return NULL;
            }
            if (i >= m->num) {
                LOGT("%s: %s out of range %d", __func__, token, msg->num);
                return NULL;
            }
            m = osbus_msg_get_item(m, i);
        } else {
            // token is property name
            m = osbus_msg_get_prop(m, token);
            if (!m) {
                LOGT("%s: %s not found", __func__, token);
            }
        }
        if (!m) return NULL;
        ret = m;
    }
    return ret;
}

// lookup nested property
// example: osbus_msg_lookup(msg, "a.b.[3].c");
// returns ptr to last token
osbus_msg_t* osbus_msg_lookup(const osbus_msg_t *msg, const char *path)
{
    return osbus_msg_lookup_ex(msg, path, ".", "[]{}");
}

osbus_msg_t* osbus_msg_mkpath_ex(osbus_msg_t *msg, const char *path, const char *dobj, const char *darr)
{
    if (!msg || !path || !dobj || !darr) return NULL;
    osbus_msg_t *m = msg;
    osbus_msg_t *tmp = NULL;
    osbus_msg_t *ret = NULL;
    char *pstr = strdupa(path);
    ASSERT(pstr, "strdupa");
    char *token = pstr;
    int i;
    while ((token = strsep(&pstr, dobj)) != NULL) {
        if (!*token) return NULL; // empty token
        if (_osbus_msg_token_is_arr_idx(token, darr, &i)) {
            // token is array index
            if (m->type == OSBUS_DATA_TYPE_NULL) {
                osbus_msg_set_type(m, OSBUS_DATA_TYPE_ARRAY);
            }
            if (m->type != OSBUS_DATA_TYPE_ARRAY) {
                LOGT("%s: %s not array", __func__, token);
                return NULL;
            }
            if (i >= m->num) {
                if (!osbus_msg_array_resize(m, i + 1)) {
                    LOGT("%s: %s resize", __func__, token);
                    return NULL;
                }
            }
            m = osbus_msg_get_item(m, i);
        } else {
            // token is property name
            if (m->type == OSBUS_DATA_TYPE_NULL) {
                osbus_msg_set_type(m, OSBUS_DATA_TYPE_OBJECT);
            }
            if (m->type != OSBUS_DATA_TYPE_OBJECT) {
                LOGT("%s: %s not object", __func__, token);
                return NULL;
            }
            tmp = osbus_msg_get_prop(m, token);
            if (tmp) {
                m = tmp;
            } else {
                m = osbus_msg_set_prop_null(m, token);
            }
        }
        if (!m) return NULL;
        ret = m;
    }
    return ret;
}

// create nested property
// example: osbus_msg_mkpath(msg, "a.b.[3].c");
// returns ptr to last token
osbus_msg_t* osbus_msg_mkpath(osbus_msg_t *msg, const char *path)
{
    return osbus_msg_mkpath_ex(msg, path, ".", "[]{}");
}


