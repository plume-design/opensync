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

// stdlib
#include <errno.h>
#include <string.h>

// unix message queue API
#include <sys/stat.h>
#include <mqueue.h>
#include <fcntl.h> // oflag O_* constants

// Plume libs
#include <ds.h>
#include <log.h>

#include "os_mq_link.h"
#include "memutil.h"

// link read/write attributes
typedef struct
{
    unsigned read  :1;  //< can read (receive)
    unsigned write :1;  //< can write (send)
} link_attr_t;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct mq_link
{
    struct ev_loop *evloop;
    ev_io que_watcher;
    link_attr_t link_attr; // link attr
    char *file_path; // message queue file
    mqd_t mque; // handle to message queue
    size_t max_msg_size;
    mq_msg_received_func_t *on_msg_rcvd_fp;
    void *subscr_ptr;
};

/* Read msg queue until it is empty to make sure we do not overlook any 
 * any message and empty the queue asap to make space for new messages */
static void service_queue_not_empty(mq_link_t *self)
{
    char msgbuf[self->max_msg_size];
    ssize_t rv;
    mq_priority_t mpri;
    while (rv = mq_receive(self->mque, msgbuf, sizeof(msgbuf), &mpri), rv >= 0)
    {
        if (self->on_msg_rcvd_fp != NULL)
        {
            self->on_msg_rcvd_fp(self, self->subscr_ptr, (uint8_t *)msgbuf, (size_t)rv, mpri);
        }
    }
}

static link_attr_t oflag_to_attr(int oflag)
{
    link_attr_t attr = { 0 };
    if (oflag & O_RDWR) { attr.read = 1; attr.write = 1; }
    else if (oflag & O_WRONLY) attr.write = 1;
    else attr.read = 1;
    return attr;
}

static void recv_handler(struct ev_loop *loop, ev_io *w, int revents)
{
    (void)loop;
    (void)revents;
    mq_link_t *self = (mq_link_t *)w->data;

    // read data from the queue
    service_queue_not_empty(self);
}

static void init_notify_receive(mq_link_t *self)
{
    ev_io_init(&self->que_watcher, &recv_handler, self->mque, EV_READ);
    self->que_watcher.data = self;
    ev_io_start(self->evloop, &self->que_watcher);
}

mq_link_t *mq_link_open(const char *addr, struct ev_loop *evloop, 
        enum mq_link_mode link_mode, size_t max_msize, size_t max_mcount)
{
    int oflag = 0;
    if (link_mode == MQ_MODE_SEND_RECV)
    {
        oflag = O_CREAT | O_RDWR;
    }
    else
    {
        if (link_mode & MQ_MODE_SEND) oflag |= O_CREAT | O_WRONLY;
        if (link_mode & MQ_MODE_RECV) oflag |= O_CREAT | O_RDONLY;
    }
    
    if (!addr)
    {
        LOG(ERR, "%s failed : addr is NULL", __FUNCTION__);
        return NULL;
    }

    // apply R/W attr for created mqueue file
    mode_t mode = (S_IRUSR | S_IWUSR);

    link_attr_t link_attr = oflag_to_attr(oflag);
    // check if requested read via event loop
    bool recv_notify = (link_attr.read && evloop != NULL);
    // use non-blocking mode for sending OR event loop reception
    if (recv_notify || link_attr.write)
    {
        oflag |= O_NONBLOCK;
    }

    struct mq_attr attr = { 0 };
    attr.mq_msgsize = max_msize > 0 ? max_msize : 8192 /*default*/;
    attr.mq_maxmsg = max_mcount > 0 ? max_mcount : 10 /*default*/;

    /* When opening already existing queue, message size limits are ignored. Only first
     * mq_open() call which creates the queue applies the limits for all other queue users */
    mqd_t mqh = mq_open(addr, oflag, mode, &attr);
    if (mqh < 0)
    {
        LOG(ERR, "%s failed on mq_open(%s, 0x%x, 0x%x, attr.mq_maxmsg=%d, attr.mq_msgsize=%d), err=%s",
            __FUNCTION__, addr, oflag, mode, (int)attr.mq_maxmsg, (int)attr.mq_msgsize, strerror(errno));
        return NULL;
    }

    /* Get actual limits if already set by queue creator */
    if (0 != mq_getattr(mqh, &attr))
    {
        mq_close(mqh);
        LOG(ERR, "%s failed : mq_getattr() failed, correct max msg size unknown.", __FUNCTION__);
        return NULL;
    }

    mq_link_t *self = (mq_link_t *)MALLOC(sizeof(*self));

    memset(self, 0, sizeof(*self));
    self->file_path = strdup(addr);
    self->mque = mqh;
    self->max_msg_size = attr.mq_msgsize;
    self->link_attr = link_attr;

    self->evloop = evloop;
    if (recv_notify)
    {
        init_notify_receive(self);
    }

    return self;
}

void mq_link_close(mq_link_t *self, bool unlink)
{
    if (self == NULL || self->mque < 0) return;

    if (self->evloop && self->link_attr.read)
    {
        ev_io_stop(self->evloop, &self->que_watcher);
    }

    mq_close(self->mque);
    self->mque = -1; // fuse against multiple close() call
    
    if (unlink)
    {
        (void)mq_unlink(self->file_path);
    }
    FREE(self->file_path);
    FREE(self);
}

bool mq_link_subscribe_receive(mq_link_t *self, void *subscr, mq_msg_received_func_t *pfn)
{
    if (self->evloop && self->link_attr.read)
    {
        self->subscr_ptr = subscr;
        self->on_msg_rcvd_fp = pfn;
        return true;
    }
    return false;
}

bool mq_link_sendto(mq_link_t *self, const uint8_t *msg, size_t mlen, mq_priority_t mpri)
{
    if (msg == NULL || mpri > MQ_PRI_MAX)
    {
        LOG(ERR, "%s failed : invalid input argument(s)", __FUNCTION__);
        return false;
    }

    if (!self->link_attr.write)
    {
        LOG(WARN, "%s failed : no write capability", __FUNCTION__);
        return false;
    }

    int rv = mq_send(self->mque, (const char *)msg, mlen, mpri);
    if (rv != 0)
    {
        LOG(WARN, "%s failed : mq_send() failed : %s", __FUNCTION__, strerror(errno));
        return false;
    }
    return true;
}

bool mq_link_receive(mq_link_t *self, uint8_t *msg, size_t *pmlen, mq_priority_t *p_mpri)
{
    if (!msg || *pmlen < self->max_msg_size)
    {
        LOG(ERR, "%s failed : invalid argument", __FUNCTION__);
        return false;
    }

    if (self->evloop != NULL)
    {
        LOG(WARN, "%s failed : bad config", __FUNCTION__);
        return false;
    }

    if (!self->link_attr.read)
    {
        LOG(WARN, "%s failed : no read capability", __FUNCTION__);
        return false;
    }

    struct mq_attr attr_old; // copy of old settings
    struct mq_attr attr_new = { 0 }; // <= force blocking mode

    if (mq_setattr(self->mque, &attr_new, &attr_old) < 0)
    {
        LOG(WARN, "%s failed : mq_setattr() failed to set blocking mode: %s", __FUNCTION__, strerror(errno));
        return false;
    }

    // receive in blocking mode
    ssize_t rv = mq_receive(self->mque, (char *)msg, *pmlen, p_mpri);

    // restore non-blocking mode if any
    if (attr_old.mq_flags & O_NONBLOCK)
    {
        attr_new.mq_flags = O_NONBLOCK;
        (void)mq_setattr(self->mque, &attr_new, NULL);
    }

    if (rv < 0)
    {
        LOG(WARN, "%s failed : mq_receive() failed : %s", __FUNCTION__, strerror(errno));
        return false;
    }

    *pmlen = (size_t)rv;
    return true;
}

bool mq_link_can_send(const mq_link_t *self)
{
    return 0 != self->link_attr.write;
}

bool mq_link_can_receive(const mq_link_t *self)
{
    return 0 != self->link_attr.read;
}

const char *mq_link_name(const mq_link_t *self)
{
    return self->file_path;
}

size_t mq_link_max_msize(const mq_link_t *self)
{
    return self->max_msg_size;
}
