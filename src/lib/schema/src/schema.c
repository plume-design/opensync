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

/** Generate the C functions for parsing the schema file */
#include "schema.h"
#include "const.h"
#include "log.h"

#include "schema_gen.h"
#include "pjs_gen_c.h"

SCHEMA_LISTX(_SCHEMA_COL_IMPL)

void schema_filter_add(schema_filter_t *f, char *column)
{
    int idx = 0;
    if (f->num < 0 || f->num >= ARRAY_LEN(f->columns) - 1)
    {
        LOG(ERR, "Filter full %d/%d %s",
                f->num, ARRAY_LEN(f->columns) - 1, column);
        return;
    }
    // Does already exists
    for (idx=0; idx < f->num; idx++){
        if(!strcmp(f->columns[idx], column)){
            LOG(TRACE, "Filter already exists %s",
                    column);
            return;
        }
    }

    f->columns[f->num] = column;
    f->num++;
    f->columns[f->num] = NULL;
}

void schema_filter_init(schema_filter_t *f, char *op)
{
    memset(f, 0, sizeof(*f));
    schema_filter_add(f, op);
}

