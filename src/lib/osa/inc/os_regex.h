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

#ifndef OS_REGEX_H_INCLUDED
#define OS_REGEX_H_INCLUDED

#include <regex.h>
#include <inttypes.h>

#include "os.h"

#define OS_REG_LIST_ENTRY(id, str)      \
{                                       \
    .re_id = (id),                      \
    .re_str = (str),                    \
    .PRIV(re_flags) = 0x0               \
}

#define OS_REG_LIST_END(id)   OS_REG_LIST_ENTRY(id, NULL)

#define OS_REG_FLAG_INIT      (1 << 0)
#define OS_REG_FLAG_INVALID   (1 << 1)      /* Relist entry is invalid */

typedef struct
{
    const char*     re_str;
    int32_t         re_id;

    regex_t         PRIV(re_ex);
    uint32_t        PRIV(re_flags);
} os_reg_list_t;

extern int os_reg_list_match(
        os_reg_list_t* relist,
        char*  str,
        regmatch_t* pmatch,
        size_t nmatch);

extern void os_reg_match_cpy(
        char* dest,
        size_t destsz,
        char* src,
        regmatch_t srm);
#endif

