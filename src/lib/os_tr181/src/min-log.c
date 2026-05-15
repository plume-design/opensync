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
#include <stdbool.h>
#include <stdarg.h>
#include "min-log.h"

static log_severity_t g_log_severity = LOG_SEVERITY_DEFAULT;

#define LOG_SEVERITY_TO_STR(level, color) [LOG_SEVERITY_##level] = #level,
static const char *g_log_severity_str_map[] = {LOG_SEVERITY_TABLE(LOG_SEVERITY_TO_STR)};

bool log_open(char *name, int flags)
{
    (void)name;
    (void)flags;
    return true;
}

void log_severity_set(log_severity_t s)
{
    g_log_severity = s;
}

const char *log_severity_to_str(log_severity_t severity)
{
    if (severity < 0 || severity >= LOG_SEVERITY_LAST) return "INVALID";
    return g_log_severity_str_map[severity];
}

void log_print(log_severity_t severity, const char *fmt, ...)
{
    if (severity > g_log_severity) return;
    const char *lname = log_severity_to_str(severity);
    printf("[%s] ", lname);
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
    fflush(stdout);
}
