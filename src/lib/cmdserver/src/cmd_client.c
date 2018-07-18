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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ev.h"

#include "util.h"
#include "os_socket.h"

static struct ev_io client_watcher;
static void cmdclient_recv(struct ev_loop *loop, struct ev_io *w, int revents);
static char *cmdclient_get_stdin(void);

/**
 * Return true if the command line looks like it's a CLI invokation
 *
 * Check if the command was executed in the form of: BIN --cli or BIN cli arg1 arg2 arg3
 */
bool cmdclient_argv_check(int argc, char *argv[])
{
    if (argc < 2)
    {
        return false;
    }

    if (strcmp(argv[1], "cli") != 0 &&
        strcmp(argv[1], "--cli") != 0)
    {
        return false;
    }

    return true;
}

bool cmdclient(int port, int argc, char *argv[])
{
    char b64_buffer[32768];
    char arg_list[32768 + 20];
    char *arg_list_p;
    size_t arg_list_sz;
    int ii;

    int sock_fd = -1;
    bool retval = false;

    /* open socket for listen   */
    sock_fd = client_socket_create(OS_SOCK_TYPE_TCP);
    if (sock_fd < 0)
    {
        printf("ERROR: Unable to create client TCP socket.\n");
        goto error;
    }

    if (!client_connect(sock_fd, SOCKET_ADDR_LOCALHOST, port))
    {
        printf("ERRROR: Unable to conenct to %s:%d.\n", SOCKET_ADDR_LOCALHOST, port);
        goto error;
    }

    /*
     * Build the command list - convert argv to "arg1\0arg2\0arg3\0..."
     */
    arg_list_p = arg_list;
    arg_list_sz = sizeof(arg_list);

    arg_list[0] = '\0';
    for (ii = 2; ii < argc; ii++)
    {
        /* Special keyword -- read the next argument from stdin */
        if (strcmp(argv[ii], "@-") == 0)
        {
            csnprintf(&arg_list_p, &arg_list_sz, "%s%c", cmdclient_get_stdin(), '\0');
        }
        else
        {
            csnprintf(&arg_list_p, &arg_list_sz, "%s%c", argv[ii], '\0');
        }
    }

    /* Convert to base64 */
    if (base64_encode(b64_buffer, sizeof(b64_buffer), arg_list, arg_list_p - arg_list) < 0)
    {
        printf("ERROR: Buffer too small.\n");
        goto error;
    }

    /* Now build the base64 command arguments again in arg_list */
    arg_list[0] = '\0';
    snprintf(arg_list, sizeof(arg_list), "base64 %s\nexit\n", b64_buffer);

    if (write(sock_fd, arg_list, strlen(arg_list)) < (ssize_t)strlen(arg_list))
    {
        printf("ERROR: Short write.\n");
        goto error;
    }

    /* Process the response */
    ev_io_init(&client_watcher, cmdclient_recv, sock_fd, EV_READ);
    client_watcher.data = (void *)(intptr_t)sock_fd;

    ev_io_start(EV_DEFAULT, &client_watcher);
    ev_run(EV_DEFAULT, 0);

    retval = true;

error:
    if (sock_fd >= 0) close(sock_fd);

    ev_io_stop(EV_DEFAULT, &client_watcher);

    return retval;
}

/**
 * libev receive callback for cmdclient sockets
 */
void cmdclient_recv(struct ev_loop *loop, struct ev_io *w, int revents)
{
    int sock_fd;
    char buf[1024];
    ssize_t nread;

    (void)loop;
    (void)revents;

    sock_fd = (intptr_t)w->data;

    nread = read(sock_fd, buf, sizeof(buf));
    if (nread <= 0)
    {
        /* EOF or ERROR, in any case stop the watcher */
        ev_io_stop(EV_DEFAULT, &client_watcher);
    }

    /* Dump to stdout */
   if ( write(1, buf, nread) < nread)
   {
       printf("ERROR: Short write\n");
   }
}

char *cmdclient_get_stdin(void)
{
    bool have_stdin = false;
    static char buf[65536];
    ssize_t nread;
    ssize_t total_read;

    if (have_stdin)
    {
        return buf;
    }

    have_stdin = true;

    total_read = 0;
    while (total_read < (ssize_t)sizeof(buf) - 1)
    {
        nread = read(0, buf + total_read, sizeof(buf) - 1 - total_read);
        if (nread <= 0)
        {
            break;
        }

        total_read += nread;
    }

    buf[total_read] = '\0';

    return buf;
}

