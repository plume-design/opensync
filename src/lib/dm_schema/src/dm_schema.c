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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "memutil.h"
#include "dm_schema_internal.h"

#define DM_SCHEMA_STACK_SIZE 32 /* max tree depth */

const char *dm_node_type_str(dm_node_type_t type)
{
    switch (type)
    {
        case DM_NODE_OBJECT:
            return "object";
        case DM_NODE_TABLE:
            return "table";
        case DM_NODE_INSTANCE:
            return "instance";
        case DM_NODE_PARAMETER:
            return "parameter";
        case DM_NODE_METHOD:
            return "method";
        case DM_NODE_EVENT:
            return "event";
        case DM_NODE_STUB:
            return "stub";
    }
    return "";
}

/* Object, table, and instance paths require a trailing dot per TR181 */
static bool node_has_trailing_dot(dm_node_type_t type)
{
    return type == DM_NODE_OBJECT || type == DM_NODE_TABLE || type == DM_NODE_INSTANCE;
}

/* Append src to buf[pos..buf_size-1], return updated pos. Always NUL-terminates.
 * Returns SIZE_MAX on overflow (self-propagates: passing SIZE_MAX as pos returns SIZE_MAX).
 * Caller must ensure buf != NULL and buf_size > 0. */
static size_t buf_append(char *buf, size_t buf_size, size_t pos, const char *src)
{
    if (pos == SIZE_MAX) return SIZE_MAX;
    if (buf_size == 0 || pos >= buf_size - 1) return SIZE_MAX;
    size_t remaining = buf_size - 1 - pos;
    size_t len = strlen(src);
    if (len > remaining) return SIZE_MAX;
    memcpy(buf + pos, src, len);
    buf[pos + len] = '\0';
    return pos + len;
}

const char *dm_schema_str_buf(char *buf, size_t buf_size, dm_schema_node_t node)
{
    if (!buf || buf_size == 0) return NULL;

    const char *stack[DM_SCHEMA_STACK_SIZE];
    int depth = 0;
    uint16_t idx = node.idx;
    while (idx != DM_IDX_NONE && depth < DM_SCHEMA_STACK_SIZE)
    {
        stack[depth++] = dm_node_name(dm_node_from_idx(idx));
        idx = dm_nodes[idx].parent;
    }

    size_t pos = 0;
    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--)
    {
        if (pos > 0) pos = buf_append(buf, buf_size, pos, ".");
        pos = buf_append(buf, buf_size, pos, stack[i]);
    }
    if (node.idx != DM_IDX_NONE && node_has_trailing_dot(dm_nodes[node.idx].type))
        pos = buf_append(buf, buf_size, pos, ".");
    return (pos == SIZE_MAX) ? NULL : buf;
}

const char *dm_schema_str(dm_schema_node_t node)
{
    static char buf[DM_SCHEMA_BUF_SIZE];
    return dm_schema_str_buf(buf, sizeof(buf), node);
}

char *dm_schema_str_dup(dm_schema_node_t node)
{
    char buf[DM_SCHEMA_BUF_SIZE];
    const char *s = dm_schema_str_buf(buf, sizeof(buf), node);
    return s ? STRDUP(s) : NULL;
}

void dm_schema_init(dm_schema_t *path, dm_schema_node_t leaf)
{
    memset(path, 0, sizeof(*path));
    path->leaf = leaf;

    uint16_t tmp[DM_SCHEMA_MAX_INST];
    int count = 0;
    uint16_t idx = leaf.idx;
    while (idx != DM_IDX_NONE && count < DM_SCHEMA_MAX_INST)
    {
        if (dm_nodes[idx].type == DM_NODE_INSTANCE) tmp[count++] = idx;
        idx = dm_nodes[idx].parent;
    }
    path->inst_count = count;
    for (int i = 0; i < count; i++)
        path->inst[i].idx = tmp[count - 1 - i];
}

void dm_schema_free(dm_schema_t *path)
{
    (void)path; /* no internal dynamic allocation today */
}

dm_schema_t *dm_schema_new(dm_schema_node_t leaf)
{
    dm_schema_t *path = CALLOC(1, sizeof(*path));
    if (path) dm_schema_init(path, leaf);
    return path;
}

void dm_schema_delete(dm_schema_t *path)
{
    FREE(path);
}

bool dm_schema_set_instance(dm_schema_t *path, dm_schema_inst_t inst, uint32_t num)
{
    for (int i = 0; i < path->inst_count; i++)
    {
        if (path->inst[i].idx == inst.idx)
        {
            path->inst[i].num = num;
            path->inst[i].str = NULL;
            path->inst[i].set = true;
            return true;
        }
    }
    return false;
}

bool dm_schema_set_instance_str(dm_schema_t *path, dm_schema_inst_t inst, const char *str)
{
    for (int i = 0; i < path->inst_count; i++)
    {
        if (path->inst[i].idx == inst.idx)
        {
            path->inst[i].str = str;
            path->inst[i].num = 0;
            path->inst[i].set = true;
            return true;
        }
    }
    return false;
}

void dm_schema_set_no_dot(dm_schema_t *path)
{
    path->no_dot = true;
}

const char *dm_schema_fmt(dm_schema_t *path)
{
    /* Collect segments leaf→root onto a stack */
    const char *names[DM_SCHEMA_STACK_SIZE];
    const char *istrs[DM_SCHEMA_STACK_SIZE];
    uint32_t inums[DM_SCHEMA_STACK_SIZE];
    bool is_inst[DM_SCHEMA_STACK_SIZE];
    int depth = 0;

    uint16_t idx = path->leaf.idx;
    while (idx != DM_IDX_NONE && depth < DM_SCHEMA_STACK_SIZE)
    {
        const dm_schema_node_data_t *n = &dm_nodes[idx];
        names[depth] = dm_node_name(dm_node_from_idx(idx));
        is_inst[depth] = (n->type == DM_NODE_INSTANCE);
        inums[depth] = 0;
        istrs[depth] = NULL;
        if (is_inst[depth])
        {
            bool found = false;
            for (int i = 0; i < path->inst_count; i++)
            {
                if (path->inst[i].idx == idx)
                {
                    if (!path->inst[i].set) return NULL;
                    inums[depth] = path->inst[i].num;
                    istrs[depth] = path->inst[i].str;
                    found = true;
                    break;
                }
            }
            if (!found) return NULL;
        }
        depth++;
        idx = n->parent;
    }

    /* Render root-to-leaf into buf */
    char *buf = path->buf;
    size_t pos = 0;
    buf[0] = '\0';
    for (int i = depth - 1; i >= 0; i--)
    {
        if (pos > 0) pos = buf_append(buf, DM_SCHEMA_BUF_SIZE, pos, ".");
        if (is_inst[i])
        {
            if (istrs[i])
            {
                pos = buf_append(buf, DM_SCHEMA_BUF_SIZE, pos, istrs[i]);
            }
            else
            {
                char num[16];
                snprintf(num, sizeof(num), "%u", inums[i]);
                pos = buf_append(buf, DM_SCHEMA_BUF_SIZE, pos, num);
            }
        }
        else
        {
            pos = buf_append(buf, DM_SCHEMA_BUF_SIZE, pos, names[i]);
        }
    }
    if (!path->no_dot && node_has_trailing_dot(dm_nodes[path->leaf.idx].type))
        pos = buf_append(buf, DM_SCHEMA_BUF_SIZE, pos, ".");
    return (pos == SIZE_MAX) ? NULL : buf;
}

const char *dm_schema_fmt_at_buf_ap(char *buf, size_t buf_size, dm_schema_node_t leaf, va_list ap)
{
    if (!buf || buf_size == 0) return NULL;

    dm_schema_t path;
    dm_schema_init(&path, leaf);

    while (1)
    {
        dm_schema_inst_val_t val = va_arg(ap, dm_schema_inst_val_t);
        if (val.inst.idx == DM_IDX_NONE)
        {
            if (val.no_dot) path.no_dot = true;
            break;
        }
        if (val.str)
            dm_schema_set_instance_str(&path, val.inst, val.str);
        else
            dm_schema_set_instance(&path, val.inst, val.num);
    }

    const char *result = dm_schema_fmt(&path);
    if (result && strlen(result) < buf_size)
    {
        memcpy(buf, result, strlen(result) + 1);
    }
    else
    {
        buf[0] = '\0';
        result = NULL;
    }
    dm_schema_free(&path);
    return result ? buf : NULL;
}

const char *dm_schema_fmt_at_buf(char *buf, size_t buf_size, dm_schema_node_t leaf, ...)
{
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_at_buf_ap(buf, buf_size, leaf, ap);
    va_end(ap);
    return result;
}

const char *dm_schema_fmt_at(dm_schema_node_t leaf, ...)
{
    static char buf[DM_SCHEMA_BUF_SIZE];
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_at_buf_ap(buf, sizeof(buf), leaf, ap);
    va_end(ap);
    return result;
}

char *dm_schema_fmt_at_dup(dm_schema_node_t leaf, ...)
{
    char buf[DM_SCHEMA_BUF_SIZE];
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_at_buf_ap(buf, sizeof(buf), leaf, ap);
    va_end(ap);
    return result ? STRDUP(result) : NULL;
}

const char *dm_schema_fmt_pos_buf_ap(char *buf, size_t buf_size, dm_schema_node_t leaf, va_list ap)
{
    if (!buf || buf_size == 0) return NULL;

    dm_schema_t path;
    dm_schema_init(&path, leaf);

    for (int i = 0; i <= path.inst_count; i++)
    {
        uint32_t num = va_arg(ap, uint32_t);
        if (num == DM_FMT_END_NO_DOT)
        {
            path.no_dot = true;
            break;
        }
        if (num == DM_FMT_END) break;
        if (i < path.inst_count)
        {
            path.inst[i].num = num;
            path.inst[i].set = true;
        }
    }

    const char *result = dm_schema_fmt(&path);
    if (result && strlen(result) < buf_size)
    {
        memcpy(buf, result, strlen(result) + 1);
    }
    else
    {
        buf[0] = '\0';
        result = NULL;
    }
    dm_schema_free(&path);
    return result ? buf : NULL;
}

const char *dm_schema_fmt_pos_buf(char *buf, size_t buf_size, dm_schema_node_t leaf, ...)
{
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_pos_buf_ap(buf, buf_size, leaf, ap);
    va_end(ap);
    return result;
}

const char *dm_schema_fmt_pos(dm_schema_node_t leaf, ...)
{
    static char buf[DM_SCHEMA_BUF_SIZE];
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_pos_buf_ap(buf, sizeof(buf), leaf, ap);
    va_end(ap);
    return result;
}

char *dm_schema_fmt_pos_dup(dm_schema_node_t leaf, ...)
{
    char buf[DM_SCHEMA_BUF_SIZE];
    va_list ap;
    va_start(ap, leaf);
    const char *result = dm_schema_fmt_pos_buf_ap(buf, sizeof(buf), leaf, ap);
    va_end(ap);
    return result ? STRDUP(result) : NULL;
}

/* Build ancestor chain from root down to inst_idx (inclusive).
 * Returns number of nodes placed into chain[]. */
static int build_ancestor_chain(uint16_t inst_idx, uint16_t *chain, int max)
{
    int count = 0;
    uint16_t idx = inst_idx;
    while (idx != DM_IDX_NONE && count < max)
    {
        chain[count++] = idx;
        idx = dm_nodes[idx].parent;
    }
    /* Reverse: chain[0] = Device root, chain[count-1] = inst_idx */
    for (int i = 0, j = count - 1; i < j; i++, j--)
    {
        uint16_t tmp = chain[i];
        chain[i] = chain[j];
        chain[j] = tmp;
    }
    return count;
}

const char *dm_schema_parse_inst_buf(char *buf, size_t buf_size, dm_schema_inst_t inst, const char *path)
{
    if (!buf || buf_size == 0 || !path) return NULL;

    uint16_t chain[DM_SCHEMA_STACK_SIZE];
    int chain_len = build_ancestor_chain(inst.idx, chain, DM_SCHEMA_STACK_SIZE);
    if (chain_len == 0) return NULL;

    const char *p = path;
    for (int ci = 0; ci < chain_len; ci++)
    {
        if (*p == '\0') return NULL; /* path too short */

        const char *seg_start = p;
        const char *dot = strchr(p, '.');
        size_t seg_len = dot ? (size_t)(dot - p) : strlen(p);
        p = dot ? dot + 1 : p + seg_len;

        const dm_schema_node_data_t *n = &dm_nodes[chain[ci]];

        if (n->type == DM_NODE_INSTANCE)
        {
            if (chain[ci] == inst.idx)
            {
                /* Target slot — copy segment to buf */
                if (seg_len >= buf_size) return NULL;
                memcpy(buf, seg_start, seg_len);
                buf[seg_len] = '\0';
                return buf;
            }
            /* Intermediate {i} — accept any segment, continue */
        }
        else
        {
            /* Non-instance node — segment must match schema name exactly */
            const char *name = dm_node_name(dm_node_from_idx(chain[ci]));
            if (strlen(name) != seg_len || memcmp(name, seg_start, seg_len) != 0) return NULL;
        }
    }
    return NULL;
}

char *dm_schema_parse_inst_dup(dm_schema_inst_t inst, const char *path)
{
    char buf[DM_SCHEMA_BUF_SIZE];
    const char *result = dm_schema_parse_inst_buf(buf, sizeof(buf), inst, path);
    return result ? STRDUP(result) : NULL;
}
