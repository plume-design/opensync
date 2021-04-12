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

#include <stdarg.h>
#include <stdlib.h> // lldiv
#include <time.h>

#include "timevt.h"

static size_t s_snprintf(char *dst, size_t dstsize, const char *fmt, ...)
{
    if (dstsize == 0 || dst == NULL) return 0;

    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(dst, dstsize, fmt, args);
    va_end(args);

    /* In case of formatting error return 0 : nothing was written */
    if (ret < 0) return 0;
    /* In case of possible buffer overflow return max of written chars */
    if (ret >= (int)dstsize)
    { // make sure string is terminated
        dst[dstsize - 1] = 0;
        return dstsize - 1;
    }
    /* success */
    return (size_t)ret;
}

size_t print_utc_time(char *dst, size_t dstsize, uint64_t time_ms)
{
    lldiv_t dr = lldiv(time_ms, 1000);
    time_t t = (time_t)dr.quot;
    int ms = (int)dr.rem;

    size_t n = strftime(dst, dstsize, "%b %d %H:%M:%S", gmtime(&t));
    if (n > 0)
    {
        n += s_snprintf(dst + n, dstsize - n, ".%03d", ms);
    }
    return n;
}

size_t print_mono_time(char *dst, size_t dstsize, uint64_t time_ms)
{
    // extract days
    lldiv_t dr1 = lldiv(time_ms, 24*60*60*1000LL);
    int days = (int)dr1.quot;

    // extract [ms]
    lldiv_t dr2 = lldiv(dr1.rem, 1000LL);
    int ms = dr2.rem;

    time_t t = (time_t)dr2.quot;
    
    size_t n = 0;
    if (days > 0)
    {
        n += s_snprintf(dst + n, dstsize - n, "%dd,", days);
    }
    size_t rv = strftime(dst + n, dstsize - n, "%H:%M:%S", gmtime(&t));
    if (rv > 0)
    {
        n += rv;
        n += s_snprintf(dst + n, dstsize - n, ".%03d", ms);
    }
    return n;
}

void print_time_event(FILE *f, const Sts__TimeEvent *te)
{
    char msg[1024 * 8];
    size_t len = 0;

    char timestr[256];
    print_mono_time(timestr, sizeof(timestr), te->time);

    len += s_snprintf(msg + len, sizeof(msg) - len, " %s <%s>", te->source, te->cat);

    if (te->subject)
    {
        len += s_snprintf(msg + len, sizeof(msg) - len, " %s:", te->subject);
    }

    if (te->seq)
    {
        len += s_snprintf(msg + len, sizeof(msg) - len, " %s:", te->seq);
    }

    if (te->msg)
    {
        len += s_snprintf(msg + len, sizeof(msg) - len, " %s", te->msg);
    }

    fprintf(f, "%s %s\n", timestr, msg);
}

void print_device_id(FILE *f, const Sts__DeviceID *did)
{
    fprintf(f, "LOC ID : %s\n", did->location_id);
    fprintf(f, "NODE ID: %s\n", did->node_id);
    fprintf(f, "FW VER : %s\n", did->firmware_version);
}

void print_time_event_report(FILE *f, const Sts__TimeEventsReport *rep)
{
    fprintf(f, "REPORT: %u\n", rep->seqno);

    char timestr[256];
    print_utc_time(timestr, sizeof(timestr), rep->realtime);
    fprintf(f, "TIME REAL: %s\n", timestr);

    print_mono_time(timestr, sizeof(timestr), rep->monotime);
    fprintf(f, "TIME MONO: %s\n", timestr);

    print_device_id(f, rep->deviceid);

    size_t n;
    for(n = 0; n < rep->n_events; n++)
    {
        print_time_event(f, rep->events[n]);
    }
}
