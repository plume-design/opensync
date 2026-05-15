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

json_t *osbus_msg_to_json(const osbus_msg_t *data)
{
    if (!data) return NULL;
    osbus_msg_type type = data->type;
    osbus_msg_t *e = NULL;
    json_t *j = NULL;
    json_t *v = NULL;

    switch (type)
    {
        // container:
        case OSBUS_DATA_TYPE_OBJECT:
            j = json_object();
            osbus_msg_foreach(data, e)
            {
                if ((v = osbus_msg_to_json(e)))
                {
                    json_object_set_new(j, e->name, v);
                }
                else
                {
                    json_decref(j);
                    j = NULL;
                    break;
                }
            }
            break;
        case OSBUS_DATA_TYPE_ARRAY:
            j = json_array();
            osbus_msg_foreach(data, e)
            {
                if ((v = osbus_msg_to_json(e)))
                {
                    json_array_append_new(j, v);
                }
                else
                {
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
        case OSBUS_DATA_TYPE_BINARY: {
            osbus_msg_t *encoded = NULL;
            if (osbus_msg_encode_binary_obj(data, &encoded))
            {
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

osbus_msg_t *osbus_msg_from_json(const json_t *json)
{
    if (!json) return false;

    osbus_msg_t *d = NULL;
    osbus_msg_t *e = NULL;
    const char *key = NULL;
    const json_t *jval = NULL;
    int jtype = json_typeof(json);
    size_t ji;
    json_int_t jint;

    switch (jtype)
    {
        case JSON_OBJECT:
            d = osbus_msg_new_object();
            json_object_foreach((json_t *)json, key, jval)
            {
                if ((e = osbus_msg_from_json(jval)))
                {
                    osbus_msg_set_prop(d, (char *)key, e);
                }
                else
                {
                    osbus_msg_free(d);
                    d = NULL;
                    break;
                }
            }
            // try binary decode
            osbus_msg_t *decoded = NULL;
            if (osbus_msg_decode_binary_obj(d, &decoded))
            {
                osbus_msg_free(d);
                d = decoded;
            }
            break;
        case JSON_ARRAY:
            d = osbus_msg_new_array();
            json_array_foreach((json_t *)json, ji, jval)
            {
                if ((e = osbus_msg_from_json(jval)))
                {
                    osbus_msg_add_item(d, e);
                }
                else
                {
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
            if (jint >= INT_MIN && jint <= INT_MAX)
            {
                d = osbus_msg_new_int(jint);
            }
            else
            {
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

char *osbus_msg_to_json_string_flags(const osbus_msg_t *data, size_t jansson_dumps_flags)
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

char *osbus_msg_to_json_string(const osbus_msg_t *data)
{
    return osbus_msg_to_json_string_flags(data, JSON_COMPACT | JSON_ENCODE_ANY);
}

osbus_msg_t *osbus_msg_from_json_string(const char *str)
{
    if (!str) return NULL;
    return osbus_msg_from_json_string_buf(str, strlen(str));
}

osbus_msg_t *osbus_msg_from_json_string_buf(const char *str, int size)
{
    if (!str) return NULL;
    osbus_msg_t *msg = NULL;
    json_error_t json_error = {0};
    json_t *json = json_loadb(str, size, JSON_DECODE_ANY | JSON_ALLOW_NUL, &json_error);
    if (!json)
    {
        LOGD("%s: %s", __func__, json_error.text);
        return NULL;
    }
    /*
    char *ref = json_dumps(json, JSON_ENCODE_ANY | JSON_COMPACT);
    LOGT("%s json_dump: %s", __func__, ref);
    json_free(ref);
    */
    if (!(msg = osbus_msg_from_json(json)))
    {
        LOGD("%s: msg_from_json", __func__);
    }
    json_decref(json);
    return msg;
}
