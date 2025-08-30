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

/* Factory of ipc_msg_link_t objects on basis of UNIX datagram sockets */
#include <stdio.h>
#include <unistd.h> // getpid()

#include <sys/types.h>
#include <dirent.h> // opendir()

#include "timevt_msg_link.h"

#include "udsock_link.h"
#include "mque_link.h"

// message queue /proc interface directory
#define MQ_PROC_DIR "/proc/sys/fs/mqueue"

// Default server socket addr in abstract namespace
#ifndef TESRV_SOCKET_ADDR
#define TESRV_SOCKET_ADDR "@timevt-server"
#endif

// Default client socket addr in abstract namespace
#ifndef TECLI_SOCKET_ADDR
#define TECLI_SOCKET_ADDR "@timevt-client"
#endif

/* Implementation of the factory of ipc_msg_link_t objects for unix datagram sockets
 * Objects are specialized for use in time-event logging application */

static ipc_msg_link_t *te_uds_link_open(const char *addr, void *data, int link_id)
{
    switch(link_id)
    {
    case TELOG_SERVER_MSG_LINK_ID:
        return udsock_link_open(addr ? addr : TESRV_SOCKET_ADDR, 
            NULL /*server doesn't send anything*/,
            (struct ev_loop *)data,
            TELOG_MAX_MSG_SIZE,
            0 /*disable TX bufferring : TX not used by the server */);

    case TELOG_CLIENT_MSG_LINK_ID:
    {
        char cli_addr[sizeof(TECLI_SOCKET_ADDR) + sizeof("9999999")];
        if (!addr)
        {// create addr by using default client addr with leading PID after the dot
            (void)snprintf(cli_addr, sizeof(cli_addr), "%s.%d", TECLI_SOCKET_ADDR, getpid());
            addr = cli_addr;
        }
        return udsock_link_open(addr, TESRV_SOCKET_ADDR,
            (struct ev_loop *)data,
            TELOG_MAX_MSG_SIZE,
            TELOG_MAX_BUF_MSGS /*enable TX bufferring for clients*/);
    }

    default:
        return NULL;
    }
}

// default file for time-event message queue, usually located in /dev/mqueue/
#ifndef TE_MQ_LINK_FILE
#define TE_MQ_LINK_FILE "/telog.mq"
#endif

/* Implementation of the factory of ipc_msg_link_t objects for unix message queue
 * Objects are specialized for use in time-event logging application */

static ipc_msg_link_t *te_mq_link_open(const char *addr, void *data, int link_id)
{
    switch(link_id)
    {
    case TELOG_CLIENT_MSG_LINK_ID:
        return mque_link_open(addr ? addr : TE_MQ_LINK_FILE, NULL,
            MQ_MODE_SEND,
            TELOG_MAX_MSG_SIZE,
            TELOG_MAX_BUF_MSGS);

    case TELOG_SERVER_MSG_LINK_ID:
        return mque_link_open(addr ? addr : TE_MQ_LINK_FILE, data ? (struct ev_loop *)data : EV_DEFAULT,
            MQ_MODE_RECV, 0, 0);

    default:
        return NULL;
    }
}

static bool kernel_mqueue_supported()
{
    DIR *dir;
    dir = opendir(MQ_PROC_DIR);
    if (dir == NULL) return false;
    closedir(dir);
    return true;
}

ipc_msg_link_t *ipc_msg_link_open(const char *addr, void *data, int link_id)
{
    if (kernel_mqueue_supported())
    {
        return te_mq_link_open(addr, data, link_id);
    }
    else
    {
        return te_uds_link_open(addr, data, link_id);
    }
}
