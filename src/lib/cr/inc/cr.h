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

#ifndef CR_RT_H_INCLUDED
#define CR_RT_H_INCLUDED

#include <stdbool.h>

struct cr_rt;
struct cr_context;
struct cr_task;
struct cr_poll;

typedef struct cr_rt cr_rt_t;
typedef struct cr_context cr_context_t;
typedef struct cr_task cr_task_t;
typedef struct cr_poll cr_poll_t;

typedef bool cr_run_fn_t(void *priv);
typedef void cr_drop_fn_t(void *priv);
typedef void cr_done_fn_t(void *priv);

cr_rt_t *cr_rt(void);

cr_rt_t *cr_rt_global(void);

void cr_rt_run(cr_rt_t *rt);

void cr_rt_drop(cr_rt_t **rt);

cr_context_t *cr_context(cr_rt_t *rt);

void cr_context_wakeup(cr_context_t *c);

void cr_context_drop(cr_context_t *c);

cr_task_t *cr_task(void *priv, cr_run_fn_t *run_fn, cr_drop_fn_t *drop_fn);

void cr_task_set_done_fn(cr_task_t *t, void *priv, cr_done_fn_t *done_fn);

void *cr_task_priv(cr_task_t *t);

void cr_task_start(cr_task_t *t, cr_context_t *c);

void cr_task_drop(cr_task_t **t);

cr_poll_t *cr_poll_read(cr_context_t *c, int fd);

cr_poll_t *cr_poll_write(cr_context_t *c, int fd);

bool cr_poll_run(cr_poll_t *poll);

void cr_poll_drop(cr_poll_t **poll);

#endif /* CR_RT_H_INCLUDED */
