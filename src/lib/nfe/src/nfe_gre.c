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

#include "nfe_proto.h"
#include "nfe_input.h"
#include "nfe_gre.h"

int
nfe_proto_gre(struct nfe_packet *p)
{
    unsigned short flag;
    unsigned short type;
    size_t off;

    if ((size_t)(p->tail - p->data) < sizeof(struct grehdr))
        return -1;

    flag = read16(p->data + 0);
    type = read16(p->data + 2);

    if (flag & (0x0003))
        return 0;

    off = 4;
    /* routing & checksum */
    if (flag & (0x8000 | 0x4000))
        off += 4;

    /* key */
    if (flag & (0x2000))
        off += 4;

    /* seq */
    if (flag & (0x1000))
        off += 4;

    /* sre variable length fields */
    if (flag & (0x4000)) {
        for (;;) {
            if (p->data + (off + sizeof(struct srehdr)) > p->tail)
                return -1;
            if (p->data[off + 3] == 0)
                break;
            off += p->data[off + 3];
        }
    }

    p->data += off;

    return nfe_input_handoff(type)(p);
}
