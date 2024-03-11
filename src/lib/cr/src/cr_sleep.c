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

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cr.h>
#include <cr_goto.h>
#include <cr_sleep.h>

#include <memutil.h>

struct cr_sleep
{
    cr_state_t state;
    cr_context_t *c;
    cr_poll_t *poll;
    uint64_t v;
    ssize_t len;
    int fd;
    int msec;
};

static int cr_sleep_timerfd(int msec)
{
    const int flags = TFD_NONBLOCK | TFD_CLOEXEC;
    const int fd = timerfd_create(CLOCK_MONOTONIC, flags);
    const struct itimerspec spec = {
        .it_interval =
                {
                    .tv_sec = 0,
                    .tv_nsec = 0,
                },
        .it_value =
                {
                    .tv_sec = msec / 1000,
                    .tv_nsec = (msec % 1000) * 1000000,
                },
    };
    assert(fd >= 0);
    assert(timerfd_settime(fd, 0, &spec, NULL) == 0);
    return fd;
}

bool cr_sleep_run(cr_sleep_t *s)
{
    CR_BEGIN(&s->state);

    s->fd = cr_sleep_timerfd(s->msec);
    for (;;)
    {
        s->len = read(s->fd, &s->v, sizeof(s->v));
        if (s->len < 0)
        {
            if (errno == EAGAIN)
            {
                cr_poll_drop(&s->poll);
                s->poll = cr_poll_read(s->c, s->fd);
                while (cr_poll_run(s->poll) == false)
                {
                    CR_YIELD(&s->state);
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            assert(s->len == sizeof(s->v));
        }
    }

    CR_END(&s->state);
}

static void cr_sleep_drop_ptr(cr_sleep_t *s)
{
    if (s == NULL) return;
    close(s->fd);
    cr_poll_drop(&s->poll);
    s->poll = NULL;
    s->fd = -1;
    FREE(s);
}

void cr_sleep_drop(cr_sleep_t **s)
{
    if (s == NULL) return;
    cr_sleep_drop_ptr(*s);
    *s = NULL;
}

cr_sleep_t *cr_sleep_msec(cr_context_t *c, int msec)
{
    cr_sleep_t *s = CALLOC(1, sizeof(*s));
    cr_state_init(&s->state);
    s->c = c;
    s->fd = -1;
    s->msec = msec;
    return s;
}
