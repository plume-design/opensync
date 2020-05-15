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

#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <zmq.h>

#include "imc.h"
#include "log.h"
#include "os.h"
#include "target.h"
#include "unity.h"

const char *test_name = "icm_tests";

struct test_imc_timers
{
    ev_timer start_timeout;
    ev_timer test_timeout;
    ev_timer finish_timeout;
};

struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    double timeout;
    int64_t val;
    struct test_imc_timers basic_send_receive;
    struct test_imc_timers repeated_send;
    struct imc_context *client;
    struct imc_context *server;
    char *endpoint;
    size_t send_size;
    int repeat_limit;
    int repeat_cnt;
    int repeat;
    int sndhwm;
} g_test_mgr;


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}


int test_imc_ev_setup(double timeout)
{
    ev_timer *p_timeout_watcher;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    ev_timer_init(p_timeout_watcher, timeout_cb, timeout, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);

    return 0;
}


void imc_global_test_setup(void)
{
    memset(&g_test_mgr, 0, sizeof(g_test_mgr));

    g_test_mgr.timeout = 0.0;
    g_test_mgr.val = 0x123456789abcdef;
    g_test_mgr.loop = EV_DEFAULT;
}

/**
 * @brief called by the unity framework at the start of each test
 */
void
setUp(void)
{
    return;
}


/**
 * @brief called by the unity framework at the end of each test
 */
void tearDown(void)
{
    return;
}


/**
 * @brief callback passed to zmq_msg_send() to free the sent message
 *
 * zmq_msg_send() only guarantees that the message is successfully queued
 * for transmission upon return. Therefore the message cannot be a simple
 * local variable.
 * Please refer to zmq_msg_send() man page for further details.
 */
static void free_send_msg(void *data, void *hint)
{
    free(data);
}


/**
 * @brief allocate imc client and imc server
 */
void
allocate_sender_and_receiver(void)
{
    struct imc_context *client;
    struct imc_context *server;

    /* Initialize the server */
    server = calloc(1, sizeof(*server));
    TEST_ASSERT_NOT_NULL(server);

    server->ztype = ZMQ_PULL;
    server->endpoint = strdup(g_test_mgr.endpoint);
    TEST_ASSERT_NOT_NULL(server->endpoint);

    g_test_mgr.server = server;
    imc_init_context(server);

    /* Start the client */
    client = calloc(1, sizeof(*client));
    TEST_ASSERT_NOT_NULL(client);

    client->ztype = ZMQ_PUSH;
    client->endpoint = strdup(g_test_mgr.endpoint);
    TEST_ASSERT_NOT_NULL(client->endpoint);

    g_test_mgr.client = client;
    imc_init_context(client);
}


/**
 * @brief free imc client and imc server
 */
void
free_sender_and_receiver(void)
{
    struct imc_context *client;
    struct imc_context *server;

    client = g_test_mgr.client;
    imc_reset_context(client);
    free(client->endpoint);
    free(client);
    g_test_mgr.client = NULL;

    server = g_test_mgr.server;
    imc_reset_context(server);
    free(server->endpoint);
    free(server);
    g_test_mgr.server = NULL;

    free(g_test_mgr.endpoint);
    g_test_mgr.endpoint = NULL;
}


/**
 * @brief user (ie manager) provided routine to process a received buffer.
 */
static void
test_basic_recv_cb(void *data, size_t len)
{
    int64_t *recv;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    TEST_ASSERT_TRUE(len == sizeof(*recv));

    recv = (int64_t *)(data);
    TEST_ASSERT_TRUE(*recv == g_test_mgr.val);
    LOGI("\n***** %s: done\n", __func__);
}


static void
start_basic_test_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    g_test_mgr.endpoint = w->data;
    LOGI("%s: end point: %s", __func__, g_test_mgr.endpoint);

    allocate_sender_and_receiver();

    /* Start server */
    rc = imc_init_server(g_test_mgr.server, loop, test_basic_recv_cb);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Start client */
    rc = imc_init_client(g_test_mgr.client, free_send_msg, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);
    LOGI("\n***** %s: done\n", __func__);
}


/**
 * @brief send data when a dedicated timer fires.
 *
 * When the timer fires, send data. The reception of the data triggers
 * an ev event.
 */
static void
basic_send_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    int64_t *data;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    data = malloc(sizeof(*data));
    *(int64_t *)data = g_test_mgr.val;

    rc = imc_send(g_test_mgr.client, data, sizeof(data), IMC_DONTWAIT);
    TEST_ASSERT_EQUAL_INT(0, rc);
    LOGI("\n***** %s: done\n", __func__);
}


static void
finish_basic_test_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    /* Terminate the client */
    imc_terminate_client(g_test_mgr.client);

    /* Terminate the server */
    imc_terminate_server(g_test_mgr.server);

    free_sender_and_receiver();
    LOGI("\n***** %s: done\n", __func__);
}


void
setup_basic_send_receive(void)
{
    struct test_imc_timers *t;
    struct ev_loop *loop;
    char *endpoint;

    t = &g_test_mgr.basic_send_receive;
    loop = g_test_mgr.loop;

    endpoint = strdup("ipc:///tmp/basic_test_imc");

    /* Arm the send execution timer */
    ev_timer_init(&t->start_timeout,
                  start_basic_test_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->start_timeout.data = endpoint;

    /* Arm the test execution timer */
    ev_timer_init(&t->test_timeout,
                  basic_send_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->test_timeout.data = NULL;

    /* Arm the finish execution timer */
    ev_timer_init(&t->finish_timeout,
                  finish_basic_test_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->finish_timeout.data = NULL;

    ev_timer_start(loop, &t->start_timeout);
    ev_timer_start(loop, &t->test_timeout);
    ev_timer_start(loop, &t->finish_timeout);
}


static void
start_repeated_send_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    struct imc_sockoption opt;
    int opt_value;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    g_test_mgr.endpoint = w->data;
    LOGI("%s: end point: %s", __func__, g_test_mgr.endpoint);

    /* allocate end points */
    allocate_sender_and_receiver();

    /* Set the send threshold option */
    opt.option_name = IMC_SNDHWM;
    opt_value = g_test_mgr.sndhwm;
    opt.value = &opt_value;
    opt.len = sizeof(opt_value);
    rc = imc_add_sockopt(g_test_mgr.client, &opt);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Set the linger option */
    opt_value = 0;
    opt.option_name = IMC_LINGER;
    opt.value = &opt_value;
    opt.len = sizeof(opt_value);
    rc = imc_add_sockopt(g_test_mgr.client, &opt);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Start client */
    rc = imc_init_client(g_test_mgr.client, free_send_msg, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}

/**
 * @brief send data when a dedicated timer fires.
 *
 * used by send/receive tests: arm the send timer, start ev_run().
 * When the timer fires, send data. The reception of the data triggers
 * an ev event.
 */
static void
repeated_send_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    int expected;
    uint8_t *buf;
    size_t size;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering, #%d\n", __func__, g_test_mgr.repeat);
    size = g_test_mgr.send_size;

    expected = (g_test_mgr.repeat < g_test_mgr.sndhwm) ? 0 : -1;

    buf = calloc(1, size);
    memset(buf, 1, size);
    rc = imc_send(g_test_mgr.client, buf, size, IMC_DONTWAIT);
    TEST_ASSERT_EQUAL_INT(expected, rc);

    // if (rc != 0) free(buf);

    g_test_mgr.repeat++;
    if (g_test_mgr.repeat_cnt < g_test_mgr.repeat_limit)
    {
        w->repeat = 1;
        ev_timer_again(EV_A_ w);
    }
    LOGI("\n***** %s: done\n", __func__);
}


static void
finish_repeated_test_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    /* Terminate the client */
    imc_terminate_client(g_test_mgr.client);

    free_sender_and_receiver();
    LOGI("\n***** %s: done\n", __func__);
}


/**
 * @brief test repeated sends without a receiver
 */
void
setup_repeated_send(size_t send_size, int sndhwm, int repeat)
{
    struct test_imc_timers *t;
    struct ev_loop *loop;
    char *endpoint;

    g_test_mgr.send_size = send_size;

    t = &g_test_mgr.repeated_send;
    loop = g_test_mgr.loop;

    endpoint = strdup("ipc:///tmp/basic_test_imc2");

    /* Arm the send execution timer */
    ev_timer_init(&t->start_timeout,
                  start_repeated_send_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->start_timeout.data = endpoint;

    /* Arm the test execution timer */
    ev_timer_init(&t->test_timeout,
                  repeated_send_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->test_timeout.data = NULL;

    g_test_mgr.repeat_limit = repeat;
    g_test_mgr.sndhwm = sndhwm;
    g_test_mgr.timeout += repeat;

    /* Arm the finish execution timer */
    ev_timer_init(&t->finish_timeout,
                  finish_repeated_test_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->finish_timeout.data = NULL;

    ev_timer_start(loop, &t->start_timeout);
    ev_timer_start(loop, &t->test_timeout);
    ev_timer_start(loop, &t->finish_timeout);
}


void test_events(void)
{
    setup_basic_send_receive();
    setup_repeated_send(4096, 5, 10);

    /* Set overall test duration */
     test_imc_ev_setup(g_test_mgr.timeout);

    /* Start the main loop */
    ev_run(g_test_mgr.loop, 0);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);
    imc_global_test_setup();

    RUN_TEST(test_events);

    return UNITY_END();
}
