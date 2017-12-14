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

#ifndef CMD_SERVER_H_INCLUDED
#define CMD_SERVER_H_INCLUDED

#include <stdbool.h>
#include <ev.h>

#define CMD_SERVER_PORT_BASE    8989

#define CMD_SERVER_PORT_DM      (CMD_SERVER_PORT_BASE + 0)
#define CMD_SERVER_PORT_NM      (CMD_SERVER_PORT_BASE + 1)

/** Function prototype that is used for printing by the client commands */
struct client;

typedef struct client client_t;

typedef int     client_printf_t(client_t *cli, char *fmt, ...);

typedef bool    client_cmd_func_t(client_t *cli, int argc, char *argv[]);

struct client_cmd
{
    char                    *acc_name;
    client_cmd_func_t       *acc_func;
    struct client_help      *acc_help;
};

extern struct client_cmd *client_cmd_table;
extern struct client_cmd __client_cmd_table[];

/*
 * Client Commands
 */

/** Maximum number of arguments that can be passed to client_exec_argv() */
#define CLIENT_ARGV_MAX    64
/** Print function used in CMDs */
#define CLI_PRINTF(x, ...)      (x)->ac_printf_cbk((x), __VA_ARGS__)

/** Macro that defines a command handler function */
#define CLIENT_CMD(cmd)         bool client_cmd_##cmd(client_t *cli, int argc, char *argv[])
/** Macro that defines a command help structure */
#define CLIENT_HELP(cmd)        struct client_help client_help_##cmd

/**
 * context function, must be passed to all client_exec_*() functions; the
 * actx_printf_cbk member must be initialized to a working printf-like function
 */
struct client
{
    client_printf_t *ac_printf_cbk;
};

struct client_help
{
    char    *ch_short;  /**< Short help     */
    char    *ch_long;   /**< Long help      */
};

extern bool             client_exec_cmd(client_t *cli, char *cmd);
extern bool             client_exec_argv(client_t *cli, int argc, char *argv[]);
extern bool             init_cmdserver_2(struct client_cmd *cmd_table, struct ev_loop * loop, int port);

#define init_cmdserver(loop,port) init_cmdserver_2(__client_cmd_table,loop,port)



#endif /* CMD_SERVER_H_INCLUDED */

