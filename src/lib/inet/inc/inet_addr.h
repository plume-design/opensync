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

#ifndef INET_ADDR_H_INCLUDED
#define INET_ADDR_H_INCLUDED

#include <arpa/inet.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "os_util.h"
#include "util.h"

typedef union { uint8_t  addr[4];   uint32_t raw; }       inet_ip4addr_t;
typedef union { uint16_t addr[16];  uint16_t raw[8]; }    inet_ip6addr_t;
typedef union { uint8_t  addr[6];   uint16_t raw; }       inet_macaddr_t;

#define PRI_inet_ip4addr_t              "%u.%u.%u.%u"
#define FMT_inet_ip4addr_t(x)           (x).addr[0], (x).addr[1], (x).addr[2], (x).addr[3]

#define PRI_inet_macaddr_t              "%02X:%02X:%02X:%02X:%02X:%02X"
#define FMT_inet_macaddr_t(x)           (x).addr[0], (x).addr[1], (x).addr[2], (x).addr[3], (x).addr[4], (x).addr[5]

#define PRI_inet_ip6addr_t              "%04X:%04X:%04X:%04X:%04X:%04X:%04X:%04X"
#define FMT_inet_ip6addr_t(x)           ntohs((x).raw[0]), \
                                        ntohs((x).raw[1]), \
                                        ntohs((x).raw[2]), \
                                        ntohs((x).raw[3]), \
                                        ntohs((x).raw[4]), \
                                        ntohs((x).raw[5]), \
                                        ntohs((x).raw[6]), \
                                        ntohs((x).raw[7])

#define INET_IP4ADDR_ANY                ((inet_ip4addr_t){ .raw = 0 })
#define INET_IP4ADDR_IS_ANY(x)          ((x).raw == 0)

#define INET_IP4ADDR(a, b, c, d)        ((inet_ip4addr_t){ .addr = {(a), (b), (c), (d) } })

#define INET_MACADDR(a, b, c, d, e, f)  ((inet_macaddr_t){ .addr = {(a), (b), (c), (d), (e), (f) } })

#define INET_MACADDR_BCAST              INET_MACADDR(0xff, 0xff, 0xff, 0xff, 0xff, 0xff)

static inline int inet_ip4addr_cmp(inet_ip4addr_t *a, inet_ip4addr_t *b)
{
    return ntohl(a->raw) - ntohl(b->raw);
}

static inline bool inet_ip4addr_fromstr(inet_ip4addr_t *addr, const char *str)
{

    if (inet_pton(AF_INET, str, addr)) return true;

    *addr = INET_IP4ADDR_ANY;

    return false;
}

static inline bool inet_macaddr_fromstr(inet_macaddr_t* mac, const char* str)
{
    char    pstr[64];
    char*   mstr = NULL;
    char*   mtok = NULL;
    long    cnum = 0;
    int     cmak = 0;

    if (strscpy(pstr, str, sizeof(pstr)) < 0) return false;

    mstr = pstr;
    while ((mtok = strsep(&mstr, ":-")) != NULL)
    {
        if (os_strtoul(mtok, &cnum, 16) != true)
        {
            return false;
        }

        /* Check if the parsed number is between 0 and 255 */
        if (cnum >= 256)
        {
            return false;
        }

        /* Check if we have more than 6 bytes */
        if (cmak >= 6)
        {
            return false;
        }

        mac->addr[cmak++] = cnum;
    }

    return true;
}

#endif /* INET_ADDR_H_INCLUDED */
