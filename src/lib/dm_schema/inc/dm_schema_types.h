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

/* dm_schema_types.h — TR181 schema utilities core types */
#ifndef DM_SCHEMA_TYPES_H
#define DM_SCHEMA_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

typedef enum
{
    DM_NODE_OBJECT = 0,
    DM_NODE_TABLE,
    DM_NODE_INSTANCE,
    DM_NODE_PARAMETER,
    DM_NODE_METHOD,
    DM_NODE_EVENT,
    DM_NODE_STUB, /* subtree excluded from build */
} dm_node_type_t;

#define DM_IDX_NONE ((uint16_t)0) /* sentinel — "no node", like NULL */

/* Public handles — distinct types prevent accidental mixing */
typedef struct
{
    uint16_t idx;
} dm_schema_node_t; /* reference to any node   */
typedef struct
{
    uint16_t idx;
} dm_schema_inst_t; /* reference to a {i} node */

/*
 * Internal flat node descriptor — 8 bytes, zero relocations.
 * Stored in dm_nodes[]; dm_nodes[0] is unused (sentinel).
 * Device root is dm_nodes[1].
 *
 * name is a byte offset into the flat dm_strings[] char array.
 * parent/child/next are indices into dm_nodes[] (0 = none).
 */
typedef struct
{
    uint64_t type : 4;    /* dm_node_type_t                                */
    uint64_t name : 17;   /* byte offset into dm_strings[]                 */
    uint64_t parent : 14; /* index into dm_nodes[], 0 = none               */
    uint64_t child : 14;  /* first child in sibling list, 0 = none         */
    uint64_t next : 14;   /* next sibling, 0 = none                        */
} dm_schema_node_data_t;  /* sizeof == 8 */

extern const dm_schema_node_data_t dm_nodes[]; /* [0] unused, [1] = Device root */
extern const char dm_strings[];                /* flat NUL-terminated string pool */
extern const size_t dm_nodes_count;            /* total entries in dm_nodes[] */

#endif /* DM_SCHEMA_TYPES_H */
