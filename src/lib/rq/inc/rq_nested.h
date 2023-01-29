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

#ifndef RQ_NESTED_H_INCLUDED
#define RQ_NESTED_H_INCLUDED

#include <rq.h>

/* This is intended to be used to group tasks:
 *
 *   some_q.empty_cb = all_groups_done_cb;
 *   group1.completed_fn = group1_done_cb;
 *   group2.completed_fn = group2_done_cb;
 *   rq_add_task(&group1.q, task1);
 *   rq_add_task(&group1.q, task2);
 *   rq_add_task(&group1.q, task3);
 *   rq_add_task(&group2.q, task4);
 *   rq_add_task(&group2.q, task5);
 *   rq_add_task(&group2.q, task6);
 *   rq_add_task(&some_q, &group1.task);
 *   rq_add_task(&some_q, &group2.task);
 *   rq_resume(&some_q); // if it was stopped
 */
struct rq_nested {
    struct rq q;
    struct rq_task task;
};

void
rq_nested_init(struct rq_nested *n,
               struct ev_loop *loop);

void
rq_nested_add_task(struct rq *q,
                   struct rq_nested *n);

#endif /* RQ_NESTED_H_INCLUDED */
