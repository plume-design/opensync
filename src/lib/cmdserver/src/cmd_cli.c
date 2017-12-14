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
#include <string.h>

#include "util.h"
#include "log.h"
#include "cmd_server.h"

/**
 * "help" command
 */
CLIENT_HELP(help) =
{
    "[CMD] ; Show help for command CMD",

    NULL
};

CLIENT_CMD(help)
{
    (void)argc;
    (void)argv;

    struct client_cmd *cmd;

    if (argc < 2)
    {
        CLI_PRINTF(cli, "\n");

        cmd = client_cmd_table;
        while (cmd->acc_name != NULL)
        {
            CLI_PRINTF(cli, "%-16s%s\n", cmd->acc_name,
                                         cmd->acc_help->ch_short);
            cmd++;
        }

        CLI_PRINTF(cli, "\n");
    }
    else
    {
        cmd = client_cmd_table;
        while (cmd->acc_name != NULL)
        {
            if (strcmp(argv[1], cmd->acc_name) == 0)
            {
                if (cmd->acc_help->ch_long == NULL)
                {
                    CLI_PRINTF(cli, "\nHelp for '%s' not available.\n\n", cmd->acc_name);
                }
                else
                {
                    CLI_PRINTF(cli, "%s - %s\n\n%s\n", cmd->acc_name, cmd->acc_help->ch_short, cmd->acc_help->ch_long);
                }

                return true;
            }

            cmd++;
        }

        CLI_PRINTF(cli, "Help for '%s' not found.\n", argv[1]);
    }

    return true;
}

/**
 * Show current version
 */
extern const char *app_build_ver_get();
extern const char *app_build_time_get();
extern const char *app_build_author_get();

CLIENT_HELP(version) =
{
    "show current version",

    NULL
};

CLIENT_CMD(version)
{
    (void)argc;
    (void)argv;

    CLI_PRINTF(cli, "%s %s %s\n",
              app_build_ver_get(),
              app_build_author_get(),
              app_build_time_get());

    return true;
}

CLIENT_HELP(base64) =
{
    "Execute a command encoded in base64 (advanced users)",

    "Receive a base64 encoded string list (separated by the null-character) and execute it as a client command."
};

#define BASE64_ARGV_MAX 64

CLIENT_CMD(base64)
{
    char buf[8192];
    char *pbuf;
    ssize_t bufsz;

    int cmd_argc = 0;
    char *cmd_argv[BASE64_ARGV_MAX];

    if (argc != 2)
    {
        CLI_PRINTF(cli, "base64: Invalid number of arguments.\n");
        return false;
    }

    /* Base64 decode */
    bufsz = base64_decode(buf, sizeof(buf), argv[1]);
    if (bufsz < 0)
    {
        CLI_PRINTF(cli, "base64: Error decoding buffer. Too big?\n");
        return false;
    }

    /* Parse the string list inside the decoded buffer */
    cmd_argc = 0;
    pbuf = buf;
    while (pbuf < buf + bufsz)
    {
        if (pbuf + strlen(pbuf) > buf + bufsz)
        {
            CLI_PRINTF(cli, "base64: Format error.\n");
            return false;
        }

        if (cmd_argc >= BASE64_ARGV_MAX)
        {
            CLI_PRINTF(cli, "base64: Too many arguments in buffer.\n");
            return false;
        }

        cmd_argv[cmd_argc++] = pbuf;

        /* Move to the next string */
        pbuf += strlen(pbuf) + 1;
    }

    if (cmd_argc <= 0)
    {
        CLI_PRINTF(cli, "base64: No command received.\n");
        return false;
    }

    return client_exec_argv(cli, cmd_argc, cmd_argv);
}

/**
 * "loglevel" command
 */
CLIENT_HELP(loglevel) =
{
    "[LEVEL] ; Show or Set the current log level",

    "When invoked without arguments it shows the current logging levels.\n"
    "\n"
    "With arguments, it sets the current logging levels. The LEVEL parameter is in the \n"
    "\"[MODULE:]SEVERITY,...\" format. If the module is omitted, the default log level is\n"
    "set.\n",
};

CLIENT_CMD(loglevel)
{
    int ii;


    if (false == log_isenabled())
    {
        CLI_PRINTF(cli, "Logger not enabled yet.\n");
        return true;
    }

    if (argc < 2)
    {
        CLI_PRINTF(cli, "\nDefault log level is: %s\n",
                        log_severity_str(log_severity_get()));

        CLI_PRINTF(cli, "Log levels:");
        for (ii = 0; ii < LOG_SEVERITY_LAST; ii++)
        {
            CLI_PRINTF(cli, " %s", log_severity_str(ii));
        }

        CLI_PRINTF(cli, "\n");
        CLI_PRINTF(cli, "Module log levels:\n");
        for (ii = 0; ii <LOG_MODULE_ID_LAST; ii++)
        {
            CLI_PRINTF(cli, "\t%8s - %s\n",
                            log_module_str(ii),
                            log_severity_str(log_module_severity_get(ii)));
        }

        CLI_PRINTF(cli, "\n");
    }
    else
    {
        if (!log_severity_parse(argv[1]))
        {
            CLI_PRINTF(cli, "Error: Invalid loglevel string '%s'.\n", argv[1]);
        }
    }
    return true;
}

