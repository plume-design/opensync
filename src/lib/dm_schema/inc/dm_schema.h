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

/* dm_schema.h — TR181 schema utilities public API */
#ifndef DM_SCHEMA_H
#define DM_SCHEMA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <assert.h>

#include "dm_schema_types.h"
#include "dm_tr181_schema.h"

/*
 * Tagged instance value passed to dm_schema_fmt_at().
 * str != NULL: render str verbatim (alias, "*", search expression, …)
 * str == NULL: render num as decimal integer
 * no_dot: when set on the terminal sentinel, suppresses the trailing dot
 */
typedef struct
{
    dm_schema_inst_t inst;
    uint32_t num;
    const char *str;
    bool no_dot;
} dm_schema_inst_val_t;

/* Navigation macros — DM() returns dm_schema_node_t, DMI() returns dm_schema_inst_t */
#define DM(n)  ((n).node)
#define DMI(i) ((i).inst)

/* Pair an instance slot with an integer — compile error if i is not a {i} node */
#define DM_AT(i, n) dm_at(DMI(i), (n))

/* Pair an instance slot with a string (alias, wildcard, search expression, …) */
#define DM_AT_S(i, s) dm_at_s(DMI(i), (s))

/* Sentinels for terminating the varargs lists of the raw formatter functions */
#define DM_AT_END  ((dm_schema_inst_val_t){0})
#define DM_FMT_END ((uint32_t)-1)
/* No-dot variants — suppress trailing dot (e.g. for TR-181 reference values) */
#define DM_AT_END_NO_DOT  ((dm_schema_inst_val_t){.no_dot = true})
#define DM_FMT_END_NO_DOT ((uint32_t)-2)

/* One-shot formatter — explicit slots via DM_AT/DM_AT_S, any order */
#define DM_FMT_AT(leaf, ...)        dm_schema_fmt_at(DM(leaf), ##__VA_ARGS__, DM_AT_END)
#define DM_FMT_AT_NO_DOT(leaf, ...) dm_schema_fmt_at(DM(leaf), ##__VA_ARGS__, DM_AT_END_NO_DOT)
/* _dup variants — caller owns the returned string (must free) */
#define DM_FMT_AT_DUP(leaf, ...) dm_schema_fmt_at_dup(DM(leaf), ##__VA_ARGS__, DM_AT_END)

/* One-shot formatter — positional integers, root-to-leaf order.
 * Simpler than DM_FMT_AT when slot identity is unambiguous. */
#define DM_FMT(leaf, ...)        dm_schema_fmt_pos(DM(leaf), ##__VA_ARGS__, DM_FMT_END)
#define DM_FMT_NO_DOT(leaf, ...) dm_schema_fmt_pos(DM(leaf), ##__VA_ARGS__, DM_FMT_END_NO_DOT)
/* _dup variant — caller owns the returned string (must free) */
#define DM_FMT_DUP(leaf, ...) dm_schema_fmt_pos_dup(DM(leaf), ##__VA_ARGS__, DM_FMT_END)

/* Template path string — returns "Device.Foo.{i}.Bar" style, uses static buffer */
#define DM_SCHEMA(n) dm_schema_str(DM(n))
/* _dup variant — caller owns the returned string (must free) */
#define DM_SCHEMA_DUP(n) dm_schema_str_dup(DM(n))

/* Last segment name — returns e.g. "Name" or "FirmwareImageNumberOfEntries" */
#define DM_NAME(n) dm_node_name(DM(n))

/* Type-safe instance value constructors — compiler rejects dm_schema_node_t for inst */
static inline dm_schema_inst_val_t dm_at(dm_schema_inst_t inst, uint32_t num)
{
    return (dm_schema_inst_val_t){inst, num, NULL, false};
}

static inline dm_schema_inst_val_t dm_at_s(dm_schema_inst_t inst, const char *str)
{
    return (dm_schema_inst_val_t){inst, 0, str, false};
}

/* Inline accessors */
static inline const char *dm_node_name(dm_schema_node_t n)
{
    assert(n.idx < dm_nodes_count);
    return dm_strings + dm_nodes[n.idx].name;
}

/* Tree traversal */
static inline const dm_schema_node_data_t *dm_node(dm_schema_node_t n)
{
    assert(n.idx < dm_nodes_count);
    return &dm_nodes[n.idx];
}

static inline dm_schema_node_t dm_node_ref(const dm_schema_node_data_t *data)
{
    assert(data != NULL);
    assert(data >= dm_nodes && data < dm_nodes + dm_nodes_count);
    return (dm_schema_node_t){.idx = (uint16_t)(data - dm_nodes)};
}

/* Construct a dm_schema_node_t from a raw index — for code traversing dm_nodes[] directly. */
static inline dm_schema_node_t dm_node_from_idx(uint16_t idx)
{
    assert(idx < dm_nodes_count);
    return (dm_schema_node_t){.idx = idx};
}

/* ---------------------------------------------------------------------------
 * Runtime path API
 * -------------------------------------------------------------------------*/

#define DM_SCHEMA_BUF_SIZE 1024 /* max rendered path string length */

/* Returns the name of a node type (e.g. "object", "table").
 * Returns "" for unknown values. No padding — caller formats as needed. */
const char *dm_node_type_str(dm_node_type_t type);

/* Opaque path object — heap-allocated via dm_schema_new/dm_schema_delete. */
typedef struct dm_schema dm_schema_t;

/* Template string with {i} placeholders, e.g. "Device.WiFi.AccessPoint.{i}.Status".
 * _buf writes to caller-supplied buffer; bare version uses a static buffer. */
const char *dm_schema_str_buf(char *buf, size_t buf_size, dm_schema_node_t node);
const char *dm_schema_str(dm_schema_node_t node);
/* _dup variant — caller owns the returned string (must free) */
char *dm_schema_str_dup(dm_schema_node_t node);

/* Heap allocation pair */
dm_schema_t *dm_schema_new(dm_schema_node_t leaf);
void dm_schema_delete(dm_schema_t *path);

/* Assign integer instance number to the {i} slot identified by inst.
 * Returns false if inst is not an ancestor of the leaf. */
bool dm_schema_set_instance(dm_schema_t *path, dm_schema_inst_t inst, uint32_t num);

/* Assign string instance value (alias, "*", search expression, …) to the slot.
 * The string pointer is borrowed — caller must keep it alive until the path
 * is rendered or freed. Returns false if inst is not an ancestor of the leaf. */
bool dm_schema_set_instance_str(dm_schema_t *path, dm_schema_inst_t inst, const char *str);

/* Suppress trailing dot on the rendered path (e.g. for TR-181 reference values). */
void dm_schema_set_no_dot(dm_schema_t *path);

/* Render to instantiated string, e.g. "Device.WiFi.AccessPoint.3.Status".
 * Returns NULL if any instance slot has not been set.
 * The returned pointer is valid until the next call to dm_schema_fmt()
 * or dm_schema_delete()/dm_schema_free() on this path. */
const char *dm_schema_fmt(dm_schema_t *path);

/* One-shot explicit-slot formatter (use DM_AT/DM_AT_S for type safety).
 * Returns NULL if any instance slot has not been set.
 * _ap accepts a va_list; _buf writes to caller buffer; bare uses static buffer. */
const char *dm_schema_fmt_at_buf_ap(char *buf, size_t buf_size, dm_schema_node_t leaf, va_list ap);
const char *dm_schema_fmt_at_buf(char *buf, size_t buf_size, dm_schema_node_t leaf, ...);
const char *dm_schema_fmt_at(dm_schema_node_t leaf, ...);
/* _dup variant — caller owns the returned string (must free) */
char *dm_schema_fmt_at_dup(dm_schema_node_t leaf, ...);

/* One-shot positional formatter — integers root-to-leaf (use DM_FMT macro).
 * Returns NULL if any instance slot has not been set.
 * _ap accepts a va_list; _buf writes to caller buffer; bare uses static buffer. */
const char *dm_schema_fmt_pos_buf_ap(char *buf, size_t buf_size, dm_schema_node_t leaf, va_list ap);
const char *dm_schema_fmt_pos_buf(char *buf, size_t buf_size, dm_schema_node_t leaf, ...);
const char *dm_schema_fmt_pos(dm_schema_node_t leaf, ...);
/* _dup variant — caller owns the returned string (must free) */
char *dm_schema_fmt_pos_dup(dm_schema_node_t leaf, ...);

/*
 * Parse a single instance identifier from a runtime path string.
 *
 * All non-instance path segments must match the corresponding schema node
 * names. The segment at the position of inst is extracted and returned.
 *
 * Returns NULL if the path does not conform to the schema (wrong object name,
 * path too short, or output buffer too small).
 *
 * Example:
 *   const char *path = "Device.WiFi.AccessPoint.home2G.AssociatedDevice.4";
 *   char *ap = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i), path); // "home2G"
 *   char *ad = dm_schema_parse_inst_dup(DMI(Device.WiFi.AccessPoint.i.AssociatedDevice.i), path); // "4"
 */
const char *dm_schema_parse_inst_buf(char *buf, size_t buf_size, dm_schema_inst_t inst, const char *path);
/* _dup variant — caller owns the returned string (must free) */
char *dm_schema_parse_inst_dup(dm_schema_inst_t inst, const char *path);

#endif /* DM_SCHEMA_H */
