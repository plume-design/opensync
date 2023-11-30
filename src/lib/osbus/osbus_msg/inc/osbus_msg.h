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

#ifndef OSBUS_DATA_H_INCLUDED
#define OSBUS_DATA_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <jansson.h>

#include "ds_tree.h"

#define OSBUS_DBG_STR_SIZE 256

typedef enum osbus_msg_type
{
    OSBUS_DATA_TYPE_NULL,
    OSBUS_DATA_TYPE_OBJECT,
    OSBUS_DATA_TYPE_ARRAY,
    OSBUS_DATA_TYPE_BOOL,
    OSBUS_DATA_TYPE_INT,
    OSBUS_DATA_TYPE_INT64,
    OSBUS_DATA_TYPE_DOUBLE,
    OSBUS_DATA_TYPE_STRING,
    OSBUS_DATA_TYPE_BINARY,
} osbus_msg_type;

struct osbus_msg;
typedef struct osbus_msg osbus_msg_t;

struct osbus_msg
{
    osbus_msg_type type;
    char *name;
    ds_tree_t tree;
    ds_tree_node_t node;
    int idx;
    union {
        bool    v_bool;
        int     v_int;
        int64_t v_int64;
        double  v_double;
        char    *v_string;
        uint8_t *v_buf;
        osbus_msg_t **list;
    } val;
    int num;
};

struct osbus_msg_policy
{
    char            *name;
    osbus_msg_type  type;
    bool            required;
};

typedef struct osbus_msg_policy osbus_msg_policy_t;

osbus_msg_t*    osbus_msg_new_null  (void);
osbus_msg_t*    osbus_msg_new_object(void);
osbus_msg_t*    osbus_msg_new_array (void);
osbus_msg_t*    osbus_msg_new_bool  (bool val);
osbus_msg_t*    osbus_msg_new_int   (int val);
osbus_msg_t*    osbus_msg_new_int64 (int64_t val);
osbus_msg_t*    osbus_msg_new_double(double val);
osbus_msg_t*    osbus_msg_new_string(const char *val);
osbus_msg_t*    osbus_msg_new_binary(const uint8_t *buf, int size);
osbus_msg_t*    osbus_msg_new_type  (osbus_msg_type type);

void            osbus_msg_free(osbus_msg_t *data);
void            osbus_msg_free_value(osbus_msg_t *data);

bool            osbus_msg_is_container(const osbus_msg_t *data);
bool            osbus_msg_is_number(const osbus_msg_t *data);
bool            osbus_msg_is_integer(const osbus_msg_t *data);
char*           osbus_msg_type_str(osbus_msg_type type);

// set
bool            osbus_msg_set_type  (osbus_msg_t *data, osbus_msg_type type);
bool            osbus_msg_set_bool  (osbus_msg_t *data, bool val);
bool            osbus_msg_set_int   (osbus_msg_t *data, int val);
bool            osbus_msg_set_int64 (osbus_msg_t *data, int64_t val);
bool            osbus_msg_set_double(osbus_msg_t *data, double val);
bool            osbus_msg_set_string(osbus_msg_t *data, const char *val);
bool            osbus_msg_set_binary(osbus_msg_t *data, const uint8_t *buf, int size);

// get
osbus_msg_type  osbus_msg_get_type        (const osbus_msg_t *data);
char*           osbus_msg_get_name        (const osbus_msg_t *data);
bool            osbus_msg_get_bool        (const osbus_msg_t *data, bool *val);
bool            osbus_msg_get_int         (const osbus_msg_t *data, int *val);
bool            osbus_msg_get_int64       (const osbus_msg_t *data, int64_t *val);
bool            osbus_msg_get_double      (const osbus_msg_t *data, double *val);
bool            osbus_msg_get_string      (const osbus_msg_t *data, const char **val); // reference to string
bool            osbus_msg_get_string_alloc(const osbus_msg_t *data, char **str);
bool            osbus_msg_get_string_fixed(const osbus_msg_t *data, char *str, int size);
bool            osbus_msg_get_binary      (const osbus_msg_t *data, const uint8_t **buf, int *size); // reference to buf
bool            osbus_msg_get_binary_alloc(const osbus_msg_t *data, uint8_t **buf, int *size);
bool            osbus_msg_get_binary_fixed(const osbus_msg_t *data, uint8_t *buf, int max_buf_size, int *size);

// arrays contain unnamed value items
osbus_msg_t*    osbus_msg_add_item       (osbus_msg_t *data, osbus_msg_t *e);
osbus_msg_t*    osbus_msg_add_item_type  (osbus_msg_t *data, osbus_msg_type type);
osbus_msg_t*    osbus_msg_add_item_object(osbus_msg_t *data);
osbus_msg_t*    osbus_msg_add_item_array (osbus_msg_t *data);
osbus_msg_t*    osbus_msg_add_item_null  (osbus_msg_t *data);
osbus_msg_t*    osbus_msg_add_item_bool  (osbus_msg_t *data, bool val);
osbus_msg_t*    osbus_msg_add_item_int   (osbus_msg_t *data, int val);
osbus_msg_t*    osbus_msg_add_item_int64 (osbus_msg_t *data, int64_t val);
osbus_msg_t*    osbus_msg_add_item_double(osbus_msg_t *data, double val);
osbus_msg_t*    osbus_msg_add_item_string(osbus_msg_t *data, const char *val);
osbus_msg_t*    osbus_msg_add_item_binary(osbus_msg_t *data, const uint8_t *buf, int size);
osbus_msg_t*    osbus_msg_get_item       (const osbus_msg_t *data, int i);
int             osbus_msg_item_count     (const osbus_msg_t *data);

// objects contain named key/value properties
osbus_msg_t*    osbus_msg_set_prop       (osbus_msg_t *data, const char *name, osbus_msg_t *e);
osbus_msg_t*    osbus_msg_set_prop_type  (osbus_msg_t *data, const char *name, osbus_msg_type type);
osbus_msg_t*    osbus_msg_set_prop_object(osbus_msg_t *data, const char *name);
osbus_msg_t*    osbus_msg_set_prop_array (osbus_msg_t *data, const char *name);
osbus_msg_t*    osbus_msg_set_prop_null  (osbus_msg_t *data, const char *name);
osbus_msg_t*    osbus_msg_set_prop_bool  (osbus_msg_t *data, const char *name, bool val);
osbus_msg_t*    osbus_msg_set_prop_int   (osbus_msg_t *data, const char *name, int val);
osbus_msg_t*    osbus_msg_set_prop_int64 (osbus_msg_t *data, const char *name, int64_t val);
osbus_msg_t*    osbus_msg_set_prop_double(osbus_msg_t *data, const char *name, double val);
osbus_msg_t*    osbus_msg_set_prop_string(osbus_msg_t *data, const char *name, const char *val);
osbus_msg_t*    osbus_msg_set_prop_binary(osbus_msg_t *data, const char *name, const uint8_t *buf, int size);

osbus_msg_t*    osbus_msg_get_prop             (const osbus_msg_t *data, const char *name);
bool            osbus_msg_get_prop_bool        (const osbus_msg_t *data, const char *name, bool *val);
bool            osbus_msg_get_prop_int         (const osbus_msg_t *data, const char *name, int *val);
bool            osbus_msg_get_prop_int64       (const osbus_msg_t *data, const char *name, int64_t *val);
bool            osbus_msg_get_prop_double      (const osbus_msg_t *data, const char *name, double *val);
bool            osbus_msg_get_prop_string      (const osbus_msg_t *data, const char *name, const char **val);
bool            osbus_msg_get_prop_string_alloc(const osbus_msg_t *data, const char *name, char **str);
bool            osbus_msg_get_prop_string_fixed(const osbus_msg_t *data, const char *name, char *str, int size);
bool            osbus_msg_get_prop_binary      (const osbus_msg_t *data, const char *name, const uint8_t **buf, int *size);
bool            osbus_msg_get_prop_binary_alloc(const osbus_msg_t *data, const char *name, uint8_t **buf, int *size);
bool            osbus_msg_get_prop_binary_fixed(const osbus_msg_t *data, const char *name, uint8_t *buf, int max_buf_size, int *size);

bool            osbus_msg_map_prop_bool        (bool set, osbus_msg_t *data, const char *name, bool *val);
bool            osbus_msg_map_prop_int         (bool set, osbus_msg_t *data, const char *name, int *val);
bool            osbus_msg_map_prop_int64       (bool set, osbus_msg_t *data, const char *name, int64_t *val);
bool            osbus_msg_map_prop_double      (bool set, osbus_msg_t *data, const char *name, double *val);
bool            osbus_msg_map_prop_string      (bool set, osbus_msg_t *data, const char *name, const char **val);
bool            osbus_msg_map_prop_string_alloc(bool set, osbus_msg_t *data, const char *name, char **val);
bool            osbus_msg_map_prop_string_fixed(bool set, osbus_msg_t *data, const char *name, char *str, int size);
bool            osbus_msg_map_prop_binary      (bool set, osbus_msg_t *data, const char *name, const uint8_t **buf, int *size);
bool            osbus_msg_map_prop_binary_alloc(bool set, osbus_msg_t *data, const char *name, uint8_t **buf, int *size);
bool            osbus_msg_map_prop_binary_fixed(bool set, osbus_msg_t *data, const char *name, uint8_t *buf, int max_buf_size, int *size);

typedef bool    osbus_msg_map_bool_fn_t(bool set, bool *bool_val, void *fn_val);
bool            osbus_msg_map_prop_bool_fn(bool set, osbus_msg_t *data, const char *name, void *fn_val, osbus_msg_map_bool_fn_t *map_fn);

typedef bool    osbus_msg_map_string_fn_t(bool set, const char **str, void *fn_val);
bool            osbus_msg_map_prop_string_fn(bool set, osbus_msg_t *data, const char *name, void *fn_val, osbus_msg_map_string_fn_t *map_fn);

// conversions

bool            osbus_msg_array_resize(osbus_msg_t *msg, int num);
osbus_msg_t*    osbus_msg_copy(const osbus_msg_t *data);
bool            osbus_msg_assign(osbus_msg_t *dest, osbus_msg_t *src);
int             osbus_msg_compare(osbus_msg_t *a, osbus_msg_t *b);

bool            osbus_msg_encode_binary_obj(const osbus_msg_t *data, osbus_msg_t **encoded);
bool            osbus_msg_decode_binary_obj(const osbus_msg_t *data, osbus_msg_t **decoded);

typedef struct
{
    bool compact;
    char indent;
    short cur_indent;
} osbus_msg_to_dbg_str_flags_t;

char*           osbus_msg_to_dbg_str_flags  (const osbus_msg_t *d, osbus_msg_to_dbg_str_flags_t flags);
char*           osbus_msg_to_dbg_str_indent (const osbus_msg_t *d, int indent);
char*           osbus_msg_to_dbg_str_compact(const osbus_msg_t *d);
char*           osbus_msg_to_dbg_str        (const osbus_msg_t *d);
bool            osbus_msg_to_dbg_str_fixed  (const osbus_msg_t *d, char *str, int size);

char*           osbus_msg_to_json_string_flags(const osbus_msg_t *data, size_t jansson_dumps_flags);
char*           osbus_msg_to_json_string      (const osbus_msg_t *data);
osbus_msg_t*    osbus_msg_from_json_string    (const char *str);
osbus_msg_t*    osbus_msg_from_json_string_buf(const char *str, int size);

json_t*         osbus_msg_to_json  (const osbus_msg_t *data);
osbus_msg_t*    osbus_msg_from_json(const json_t *json);

osbus_msg_t* osbus_msg_lookup_ex(const osbus_msg_t *msg, const char *path, const char *dobj, const char *darr);
osbus_msg_t* osbus_msg_lookup(const osbus_msg_t *msg, const char *path);
osbus_msg_t* osbus_msg_mkpath_ex(osbus_msg_t *msg, const char *path, const char *dobj, const char *darr);
osbus_msg_t* osbus_msg_mkpath(osbus_msg_t *msg, const char *path);

// iterators

#define osbus_msg_foreach(DATA, E) \
    for (int _i = (E = osbus_msg_get_item(DATA,0), 0); \
            _i < osbus_msg_item_count(DATA); \
            E = osbus_msg_get_item(DATA, ++_i))

#define osbus_msg_foreach_item(DATA, I, E) \
    for (I = (E = osbus_msg_get_item(DATA,0), 0); \
            I < osbus_msg_item_count(DATA); \
            E = osbus_msg_get_item(DATA, ++I))

#define osbus_msg_foreach_prop(DATA, KEY, E) \
    for (int _i = (E = osbus_msg_get_item(DATA,0), KEY = osbus_msg_get_name(E), 0); \
            _i < osbus_msg_item_count(DATA); \
            E = osbus_msg_get_item(DATA, ++_i), KEY = osbus_msg_get_name(E))

#endif /* OSBUS_DATA_H_INCLUDED */

