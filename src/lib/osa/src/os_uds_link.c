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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <libgen.h> // dirname
#include <sys/stat.h> // mkdir

#include <sys/socket.h>
#include <unistd.h> // unlink(), close()

#include <log.h>
#include <ds.h>

#include "os_uds_link.h"

// opens unix datagram socket
static int open_unix_socket(const char *path, /*out*/struct sockaddr_un *p_addr)
{
    struct sockaddr_un addr = { 0 };

    if (path == NULL || *path == '\0')
    {
        LOG(ERR, "Socket path is (null) or empty");
        return -1;
    }

    // abstract ns socket name shall start with @
    bool abstract_ns = (path[0] == '@');
    if (abstract_ns) path += 1;

    size_t max_path_len = sizeof(addr.sun_path) - 1;
    if (abstract_ns) max_path_len -= 1;

    size_t path_len = strlen(path);
    if (path_len == 0 || path_len > max_path_len)
    {
        LOG(ERR, "Too short/long socket path: %s", path);
        return -1;
    }

    if (abstract_ns)
    {
        // init abstract socket addr
        addr.sun_path[0] = '\0';
        strcpy(addr.sun_path + 1, path);
        addr.sun_family = AF_UNIX;
        path_len += 1;
    }
    else
    {
        // init regular socket addr
        strcpy(addr.sun_path, path);
        addr.sun_family = AF_UNIX;

        // only for FS paths take care of socket path location
        char *dir = dirname(strdupa(path));
        if (mkdir(dir, 0775) != 0 && errno != EEXIST)
        {
            LOG(ERR, "Cannot make dir for the socket %s : %s", path, strerror(errno));
            return -1;
        }

        // unlink addr pathname if already exists
        if (-1 == unlink(path) && errno != ENOENT)
        {
            LOG(ERR, "Cannot remove old socket file %s : %s", path, strerror(errno));
            return -1;
        }
    }

    int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sfd == -1)
    {
        LOG(ERR, "Cannot open unix datagram socket : %s", strerror(errno));
        return -1;
    }


    if (-1 == bind(sfd, (struct sockaddr *)&addr, offsetof(struct sockaddr_un, sun_path) + path_len))
    {
        close(sfd);
        LOG(ERR, "Cannot bind the socket with addr %s : %s", path, strerror(errno));
        return -1;
    }

    if (p_addr != NULL)
    {
        *p_addr = addr;
    }
    return sfd;
}

static void eh_on_datagram_received(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)loop;
    uds_link_t *self = CONTAINER_OF(w, struct uds_link, socket_watcher);

    if (revents & EV_ERROR)
    {
        LOG(ERR, "Socket watcher reported EV_ERROR");
        // TODO: close the socket, maybe reopen it again
        return;
    }

    if (!(revents & EV_READ)) return;

    uint8_t rxbuf[getpagesize()];
    udgram_t msg;

    do
    {
        socklen_t salen = sizeof(msg.addr);
        ssize_t len = recvfrom(self->socket_fd, rxbuf, sizeof(rxbuf), MSG_DONTWAIT, 
            (struct sockaddr *) &msg.addr, &salen);

        if (len > 0)
        {
            self->cnt_rdg++;
            self->cnt_rb += len;

            if (self->dg_read_fp != NULL)
            {
                msg.data = rxbuf;
                msg.size = (size_t)len;
                self->dg_read_fp(self, &msg);
            }
        }
        else
        {
            if (len < 0)
            {
                int err = errno;
                if(err != EAGAIN && err != EWOULDBLOCK)
                {
                    LOG(ERR, "Socket %s receive error : %s", self->sname, strerror(err));
                }
            }
            break;
        }

    } while (true);
}

bool uds_link_init(uds_link_t *self, const char *path, struct ev_loop *ev)
{
    memset(self, 0, sizeof(*self));
    self->socket_fd = -1;

    int sfd = open_unix_socket(path, &self->socket_addr);
    if (sfd < 0)
    {
        free(self);
        return false;
    }
    
    self->abstract = (self->socket_addr.sun_path[0] == '\0');
    self->sname = &self->socket_addr.sun_path[self->abstract ? 1 : 0];
    self->socket_fd = sfd;
    self->ev_loop = ev;

    if (self->ev_loop)
    {
        ev_io_init(&self->socket_watcher, &eh_on_datagram_received, self->socket_fd, EV_READ);
        ev_io_start(self->ev_loop, &self->socket_watcher);
    }
    return true;
}

void uds_link_fini(uds_link_t *self)
{
    if (self == NULL || self->socket_fd < 0) return;

    ev_io_stop(self->ev_loop, &self->socket_watcher);
    if (0 != close(self->socket_fd))
    {
        LOG(ERR, "Socket %s closed with error : %s", self->sname, strerror(errno));
    }    
    self->socket_fd = -1;

    if (!self->abstract)
    {
        (void)unlink(self->sname);
    }
}

void uds_link_subscribe_datagram_read(uds_link_t *self, dgram_read_fp_t pfn)
{
    self->dg_read_fp = pfn;
}

bool uds_link_sendto(uds_link_t *self, const udgram_t *dg)
{
	socklen_t slen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(&dg->addr.sun_path[1]);
    ssize_t rv = sendto(self->socket_fd, dg->data, dg->size, 0, (struct sockaddr *)&dg->addr, slen);
    
    if (rv < 0)
    {
        LOG(DEBUG, "Socket %s dgram sendto() failed : %s", self->sname, strerror(errno));
        return false;
    }

    self->cnt_tb += (size_t)rv;

    if ((size_t)rv != dg->size)
    {
        LOG(ERR, "Incomplete dgram sent with %s socket. Size expected %zu, sent %zd", self->sname, dg->size, rv);
        return false;
    }

    self->cnt_tdg++;
    return true;
}

bool uds_link_receive(uds_link_t *self, udgram_t *dg)
{
    if (self->ev_loop)
    {
        LOG(ERR, "Socket %s reception setup with ev_io watcher, blocking mode not supported", self->sname);
        return false;
    }

    dg->addr.sun_family = AF_UNIX;
    socklen_t salen = sizeof(dg->addr);
    ssize_t len = recvfrom(self->socket_fd, dg->data, dg->size, 0, (struct sockaddr *) &dg->addr, &salen);
    if (len < 0)
    {
        LOG(ERR, "Socket %s dgram readfrom() error : %s", self->sname, strerror(errno));
        return false;
    }
    // update size, sender addr already in dg message
    dg->size = (size_t)len;

    self->cnt_rdg++;
    self->cnt_rb += dg->size;
    return true;
}

uint32_t uds_link_received_dgrams(uds_link_t *self)
{
    return self->cnt_rdg;
}

uint32_t uds_link_received_bytes(uds_link_t *self)
{
    return self->cnt_rb;
}

uint32_t uds_link_sent_dgrams(uds_link_t *self)
{
    return self->cnt_tdg;
}

uint32_t uds_link_sent_bytes(uds_link_t *self)
{
    return self->cnt_tb;
}

const struct sockaddr_un *uds_link_get_addr(uds_link_t *self)
{
    return &self->socket_addr;
}

const char *uds_link_socket_name(uds_link_t *self)
{
    return self->sname;
}
