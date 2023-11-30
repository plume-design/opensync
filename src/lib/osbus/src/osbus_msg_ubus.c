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
#include "osbus_msg_ubus.h"

#include <libubox/blobmsg.h>

#define MODULE_ID LOG_MODULE_ID_OSBUS


bool osbus_msg_add_items_to_blob_buf(const osbus_msg_t *data, struct blob_buf *b);
bool osbus_msg_add_to_blob_buf(const osbus_msg_t *data, const char *name, struct blob_buf *b);

enum blobmsg_type osbus_msg_type_to_blobmsg_type(osbus_msg_type type)
{
    switch (type) {
        case OSBUS_DATA_TYPE_NULL:      return BLOBMSG_TYPE_UNSPEC;
        case OSBUS_DATA_TYPE_OBJECT:    return BLOBMSG_TYPE_TABLE;
        case OSBUS_DATA_TYPE_ARRAY:     return BLOBMSG_TYPE_ARRAY;
        case OSBUS_DATA_TYPE_BOOL:      return BLOBMSG_TYPE_BOOL;
        case OSBUS_DATA_TYPE_INT:       return BLOBMSG_TYPE_INT32;
        case OSBUS_DATA_TYPE_INT64:     return BLOBMSG_TYPE_INT64;
        case OSBUS_DATA_TYPE_DOUBLE:    return BLOBMSG_TYPE_DOUBLE;
        case OSBUS_DATA_TYPE_STRING:    return BLOBMSG_TYPE_STRING;
        case OSBUS_DATA_TYPE_BINARY:    return BLOBMSG_TYPE_TABLE;
        default:                        return BLOBMSG_TYPE_UNSPEC;
    }
}

osbus_msg_type osbus_msg_type_from_blobmsg_type(enum blobmsg_type type)
{
    switch (type) {
        case BLOBMSG_TYPE_UNSPEC: return OSBUS_DATA_TYPE_NULL;
        case BLOBMSG_TYPE_TABLE:  return OSBUS_DATA_TYPE_OBJECT;
        case BLOBMSG_TYPE_ARRAY:  return OSBUS_DATA_TYPE_ARRAY;
        case BLOBMSG_TYPE_BOOL:   return OSBUS_DATA_TYPE_BOOL;
        case BLOBMSG_TYPE_INT16:  return OSBUS_DATA_TYPE_INT;
        case BLOBMSG_TYPE_INT32:  return OSBUS_DATA_TYPE_INT;
        case BLOBMSG_TYPE_INT64:  return OSBUS_DATA_TYPE_INT64;
        case BLOBMSG_TYPE_DOUBLE: return OSBUS_DATA_TYPE_DOUBLE;
        case BLOBMSG_TYPE_STRING: return OSBUS_DATA_TYPE_STRING;
        default:                  return OSBUS_DATA_TYPE_NULL;
    }
}

bool osbus_msg_add_items_to_blob_buf(const osbus_msg_t *data, struct blob_buf *b)
{
    bool retval = true;
    osbus_msg_t *e = NULL;
    osbus_msg_foreach(data, e) {
        if (!osbus_msg_add_to_blob_buf(e, e->name, b)) {
            retval = false;
            break;
        }
    }
    return retval;
}

bool osbus_msg_add_to_blob_buf(const osbus_msg_t *data, const char *name, struct blob_buf *b)
{
    if (!data || !b) return false;

    bool retval = true;
    void *b_cookie = NULL;
    int rc;

    switch (data->type) {
        // container:
        case OSBUS_DATA_TYPE_OBJECT:
            b_cookie = blobmsg_open_table(b, name);
            retval = osbus_msg_add_items_to_blob_buf(data, b);
            blobmsg_close_table(b, b_cookie);
            break;
        case OSBUS_DATA_TYPE_ARRAY:
            b_cookie = blobmsg_open_array(b, name);
            retval = osbus_msg_add_items_to_blob_buf(data, b);
            blobmsg_close_array(b, b_cookie);
            break;
        // values:
        case OSBUS_DATA_TYPE_NULL:
            rc = blobmsg_add_field(b, BLOBMSG_TYPE_UNSPEC, name, NULL, 0);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_BOOL:
            rc = blobmsg_add_u8(b, name, data->val.v_bool);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_INT:
            rc = blobmsg_add_u32(b, name, data->val.v_int);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_INT64:
            rc = blobmsg_add_u64(b, name, data->val.v_int64);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            rc = blobmsg_add_double(b, name, data->val.v_double);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_STRING:
            rc = blobmsg_add_string(b, name, data->val.v_string);
            if (rc < 0) retval = false;
            break;
        case OSBUS_DATA_TYPE_BINARY:
            {
                osbus_msg_t *encoded = NULL;
                if (!osbus_msg_encode_binary_obj(data, &encoded)) {
                    retval = false;
                } else {
                    retval = osbus_msg_add_to_blob_buf(encoded, name, b);
                    osbus_msg_free(encoded);
                }
            }
            break;
        default:
            retval = false;
            break;
    }

    return retval;
}

void osbus_msg_free_blob_buf(struct blob_buf *b)
{
    if (b) {
        blob_buf_free(b);
        free(b);
    }
}

bool osbus_msg_to_blob_buf(const osbus_msg_t *data, struct blob_buf **bb)
{
    if (!data || !bb) return false;

    bool retval = false;
    struct blob_buf *b = NULL;

    *bb = NULL;
    if (data->type != OSBUS_DATA_TYPE_OBJECT) goto out;
    b = CALLOC(sizeof(struct blob_buf), 1);
    blob_buf_init(b, 0);
    retval = osbus_msg_add_items_to_blob_buf(data, b);
out:
    if (!retval) {
        osbus_msg_free_blob_buf(b);
    }
    *bb = b;
    return retval;
}

bool osbus_msg_add_blob_attr_list(osbus_msg_t *data, struct blob_attr *attr);
bool osbus_msg_add_blob_attr_item(osbus_msg_t *data, struct blob_attr *attr);
bool osbus_msg_add_blobmsg_attr_list(osbus_msg_t *data, struct blob_attr *attr);

bool osbus_msg_add_blob_attr_item(osbus_msg_t *data, struct blob_attr *attr)
{
    if (!data || !attr) return false;
    if (!blobmsg_check_attr(attr, false)) return false;
    bool retval = false;
    osbus_msg_t *e = NULL;
    char *name = NULL;

    if (data->type == OSBUS_DATA_TYPE_OBJECT) {
        name = (char*)blobmsg_name(attr);
    }
    //LOGT("%s: %s %d", __func__, name, blob_id(attr));

    switch(blob_id(attr)) {
        case BLOBMSG_TYPE_UNSPEC:
            retval = osbus_msg_set_prop_null(data, name);
            break;
        case BLOBMSG_TYPE_BOOL:
            retval = osbus_msg_set_prop_bool(data, name, blobmsg_get_bool(attr));
            break;
        case BLOBMSG_TYPE_INT16:
            retval = osbus_msg_set_prop_int(data, name, blobmsg_get_u16(attr));
            break;
        case BLOBMSG_TYPE_INT32:
            retval = osbus_msg_set_prop_int(data, name, blobmsg_get_u32(attr));
            break;
        case BLOBMSG_TYPE_INT64:
            retval = osbus_msg_set_prop_int64(data, name, blobmsg_get_u64(attr));
            break;
        case BLOBMSG_TYPE_DOUBLE:
            retval = osbus_msg_set_prop_double(data, name, blobmsg_get_double(attr));
            break;
        case BLOBMSG_TYPE_STRING:
            retval = osbus_msg_set_prop_string(data, name, blobmsg_get_string(attr));
            break;
        case BLOBMSG_TYPE_ARRAY:
            e = osbus_msg_set_prop_array(data, name);
            retval = osbus_msg_add_blobmsg_attr_list(e, attr);
            break;
        case BLOBMSG_TYPE_TABLE:
            e = osbus_msg_new_object();
            if (!e) break;
            if (!osbus_msg_add_blobmsg_attr_list(e, attr)) {
                osbus_msg_free(e);
                break;
            }
            // try binary decode
            osbus_msg_t *decoded = NULL;
            if (osbus_msg_decode_binary_obj(e, &decoded)) {
                osbus_msg_free(e);
                e = decoded;
            }
            retval = osbus_msg_set_prop(data, name, e);
            break;
    }
    return retval;
}

// for blob_attr containers
bool osbus_msg_add_blobmsg_attr_list(osbus_msg_t *data, struct blob_attr *attr)
{
    if (!data || !attr) return false;
    bool retval = true;
    struct blob_attr *pos = NULL;
    size_t rem = 0;
    rem = blobmsg_data_len(attr);
    __blob_for_each_attr(pos, blobmsg_data(attr), rem) {
        retval = osbus_msg_add_blob_attr_item(data, pos);
        if (!retval) break;
    }
    return retval;
}

// for top level blob_buf
bool osbus_msg_add_blob_attr_list(osbus_msg_t *data, struct blob_attr *attr)
{
    //LOGT("%s: %p %p", __func__, data, attr);
    if (!data || !attr) return false;
    bool retval = true;
    struct blob_attr *pos = NULL;
    size_t rem = 0;
    blob_for_each_attr(pos, attr, rem) {
        //LOGT("%s: %p %p", __func__, data, pos);
        retval = osbus_msg_add_blob_attr_item(data, pos);
        if (!retval) break;
    }
    return retval;
}

bool osbus_msg_from_blob_attr(osbus_msg_t **data, struct blob_attr *attr)
{
    LOGT("%s: %p %p", __func__, data, attr);
    if (!data || !attr) return false;
    bool retval = false;
    osbus_msg_t *d = NULL;
    bool is_array = false;

    if (blob_is_extended(attr) && blobmsg_type(attr) == BLOBMSG_TYPE_ARRAY) {
        is_array = true;
    }
    if (is_array) {
        d = osbus_msg_new_array();
    } else {
        d = osbus_msg_new_object();
    }
    if (!d) goto out;
    retval = osbus_msg_add_blob_attr_list(d, attr);

out:
    if (!retval) {
        if (d) {
            osbus_msg_free(d);
            d = NULL;
        }
    }
    *data = d;
    return retval;
}

bool osbus_msg_from_blob_buf(osbus_msg_t **data, struct blob_buf *bb)
{
    if (!data || !bb) return false;
    return osbus_msg_from_blob_attr(data, bb->head);
}


