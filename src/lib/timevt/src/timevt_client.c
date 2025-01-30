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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <zlib.h> // crc32
#include <ev.h>

#include <unistd.h> // getpid()

#include <sys/socket.h> // AF_UNIX

#define MODULE_NAME "TELOG"
#include "log.h"
#include "os_time.h"
#include "memutil.h"
#include "util.h"

#include "timevt_client.h"
#include "timevt_msg_link.h"

struct te_client
{
    ipc_msg_link_t *msglink; // IPC message link
    char *name;
};

te_client_handle tecli_open(const char *name, const char *addr)
{
    if (!name) return false;

    ipc_msg_link_t *ml = ipc_msg_link_open(addr, EV_DEFAULT, TELOG_CLIENT_MSG_LINK_ID);

    if (ml == NULL) return NULL;

    te_client_handle h = (te_client_handle)MALLOC(sizeof(*h));

    // reset object before use
    memset(h, 0, sizeof(*h));

    h->name = strdup(name);
    h->msglink = ml;

    return h;
}

void tecli_close(te_client_handle h)
{
    if (h == NULL) return;
    ipc_msg_link_close(h->msglink);
    FREE(h->name);
    FREE(h);
}

static void time_event_init(Sts__TimeEvent *pte, const char *cat, const char *source,
                    const char *subject, const char *step, const char *msg)
{
    sts__time_event__init(pte);

    pte->time = clock_mono_ms();
    pte->cat = (char *)cat;
    pte->source = (char *)source;
    pte->subject = (char *)subject;
    pte->seq = (char *)step;
    pte->msg = (char *)msg;
}

bool tecli_log_event(te_client_handle h, const char *cat,
        const char *subject, const char *step, const char *fmt, va_list args)
{
    char msg[ipc_msg_link_max_msize(h->msglink)];
    int slen = 0;

    if (step == NULL) step = TECLI_DEFAULT_STEP;

    if (fmt != NULL)
    {
        slen = vsnprintf(msg, sizeof(msg), fmt, args);
    }

    Sts__TimeEvent timevt;
    time_event_init(&timevt, cat, h->name, subject, step, slen > 0 ? msg : NULL);

    size_t proto_size = sts__time_event__get_packed_size(&timevt);
    size_t hdrlen = strlen(TIMEVT_HEADER);
    uint8_t outbuf[hdrlen + proto_size + sizeof(uint32_t)];

    strscpy((char*)outbuf, TIMEVT_HEADER, sizeof(outbuf));
    (void)sts__time_event__pack(&timevt, &outbuf[hdrlen]);
    (void)te_crc32_append(outbuf, hdrlen + proto_size);

    ipc_msg_t ipc_msg = { .addr = NULL, .data = outbuf, .size = sizeof(outbuf) };

    // no log on failure : server may be offline, it'sOK
    return ipc_msg_link_sendto(h->msglink, &ipc_msg);
}

uint32_t te_crc32_compute(uint8_t *src, size_t length)
{
    return crc32(~0UL, src, length);
}

size_t te_crc32_append(uint8_t *src, size_t length)
{
    uint32_t crc = te_crc32_compute(src, length);
    src[length++] = (uint8_t)(crc >> 24 & 0x0ff);
    src[length++] = (uint8_t)(crc >> 16 & 0x0ff);
    src[length++] = (uint8_t)(crc >> 8  & 0x0ff);
    src[length] = (uint8_t)(crc & 0x0ff);
    return sizeof(crc);
}

uint32_t te_crc32_read(const uint8_t *src)
{
    uint32_t crc = src[0];
    crc = (crc << 8) + src[1];
    crc = (crc << 8) + src[2];
    crc = (crc << 8) + src[3];
    return crc;
}
