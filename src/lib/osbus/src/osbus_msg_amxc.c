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
#include "osbus_msg_amxc.h"

#include <amxc/amxc.h>

#define MODULE_ID LOG_MODULE_ID_OSBUS

static bool osbus_msg_to_amxc_var_internal(const osbus_msg_t *msg, amxc_var_t *var);
static bool osbus_msg_from_amxc_var_internal(osbus_msg_t **msg, const amxc_var_t *var);

static bool osbus_msg_to_amxc_var_internal(const osbus_msg_t *msg, amxc_var_t *var)
{
    if (!msg || !var) return false;
    bool retval = true;
    osbus_msg_t *e = NULL;
    amxc_var_t *sub = NULL;

    switch (msg->type)
    {
        case OSBUS_DATA_TYPE_NULL:
            amxc_var_set_type(var, AMXC_VAR_ID_NULL);
            break;
        case OSBUS_DATA_TYPE_BOOL:
            amxc_var_set_bool(var, msg->val.v_bool);
            break;
        case OSBUS_DATA_TYPE_INT:
            amxc_var_set_int32_t(var, msg->val.v_int);
            break;
        case OSBUS_DATA_TYPE_INT64:
            amxc_var_set_int64_t(var, msg->val.v_int64);
            break;
        case OSBUS_DATA_TYPE_DOUBLE:
            amxc_var_set_double(var, msg->val.v_double);
            break;
        case OSBUS_DATA_TYPE_STRING:
            amxc_var_set_cstring_t(var, msg->val.v_string);
            break;
        case OSBUS_DATA_TYPE_BINARY: {
            osbus_msg_t *encoded = NULL;
            if (!osbus_msg_encode_binary_obj(msg, &encoded))
            {
                retval = false;
            }
            else
            {
                retval = osbus_msg_to_amxc_var_internal(encoded, var);
                osbus_msg_free(encoded);
            }
        }
        break;
        case OSBUS_DATA_TYPE_ARRAY:
            amxc_var_set_type(var, AMXC_VAR_ID_LIST);
            osbus_msg_foreach(msg, e)
            {
                sub = amxc_var_add_new(var);
                if (!sub)
                {
                    retval = false;
                    break;
                }
                retval = osbus_msg_to_amxc_var_internal(e, sub);
                if (!retval) break;
            }
            break;
        case OSBUS_DATA_TYPE_OBJECT:
            amxc_var_set_type(var, AMXC_VAR_ID_HTABLE);
            osbus_msg_foreach(msg, e)
            {
                const char *key = e->name ? e->name : "";
                sub = amxc_var_add_new_key(var, key);
                if (!sub)
                {
                    retval = false;
                    break;
                }
                retval = osbus_msg_to_amxc_var_internal(e, sub);
                if (!retval) break;
            }
            break;
        default:
            retval = false;
            break;
    }
    return retval;
}

static bool osbus_msg_from_amxc_var_internal(osbus_msg_t **msg, const amxc_var_t *var)
{
    if (!msg || !var) return false;
    bool retval = false;
    osbus_msg_t *d = NULL;
    *msg = NULL;

    amxc_var_type_id_t type = amxc_var_type_of(var);

    switch (type)
    {
        case AMXC_VAR_ID_NULL:
            d = osbus_msg_new_null();
            break;
        case AMXC_VAR_ID_BOOL:
            d = osbus_msg_new_bool(amxc_var_get_bool(var));
            break;
        case AMXC_VAR_ID_INT8:
            d = osbus_msg_new_int(amxc_var_get_int8_t(var));
            break;
        case AMXC_VAR_ID_UINT8:
            d = osbus_msg_new_int(amxc_var_get_uint8_t(var));
            break;
        case AMXC_VAR_ID_INT16:
            d = osbus_msg_new_int(amxc_var_get_int16_t(var));
            break;
        case AMXC_VAR_ID_UINT16:
            d = osbus_msg_new_int(amxc_var_get_uint16_t(var));
            break;
        case AMXC_VAR_ID_INT32:
            d = osbus_msg_new_int(amxc_var_get_int32_t(var));
            break;
        case AMXC_VAR_ID_UINT32:
            d = osbus_msg_new_int((int)amxc_var_get_uint32_t(var));
            break;
        case AMXC_VAR_ID_INT64:
            d = osbus_msg_new_int64(amxc_var_get_int64_t(var));
            break;
        case AMXC_VAR_ID_UINT64:
            d = osbus_msg_new_int64((int64_t)amxc_var_get_uint64_t(var));
            break;
        case AMXC_VAR_ID_FLOAT:
        case AMXC_VAR_ID_DOUBLE:
            d = osbus_msg_new_double(amxc_var_get_double(var));
            break;
        case AMXC_VAR_ID_CSTRING:
        case AMXC_VAR_ID_CSV_STRING:
        case AMXC_VAR_ID_SSV_STRING: {
            char *str = amxc_var_get_cstring_t(var);
            d = osbus_msg_new_string(str);
            free(str);
        }
        break;
        case AMXC_VAR_ID_LIST:
            d = osbus_msg_new_array();
            if (!d) break;
            {
                const amxc_llist_t *list = &var->data.vl;
                amxc_llist_it_t *it _U_ = NULL;
                amxc_llist_for_each(it, list)
                {
                    const amxc_var_t *item = amxc_var_from_llist_it(it);
                    osbus_msg_t *e = NULL;
                    if (!osbus_msg_from_amxc_var_internal(&e, item))
                    {
                        osbus_msg_free(d);
                        d = NULL;
                        break;
                    }
                    osbus_msg_add_item(d, e);
                }
            }
            break;
        case AMXC_VAR_ID_HTABLE:
            d = osbus_msg_new_object();
            if (!d) break;
            {
                const amxc_htable_t *htable = &var->data.vm;
                amxc_htable_it_t *it _U_ = NULL;
                amxc_htable_for_each(it, htable)
                {
                    const amxc_var_t *item = amxc_var_from_htable_it(it);
                    const char *key = amxc_htable_it_get_key(it);
                    osbus_msg_t *e = NULL;
                    if (!osbus_msg_from_amxc_var_internal(&e, item))
                    {
                        osbus_msg_free(d);
                        d = NULL;
                        break;
                    }
                    if (!osbus_msg_set_prop(d, key, e))
                    {
                        osbus_msg_free(e);
                        osbus_msg_free(d);
                        d = NULL;
                        break;
                    }
                }
            }
            if (d)
            {
                // try binary decode
                osbus_msg_t *decoded = NULL;
                if (osbus_msg_decode_binary_obj(d, &decoded))
                {
                    osbus_msg_free(d);
                    d = decoded;
                }
            }
            break;
        default:
            retval = false;
            break;
    }

    if (d)
    {
        retval = true;
        *msg = d;
    }
    return retval;
}

bool osbus_msg_to_amxc_var(const osbus_msg_t *msg, amxc_var_t **var)
{
    if (!msg || !var) return false;
    bool retval = false;
    amxc_var_t *v = NULL;

    *var = NULL;
    if (amxc_var_new(&v) != 0) return false;

    retval = osbus_msg_to_amxc_var_internal(msg, v);
    if (!retval)
    {
        amxc_var_delete(&v);
    }
    else
    {
        *var = v;
    }
    return retval;
}

bool osbus_msg_from_amxc_var(osbus_msg_t **msg, amxc_var_t *var)
{
    if (!msg || !var) return false;
    return osbus_msg_from_amxc_var_internal(msg, var);
}
