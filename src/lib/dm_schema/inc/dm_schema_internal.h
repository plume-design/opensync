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

/* dm_schema_internal.h — private struct definition for dm_schema_t.
 * Include only from dm_schema.c. Do not include from public headers or callers. */
#ifndef DM_SCHEMA_INTERNAL_H
#define DM_SCHEMA_INTERNAL_H

#include "dm_schema.h"

#define DM_SCHEMA_MAX_INST 8 /* max {i} ancestors in any path */

struct dm_schema
{
    dm_schema_node_t leaf;
    struct
    {
        uint16_t idx;    /* {i} node index (into dm_nodes[])     */
        uint32_t num;    /* instance number (when str == NULL)   */
        const char *str; /* non-NULL: render verbatim, not num   */
        bool set;        /* whether this slot has been assigned  */
    } inst[DM_SCHEMA_MAX_INST];
    int inst_count;
    bool no_dot;                  /* suppress trailing dot */
    char buf[DM_SCHEMA_BUF_SIZE]; /* rendered string storage */
};

#endif /* DM_SCHEMA_INTERNAL_H */
