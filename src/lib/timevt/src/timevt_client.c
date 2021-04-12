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
#include <string.h>
#include <errno.h>
#include <time.h>

#include <zlib.h> // crc32

// getpid()
#include <unistd.h>

#include <sys/socket.h> // AF_UNIX

// open-sync libs
#define MODULE_NAME "te-client"
#include <log.h>
#include <os_uds_link.h>
#include <os_time.h>

#include "timevt_client.h"

struct te_client
{
    bool enabled;
    uds_link_t socklink; // unix datagram socket link
    udgram_t srvdg; // datagram for server
    char *proc_name;
};

te_client_handle tecli_open(const char *srv_name, const char *procname)
{
    te_client_handle h = NULL;

    if (srv_name == NULL) srv_name = TESRV_SOCKET_ADDR;
    if (strlen(srv_name) >= sizeof(h->srvdg.addr.sun_path) - 1)
    {
        LOG(ERR, "Too long server socket name: %s", srv_name);
        return NULL;
    }

    h = (te_client_handle)malloc(sizeof(*h));
    if (h == NULL)
    {
        LOG(ERR, "Cannot allocate memory for client object");
        return NULL;
    }

    // reset object before use
    memset(h, 0, sizeof(*h));

    // create client addr path by appending its process ID at the end
    int pidpos = -1;
    char sockname[sizeof(h->srvdg.addr.sun_path)];
    int slen = snprintf(sockname, sizeof(sockname), "%s.%n%d", TECLI_SOCKET_ADDR, &pidpos, getpid());
    // check socket name before init socket link
    if (slen <= 0 || !uds_link_init(&h->socklink, sockname, NULL))
    {
        free(h);
        return NULL;
    }

    h->proc_name = strdup(procname ? procname : sockname + pidpos);

    strcpy(h->srvdg.addr.sun_path, srv_name);
    // support abstract namespace socket
    if (srv_name[0] == '@') h->srvdg.addr.sun_path[0] = 0;
    h->srvdg.addr.sun_family = AF_UNIX;
    h->enabled = false;

    return h;
}

void tecli_close(te_client_handle h)
{
    if (h == NULL) return;
    uds_link_fini(&h->socklink);
    free(h->proc_name);
    free(h);
}

void tecli_enable(te_client_handle h, bool enable)
{
    h->enabled = enable;
}

static void log_event_local(const Sts__TimeEvent *pte)
{
    const char *cat_str = pte->cat;
    const char *subject = pte->subject ? pte->subject : "time-event";
    const char *seq = pte->seq ? pte->seq : "UNI";
    const char *msg = pte->msg ? pte->msg : "";

    char monotime[256];
    (void)print_mono_time(monotime, sizeof(monotime), pte->time);

    // time and source skipped because provided by logging engine
    LOG(INFO, "[%s] %s: %s: [%s] %s", monotime, cat_str, subject, seq, msg);
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
    char msg[TIMEVT_MAX_MSG_LEN] = { 0 };
    int slen = 0;

    if (step == NULL) step = TECLI_DEFAULT_STEP;

    if (fmt != NULL)
    {
        slen = vsnprintf(msg, sizeof(msg), fmt, args);
    }

    Sts__TimeEvent timevt;
    time_event_init(&timevt, cat, h->proc_name, subject, step, slen > 0 ? msg : NULL);

    log_event_local(&timevt);

    if (!h->enabled) return true;

    size_t proto_size = sts__time_event__get_packed_size(&timevt);
    size_t hdrlen = strlen(TIMEVT_HEADER);
    uint8_t outbuf[hdrlen + proto_size + sizeof(uint32_t)];

    strcpy((char*)outbuf, TIMEVT_HEADER);
    (void)sts__time_event__pack(&timevt, &outbuf[hdrlen]);
    (void)te_crc32_append(outbuf, hdrlen + proto_size);

    h->srvdg.data = outbuf;
    h->srvdg.size = sizeof(outbuf);
    // no log on failure : server may be offline, it'sOK
    return uds_link_sendto(&h->socklink, &h->srvdg);
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
