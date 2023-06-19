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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include "log.h"
#include "module.h"
#include "ff_lib.h"

#define OSFF_CLI_OK 0
#define OSFF_CLI_ERROR -1

static const char usage[] =
"osff_get FEATURE_FLAG  ...\n"
"\n"
"   -d  debug mode\n";

void osff_usage(const char *fmt, ...)
{
    fprintf(stderr, usage);

    if (fmt != NULL)
    {
        fprintf(stderr, "\nERROR: ");
        va_list va;

        va_start(va, fmt);
        vfprintf(stderr, fmt, va);
        va_end(va);

        fprintf(stderr, "\n");
    }
}


int main(int argc, char *argv[])
{
    int opt;
    bool debug = false;

    module_init();

    opterr = 0;

    while ((opt = getopt(argc, argv, "d")) != -1)
    {
        switch (opt)
        {
            case 'd':
                debug = true;
                break;

            case '?':
                osff_usage("Invalid option -%c", optopt);
                return OSFF_CLI_ERROR;
        }
    }

    argc -= optind;
    argv += optind;

    log_open("osff_get", LOG_OPEN_DEFAULT);

    if (debug)
    {
        log_severity_set(LOG_SEVERITY_DEBUG);
    }

    if (argc < 1)
    {
        osff_usage("At least 1 argument is required.");
        return OSFF_CLI_ERROR;
    }

    LOGD("Checking flag: %s", argv[0]);

    return ff_is_flag_enabled(argv[0]) ? OSFF_CLI_OK : OSFF_CLI_ERROR;
}
