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
#include <getopt.h>

#include "target.h"

#include "dm.h"


#define DM_CLI_SHOW_INFO_ID         "id"
#define DM_CLI_SHOW_INFO_SERIAL     "serial_number"
#define DM_CLI_SHOW_INFO_MODEL      "model"
#define DM_CLI_SHOW_INFO_F_VERSION  "firmware_version"
#define DM_CLI_SHOW_INFO_P_VERSION  "platform_version"


static int dm_cli_show_info(char *opt)
{
    char buf[128];
    int  buflen = sizeof(buf);

    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_ID))
    {
        printf("id=%s\n", target_id_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_SERIAL))
    {
        printf("serial_number=%s\n", target_serial_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_MODEL))
    {
        printf("model=%s\n", target_model_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_F_VERSION))
    {
        printf("firmware_version=%s\n", target_sw_version_get(buf, buflen) ? buf : "?");
    }
    if (!opt || !strcmp(opt, DM_CLI_SHOW_INFO_P_VERSION))
    {
        printf("platform_version=%s\n", target_platform_version_get(buf, buflen) ? buf : "?");
    }

    return DM_CLI_DONE;
}

static bool dm_cli_help()
{
    printf("Usage:\n");
    printf("  dm [options]\n");
    printf("Options:\n");
    printf("  -h, --help          display this help message\n");
    printf("  -v, --verbose       increase verbosity\n");
    printf("  -i, --show-info [field name]\n");
    printf("                      display basic device info\n");
    return DM_CLI_DONE;
}

/**
 * dm_cli - handles DM command line arguments
 */
bool dm_cli(int argc, char *argv[], log_severity_t *log_severity)
{
    int opt;
    int verbose = 0;
    bool done = DM_CLI_CONTINUE;
    struct option long_options[] =
    {
        { .name = "help",          .has_arg = no_argument,       .val = 'h', },
        { .name = "verbose",       .has_arg = no_argument,       .val = 'v', },
        { .name = "show-info",     .has_arg = optional_argument, .val = 'i', },
        { NULL, 0, 0, 0 },
    };

    while ((opt = getopt_long(argc, argv, "hvi::", long_options, NULL)) != -1)
    {
        switch (opt)
        {
            case 'v':
                /* Handle log verbositiy level. This keeps compatibility with
                 * other managers that use os_opt_get().
                 */
                verbose += 1;
                *log_severity = (verbose == 0) ? LOG_SEVERITY_INFO :
                                (verbose == 1) ? LOG_SEVERITY_DEBUG :
                                                 LOG_SEVERITY_TRACE;
                /* This options allows DM to run as a daemon */
                break;
            case 'i':
                return dm_cli_show_info(optind < argc ? argv[optind] : NULL);
            case '?':
            case 'h':
            default:
                return dm_cli_help();
        }
     }

    return done;
}
