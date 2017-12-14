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
#include <ev.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <strings.h>

#include "log.h"
#include "ds.h"
#include "util.h"
#include "os.h"
#include "os_socket.h"

#include "cmd_server.h"


/* definitions */
#define MODULE_ID LOG_MODULE_ID_CMD

#define BUFFER_SIZE 32768

struct client_cmd *client_cmd_table = NULL;

/* type definitions */
/* TCP Client context, extends the client context with a socket fd */
struct client_tcp_ctx
{
    int             tc_sockfd;          /**< Socket file descriptor  */
    client_t        tc_cli;             /**< Client context         */
};

/* connection accept callback */
void cmd_cb_accept(struct ev_loop *loop, struct ev_io *watcher, int revents);

/* socket read callback */
void cmd_cb_read(struct ev_loop *loop, struct ev_io *watcher, int revents);


/* global to avoid any potential issues with stack */
struct ev_io waccept;

/*
 * Socket printf function used by CLI implementation
 */
static int peer_tcp_printf(client_t *arg, char *fmt, ...)
{
    va_list varg;
    int     retval;

    struct client_tcp_ctx *ctx = CONTAINER_OF(arg, struct client_tcp_ctx, tc_cli);

    va_start(varg, fmt);
    retval = vdprintf(ctx->tc_sockfd, fmt, varg);
    va_end(varg);

    return retval;
}

static void close_peer(struct ev_loop *loop, struct ev_io *watcher)
{
    ev_io_stop(loop,watcher);
    close(watcher->fd);
    os_free(watcher);
    LOG(DEBUG, "CMD server: closing connection");
}

/* callback declaration  */
void cmd_cb_accept(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    struct sockaddr_in peer_addr;
    int sock_peer;
    socklen_t peer_len = sizeof(peer_addr);
    struct ev_io *w_client = NULL;

    w_client = (struct ev_io*) os_malloc (sizeof(struct ev_io));
    if (!w_client)
    {
        LOG(ERR, "w_client allocation error");
        return;
    }

    /* check for error event */
    if(EV_ERROR & revents)
    {
        LOG(ERR, "cmd_cb_accept got invalid event.");
        return;
    }

    // Accept client request
    sock_peer = accept(watcher->fd, (struct sockaddr *)&peer_addr, &peer_len);

    if (sock_peer < 0)
    {
        if (errno != EWOULDBLOCK)
        {
            LOG(ERR, "Error accepting socket::error=%s\n", strerror(errno));
            return;
        }
    }

    if (-1 == fcntl(sock_peer, F_SETFL, (fcntl(sock_peer, F_GETFL)) | O_NONBLOCK))
    {
        LOG(ERR, "Error setting the non-blocking-flag::error=%s\n", strerror(errno));
        return; 
    }

    if (-1 == fcntl(sock_peer, F_SETFD, fcntl(sock_peer, F_GETFD) | FD_CLOEXEC))
    {
        LOG(ERR, "Error setting the close-on-exec flag::error=%s\n", strerror(errno));
        return;
    }
#if defined(SO_NOSIGPIPE)
    {
        int32_t set =1;
        setsockopt(sock_peer, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
    }
#endif

    LOG(DEBUG, "CMD peer connected...\n");

    // Initialize and start watcher to read client requests
    ev_io_init(w_client, cmd_cb_read, sock_peer, EV_READ);
    ev_io_start(loop, w_client);
}


void cmd_cb_read(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    char buffer[BUFFER_SIZE];
    ssize_t read;
    char *scmd, *ecmd;

    if(EV_ERROR & revents)
    {
        LOG(ERR,"cmd_cb_read: got invalid event");
        return;
    }

    /* clean up buffer */
    memset(buffer, 0, BUFFER_SIZE);

    // Receive message from client socket
    read = recv(watcher->fd, buffer, BUFFER_SIZE, 0);

    if(read < 0)
    {
        LOG(WARNING, "cmd_cb_read: read error");
        return;
    }

    if(read == 0)
    {
        // peer closed, stop watching, close socket
        close_peer(loop, watcher);
        return;
    }

    /* Split the received buffer into lines */
    for (scmd = ecmd = buffer; ecmd < buffer + read; ecmd++)
    {
        /* CTRL-D was pressed */
        if (*ecmd == '\x04')
        {
            // peer closed, stop watching, close socket
            close_peer(loop, watcher);
            return;
        }

        /*
         * buffer is an arbitrary buffer; there's no grantee it contains 
         * \n or even \0 characters.
         * Make sure to handle either case.
         */
        if (strchr("\n\r", *ecmd) != NULL)
        {
            *ecmd = '\0';

            /* handle the "exit" command here. It is not part of the standard
             * set of commands 
             */
            if (strncmp(scmd, "exit", strlen("exit")) == 0)
            {
                // peer wants to close, stop watching, close socket
                close_peer(loop, watcher);
                return;
            }

            /* Skip empty strings */
            if (scmd != ecmd)
            {
                struct client_tcp_ctx tc;

                tc.tc_sockfd            = watcher->fd; 
                tc.tc_cli.ac_printf_cbk = peer_tcp_printf;

                client_exec_cmd(&tc.tc_cli, scmd);
            }

            /* Move scmd past the current end of the string */
            scmd = ecmd + 1;
        }
    }

    //clean buffer
    bzero(buffer, read);
}

/**
 * Execute an CLIENT command that is defined with the string @p cmd. Arguments
 * are parsed in a shell-like manner, which means that it understands quotes 
 * and double quotes as well as back-slash sequences.
 * NOTE JSON requires '\"' as part string. Therefore '\"' chars are currently 
 * treated as part of the token - to be discussed
 */
bool client_exec_cmd(client_t *cli, char *cmd)
{
    int argc;
    char *parg = cmd;
    char *argv[CLIENT_ARGV_MAX];

    for (argc = 0; argc < CLIENT_ARGV_MAX; argc++)
    {
        argv[argc] = strargv(&parg, false);

        if (argv[argc] == NULL) break;
    }

    return client_exec_argv(cli, argc, argv);
}

/**
 * Execute an CLIENT command. Arguments are passed as an array of characters, 
 * just like for the main function.
 */
bool client_exec_argv(client_t *cli, int argc, char *argv[])
{
    struct client_cmd *cmd = client_cmd_table;

    while (cmd->acc_name != NULL)
    {
        if (strcmp(argv[0], cmd->acc_name) == 0)
        {
            return cmd->acc_func(cli, argc, argv);
        }

        cmd++;
    }

    CLI_PRINTF(cli, "\nCommand '%s' not found.\n\n", argv[0]);

    return false;
}


bool init_cmdserver_2(struct client_cmd *cmd_table, struct ev_loop * loop, int port)
{
    bool success = false;
    int32_t      sock_fd;

    client_cmd_table = cmd_table;

    /* open socket for listen */
    sock_fd = server_socket_create(OS_SOCK_TYPE_TCP, 
                                   SOCKET_ADDR_LOCALHOST, 
                                   port);

    LOG(NOTICE, "CLI server listening.::address=%s|port=%d", 
                SOCKET_ADDR_LOCALHOST, 
                port);

    success = tcp_server_listen(sock_fd);

    ev_io_init(&waccept, cmd_cb_accept, sock_fd, EV_READ);
    ev_io_start(loop, &waccept);

    return success;
}

