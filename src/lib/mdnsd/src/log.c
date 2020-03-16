/*
 * Copyright (c) 2018  Joachim Nilsson <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define SYSLOG_NAMES
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

static int do_syslog = 1;
static int loglevel  = LOG_NOTICE;

int mdnsd_log_level(char *level)
{
    int lvl = -1;
    int i;

    for (i = 0; prioritynames[i].c_name; i++) {
        size_t len = MAX(strlen(prioritynames[i].c_name),
                 strlen(level));

        if (!strncasecmp(prioritynames[i].c_name, level, len)) {
            lvl = prioritynames[i].c_val;
            break;
        }
    }

    if (-1 == lvl)
        lvl = atoi(level);
    if (lvl >= 0)
        loglevel = lvl;

    if (do_syslog)
        setlogmask(LOG_UPTO(loglevel));

    return lvl;
}

void mdnsd_log_open(const char *ident)
{
    openlog(ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
    setlogmask(LOG_UPTO(loglevel));
    do_syslog = 1;
}

void mdnsd_log_hex(char *msg, unsigned char *buffer, ssize_t len)
{
    char ascii[17];
    int i;

    if (do_syslog)
        return;

    if (loglevel < LOG_DEBUG)
        return;

    printf("%s", msg);

    memset(ascii, 0, sizeof(ascii));
    for (i = 0; i < len; i++) {
        if (i % 16 == 0)
            printf("%s\n%06x ", ascii, i);
        printf("%02X ", buffer[i]);

        if (isprint((int)(buffer[i])))
            ascii[i%16] = buffer[i];
        else
            ascii[i%16] = '.';
    }

    ascii[i % 16] = 0;
    while (i % 16) {
        printf("   ");
        i++;
    }

    printf("%s\n", ascii);
    printf("\n");
}

void mdnsd_log(int severity, const char *fmt, ...)
{
    FILE *file;
        va_list args;

    if (loglevel == INTERNAL_NOPRI)
        return;

    if (severity > LOG_WARNING)
        file = stdout;
    else
        file = stderr;

        va_start(args, fmt);
    if (do_syslog)
        vsyslog(severity, fmt, args);
    else if (severity <= loglevel) {
        vfprintf(file, fmt, args);
        fprintf(file, "\n");
        fflush(file);
    }
        va_end(args);
}

void mdnsd_log_time(struct timeval *tv, char *buf, size_t len)
{
    char tmp[15];
    time_t t;
    struct tm *tm;

    if (loglevel < LOG_DEBUG)
        return;

    t = tv->tv_sec;
    tm = localtime(&t);
    tm->tm_sec += tv->tv_usec / 1000000;
    if (buf && len > 8) {
        strftime(buf, len, "%H:%M:%S", tm);
        return;
    }

    strftime(tmp, sizeof(tmp), "%H:%M:%S", tm);
    mdnsd_log(LOG_DEBUG, "@%s", tmp);
}
