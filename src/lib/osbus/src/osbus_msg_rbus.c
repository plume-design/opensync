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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>

#include "log.h"
#include "os.h"
#include "os_time.h"
#include "util.h"
#include "memutil.h"
#include "const.h"
#include "osbus_msg.h"
#include "osbus_msg_rbus.h"

#include <rbus.h>

#define MODULE_ID LOG_MODULE_ID_OSBUS

bool osbus_msg_add_rbusProperty_list(osbus_msg_t *data, rbusProperty_t prop);


bool osbus_msg_to_rbus_value(const osbus_msg_t *data, rbusValue_t *pval)
{
    if (!data || !pval) return false;

    bool retval = true;
    rbusObject_t sub = NULL;
    *pval = NULL;
    switch (data->type) {
        // container:
        case OSBUS_DATA_TYPE_OBJECT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_ARRAY:
            retval = osbus_msg_to_rbus_object(data, &sub);
            if (retval) {
                *pval = rbusValue_InitObject(sub);
            }
            break;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            {
                // add a value type RBUS_NONE
                rbusValue_t v;
                rbusValue_Init(&v);
                if (!v) { retval = false; break; }
                *pval = v;
            }
            break;
        case OSBUS_DATA_TYPE_BOOL:
            *pval = rbusValue_InitBoolean(data->val.v_bool);
            break;
        case OSBUS_DATA_TYPE_INT:
            *pval = rbusValue_InitInt32(data->val.v_int);
            break;
        case OSBUS_DATA_TYPE_INT64:
            *pval = rbusValue_InitInt64(data->val.v_int64);
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            *pval = rbusValue_InitDouble(data->val.v_double);
            break;
        case OSBUS_DATA_TYPE_STRING:
            *pval = rbusValue_InitString(data->val.v_string);
            break;
        case OSBUS_DATA_TYPE_BINARY:
            *pval = rbusValue_InitBytes(data->val.v_buf, data->num);
            break;
        default:
            retval = false;
            break;
    }
    if (!*pval) retval = false;

    return retval;
}


bool osbus_msg_from_rbus_value(osbus_msg_t **data, rbusValue_t v)
{
    if (!data || !v) return false;
    bool retval = false;
    rbusValueType_t type = rbusValue_GetType(v);
    osbus_msg_t *d = NULL;
    *data = NULL;
    switch (type) {
        case RBUS_NONE:
            d = osbus_msg_new_null();
            break;
        case RBUS_BOOLEAN:
            d = osbus_msg_new_bool(rbusValue_GetBoolean(v));
            break;
        case RBUS_CHAR:
            d = osbus_msg_new_int(rbusValue_GetChar(v));
            break;
        case RBUS_BYTE:
            d = osbus_msg_new_int(rbusValue_GetByte(v));
            break;
        case RBUS_INT8:
            d = osbus_msg_new_int(rbusValue_GetInt8(v));
            break;
        case RBUS_UINT8:
            d = osbus_msg_new_int(rbusValue_GetUInt8(v));
            break;
        case RBUS_INT16:
            d = osbus_msg_new_int(rbusValue_GetInt16(v));
            break;
        case RBUS_UINT16:
            d = osbus_msg_new_int(rbusValue_GetUInt16(v));
            break;
        case RBUS_INT32:
            d = osbus_msg_new_int(rbusValue_GetInt32(v));
            break;
        case RBUS_UINT32:
            d = osbus_msg_new_int(rbusValue_GetUInt32(v));
            break;
        case RBUS_INT64:
            d = osbus_msg_new_int64(rbusValue_GetInt64(v));
            break;
        case RBUS_UINT64:
            d = osbus_msg_new_int64(rbusValue_GetUInt64(v));
            break;
        case RBUS_SINGLE:
            d = osbus_msg_new_double(rbusValue_GetSingle(v));
            break;
        case RBUS_DOUBLE:
            d = osbus_msg_new_double(rbusValue_GetDouble(v));
            break;
        case RBUS_STRING:
            d = osbus_msg_new_string((char*)rbusValue_GetString(v, NULL));
            break;
        case RBUS_BYTES:
            {
                uint8_t *buf;
                int size;
                buf = (uint8_t*)rbusValue_GetBytes(v, &size);
                d = osbus_msg_new_binary(buf, size);
            }
            break;

        case RBUS_DATETIME:
            // todo
            retval = false;
            break;

        case RBUS_PROPERTY:
            d = osbus_msg_new_object();
            if (!d) break;
            if (!osbus_msg_add_rbusProperty_list(d, rbusValue_GetProperty(v))) {
                osbus_msg_free(d);
                d = NULL;
            }
            break;

        case RBUS_OBJECT:
            osbus_msg_from_rbus_object(&d, rbusValue_GetObject(v));
            break;

        default:
            retval = false;
            break;
    }
    if (d) {
        retval = true;
        *data = d;
    }
    return retval;
}


bool osbus_msg_prop_to_rbusObject(const osbus_msg_t *data, char *name, rbusObject_t robj)
{
    if (!data || !robj) return false;

    bool retval = true;
    rbusObject_t sub = NULL;
    void *buf_ptr = NULL;

    if (!name) name = "";

    switch (data->type) {
        // container:
        case OSBUS_DATA_TYPE_OBJECT: /* FALLTHROUGH */
        case OSBUS_DATA_TYPE_ARRAY:
            retval = osbus_msg_to_rbus_object(data, &sub);
            if (retval) {
                rbusObject_SetPropertyObject(robj, name, sub);
            }
            break;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            {
                // add a value type RBUS_NONE
                rbusValue_t v;
                rbusValue_Init(&v);
                if (!v) { retval = false; break; }
                rbusObject_SetPropertyValue(robj, name, v);
                rbusValue_Release(v);
            }
            break;
        case OSBUS_DATA_TYPE_BOOL:
            rbusObject_SetPropertyBoolean(robj, name, data->val.v_bool);
            break;
        case OSBUS_DATA_TYPE_INT:
            rbusObject_SetPropertyInt32(robj, name, data->val.v_int);
            break;
        case OSBUS_DATA_TYPE_INT64:
            rbusObject_SetPropertyInt64(robj, name, data->val.v_int64);
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            rbusObject_SetPropertyDouble(robj, name, data->val.v_double);
            break;
        case OSBUS_DATA_TYPE_STRING:
            rbusObject_SetPropertyString(robj, name, data->val.v_string);
            break;
        case OSBUS_DATA_TYPE_BINARY:
            // rbusObject_SetPropertyBytes requires a valid data ptr even if size==0
            if (!data->num && !data->val.v_buf) buf_ptr = "";
            else buf_ptr = data->val.v_buf;
            ASSERT(buf_ptr, __func__);
            rbusObject_SetPropertyBytes(robj, name, buf_ptr, data->num);
            break;
        default:
            retval = false;
            break;
    }

    return retval;
}

bool osbus_msg_append_to_rbus_object(const osbus_msg_t *data, rbusObject_t robj)
{
    if (!data || !robj) return false;
    bool retval = false;
    osbus_msg_t *e = NULL;
    char name[32];
    int i;

    switch (data->type) {
        case OSBUS_DATA_TYPE_OBJECT:
            retval = true;
            osbus_msg_foreach(data, e) {
                retval = osbus_msg_prop_to_rbusObject(e, e->name, robj);
                if (!retval) break;
            }
            break;
        case OSBUS_DATA_TYPE_ARRAY:
            rbusObject_SetName(robj, "[]");
            retval = true;
            osbus_msg_foreach_item(data, i, e) {
                snprintf(name, sizeof(name), "%d", i);
                retval = osbus_msg_prop_to_rbusObject(e, name, robj);
                if (!retval) break;
            }
            break;
        default:
            break;
    }
out:
    return retval;
}

bool osbus_msg_to_rbus_object(const osbus_msg_t *data, rbusObject_t *p_robj)
{
    if (!data || !p_robj) return false;
    bool retval = false;
    rbusObject_t robj = NULL;

    rbusObject_Init(&robj, NULL);
    if (!robj) return false;
    retval = osbus_msg_append_to_rbus_object(data, robj);
    if (!retval) {
        rbusObject_Release(robj);
        robj = NULL;
    }
    *p_robj = robj;

    return retval;
}

bool osbus_msg_add_rbusProperty(osbus_msg_t *data, rbusProperty_t prop)
{
    if (!data || !prop) return false;
    bool retval = false;
    char *name = NULL;
    osbus_msg_t *e = NULL;
    if (data->type == OSBUS_DATA_TYPE_OBJECT) {
        name = (char*)rbusProperty_GetName(prop);
    }
    rbusValue_t v = rbusProperty_GetValue(prop);
    rbusValueType_t type = rbusValue_GetType(v);
    switch (type) {
        case RBUS_NONE:
            retval = osbus_msg_set_prop_null(data, name);
            break;
        case RBUS_BOOLEAN:
            retval = osbus_msg_set_prop_bool(data, name, rbusValue_GetBoolean(v));
            break;
        case RBUS_CHAR:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetChar(v));
            break;
        case RBUS_BYTE:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetByte(v));
            break;
        case RBUS_INT8:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetInt8(v));
            break;
        case RBUS_UINT8:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetUInt8(v));
            break;
        case RBUS_INT16:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetInt16(v));
            break;
        case RBUS_UINT16:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetUInt16(v));
            break;
        case RBUS_INT32:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetInt32(v));
            break;
        case RBUS_UINT32:
            retval = osbus_msg_set_prop_int(data, name, rbusValue_GetUInt32(v));
            break;
        case RBUS_INT64:
            retval = osbus_msg_set_prop_int64(data, name, rbusValue_GetInt64(v));
            break;
        case RBUS_UINT64:
            retval = osbus_msg_set_prop_int64(data, name, rbusValue_GetUInt64(v));
            break;
        case RBUS_SINGLE:
            retval = osbus_msg_set_prop_double(data, name, rbusValue_GetSingle(v));
            break;
        case RBUS_DOUBLE:
            retval = osbus_msg_set_prop_double(data, name, rbusValue_GetDouble(v));
            break;
        case RBUS_STRING:
            retval = osbus_msg_set_prop_string(data, name, (char*)rbusValue_GetString(v, NULL));
            break;
        case RBUS_BYTES:
            {
                uint8_t *buf;
                int size;
                buf = (uint8_t*)rbusValue_GetBytes(v, &size);
                retval = osbus_msg_set_prop_binary(data, name, buf, size);
            }
            break;

        case RBUS_DATETIME:
            // todo
            retval = false;
            break;

        case RBUS_PROPERTY:
            e = osbus_msg_set_prop_object(data, name);
            if (!e) break;
            retval = osbus_msg_add_rbusProperty_list(e, rbusValue_GetProperty(v));
            break;

        case RBUS_OBJECT:
            osbus_msg_from_rbus_object(&e, rbusValue_GetObject(v));
            retval = osbus_msg_set_prop(data, name, e);
            break;

        default:
            retval = false;
            break;
    }
    //printf("add prop '%s': %d\n", name, retval);
    return retval;
}

bool osbus_msg_add_rbusProperty_list(osbus_msg_t *data, rbusProperty_t prop)
{
    if (!data) return false;
    // prop is NULL for empty list
    bool retval = true;
    while (prop) {
        retval = osbus_msg_add_rbusProperty(data, prop);
        if (!retval) break;
        prop = rbusProperty_GetNext(prop);
    }
    return retval;
}


bool osbus_msg_from_rbus_object(osbus_msg_t **data, rbusObject_t robj)
{
    if (!data || !robj)  return false;
    *data = NULL;
    bool retval = false;
    char *obj_name = (char*)rbusObject_GetName(robj);
    osbus_msg_t *d = NULL;
    if (strcmp("[]", obj_name ?: "") == 0) {
        d = osbus_msg_new_array();
    } else {
        d = osbus_msg_new_object();
    }
    if (!d) return false;
    retval = osbus_msg_add_rbusProperty_list(d, rbusObject_GetProperties(robj));
    // todo: rbusObject_GetChildren
    if (retval) {
        *data = d;
    } else {
        osbus_msg_free(d);
    }
    return retval;
}




