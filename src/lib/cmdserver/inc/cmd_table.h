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

#ifndef CMD_TABLE_H_INCLUDED
#define CMD_TABLE_H_INCLUDED

#include <stdlib.h>
#include "cmd_server.h"

/**
 *
 * There are 3 steps required to implement a new command:
 *
 *  1) Add it to the table below:
 *      CMD(new_command)
 *
 *  2) Define the help text:
 *      CLIENT_HELP(new_command) =
 *      {
 *              "Short help, as shown when running help without arguments",
 *              "Long help, as show when running help new_command"
 *      }
 *
 *  3) Define the command handler:
 *      CLIENT_CMD(new_command)
 *      {
 *          CLI_PRINTF(cli, "Hello world, my name is %s. %d arguments passed.\n", argv[0], argc);
 *          return true;
 *      }
 *
 *  A CLIENT_CMD() function will automatically define 3 parameters:
 *      cli     - Abstract ACLA_CLI context, used for CLI_PRINTF(cli, fmt, ...)
 *      argc    - Number of arguments passed to the function from the command line
 *      argv    - Arguments array passed from the command line, where argv[0] is the
 *                command itself.
 */

#define CMD_COMMON_TABLE        \
    CMD(help)                   \
    CMD(version)                \
    CMD(loglevel)               \
    CMD(base64)


#define CMD(func)                                           \
extern client_cmd_func_t  client_cmd_##func;                \
extern struct client_help client_help_##func;

CMD_COMMON_TABLE
CLIENT_CMD_TABLE

#undef CMD

/*
 * Generate global table definitions
 */
#define CMD(func)                           \
{                                           \
    .acc_name   = #func,                    \
    .acc_func   = client_cmd_##func,        \
    .acc_help   = &client_help_##func,      \
},

struct client_cmd __client_cmd_table[] =
{
    /* Common help function, always present */
    CMD_COMMON_TABLE

    CLIENT_CMD_TABLE

    {
        .acc_name = NULL,
        .acc_func = NULL,
        .acc_help = NULL
    }
};
#undef CMD

#endif /* CMD_TABLE_H_INCLUDED */
