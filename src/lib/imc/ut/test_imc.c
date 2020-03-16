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

struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    int64_t val;
} g_test_mgr;


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}


/**
 * @brief called by the unity framework at the start of each test
 */
void
setUp(void)
{
    ev_timer *p_timeout_watcher;

    g_test_mgr.val = 0x123456789abcdef;
    g_test_mgr.loop = EV_DEFAULT;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    /* The timer is set to 1 second */
    ev_timer_init(p_timeout_watcher, timeout_cb, 1.0, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);
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
 * @brief user (ie manager) provided routine to process a received buffer.
 */
static void
test_recv_cb(void *data, size_t len)
{
    int64_t *recv;

    TEST_ASSERT_TRUE(len == sizeof(*recv));

    recv = (int64_t *)(data);
    TEST_ASSERT_TRUE(*recv == g_test_mgr.val);
}


/**
 * @brief initializes and terminates a server.
 *
 * Sets up and tears down a zmq server.
 * Validates that there is no memory issue (native build with clang)
 */
void
test_start_terminate_server(void)
{
    struct imc_context server;
    int rc;

    memset(&server, 0, sizeof(server));
    server.ztype = ZMQ_PULL;
    server.endpoint = strdup("ipc:///tmp/test_imc");
    TEST_ASSERT_NOT_NULL(server.endpoint);

    /* Start the server */
    rc = imc_init_server(&server, g_test_mgr.loop, test_recv_cb);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ev_run(g_test_mgr.loop, 0);

    /* Terminate the server */
    imc_terminate_server(&server);

    free(server.endpoint);
}


/**
 * @brief initializes and terminates a server.
 *
 * Sets up and tears down a zmq client.
 * Validates that there is no memory issue (native build with clang)
 */
void
test_start_terminate_client(void)
{
    struct imc_context client;
    int rc;

    ev_run(g_test_mgr.loop, 0);

    memset(&client, 0, sizeof(client));
    client.ztype = ZMQ_PUSH;
    client.endpoint = strdup("ipc:///tmp/test_imc2");
    TEST_ASSERT_NOT_NULL(client.endpoint);

    /* Start the client */
    rc = imc_init_client(&client, free_send_msg, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Terminate the server */
    imc_terminate_client(&client);

    free(client.endpoint);
}


/**
 * @brief send data when a dedicated timer fires.
 *
 * used by send/receive tests: arm the send timer, start ev_run().
 * When the timer fires, send data. The reception of the data triggers
 * an ev event.
 */
static void
send_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    struct imc_context *client;
    int64_t *data;
    int rc;

    data = malloc(sizeof(*data));
    *(int64_t *)data = g_test_mgr.val;
    client = w->data;
    rc = imc_send(client, data, sizeof(data), 0);
    TEST_ASSERT_EQUAL_INT(0, rc);
}


/**
 * @brief test basic send/receive
 */
void
test_basic_send_recv(void)
{
    ev_timer *p_timeout_watcher;
    struct imc_context server;
    struct imc_context client;
    ev_timer timeout_watcher;
    struct ev_loop *loop;
    int rc;

    loop = g_test_mgr.loop;

    /* Start the server */
    memset(&server, 0, sizeof(server));
    server.ztype = ZMQ_PULL;
    server.endpoint = strdup("ipc:///tmp/test_imc");
    TEST_ASSERT_NOT_NULL(server.endpoint);

    rc = imc_init_server(&server, loop, test_recv_cb);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Start the client */
    memset(&client, 0, sizeof(client));
    client.ztype = ZMQ_PUSH;
    client.endpoint = strdup("ipc:///tmp/test_imc");
    TEST_ASSERT_NOT_NULL(client.endpoint);

    rc = imc_init_client(&client, free_send_msg, NULL);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Arm the send timer */
    p_timeout_watcher = &timeout_watcher;
    ev_timer_init(p_timeout_watcher, send_timeout_cb, 0.2, 0);
    timeout_watcher.data = &client;
    ev_timer_start(loop, p_timeout_watcher);

    /* Start the main loop */
    ev_run(loop, 0);

    /* Terminate the client */
    imc_terminate_client(&client);

    /* Terminate the server */
    imc_terminate_server(&server);

    free(client.endpoint);
    free(server.endpoint);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(test_name);

    RUN_TEST(test_start_terminate_server);
    RUN_TEST(test_start_terminate_client);
    RUN_TEST(test_basic_send_recv);

    return UNITY_END();
}
