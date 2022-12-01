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

#include "log.h"
#include "os.h"
#include "target.h"
#include "unity.h"
#include "memutil.h"
#include "unit_test_utils.h"

#if defined(CONFIG_IMC_ZMQ)
#include "imc_zmq.h"
#elif defined(CONFIG_IMC_SOCKETS)
#include "imc_sockets.h"
#else
#include "imc.h"
#endif

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
    struct imc_dso *imc_server_context;
    struct imc_dso *imc_client_context;
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
 * @brief callback passed to zmq_msg_send() to free the sent message
 *
 * zmq_msg_send() only guarantees that the message is successfully queued
 * for transmission upon return. Therefore the message cannot be a simple
 * local variable.
 * Please refer to zmq_msg_send() man page for further details.
 */
static void free_send_msg(void *data, void *hint)
{
    FREE(data);
}

/**
 * @brief allocate imc client
 */
void
allocate_sender(void)
{
    struct imc_dso *client_context;

    /* Initialize the client */
    client_context = CALLOC(1, sizeof(*client_context));
    g_test_mgr.imc_client_context = client_context;
}

/**
 * @brief allocate imc server
 */
void
allocate_receiver(void)
{
    struct imc_dso *server_context;

    /* Initialize the server */
    server_context = CALLOC(1, sizeof(*server_context));
    g_test_mgr.imc_server_context = server_context;
}


/**
 * @brief free imc client
 */
void
free_sender(void)
{
    struct imc_dso *client;

    client = g_test_mgr.imc_client_context;
    FREE(client);
    g_test_mgr.imc_client_context = NULL;
}

/**
 * @brief free imc server
 */
void
free_receiver(void)
{
    struct imc_dso *server;

    server = g_test_mgr.imc_server_context;
    FREE(server);
    g_test_mgr.imc_server_context = NULL;
}

/**
 * @brief free endpoint
 */
void
free_endpoint()
{
    FREE(g_test_mgr.endpoint);
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
    struct imc_dso *imc_server;
    struct imc_dso *imc_client;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    allocate_receiver();

    imc_init_dso(g_test_mgr.imc_server_context);
    /* Start server */
    imc_server = g_test_mgr.imc_server_context;
    rc = imc_server->imc_init_server(imc_server, loop, test_basic_recv_cb);

    TEST_ASSERT_EQUAL_INT(0, rc);

    allocate_sender();

    imc_init_dso(g_test_mgr.imc_client_context);

    /* Start client */
    imc_client = g_test_mgr.imc_client_context;
    rc = imc_client->imc_init_client(imc_client, free_send_msg, NULL);

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
    struct imc_dso *imc_client;
    int64_t *data;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    data = MALLOC(sizeof(*data));
    *(int64_t *)data = g_test_mgr.val;

    imc_client = g_test_mgr.imc_client_context;
    rc = imc_client->imc_client_send(imc_client, data, sizeof(data), IMC_DONTWAIT);
    TEST_ASSERT_EQUAL_INT(0, rc);
    LOGI("\n***** %s: done\n", __func__);
}


static void
finish_basic_test_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    struct imc_dso *imc_server;
    struct imc_dso *imc_client;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    /* Terminate the client */
    imc_client = g_test_mgr.imc_client_context;
    imc_client->imc_terminate_client(imc_client);

    /* Terminate the server */
    imc_server = g_test_mgr.imc_server_context;
    imc_server->imc_terminate_server(imc_server);

    free_sender();
    free_receiver();
    LOGI("\n***** %s: done\n", __func__);
}


void
setup_basic_send_receive(void)
{
    struct test_imc_timers *t;
    struct ev_loop *loop;

    t = &g_test_mgr.basic_send_receive;
    loop = g_test_mgr.loop;

    /* Arm the send execution timer */
    ev_timer_init(&t->start_timeout,
                  start_basic_test_timeout_cb,
                  ++g_test_mgr.timeout, 0);
    t->start_timeout.data = NULL;

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
    struct imc_dso *imc_client;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    /* allocate end points */
    allocate_sender();

    imc_init_dso(g_test_mgr.imc_client_context);

    /* Start client */
    imc_client = g_test_mgr.imc_client_context;

    /* Configure client endpoint */
    imc_client->imc_config_client_endpoint(imc_client, g_test_mgr.endpoint);

    rc = imc_client->imc_init_client(imc_client, free_send_msg, NULL);
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
    struct imc_dso *imc_client;
    int expected;
    uint8_t *buf;
    size_t size;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering, #%d\n", __func__, g_test_mgr.repeat);
    size = g_test_mgr.send_size;

    expected = (g_test_mgr.repeat < g_test_mgr.sndhwm) ? 0 : -1;

    buf = CALLOC(1, size);
    memset(buf, 1, size);
    imc_client = g_test_mgr.imc_client_context;
    rc = imc_client->imc_client_send(imc_client, buf, size, IMC_DONTWAIT);
    TEST_ASSERT_EQUAL_INT(expected, rc);

    //if (rc != 0) FREE(buf);

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
    struct imc_dso *imc_client;
    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    /* Terminate the client */
    imc_client = g_test_mgr.imc_client_context;
    imc_client->imc_terminate_client(imc_client);

    free_sender();
    free_endpoint();
    LOGI("\n***** %s: done\n", __func__);
}


/**
 * @brief test repeated sends without a receiver
 */
void
setup_repeated_send(size_t send_size, int sndhwm, int repeat)
{
    //struct imc_context *client;
    struct test_imc_timers *t;
    struct ev_loop *loop;
    char *endpoint;

    g_test_mgr.send_size = send_size;

    t = &g_test_mgr.repeated_send;
    loop = g_test_mgr.loop;

    endpoint = strdup("ipc:///tmp/basic_test_imc");

    g_test_mgr.endpoint = endpoint;

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
    setup_repeated_send(4096, 10, 20);

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

    ut_init(test_name, imc_global_test_setup, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    RUN_TEST(test_events);

    return ut_fini();
}
