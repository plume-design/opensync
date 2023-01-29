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

#ifndef NFE_PRIV_H
#define NFE_PRIV_H

#define NFE_H /* Act as nfe.h for private, internal use */
#include "nfe_types.h"
#undef NFE_H

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <endian.h>
#include <arpa/inet.h>

#define EXPORT __attribute__((visibility("default")))

#define nfe_assert assert
#define nfe_static_assert(cond, msg) /* disable uclibc _Static_assert(cond, msg) */

#define nfe_min(x,y) ({ \
    typeof(x) _x = (x); \
    typeof(y) _y = (y); \
    (void) (&_x == &_y); \
    _x < _y ? _x : _y; })

static inline uint16_t
read16(const uint8_t *src)
{
    return
        ((int32_t)src[0] << 8)  | ((int32_t)src[1]);
}

static inline int32_t
read32(const uint8_t *src)
{
    return
        ((int32_t)src[0] << 24) | ((int32_t)src[1] << 16) |
        ((int32_t)src[2] << 8)  | ((int32_t)src[3]);
}

static inline unsigned int
domain_len(uint8_t domain)
{
    switch (domain) {
        case NFE_AF_INET:
            return 4;
        case NFE_AF_INET6:
            return 16;
    };

    return 0;
}

#endif
