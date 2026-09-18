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

#include "dm_schema.h"

static void dump(uint16_t idx)
{
    if (idx == DM_IDX_NONE) return;

    char path[DM_SCHEMA_BUF_SIZE];
    dm_schema_str_buf(path, sizeof(path), dm_node_from_idx(idx));
    printf("%-9s  %s\n", dm_node_type_str(dm_nodes[idx].type), path);

    dump(dm_nodes[idx].child);
    dump(dm_nodes[idx].next);
}

static int count_nodes(uint16_t idx)
{
    if (idx == DM_IDX_NONE) return 0;
    return 1 + count_nodes(dm_nodes[idx].child) + count_nodes(dm_nodes[idx].next);
}

static void dump_counts(void)
{
    int total = 0;
    uint16_t child = dm_nodes[DM(Device).idx].child;

    while (child != DM_IDX_NONE)
    {
        int cnt = 1 + count_nodes(dm_nodes[child].child);
        printf("%5d  Device.%s\n", cnt, dm_node_name(dm_node_from_idx(child)));
        total += cnt;
        child = dm_nodes[child].next;
    }
    printf("%5d  Device (total)\n", total + 1);
}

void test_dump(void)
{
    dump(DM(Device).idx);
}
void test_count(void)
{
    dump_counts();
}
