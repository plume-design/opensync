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

#include <cr.h>
#include <cr_goto.h>
#include <ev.h>
#include <log.h>
#include <memutil.h>
#include <stdbool.h>

struct cr_rt
{
    struct ev_loop *l;
};

struct cr_context
{
    struct cr_rt *rt;
    struct cr_task *t;
};

struct cr_poll
{
    bool done;
    bool skipped;
    struct cr_context *c;
    cr_state_t state;
    ev_io io;
};

struct cr_task
{
    bool done;
    struct cr_context *c;
    cr_run_fn_t *run_fn;
    cr_drop_fn_t *drop_fn;
    cr_done_fn_t *done_fn;
    ev_async a;
    void *priv_work;
    void *priv_done;
};

static cr_rt_t *cr_rt_loop(struct ev_loop *l)
{
    struct cr_rt *rt = CALLOC(1, sizeof(*rt));
    rt->l = l;
    return rt;
}

cr_rt_t *cr_rt_global(void)
{
    static cr_rt_t *rt;
    if (rt == NULL)
    {
        rt = cr_rt_loop(EV_DEFAULT);
    }
    return rt;
}

cr_rt_t *cr_rt(void)
{
    return cr_rt_loop(ev_loop_new(EVFLAG_AUTO));
}

void cr_rt_run(cr_rt_t *rt)
{
    ev_run(rt->l, 0);
}

static void cr_rt_drop_ptr(cr_rt_t *rt)
{
    if (rt == NULL) return;
    if (rt->l == EV_DEFAULT) return;
    if (rt->l != NULL) ev_loop_destroy(rt->l);
    rt->l = NULL;
    FREE(rt);
}

void cr_rt_drop(cr_rt_t **rt)
{
    if (rt == NULL) return;
    cr_rt_drop_ptr(*rt);
    *rt = NULL;
}

cr_context_t *cr_context(cr_rt_t *rt)
{
    cr_context_t *c = CALLOC(1, sizeof(*c));
    c->rt = rt;
    return c;
}

void cr_context_wakeup(cr_context_t *c)
{
    if (c == NULL) return;
    if (c->t == NULL) return;
    if (c->rt == NULL) return;
    ev_async_send(c->rt->l, &c->t->a);
}

void cr_context_drop(cr_context_t *c)
{
    if (c == NULL) return;
    c->t = NULL;
    FREE(c);
}

bool cr_task_run(cr_task_t *t)
{
    if (t == NULL) return true;
    if (t->done) return true;
    if (t->run_fn(t->priv_work) == false) return false;
    t->done = true;
    if (t->c != NULL) ev_async_stop(t->c->rt->l, &t->a);
    if (t->done_fn != NULL) t->done_fn(t->priv_done);
    return true;
}

static void cr_task_cb(struct ev_loop *l, ev_async *a, int events)
{
    struct cr_task *t = a->data;
    cr_task_run(t);
}

void *cr_task_priv(cr_task_t *t)
{
    if (t == NULL) return NULL;
    return t->priv_work;
}

cr_task_t *cr_task(void *priv, cr_run_fn_t *run_fn, cr_drop_fn_t *drop_fn)
{
    if (run_fn == NULL) return NULL;
    cr_task_t *t = CALLOC(1, sizeof(*t));
    ev_async_init(&t->a, cr_task_cb);
    t->a.data = t;
    t->run_fn = run_fn;
    t->drop_fn = drop_fn;
    t->priv_work = priv;
    return t;
}

void cr_task_set_done_fn(cr_task_t *t, void *priv, cr_done_fn_t *done_fn)
{
    if (t == NULL) return;
    if (WARN_ON(t->done_fn != NULL)) return;
    t->done_fn = done_fn;
    t->priv_done = priv;
}

void cr_task_start(cr_task_t *t, cr_context_t *c)
{
    if (t == NULL) return;
    if (c == NULL) return;
    if (c->rt == NULL) return;
    if (WARN_ON(t->c != NULL)) return;
    if (WARN_ON(c->t != NULL)) return;
    t->c = c;
    c->t = t;
    ev_async_start(c->rt->l, &t->a);
    cr_context_wakeup(t->c);
}

static void cr_task_stop(cr_task_t *t)
{
    if (t == NULL) return;
    if (t->c == NULL) return;
    ev_async_stop(t->c->rt->l, &t->a);
    cr_context_drop(t->c);
    t->c = NULL;
}

static void cr_task_drop_ptr(cr_task_t *t)
{
    if (t == NULL) return;
    cr_task_stop(t);
    if (t->drop_fn != NULL)
    {
        t->drop_fn(t->priv_work);
        t->drop_fn = NULL;
    }
    FREE(t);
}

void cr_task_drop(cr_task_t **t)
{
    if (t == NULL) return;
    cr_task_drop_ptr(*t);
    *t = NULL;
}

static void cr_poll_cb(struct ev_loop *l, ev_io *io, int events)
{
    struct cr_poll *poll = io->data;
    ev_io_stop(l, io);
    cr_context_wakeup(poll->c);
}

static struct cr_poll *cr_poll(cr_context_t *c, int fd, int events)
{
    struct cr_poll *poll = CALLOC(1, sizeof(*poll));
    cr_state_init(&poll->state);
    poll->c = c;
    ev_io_init(&poll->io, cr_poll_cb, fd, events);
    poll->io.data = poll;
    return poll;
}

cr_poll_t *cr_poll_read(cr_context_t *c, int fd)
{
    return cr_poll(c, fd, EV_READ);
}

cr_poll_t *cr_poll_write(cr_context_t *c, int fd)
{
    return cr_poll(c, fd, EV_WRITE);
}

bool cr_poll_run(struct cr_poll *poll)
{
    if (poll == NULL) return true;
    CR_BEGIN(&poll->state);
    if (poll->c != NULL)
    {
        ev_io_start(poll->c->rt->l, &poll->io);
        while (ev_is_active(&poll->io))
        {
            CR_YIELD(&poll->state);
        }
    }
    else
    {
        /* This allows busy waiting when running
         * without a context while allowng
         * parallel coroutines to get a chance to
         * run.
         */
        CR_YIELD(&poll->state);
    }
    CR_END(&poll->state);
}

static void cr_poll_drop_ptr(cr_poll_t *poll)
{
    if (poll == NULL) return;
    if (poll->c != NULL) ev_io_stop(poll->c->rt->l, &poll->io);
    FREE(poll);
}

void cr_poll_drop(cr_poll_t **poll)
{
    if (poll == NULL) return;
    cr_poll_drop_ptr(*poll);
    *poll = NULL;
}
