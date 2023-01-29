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

#include <rq.h>
#include <ev.h>

#include <unity.h>
#include <unit_test_utils.h>
#include <os.h>

static char *test_name = "rq_test";

struct rq_task_cnt {
    struct rq_task task;
    int *counter;
};

static void
rq_inc_cb(struct rq *q, void *priv)
{
    int *x = priv;
    (*x)++;
}

static void
rq_task_cnt_done_cb(struct rq_task *t, void *priv)
{
    int *x = priv;
    (*x)++;
}

static void
rq_task_cnt_inc_cb(struct rq_task *t)
{
    struct rq_task_cnt *task = container_of(t, struct rq_task_cnt, task);
    (*task->counter)++;
}

static void test_max(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    int q_counter = 0;
    struct rq q = {
        .max_running = 1,
        .empty_fn = rq_inc_cb,
        .priv = &q_counter,
    };
    int t1_counter = 0;
    int t2_counter = 0;
    const struct rq_task_ops ops = {
        .run_fn = rq_task_cnt_inc_cb,
    };
    struct rq_task_cnt t1 = {
        .counter = &t1_counter,
        .task = {
            .ops = &ops,
            .completed_fn = rq_task_cnt_done_cb,
            .priv = &t1_counter,
        },
    };
    struct rq_task_cnt t2 = {
        .counter = &t2_counter,
        .task = {
            .ops = &ops,
            .completed_fn = rq_task_cnt_done_cb,
            .priv = &t2_counter,
        },
    };

    rq_init(&q, loop);
    rq_add_task(&q, &t1.task);
    rq_add_task(&q, &t2.task);
    TEST_ASSERT_TRUE(q.empty == false);

    for (;;) {
        ev_run(loop, EVRUN_ONCE);
        TEST_ASSERT_TRUE(q.num_running <= q.max_running);
        if (t1.task.running) rq_task_complete(&t1.task);
        if (t2.task.running) rq_task_complete(&t2.task);
        if (q.empty) break;
    }

    TEST_ASSERT_TRUE(t1.task.queued == false);
    TEST_ASSERT_TRUE(t2.task.queued == false);
    TEST_ASSERT_TRUE(t1.task.cancelled == false);
    TEST_ASSERT_TRUE(t2.task.cancelled == false);
    TEST_ASSERT_TRUE(t1.task.running == false);
    TEST_ASSERT_TRUE(t2.task.running == false);
    TEST_ASSERT_TRUE(q_counter == 1);
    TEST_ASSERT_TRUE(t1_counter == 2);
    TEST_ASSERT_TRUE(t2_counter == 2);
}

static void
test_cancel(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    struct rq q;
    struct rq_task t1;
    struct rq_task t2;

    MEMZERO(q);
    MEMZERO(t1);
    MEMZERO(t2);
    rq_init(&q, loop);
    rq_add_task(&q, &t1);
    rq_add_task(&q, &t2);
    ev_run(loop, 0);
    TEST_ASSERT_TRUE(t1.running == true);
    TEST_ASSERT_TRUE(t2.running == true);
    rq_task_cancel(&t1);
    TEST_ASSERT_TRUE(t1.cancelled == true);
    TEST_ASSERT_TRUE(t1.running == true);
    TEST_ASSERT_TRUE(t2.running == true);
    rq_task_complete(&t1);
    TEST_ASSERT_TRUE(t1.running == false);
    TEST_ASSERT_TRUE(t2.running == true);
    TEST_ASSERT_TRUE(q.empty == false);
    rq_cancel(&q);
    rq_task_complete(&t2);
    TEST_ASSERT_TRUE(t1.running == false);
    TEST_ASSERT_TRUE(t2.running == false);
    ev_run(loop, 0);
    TEST_ASSERT_TRUE(q.empty == true);
}

static void
test_kill(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    struct rq q;
    struct rq_task t1;
    struct rq_task t2;

    MEMZERO(q);
    MEMZERO(t1);
    MEMZERO(t2);

    rq_init(&q, loop);
    rq_add_task(&q, &t1);
    rq_add_task(&q, &t2);
    ev_run(loop, 0);
    TEST_ASSERT_TRUE(t1.running == true);
    TEST_ASSERT_TRUE(t2.running == true);
    rq_task_kill(&t1);
    TEST_ASSERT_TRUE(t1.running == false);
    TEST_ASSERT_TRUE(t2.running == true);
    TEST_ASSERT_TRUE(q.empty == false);
    rq_kill(&q);
    TEST_ASSERT_TRUE(t1.running == false);
    TEST_ASSERT_TRUE(t2.running == false);
    ev_run(loop, 0);
    TEST_ASSERT_TRUE(q.empty == true);
}

static void
test_timeout(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    struct rq q;
    int counter = 0;
    const struct rq_task_ops ops = {
        .run_fn = rq_task_cnt_inc_cb,
        .cancel_fn = rq_task_cnt_inc_cb,
        .kill_fn = rq_task_cnt_inc_cb,
    };
    struct rq_task_cnt t1 = {
        .counter= &counter,
        .task = {
            .ops = &ops,
            .run_timeout_msec = 10,
            .cancel_timeout_msec = 10,
            .completed_fn = rq_task_cnt_done_cb,
            .priv = &counter,
        },
    };

    MEMZERO(q);
    rq_init(&q, loop);
    rq_add_task(&q, &t1.task);
    ev_run(loop, 0);

    TEST_ASSERT_TRUE(t1.task.queued == false);
    TEST_ASSERT_TRUE(t1.task.running == false);
    TEST_ASSERT_TRUE(t1.task.cancelled == true);
    TEST_ASSERT_TRUE(t1.task.killed == true);
    TEST_ASSERT_TRUE(q.empty == true);
    TEST_ASSERT_TRUE(counter == 4); /* run + cancel + kill + complete */
}

static void
test_timeout_nokill(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    struct rq q;
    int counter = 0;
    const struct rq_task_ops ops = {
        .run_fn = rq_task_cnt_inc_cb,
        .cancel_fn = (rq_task_cancel_fn_t *)rq_task_complete, /* abuse cast */
        .kill_fn = rq_task_cnt_inc_cb,
    };
    struct rq_task_cnt t1 = {
        .counter = &counter,
        .task = {
            .ops = &ops,
            .run_timeout_msec = 10,
            .cancel_timeout_msec = 10,
            .completed_fn = rq_task_cnt_done_cb,
            .priv = &counter,
        },
    };

    MEMZERO(q);
    rq_init(&q, loop);
    rq_add_task(&q, &t1.task);
    ev_run(loop, 0);

    TEST_ASSERT_TRUE(t1.task.queued == false);
    TEST_ASSERT_TRUE(t1.task.running == false);
    TEST_ASSERT_TRUE(t1.task.cancelled == true);
    TEST_ASSERT_TRUE(t1.task.killed == false);
    TEST_ASSERT_TRUE(q.empty == true);
    TEST_ASSERT_TRUE(counter == 2); /* run + complete */
}

static void
test_timeout_nocancel(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    struct rq q;
    int counter = 0;
    const struct rq_task_ops ops = {
        .run_fn = rq_task_cnt_inc_cb,
        .cancel_fn = rq_task_cnt_inc_cb,
        .kill_fn = rq_task_cnt_inc_cb,
    };
    struct rq_task_cnt t1 = {
        .counter = &counter,
        .task = {
            .ops = &ops,
            .run_timeout_msec = 10,
            .cancel_timeout_msec = 10,
            .completed_fn = rq_task_cnt_done_cb,
            .priv = &counter,
        }
    };

    MEMZERO(q);
    rq_init(&q, loop);
    rq_add_task(&q, &t1.task);
    ev_run(loop, EVRUN_ONCE);

    TEST_ASSERT_TRUE(t1.task.queued == true);
    TEST_ASSERT_TRUE(t1.task.running == true);
    TEST_ASSERT_TRUE(t1.task.cancelled == false);
    TEST_ASSERT_TRUE(t1.task.killed == false);
    TEST_ASSERT_TRUE(q.empty == false);
    TEST_ASSERT_TRUE(counter == 1); /* run */

    rq_task_complete(&t1.task);
    ev_run(loop, 0);

    TEST_ASSERT_TRUE(t1.task.queued == false);
    TEST_ASSERT_TRUE(t1.task.running == false);
    TEST_ASSERT_TRUE(t1.task.cancelled == false);
    TEST_ASSERT_TRUE(t1.task.killed == false);
    TEST_ASSERT_TRUE(q.empty == true);
    TEST_ASSERT_TRUE(counter == 2); /* run + complete */
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);
    ut_setUp_tearDown(test_name, NULL, NULL);
    RUN_TEST(test_max);
    RUN_TEST(test_cancel);
    RUN_TEST(test_kill);
    RUN_TEST(test_timeout);
    RUN_TEST(test_timeout_nokill);
    RUN_TEST(test_timeout_nocancel);

    return ut_fini();
}
