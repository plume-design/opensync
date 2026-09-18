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

/*
 * os_tr181_val - Value type system implementation
 */

#define _GNU_SOURCE /* For strptime(), timegm() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <inttypes.h>
#include <time.h>
#include "os_tr181.h"
#include "os_tr181_val.h"
#include "log.h"

/* ========================================================================
 * Type Validation
 * ======================================================================== */

bool os_val_is_value_type(os_tr181_param_type_t type)
{
    switch (type)
    {
        /* Simple types (used for both paths and values) */
        case OS_TR181_TYPE_NONE:
        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_INT:
        case OS_TR181_TYPE_UINT:
        case OS_TR181_TYPE_INT64:
        case OS_TR181_TYPE_UINT64:
        case OS_TR181_TYPE_BOOL:
        case OS_TR181_TYPE_DOUBLE:
        case OS_TR181_TYPE_DATETIME:
        case OS_TR181_TYPE_BASE64:
        /* Value-only types (structured runtime data) */
        case OS_TR181_TYPE_DICT:
        case OS_TR181_TYPE_LIST:
            return true;

        /* Path-only types (describe TR-181 tree structure) */
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
        case OS_TR181_TYPE_PROPERTY:
            return false;
    }

    /* Unknown type - should not happen */
    return false;
}

bool os_val_is_container_type(os_tr181_param_type_t type)
{
    return type == OS_TR181_TYPE_DICT || type == OS_TR181_TYPE_LIST;
}

bool os_val_is_scalar_type(os_tr181_param_type_t type)
{
    return os_val_is_value_type(type) && !os_val_is_container_type(type) && (type != OS_TR181_TYPE_NONE);
}

/* ========================================================================
 * Internal Container Structures (not exposed in header)
 * ======================================================================== */

/* Dictionary entry - linked list node */
struct dict_entry
{
    char *key;               /* Key string (owned, allocated) */
    os_tr181_val_t *value;   /* Value (owned, allocated) */
    struct dict_entry *next; /* Next entry in chain */
};

/* List entry - linked list node */
struct list_entry
{
    os_tr181_val_t *value;   /* Value (owned, allocated) */
    struct list_entry *next; /* Next entry in chain */
};

/* Container holds either dict or list data */
struct os_val_container_s
{
    struct dict_entry *dict; /* Head of dict chain (NULL if LIST type) */
    struct list_entry *list; /* Head of list chain (NULL if DICT type) */
    size_t count;            /* Number of entries (cached for performance) */
};

/* ========================================================================
 * Internal Container Helper Functions
 * ======================================================================== */

/* Free dictionary chain recursively */
static void free_dict_chain(struct dict_entry *entry)
{
    while (entry)
    {
        struct dict_entry *next = entry->next;
        free(entry->key);
        os_val_delete(entry->value);
        free(entry);
        entry = next;
    }
}

/* Free list chain recursively */
static void free_list_chain(struct list_entry *entry)
{
    while (entry)
    {
        struct list_entry *next = entry->next;
        os_val_delete(entry->value);
        free(entry);
        entry = next;
    }
}

/* Free container and all its contents */
static void free_container(os_val_container_t *container)
{
    if (!container)
    {
        return;
    }

    if (container->dict)
    {
        free_dict_chain(container->dict);
    }
    if (container->list)
    {
        free_list_chain(container->list);
    }

    free(container);
}

/* Deep copy dictionary chain */
static struct dict_entry *copy_dict_chain(const struct dict_entry *src)
{
    if (!src)
    {
        return NULL;
    }

    struct dict_entry *head = NULL;
    struct dict_entry *tail = NULL;

    while (src)
    {
        struct dict_entry *entry = malloc(sizeof(*entry));
        if (!entry)
        {
            free_dict_chain(head);
            return NULL;
        }

        entry->key = strdup(src->key);
        if (!entry->key)
        {
            free(entry);
            free_dict_chain(head);
            return NULL;
        }

        entry->value = os_val_new();
        if (!entry->value)
        {
            free(entry->key);
            free(entry);
            free_dict_chain(head);
            return NULL;
        }

        if (os_val_copy(entry->value, src->value) != OS_TR181_SUCCESS)
        {
            os_val_delete(entry->value);
            free(entry->key);
            free(entry);
            free_dict_chain(head);
            return NULL;
        }

        entry->next = NULL;

        if (!head)
        {
            head = entry;
            tail = entry;
        }
        else
        {
            tail->next = entry;
            tail = entry;
        }

        src = src->next;
    }

    return head;
}

/* Deep copy list chain */
static struct list_entry *copy_list_chain(const struct list_entry *src)
{
    if (!src)
    {
        return NULL;
    }

    struct list_entry *head = NULL;
    struct list_entry *tail = NULL;

    while (src)
    {
        struct list_entry *entry = malloc(sizeof(*entry));
        if (!entry)
        {
            free_list_chain(head);
            return NULL;
        }

        entry->value = os_val_new();
        if (!entry->value)
        {
            free(entry);
            free_list_chain(head);
            return NULL;
        }

        if (os_val_copy(entry->value, src->value) != OS_TR181_SUCCESS)
        {
            os_val_delete(entry->value);
            free(entry);
            free_list_chain(head);
            return NULL;
        }

        entry->next = NULL;

        if (!head)
        {
            head = entry;
            tail = entry;
        }
        else
        {
            tail->next = entry;
            tail = entry;
        }

        src = src->next;
    }

    return head;
}

/* Deep copy container (used by os_val_copy) */
static os_val_container_t *copy_container(const os_val_container_t *src)
{
    if (!src)
    {
        return NULL;
    }

    os_val_container_t *dst = malloc(sizeof(os_val_container_t));
    if (!dst)
    {
        return NULL;
    }

    memset(dst, 0, sizeof(*dst));
    dst->count = src->count;

    if (src->dict)
    {
        dst->dict = copy_dict_chain(src->dict);
        if (!dst->dict)
        {
            free(dst);
            return NULL;
        }
    }

    if (src->list)
    {
        dst->list = copy_list_chain(src->list);
        if (!dst->list)
        {
            free_container(dst);
            return NULL;
        }
    }

    return dst;
}

/* ========================================================================
 * Construction Functions
 * ======================================================================== */

os_tr181_val_t os_val_str_ref(const char *s)
{
    return (os_tr181_val_t){.type = OS_TR181_TYPE_STRING, .str = (char *)s, .str_alloc = false};
}

os_tr181_val_t os_val_str_dup(const char *s)
{
    char *dup = s ? strdup(s) : NULL;
    return (os_tr181_val_t){.type = OS_TR181_TYPE_STRING, .str = dup, .str_alloc = (dup != NULL)};
}

/* ========================================================================
 * Memory Management
 * ======================================================================== */

void os_val_init(os_tr181_val_t *v)
{
    if (v)
    {
        memset(v, 0, sizeof(*v));
        v->type = OS_TR181_TYPE_NONE;
    }
}

void os_val_free(os_tr181_val_t *v)
{
    if (!v)
    {
        return;
    }

    /* Free string if allocated */
    if (v->str_alloc && v->str)
    {
        free(v->str);
    }

    /* Free container if present (recursive for DICT/LIST) */
    if (v->container)
    {
        free_container(v->container);
    }

    /* Reset to NONE */
    memset(v, 0, sizeof(*v));
    v->type = OS_TR181_TYPE_NONE;
}

os_tr181_error_t os_val_copy(os_tr181_val_t *dst, const os_tr181_val_t *src)
{
    if (!dst || !src)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Free destination first */
    os_val_free(dst);

    /* Copy type and union */
    dst->type = src->type;
    dst->i64 = src->i64; /* Copy largest union member */

    /* Handle string duplication */
    if (src->str)
    {
        if (src->str_alloc)
        {
            dst->str = strdup(src->str);
            if (!dst->str)
            {
                return OS_TR181_ERROR;
            }
            dst->str_alloc = true;
        }
        else
        {
            dst->str = src->str; /* Borrow pointer */
            dst->str_alloc = false;
        }
    }
    else
    {
        dst->str = NULL;
        dst->str_alloc = false;
    }

    /* Handle container deep copy */
    dst->container = copy_container(src->container);
    if (src->container && !dst->container)
    {
        /* Container copy failed */
        if (dst->str_alloc)
        {
            free(dst->str);
        }
        memset(dst, 0, sizeof(*dst));
        return OS_TR181_ERROR;
    }

    return OS_TR181_SUCCESS;
}

void os_val_move(os_tr181_val_t *dst, os_tr181_val_t *src)
{
    if (!dst || !src)
    {
        return;
    }

    /* Free destination */
    os_val_free(dst);

    /* Copy all fields */
    *dst = *src;

    /* Reset source */
    memset(src, 0, sizeof(*src));
    src->type = OS_TR181_TYPE_NONE;
}

os_tr181_val_t *os_val_new(void)
{
    os_tr181_val_t *v = malloc(sizeof(os_tr181_val_t));
    if (v)
    {
        os_val_init(v);
    }
    return v;
}

void os_val_delete(os_tr181_val_t *v)
{
    if (v)
    {
        os_val_free(v);
        free(v);
    }
}

void os_val_init_type(os_tr181_val_t *v, os_tr181_param_type_t type)
{
    if (!v)
    {
        return;
    }

    /* Validate type is appropriate for values */
    if (!os_val_is_value_type(type))
    {
        LOGE("Invalid value type: %d (path-only types not allowed)", type);
        type = OS_TR181_TYPE_NONE; /* Initialize as NONE instead */
    }

    /* Initialize without freeing (assumes v is uninitialized) */
    os_val_init(v);
    v->type = type;

    /* For container types, allocate empty container */
    if (os_val_is_container_type(type))
    {
        v->container = calloc(1, sizeof(os_val_container_t));
        /* If allocation fails, type stays as requested but container is NULL */
        /* Operations will check and handle this */
    }
}

void os_val_set_type(os_tr181_val_t *v, os_tr181_param_type_t type)
{
    os_val_free(v);
    os_val_init_type(v, type);
}

os_tr181_val_t *os_val_new_type(os_tr181_param_type_t type)
{
    os_tr181_val_t *v = os_val_new();
    if (v)
    {
        os_val_init_type(v, type);
    }
    return v;
}

/* ========================================================================
 * Setters
 * ======================================================================== */

os_tr181_error_t os_val_set_int(os_tr181_val_t *v, int i)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_INT;
    v->i = i;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_uint(os_tr181_val_t *v, unsigned int u)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_UINT;
    v->u = u;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_int64(os_tr181_val_t *v, int64_t i64)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_INT64;
    v->i64 = i64;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_uint64(os_tr181_val_t *v, uint64_t u64)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_UINT64;
    v->u64 = u64;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_bool(os_tr181_val_t *v, bool b)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_BOOL;
    v->b = b;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_double(os_tr181_val_t *v, double d)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_DOUBLE;
    v->d = d;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_datetime(os_tr181_val_t *v, os_tr181_timestamp_t ts)
{
    if (!v)
    {
        return OS_TR181_ERROR_INVALID;
    }
    os_val_free(v);
    v->type = OS_TR181_TYPE_DATETIME;
    v->ts = ts;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_str_dup(os_tr181_val_t *v, const char *s)
{
    if (!v || !s)
    {
        return OS_TR181_ERROR_INVALID;
    }

    os_val_free(v);
    v->type = OS_TR181_TYPE_STRING;
    v->str = strdup(s);
    if (!v->str)
    {
        return OS_TR181_ERROR;
    }
    v->str_alloc = true;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_val_set_str_ref(os_tr181_val_t *v, const char *s)
{
    if (!v || !s)
    {
        return OS_TR181_ERROR_INVALID;
    }

    os_val_free(v);
    v->type = OS_TR181_TYPE_STRING;
    v->str = (char *)s; /* Borrow pointer */
    v->str_alloc = false;
    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * Getters (strict type checking with safe conversions)
 * ======================================================================== */

os_tr181_error_t os_val_get_int(const os_tr181_val_t *v, int *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (v->type)
    {
        case OS_TR181_TYPE_INT:
            *out = v->i;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_UINT:
            if (v->u <= (unsigned int)INT_MAX)
            {
                *out = (int)v->u;
                return OS_TR181_SUCCESS;
            }
            *out = INT_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        case OS_TR181_TYPE_INT64:
            if (v->i64 >= INT_MIN && v->i64 <= INT_MAX)
            {
                *out = (int)v->i64;
                return OS_TR181_SUCCESS;
            }
            *out = (v->i64 < 0) ? INT_MIN : INT_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        case OS_TR181_TYPE_UINT64:
            if (v->u64 <= (uint64_t)INT_MAX)
            {
                *out = (int)v->u64;
                return OS_TR181_SUCCESS;
            }
            *out = INT_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        default:
            return OS_TR181_ERROR_INVALID;
    }
}

os_tr181_error_t os_val_get_uint(const os_tr181_val_t *v, unsigned int *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (v->type)
    {
        case OS_TR181_TYPE_UINT:
            *out = v->u;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_INT:
            if (v->i >= 0)
            {
                *out = (unsigned int)v->i;
                return OS_TR181_SUCCESS;
            }
            *out = 0; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        case OS_TR181_TYPE_UINT64:
            if (v->u64 <= UINT_MAX)
            {
                *out = (unsigned int)v->u64;
                return OS_TR181_SUCCESS;
            }
            *out = UINT_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        case OS_TR181_TYPE_INT64:
            if (v->i64 >= 0 && v->i64 <= (int64_t)UINT_MAX)
            {
                *out = (unsigned int)v->i64;
                return OS_TR181_SUCCESS;
            }
            *out = (v->i64 < 0) ? 0 : UINT_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        default:
            return OS_TR181_ERROR_INVALID;
    }
}

os_tr181_error_t os_val_get_int64(const os_tr181_val_t *v, int64_t *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (v->type)
    {
        case OS_TR181_TYPE_INT64:
            *out = v->i64;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_INT:
            *out = v->i;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_UINT:
            *out = v->u;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_UINT64:
            if (v->u64 <= (uint64_t)INT64_MAX)
            {
                *out = (int64_t)v->u64;
                return OS_TR181_SUCCESS;
            }
            *out = INT64_MAX; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        default:
            return OS_TR181_ERROR_INVALID;
    }
}

os_tr181_error_t os_val_get_uint64(const os_tr181_val_t *v, uint64_t *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (v->type)
    {
        case OS_TR181_TYPE_UINT64:
            *out = v->u64;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_UINT:
            *out = v->u;
            return OS_TR181_SUCCESS;

        case OS_TR181_TYPE_INT:
            if (v->i >= 0)
            {
                *out = (uint64_t)v->i;
                return OS_TR181_SUCCESS;
            }
            *out = 0; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        case OS_TR181_TYPE_INT64:
            if (v->i64 >= 0)
            {
                *out = (uint64_t)v->i64;
                return OS_TR181_SUCCESS;
            }
            *out = 0; /* Clamp */
            return OS_TR181_ERROR_OVERFLOW;

        default:
            return OS_TR181_ERROR_INVALID;
    }
}

os_tr181_error_t os_val_get_bool(const os_tr181_val_t *v, bool *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_get_double(const os_tr181_val_t *v, double *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (v->type == OS_TR181_TYPE_DOUBLE)
    {
        *out = v->d;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_get_datetime(const os_tr181_val_t *v, os_tr181_timestamp_t *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (v->type == OS_TR181_TYPE_DATETIME)
    {
        *out = v->ts;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

const char *os_val_get_str(const os_tr181_val_t *v, os_tr181_error_t *error)
{
    if (!v)
    {
        if (error) *error = OS_TR181_ERROR_INVALID;
        return NULL;
    }

    if (v->type == OS_TR181_TYPE_STRING || v->type == OS_TR181_TYPE_BASE64)
    {
        if (error) *error = OS_TR181_SUCCESS;
        return v->str;
    }

    if (error) *error = OS_TR181_ERROR_INVALID;
    return NULL;
}

/* ========================================================================
 * Convenience Getters
 * ======================================================================== */

int os_val_get_int_or(const os_tr181_val_t *v, int default_val)
{
    int result;
    return (os_val_get_int(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

unsigned int os_val_get_uint_or(const os_tr181_val_t *v, unsigned int default_val)
{
    unsigned int result;
    return (os_val_get_uint(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

int64_t os_val_get_int64_or(const os_tr181_val_t *v, int64_t default_val)
{
    int64_t result;
    return (os_val_get_int64(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

uint64_t os_val_get_uint64_or(const os_tr181_val_t *v, uint64_t default_val)
{
    uint64_t result;
    return (os_val_get_uint64(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

bool os_val_get_bool_or(const os_tr181_val_t *v, bool default_val)
{
    bool result;
    return (os_val_get_bool(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

double os_val_get_double_or(const os_tr181_val_t *v, double default_val)
{
    double result;
    return (os_val_get_double(v, &result) == OS_TR181_SUCCESS) ? result : default_val;
}

const char *os_val_get_str_or(const os_tr181_val_t *v, const char *default_val)
{
    const char *result = os_val_get_str(v, NULL);
    return result ? result : default_val;
}

/* ========================================================================
 * Type Conversion (with coercion)
 * ======================================================================== */

os_tr181_error_t os_val_to_int(const os_tr181_val_t *v, int *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Try direct get first */
    int ret = os_val_get_int(v, out);
    if (ret != OS_TR181_ERROR_INVALID)
    {
        return ret; /* Success or overflow */
    }

    /* Try string parsing */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        char *endptr;
        long val = strtol(v->str, &endptr, 10);
        if (endptr > v->str && (*endptr == '\0' || isspace(*endptr)))
        {
            if (val >= INT_MIN && val <= INT_MAX)
            {
                *out = (int)val;
                return OS_TR181_SUCCESS;
            }
            *out = (val < 0) ? INT_MIN : INT_MAX;
            return OS_TR181_ERROR_OVERFLOW;
        }
    }

    /* Try boolean */
    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b ? 1 : 0;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_to_uint(const os_tr181_val_t *v, unsigned int *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Try direct get first */
    int ret = os_val_get_uint(v, out);
    if (ret != OS_TR181_ERROR_INVALID)
    {
        return ret;
    }

    /* Try string parsing */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        char *endptr;
        unsigned long val = strtoul(v->str, &endptr, 10);
        if (endptr > v->str && (*endptr == '\0' || isspace(*endptr)))
        {
            if (val <= UINT_MAX)
            {
                *out = (unsigned int)val;
                return OS_TR181_SUCCESS;
            }
            *out = UINT_MAX;
            return OS_TR181_ERROR_OVERFLOW;
        }
    }

    /* Try boolean */
    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b ? 1 : 0;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_to_int64(const os_tr181_val_t *v, int64_t *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Try direct get first */
    int ret = os_val_get_int64(v, out);
    if (ret != OS_TR181_ERROR_INVALID)
    {
        return ret;
    }

    /* Try string parsing */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        char *endptr;
        long long val = strtoll(v->str, &endptr, 10);
        if (endptr > v->str && (*endptr == '\0' || isspace(*endptr)))
        {
            *out = (int64_t)val;
            return OS_TR181_SUCCESS;
        }
    }

    /* Try boolean */
    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b ? 1 : 0;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_to_uint64(const os_tr181_val_t *v, uint64_t *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Try direct get first */
    int ret = os_val_get_uint64(v, out);
    if (ret != OS_TR181_ERROR_INVALID)
    {
        return ret;
    }

    /* Try string parsing */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        char *endptr;
        unsigned long long val = strtoull(v->str, &endptr, 10);
        if (endptr > v->str && (*endptr == '\0' || isspace(*endptr)))
        {
            *out = (uint64_t)val;
            return OS_TR181_SUCCESS;
        }
    }

    /* Try boolean */
    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b ? 1 : 0;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_to_bool(const os_tr181_val_t *v, bool *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Direct get */
    if (v->type == OS_TR181_TYPE_BOOL)
    {
        *out = v->b;
        return OS_TR181_SUCCESS;
    }

    /* From integers */
    if (v->type == OS_TR181_TYPE_INT)
    {
        *out = (v->i != 0);
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_UINT)
    {
        *out = (v->u != 0);
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_INT64)
    {
        *out = (v->i64 != 0);
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_UINT64)
    {
        *out = (v->u64 != 0);
        return OS_TR181_SUCCESS;
    }

    /* From string */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        if (strcasecmp(v->str, "true") == 0 || strcmp(v->str, "1") == 0)
        {
            *out = true;
            return OS_TR181_SUCCESS;
        }
        if (strcasecmp(v->str, "false") == 0 || strcmp(v->str, "0") == 0)
        {
            *out = false;
            return OS_TR181_SUCCESS;
        }
    }

    return OS_TR181_ERROR_INVALID;
}

os_tr181_error_t os_val_to_double(const os_tr181_val_t *v, double *out)
{
    if (!v || !out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Direct get */
    if (v->type == OS_TR181_TYPE_DOUBLE)
    {
        *out = v->d;
        return OS_TR181_SUCCESS;
    }

    /* From integers */
    if (v->type == OS_TR181_TYPE_INT)
    {
        *out = (double)v->i;
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_UINT)
    {
        *out = (double)v->u;
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_INT64)
    {
        *out = (double)v->i64;
        return OS_TR181_SUCCESS;
    }
    if (v->type == OS_TR181_TYPE_UINT64)
    {
        *out = (double)v->u64;
        return OS_TR181_SUCCESS;
    }

    /* From string */
    if (v->type == OS_TR181_TYPE_STRING && v->str)
    {
        char *endptr;
        double val = strtod(v->str, &endptr);
        if (endptr > v->str && (*endptr == '\0' || isspace(*endptr)))
        {
            *out = val;
            return OS_TR181_SUCCESS;
        }
    }

    return OS_TR181_ERROR_INVALID;
}

/* ========================================================================
 * String Conversion
 * ======================================================================== */

os_tr181_error_t os_val_to_str(const os_tr181_val_t *v, char **str)
{
    char buffer[256];

    if (!v || !str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (v->type)
    {
        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            if (v->str)
            {
                *str = strdup(v->str);
                return *str ? OS_TR181_SUCCESS : OS_TR181_ERROR;
            }
            return OS_TR181_ERROR_INVALID;

        case OS_TR181_TYPE_INT:
            snprintf(buffer, sizeof(buffer), "%d", v->i);
            break;

        case OS_TR181_TYPE_UINT:
            snprintf(buffer, sizeof(buffer), "%u", v->u);
            break;

        case OS_TR181_TYPE_INT64:
            snprintf(buffer, sizeof(buffer), "%" PRId64, v->i64);
            break;

        case OS_TR181_TYPE_UINT64:
            snprintf(buffer, sizeof(buffer), "%" PRIu64, v->u64);
            break;

        case OS_TR181_TYPE_BOOL:
            snprintf(buffer, sizeof(buffer), "%s", v->b ? "true" : "false");
            break;

        case OS_TR181_TYPE_DOUBLE:
            snprintf(buffer, sizeof(buffer), "%g", v->d);
            break;

        case OS_TR181_TYPE_DATETIME: {
            /* Format as ISO 8601 with timezone offset */
            time_t t = (time_t)v->ts.sec;
            struct tm tm_utc;
            gmtime_r(&t, &tm_utc);

            /* Apply offset */
            int offset_min = v->ts.offset;
            int offset_hour = offset_min / 60;
            int offset_min_rem = abs(offset_min % 60);

            /* Format: 2024-01-21T12:53:27+01:00 */
            snprintf(
                    buffer,
                    sizeof(buffer),
                    "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                    tm_utc.tm_year + 1900,
                    tm_utc.tm_mon + 1,
                    tm_utc.tm_mday,
                    tm_utc.tm_hour,
                    tm_utc.tm_min,
                    tm_utc.tm_sec,
                    (offset_hour >= 0) ? '+' : '-',
                    abs(offset_hour),
                    offset_min_rem);
            break;
        }

        /* Complex types not supported - use JSON conversion */
        case OS_TR181_TYPE_NONE:
        case OS_TR181_TYPE_LIST:
        case OS_TR181_TYPE_DICT:
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
        case OS_TR181_TYPE_PROPERTY:
            return OS_TR181_ERROR_INVALID;
    }

    *str = strdup(buffer);
    return *str ? OS_TR181_SUCCESS : OS_TR181_ERROR;
}

/* ========================================================================
 * Helper: Parse ISO 8601 datetime string to timestamp
 * ======================================================================== */

/**
 * Parse ISO 8601 datetime string with timezone
 * Format: YYYY-MM-DDTHH:MM:SS+HH:MM or YYYY-MM-DDTHH:MM:SSZ
 * Examples: "2024-01-21T12:53:27+01:00", "2024-01-21T13:20:00Z"
 *
 * @param str Input string in ISO 8601 format
 * @param ts Output timestamp (UTC time + timezone offset)
 * @return OS_TR181_SUCCESS on success, error code on failure
 */
static os_tr181_error_t parse_iso8601_datetime(const char *str, os_tr181_timestamp_t *ts)
{
    struct tm tm = {0};
    int offset_hour = 0, offset_min = 0;
    char offset_sign = '+';

    if (!str || !ts)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Parse datetime part: YYYY-MM-DDTHH:MM:SS */
    char *rest = strptime(str, "%Y-%m-%dT%H:%M:%S", &tm);
    if (!rest)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Parse timezone offset: +HH:MM or -HH:MM or Z */
    if (*rest == 'Z' || *rest == '\0')
    {
        /* UTC timezone */
        offset_hour = 0;
        offset_min = 0;
    }
    else if (*rest == '+' || *rest == '-')
    {
        offset_sign = *rest;
        rest++;

        /* Parse HH:MM */
        if (sscanf(rest, "%d:%d", &offset_hour, &offset_min) != 2)
        {
            return OS_TR181_ERROR_INVALID;
        }

        /* Validate ranges */
        if (offset_hour < 0 || offset_hour > 23 || offset_min < 0 || offset_min > 59)
        {
            return OS_TR181_ERROR_INVALID;
        }
    }
    else
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Convert to Unix timestamp (treats parsed time as UTC) */
    time_t timestamp = timegm(&tm);
    if (timestamp == (time_t)-1)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Calculate total offset in minutes */
    int total_offset = offset_hour * 60 + offset_min;
    if (offset_sign == '-')
    {
        total_offset = -total_offset;
    }

    /* Validate offset range */
    if (total_offset < -1439 || total_offset > 1439)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Adjust timestamp to UTC: the parsed time is local time in the given timezone
     * UTC = local_time - offset
     * Example: "12:53:27+01:00" means local 12:53, which is 11:53 UTC
     */
    timestamp -= (total_offset * 60);

    ts->sec = (int64_t)timestamp;
    ts->offset = (int16_t)total_offset;

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * String Conversion Functions
 * ======================================================================== */

os_tr181_error_t os_val_from_str(os_tr181_val_t *v, const char *str, os_tr181_param_type_t type)
{
    if (!v || !str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (type)
    {
        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            return os_val_set_str_dup(v, str);

        case OS_TR181_TYPE_INT: {
            char *endptr;
            long val = strtol(str, &endptr, 10);
            if (endptr > str && (*endptr == '\0' || isspace(*endptr)))
            {
                if (val >= INT_MIN && val <= INT_MAX)
                {
                    return os_val_set_int(v, (int)val);
                }
                return OS_TR181_ERROR_OVERFLOW;
            }
            return OS_TR181_ERROR_INVALID;
        }

        case OS_TR181_TYPE_UINT: {
            char *endptr;
            unsigned long val = strtoul(str, &endptr, 10);
            if (endptr > str && (*endptr == '\0' || isspace(*endptr)))
            {
                if (val <= UINT_MAX)
                {
                    return os_val_set_uint(v, (unsigned int)val);
                }
                return OS_TR181_ERROR_OVERFLOW;
            }
            return OS_TR181_ERROR_INVALID;
        }

        case OS_TR181_TYPE_INT64: {
            char *endptr;
            long long val = strtoll(str, &endptr, 10);
            if (endptr > str && (*endptr == '\0' || isspace(*endptr)))
            {
                return os_val_set_int64(v, (int64_t)val);
            }
            return OS_TR181_ERROR_INVALID;
        }

        case OS_TR181_TYPE_UINT64: {
            char *endptr;
            unsigned long long val = strtoull(str, &endptr, 10);
            if (endptr > str && (*endptr == '\0' || isspace(*endptr)))
            {
                return os_val_set_uint64(v, (uint64_t)val);
            }
            return OS_TR181_ERROR_INVALID;
        }

        case OS_TR181_TYPE_BOOL:
            if (strcasecmp(str, "true") == 0 || strcmp(str, "1") == 0)
            {
                return os_val_set_bool(v, true);
            }
            if (strcasecmp(str, "false") == 0 || strcmp(str, "0") == 0)
            {
                return os_val_set_bool(v, false);
            }
            return OS_TR181_ERROR_INVALID;

        case OS_TR181_TYPE_DOUBLE: {
            char *endptr;
            double val = strtod(str, &endptr);
            if (endptr > str && (*endptr == '\0' || isspace(*endptr)))
            {
                return os_val_set_double(v, val);
            }
            return OS_TR181_ERROR_INVALID;
        }

        case OS_TR181_TYPE_DATETIME: {
            os_tr181_timestamp_t ts;
            os_tr181_error_t err = parse_iso8601_datetime(str, &ts);
            if (err != OS_TR181_SUCCESS)
            {
                return err;
            }
            return os_val_set_datetime(v, ts);
        }

        /* Complex types not supported - use JSON conversion */
        case OS_TR181_TYPE_NONE:
        case OS_TR181_TYPE_LIST:
        case OS_TR181_TYPE_DICT:
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
        case OS_TR181_TYPE_PROPERTY:
            return OS_TR181_ERROR_INVALID;
    }

    /* Unreachable - all cases return */
    return OS_TR181_ERROR_INVALID;
}

/* ========================================================================
 * Dictionary Operations
 * ======================================================================== */

/* Internal helper: Find dictionary entry by key */
static struct dict_entry *find_dict_entry(const os_val_container_t *container, const char *key)
{
    if (!container || !key)
    {
        return NULL;
    }

    struct dict_entry *entry = container->dict;
    while (entry)
    {
        if (strcmp(entry->key, key) == 0)
        {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

void os_val_set_dict(os_tr181_val_t *val)
{
    if (!val)
    {
        return;
    }

    os_val_free(val);
    val->type = OS_TR181_TYPE_DICT;
    val->container = calloc(1, sizeof(os_val_container_t));
}

os_tr181_error_t os_val_dict_set(os_tr181_val_t *dict, const char *key, const os_tr181_val_t *value)
{
    if (!dict || dict->type != OS_TR181_TYPE_DICT || !key || !*key || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (!dict->container)
    {
        dict->container = calloc(1, sizeof(os_val_container_t));
        if (!dict->container)
        {
            return OS_TR181_ERROR;
        }
    }

    /* Check if key already exists */
    struct dict_entry *entry = find_dict_entry(dict->container, key);
    if (entry)
    {
        /* Update existing entry */
        os_tr181_val_t *new_value = os_val_new();
        if (!new_value)
        {
            return OS_TR181_ERROR;
        }
        if (os_val_copy(new_value, value) != OS_TR181_SUCCESS)
        {
            os_val_delete(new_value);
            return OS_TR181_ERROR;
        }
        /* Free old value */
        os_val_delete(entry->value);
        entry->value = new_value;
        return OS_TR181_SUCCESS;
    }

    /* Create new entry */
    entry = malloc(sizeof(struct dict_entry));
    if (!entry)
    {
        return OS_TR181_ERROR;
    }

    entry->key = strdup(key);
    if (!entry->key)
    {
        free(entry);
        return OS_TR181_ERROR;
    }

    entry->value = os_val_new();
    if (!entry->value)
    {
        free(entry->key);
        free(entry);
        return OS_TR181_ERROR;
    }

    if (os_val_copy(entry->value, value) != OS_TR181_SUCCESS)
    {
        os_val_delete(entry->value);
        free(entry->key);
        free(entry);
        return OS_TR181_ERROR;
    }

    /* Add to head of list */
    entry->next = dict->container->dict;
    dict->container->dict = entry;
    dict->container->count++;

    return OS_TR181_SUCCESS;
}

const os_tr181_val_t *os_val_dict_get(const os_tr181_val_t *dict, const char *key)
{
    if (!dict || dict->type != OS_TR181_TYPE_DICT || !key || !dict->container)
    {
        return NULL;
    }

    struct dict_entry *entry = find_dict_entry(dict->container, key);
    return entry ? entry->value : NULL;
}

bool os_val_dict_has_key(const os_tr181_val_t *dict, const char *key)
{
    return os_val_dict_get(dict, key) != NULL;
}

os_tr181_error_t os_val_dict_remove(os_tr181_val_t *dict, const char *key)
{
    if (!dict || dict->type != OS_TR181_TYPE_DICT || !key || !dict->container)
    {
        return OS_TR181_ERROR_INVALID;
    }

    struct dict_entry **prev = &dict->container->dict;
    struct dict_entry *entry = dict->container->dict;

    while (entry)
    {
        if (strcmp(entry->key, key) == 0)
        {
            /* Remove from chain */
            *prev = entry->next;
            dict->container->count--;

            /* Free entry */
            free(entry->key);
            os_val_delete(entry->value);
            free(entry);

            return OS_TR181_SUCCESS;
        }
        prev = &entry->next;
        entry = entry->next;
    }

    return OS_TR181_ERROR_NOT_FOUND;
}

size_t os_val_dict_size(const os_tr181_val_t *dict)
{
    if (!dict || dict->type != OS_TR181_TYPE_DICT || !dict->container)
    {
        return 0;
    }
    return dict->container->count;
}

os_tr181_error_t os_val_dict_get_keys(const os_tr181_val_t *dict, const char **keys, size_t capacity, size_t *count)
{
    if (!dict || dict->type != OS_TR181_TYPE_DICT || !keys || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    *count = 0;

    if (!dict->container || dict->container->count == 0)
    {
        return OS_TR181_SUCCESS;
    }

    /* Check if capacity is sufficient */
    if (capacity < dict->container->count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Fill keys array */
    struct dict_entry *entry = dict->container->dict;
    size_t i = 0;
    while (entry && i < dict->container->count)
    {
        keys[i] = entry->key; /* Point to internal key (read-only) */
        i++;
        entry = entry->next;
    }

    *count = i;
    return OS_TR181_SUCCESS;
}

/* Dictionary convenience functions */

const char *os_val_dict_get_string_or(const os_tr181_val_t *dict, const char *key, const char *default_val)
{
    return os_val_get_str_or(os_val_dict_get(dict, key), default_val);
}

int32_t os_val_dict_get_int_or(const os_tr181_val_t *dict, const char *key, int32_t default_val)
{
    return os_val_get_int_or(os_val_dict_get(dict, key), default_val);
}

uint32_t os_val_dict_get_uint_or(const os_tr181_val_t *dict, const char *key, uint32_t default_val)
{
    return os_val_get_uint_or(os_val_dict_get(dict, key), default_val);
}

bool os_val_dict_get_bool_or(const os_tr181_val_t *dict, const char *key, bool default_val)
{
    return os_val_get_bool_or(os_val_dict_get(dict, key), default_val);
}

double os_val_dict_get_double_or(const os_tr181_val_t *dict, const char *key, double default_val)
{
    return os_val_get_double_or(os_val_dict_get(dict, key), default_val);
}

os_tr181_error_t os_val_dict_set_string(os_tr181_val_t *dict, const char *key, const char *value)
{
    /* Temporary reference - safe because os_val_dict_set makes deep copy */
    os_tr181_val_t v = OS_VAL_STR_REF(value);
    return os_val_dict_set(dict, key, &v);
}

os_tr181_error_t os_val_dict_set_int(os_tr181_val_t *dict, const char *key, int32_t value)
{
    os_tr181_val_t v = OS_VAL_INT(value);
    return os_val_dict_set(dict, key, &v);
}

os_tr181_error_t os_val_dict_set_uint(os_tr181_val_t *dict, const char *key, uint32_t value)
{
    os_tr181_val_t v = OS_VAL_UINT(value);
    return os_val_dict_set(dict, key, &v);
}

os_tr181_error_t os_val_dict_set_bool(os_tr181_val_t *dict, const char *key, bool value)
{
    os_tr181_val_t v = OS_VAL_BOOL(value);
    return os_val_dict_set(dict, key, &v);
}

os_tr181_error_t os_val_dict_set_double(os_tr181_val_t *dict, const char *key, double value)
{
    os_tr181_val_t v = OS_VAL_DOUBLE(value);
    return os_val_dict_set(dict, key, &v);
}

/* ========================================================================
 * List Operations
 * ======================================================================== */

void os_val_set_list(os_tr181_val_t *val)
{
    if (!val)
    {
        return;
    }

    os_val_free(val);
    val->type = OS_TR181_TYPE_LIST;
    val->container = calloc(1, sizeof(os_val_container_t));
}

os_tr181_error_t os_val_list_append(os_tr181_val_t *list, const os_tr181_val_t *value)
{
    if (!list || list->type != OS_TR181_TYPE_LIST || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (!list->container)
    {
        list->container = calloc(1, sizeof(os_val_container_t));
        if (!list->container)
        {
            return OS_TR181_ERROR;
        }
    }

    /* Create new entry */
    struct list_entry *entry = malloc(sizeof(struct list_entry));
    if (!entry)
    {
        return OS_TR181_ERROR;
    }

    entry->value = os_val_new();
    if (!entry->value)
    {
        free(entry);
        return OS_TR181_ERROR;
    }

    if (os_val_copy(entry->value, value) != OS_TR181_SUCCESS)
    {
        os_val_delete(entry->value);
        free(entry);
        return OS_TR181_ERROR;
    }

    entry->next = NULL;

    /* Append to end of list */
    if (!list->container->list)
    {
        list->container->list = entry;
    }
    else
    {
        struct list_entry *tail = list->container->list;
        while (tail->next)
        {
            tail = tail->next;
        }
        tail->next = entry;
    }

    list->container->count++;
    return OS_TR181_SUCCESS;
}

const os_tr181_val_t *os_val_list_get(const os_tr181_val_t *list, size_t index)
{
    if (!list || list->type != OS_TR181_TYPE_LIST || !list->container)
    {
        return NULL;
    }

    if (index >= list->container->count)
    {
        return NULL;
    }

    struct list_entry *entry = list->container->list;
    for (size_t i = 0; i < index && entry; i++)
    {
        entry = entry->next;
    }

    return entry ? entry->value : NULL;
}

os_tr181_error_t os_val_list_set(os_tr181_val_t *list, size_t index, const os_tr181_val_t *value)
{
    if (!list || list->type != OS_TR181_TYPE_LIST || !value || !list->container)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (index >= list->container->count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    struct list_entry *entry = list->container->list;
    for (size_t i = 0; i < index && entry; i++)
    {
        entry = entry->next;
    }

    if (!entry)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Replace value */
    os_tr181_val_t *new_value = os_val_new();
    if (!new_value)
    {
        return OS_TR181_ERROR;
    }

    if (os_val_copy(new_value, value) != OS_TR181_SUCCESS)
    {
        os_val_delete(new_value);
        return OS_TR181_ERROR;
    }

    /* Free old value */
    os_val_delete(entry->value);

    entry->value = new_value;
    return OS_TR181_SUCCESS;
}

size_t os_val_list_size(const os_tr181_val_t *list)
{
    if (!list || list->type != OS_TR181_TYPE_LIST || !list->container)
    {
        return 0;
    }
    return list->container->count;
}

os_tr181_error_t os_val_list_remove(os_tr181_val_t *list, size_t index)
{
    if (!list || list->type != OS_TR181_TYPE_LIST || !list->container)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (index >= list->container->count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    struct list_entry **prev = &list->container->list;
    struct list_entry *entry = list->container->list;

    for (size_t i = 0; i < index && entry; i++)
    {
        prev = &entry->next;
        entry = entry->next;
    }

    if (!entry)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Remove from chain */
    *prev = entry->next;
    list->container->count--;

    /* Free entry */
    os_val_delete(entry->value);
    free(entry);

    return OS_TR181_SUCCESS;
}

/* List convenience functions */

os_tr181_error_t os_val_list_append_string(os_tr181_val_t *list, const char *value)
{
    /* Temporary reference - safe because os_val_list_append makes deep copy */
    os_tr181_val_t v = OS_VAL_STR_REF(value);
    return os_val_list_append(list, &v);
}

os_tr181_error_t os_val_list_append_int(os_tr181_val_t *list, int32_t value)
{
    os_tr181_val_t v = OS_VAL_INT(value);
    return os_val_list_append(list, &v);
}

os_tr181_error_t os_val_list_append_uint(os_tr181_val_t *list, uint32_t value)
{
    os_tr181_val_t v = OS_VAL_UINT(value);
    return os_val_list_append(list, &v);
}

os_tr181_error_t os_val_list_append_bool(os_tr181_val_t *list, bool value)
{
    os_tr181_val_t v = OS_VAL_BOOL(value);
    return os_val_list_append(list, &v);
}

os_tr181_error_t os_val_list_append_double(os_tr181_val_t *list, double value)
{
    os_tr181_val_t v = OS_VAL_DOUBLE(value);
    return os_val_list_append(list, &v);
}

/* ========================================================================
 * Iterator Implementation
 * ======================================================================== */

os_tr181_val_t *os_val_iter_init(os_val_iter_t *iter, os_tr181_val_t *container)
{
    if (!iter || !container || !container->container)
    {
        if (iter) *iter = (os_val_iter_t){0};
        return NULL;
    }

    iter->container = container;
    iter->prev_node = NULL;

    if (container->type == OS_TR181_TYPE_DICT)
    {
        struct dict_entry *first = container->container->dict;
        iter->current = first;
        iter->next_node = first ? first->next : NULL;
        return first ? first->value : NULL;
    }
    else if (container->type == OS_TR181_TYPE_LIST)
    {
        struct list_entry *first = container->container->list;
        iter->current = first;
        iter->next_node = first ? first->next : NULL;
        return first ? first->value : NULL;
    }

    *iter = (os_val_iter_t){0};
    return NULL;
}

os_tr181_val_t *os_val_iter_next(os_val_iter_t *iter)
{
    if (!iter || !iter->next_node)
    {
        if (iter) iter->current = NULL;
        return NULL;
    }

    /* If current was deleted it will be NULL — keep prev_node as is.
     * Otherwise advance prev to current before overwriting current. */
    if (iter->current) iter->prev_node = iter->current;

    if (iter->container->type == OS_TR181_TYPE_DICT)
    {
        struct dict_entry *entry = (struct dict_entry *)iter->next_node;
        iter->current = entry;
        iter->next_node = entry->next;
        return entry->value;
    }
    else
    {
        struct list_entry *entry = (struct list_entry *)iter->next_node;
        iter->current = entry;
        iter->next_node = entry->next;
        return entry->value;
    }
}

const char *os_val_iter_key(const os_val_iter_t *iter)
{
    if (!iter || !iter->current || !iter->container || iter->container->type != OS_TR181_TYPE_DICT)
    {
        return NULL;
    }
    return ((struct dict_entry *)iter->current)->key;
}

void os_val_iter_delete(os_val_iter_t *iter)
{
    if (!iter || !iter->current || !iter->container || !iter->container->container) return;

    os_tr181_val_t *container = iter->container;

    if (container->type == OS_TR181_TYPE_DICT)
    {
        struct dict_entry *entry = (struct dict_entry *)iter->current;

        if (iter->prev_node)
            ((struct dict_entry *)iter->prev_node)->next = entry->next;
        else
            container->container->dict = entry->next;

        container->container->count--;
        free(entry->key);
        os_val_delete(entry->value);
        free(entry);
    }
    else if (container->type == OS_TR181_TYPE_LIST)
    {
        struct list_entry *entry = (struct list_entry *)iter->current;

        if (iter->prev_node)
            ((struct list_entry *)iter->prev_node)->next = entry->next;
        else
            container->container->list = entry->next;

        container->container->count--;
        os_val_delete(entry->value);
        free(entry);
    }

    /* next_node remains valid — os_val_iter_next() will use it.
     * prev_node stays as is — still the correct predecessor for the next element. */
    iter->current = NULL;
}
