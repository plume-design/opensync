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

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

/* minimal log api compatible with OpenSync */
/* can be used for a standalone build */

#include <stdbool.h>

#define LOG_SEVERITY_TABLE(ENTRY) \
    ENTRY(DISABLED, RED)          \
    ENTRY(EMERG, PURPLE)          \
    ENTRY(ALERT, PURPLE)          \
    ENTRY(CRIT, RED)              \
    ENTRY(ERR, RED)               \
    ENTRY(WARNING, YELLOW)        \
    ENTRY(NOTICE, GREEN)          \
    ENTRY(INFO, CYAN)             \
    ENTRY(DEBUG, NONE)            \
    ENTRY(TRACE, BLUE)

#define LOG_SEVERITY_DEFAULT LOG_SEVERITY_INFO

typedef enum
{
#define LOG_SEVERITY_T(sev, color) LOG_SEVERITY_##sev,
    /* Expand the LOG_SEVERITY_TABLE macro */
    LOG_SEVERITY_TABLE(LOG_SEVERITY_T) LOG_SEVERITY_LAST
} log_severity_t;

bool log_open(char *name, int flags);
void log_severity_set(log_severity_t s);
void log_print(log_severity_t severity, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOG(level, ...) log_print(LOG_SEVERITY_##level, __VA_ARGS__)
#define LOGEM(fmt, ...) LOG(EMERG, fmt, ##__VA_ARGS__)
#define LOGA(fmt, ...)  LOG(ALERT, fmt, ##__VA_ARGS__)
#define LOGC(fmt, ...)  LOG(CRIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...)  LOG(ERR, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...)  LOG(WARNING, fmt, ##__VA_ARGS__)
#define LOGN(fmt, ...)  LOG(NOTICE, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...)  LOG(INFO, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...)  LOG(DEBUG, fmt, ##__VA_ARGS__)
#define LOGT(fmt, ...)  LOG(TRACE, fmt, ##__VA_ARGS__)

#endif
