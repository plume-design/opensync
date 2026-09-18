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

#include <ox_ovsdb_type.h>
#include <string.h>

json_t *ox_ovsdb_type_borrow_compound(json_t *value, const char *identifier)
{
    json_t *type_borrowed = json_array_get(value, 0);
    json_t *data_borrowed = json_array_get(value, 1);
    if (type_borrowed == NULL) return NULL;
    if (json_string_value(type_borrowed) == NULL) return NULL;
    if (strcmp(json_string_value(type_borrowed), identifier) != 0) return NULL;
    return data_borrowed;
}

json_t *ox_ovsdb_type_borrow_set(json_t *value)
{
    return ox_ovsdb_type_borrow_compound(value, "set");
}

json_t *ox_ovsdb_type_borrow_map(json_t *value)
{
    return ox_ovsdb_type_borrow_compound(value, "map");
}

const char *ox_ovsdb_type_borrow_uuid(json_t *value)
{
    return json_string_value(ox_ovsdb_type_borrow_compound(value, "uuid"));
}

json_t *ox_ovsdb_type_map_borrow_by_key(json_t *map, const char *key)
{
    json_t *kv_pairs_borrowed = ox_ovsdb_type_borrow_map(map);
    if (kv_pairs_borrowed == NULL) return NULL;
    size_t index;
    json_t *kv_pair_borrowed;
    json_array_foreach(kv_pairs_borrowed, index, kv_pair_borrowed)
    {
        json_t *key_borrowed = json_array_get(kv_pair_borrowed, 0);
        json_t *val_borrowed = json_array_get(kv_pair_borrowed, 1);
        if (key_borrowed == NULL) continue;
        if (val_borrowed == NULL) continue;
        if (json_string_value(key_borrowed) == NULL) continue;
        if (strcmp(json_string_value(key_borrowed), key) == 0)
        {
            return val_borrowed;
        }
    }
    return NULL;
}

json_t *ox_ovsdb_type_map_to_object(json_t *map_borrowed)
{
    json_t *kv_pairs_borrowed = ox_ovsdb_type_borrow_map(map_borrowed);
    if (kv_pairs_borrowed == NULL) return NULL;
    json_t *kv_store = json_object();
    size_t index;
    json_t *kv_pair_borrowed;
    json_array_foreach(kv_pairs_borrowed, index, kv_pair_borrowed)
    {
        json_t *key_borrowed = json_array_get(kv_pair_borrowed, 0);
        json_t *val_borrowed = json_array_get(kv_pair_borrowed, 1);
        if (key_borrowed == NULL) continue;
        if (val_borrowed == NULL) continue;
        const char *key = json_string_value(key_borrowed);
        if (key == NULL) continue;
        json_object_set_new(kv_store, key, json_incref(val_borrowed));
    }
    return kv_store;
}

json_t *ox_ovsdb_type_set_with_array(json_t *array_moved)
{
    json_t *set = json_array();
    json_array_append_new(set, json_string("set"));
    json_array_append_new(set, array_moved ?: json_array());
    return set;
}
