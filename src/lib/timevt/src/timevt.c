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
#include <unistd.h> // getpid()

#include "log.h"

#include "timevt_client.h"

static te_client_handle g_client = NULL;

bool te_client_init(const char *name)
{
    if (NULL == g_client)
    {
        char clname[512];
        if (name == NULL)
        {
            (void)snprintf(clname, sizeof(clname), "%s[%d]", log_get_name(), getpid());
            name = clname;
        }
        g_client = tecli_open(name, NULL);
    }
    return g_client != NULL;
}

void te_client_deinit(void)
{
    if (g_client != NULL)
    {
        tecli_close(g_client);
        g_client = NULL;
    }
}

bool te_client_log(const char *cat, const char *subject, const char *step, const char *fmt, ... )
{
    if (g_client == NULL) return false;

    va_list args;
    va_start(args, fmt);
    bool rv = tecli_log_event(g_client, cat, subject, step, fmt, args);
    va_end(args);
    return rv;
}
