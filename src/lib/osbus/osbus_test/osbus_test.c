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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <math.h>
#include <values.h>
#include <jansson.h>
#include <ev.h>

#include "os_backtrace.h"
#include "json_util.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "os_time.h"
#include "osbus.h"
#include "evx.h"

osbus_bus_type_t g_opt_bus_type = OSBUS_BUS_TYPE_DEFAULT;

char *prog_name;

void usage(char *prog_name)
{
    printf("usage: %s [OPT] CMD [PARAM]\n", prog_name);
    printf("OPT:\n");
    printf("    -v      increase verbosity\n");
    printf("    -u      use ubus\n");
    printf("    -r      use rbus\n");
    printf("CMD:\n");
    printf("    list\n");
    printf("    mp|method_provider\n");
    printf("        implements methods:\n");
    printf("        ping echo error quit async factorial\n");
    printf("    mc|method_consumer [OBJECT.]METHOD [ARGS]\n");
    printf("    ma|method_async    [OBJECT.]METHOD [ARGS]\n");
    printf("    ep|event_pub      \n");
    printf("    es|event_sub      \n");
    printf("    tl|topic_listen   \n");
    printf("    ts|topic_send     \n");
    printf("    dp|data_provider  \n");
    printf("    get OBJ NAME VALUE\n");
    printf("    set OBJ NAME VALUE\n");
    exit(0);
}

void print_msg(const char *txt, osbus_msg_t *msg)
{
    char *str = osbus_msg_to_dbg_str(msg);
    if (txt) {
        printf("%s: %s\n", txt, str);
    } else {
        printf("%s\n", str);
    }
    free(str);
}

void print_msg_indent(const char *txt, osbus_msg_t *msg)
{
    char *str = osbus_msg_to_dbg_str_indent(msg, 4);
    if (txt) {
        printf("%s: %s\n", txt, str);
    } else {
        printf("%s\n", str);
    }
    free(str);
}

int cli_list(int argc, char *argv[])
{
    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", prog_name, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_msg_t *d = NULL;
    bool ret = osbus_list_msg(OSBUS_DEFAULT, osbus_path_gbl("",""), true, &d);
    if (!ret) return 2;
    print_msg_indent(NULL, d);
    osbus_msg_free(d);
    return 0;
}

bool provider_method_ping(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    int64_t t2 = clock_mono_usec();
    print_msg(__func__, msg);
    osbus_msg_t *d = osbus_msg_new_object();
    osbus_msg_set_prop_int64(d, "pong", t2);
    *reply = d;
    return true;
}

bool provider_method_echo(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);
    *reply = osbus_msg_copy(msg);
    return true;
}

bool provider_method_quit(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);
    ev_break(EV_DEFAULT, EVBREAK_ONE);
    return true;
}

bool provider_method_error(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);

    int i;
    osbus_error_t e = OSBUS_ERROR_SUCCESS;
    if (osbus_msg_get_prop_int(msg, "code", &i)) {
        e = i;
    }

    *reply = osbus_msg_new_object();
    osbus_msg_set_prop_int(*reply, "reply", i);

    if (e != OSBUS_ERROR_SUCCESS) {
        osbus_error_set(handle, e);
        return false;
    }

    return true;
}

typedef struct defer_reply_data
{
    ev_debounce ev_deb;
    osbus_async_reply_t *reply_handle;
    int delay;
    int arg;
} defer_reply_data_t;


static void debounce_async_reply(struct ev_loop *loop, ev_debounce *w, int revent)
{
    defer_reply_data_t *dr = (defer_reply_data_t*)w->data;
    osbus_async_reply_t *reply_handle = dr->reply_handle;
    printf("wake up (%d) sending deferred reply (%p) arg: %d\n", dr->delay, reply_handle, dr->arg);
    osbus_msg_t *reply = osbus_msg_new_object();
    osbus_msg_set_prop_int(reply, "reply", dr->arg);
    osbus_method_reply_async(reply_handle->handle, reply_handle, true, reply);
    osbus_msg_free(reply);
    free(dr);
}

bool provider_method_async(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);

    int delay_sec;
    if (!osbus_msg_get_prop_int(msg, "delay", &delay_sec)) {
        delay_sec = 3;
    }
    int i = 0;
    osbus_msg_get_prop_int(msg, "arg", &i);

    printf("defer reply by %d seconds (%p) arg: %d\n", delay_sec, reply_handle, i);

    *defer_reply = true;

    defer_reply_data_t *dr = CALLOC(1, sizeof(defer_reply_data_t));
    dr->reply_handle = reply_handle;
    dr->delay = delay_sec;
    dr->arg = i;
    dr->ev_deb.data = dr;
    ev_debounce_init(&dr->ev_deb, debounce_async_reply, (double)delay_sec);
    ev_debounce_start(EV_DEFAULT, &dr->ev_deb);

    return true;
}


void debounce_async_replyx(struct ev_loop *loop, ev_debounce *w, int revent)
{
    defer_reply_data_t *dr = (defer_reply_data_t*)w->data;
    osbus_async_reply_t *reply_handle = dr->reply_handle;
    printf("wake up (%d) sending deferred reply (%p) arg: %d\n", dr->delay, reply_handle, dr->arg);
    osbus_msg_t *reply = osbus_msg_new_object();
    osbus_msg_set_prop_int(reply, "reply", dr->arg);
    osbus_method_reply_async(reply_handle->handle, reply_handle, true, reply);
    osbus_msg_free(reply);
    free(dr);
}

typedef struct factorial_req
{
    osbus_async_reply_t *async_reply;
    osbus_async_invoke_t *async_invoke;
} factorial_req_t;

#define FACTORIAL_MAX 21
factorial_req_t factorial_array[FACTORIAL_MAX];

bool factorial_async_invoke_handler(
        osbus_handle_t handle,
        char *method_name,
        bool status,
        osbus_msg_t *reply,
        void *user_data)
{
    // received async reply
    LOGT("%s %s %s status=%d err=%d %s user_data: %p",
            __func__, osbus_handle_get_name(handle), method_name, status,
            osbus_error_get(handle), osbus_error_get_str(handle), user_data);
    print_msg("factorial recv", reply);
    int req_n;
    int64_t req_result;
    int n;
    int64_t new_result;
    if (!osbus_msg_get_prop_int(reply, "n", &req_n)) return false;
    if (!osbus_msg_get_prop_int64(reply, "result", &req_result)) return false;
    n = req_n + 1;
    /*
    // user_data not reliable on rbus with multiple
    // concurrent async instances of same method
    int index = (factorial_req_t*)user_data - factorial_array;
    if (index != n) {
        printf("warn: index %d != n %d\n", index, n);
        index = n;
    }
    */
    if (n < 0 || n >= FACTORIAL_MAX) return false;
    factorial_req_t *req = &factorial_array[n];
    new_result = n * req_result;
    LOGT("%s req n=%d result=%"PRId64" async_reply=%p", __func__, n, new_result, req->async_reply);
    // send async reply
    osbus_msg_t *new_reply = osbus_msg_new_object();
    osbus_msg_set_prop_int(new_reply, "n", n);
    osbus_msg_set_prop_int64(new_reply, "result", new_result);
    print_msg("factorial send", new_reply);
    osbus_method_reply_async(handle, req->async_reply, true, new_reply);
    osbus_msg_free(new_reply);

    return true;
}

bool provider_method_factorial(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    LOGT("%s %s %s", __func__, osbus_handle_get_name(handle), method_name);
    print_msg(__func__, msg);
    int n = 0;
    if (!osbus_msg_get_prop_int(msg, "n", &n)) n = -1;
    if (n < 0 || n >= FACTORIAL_MAX) {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    if (n <= 1) {
        osbus_msg_t *re = osbus_msg_new_object();
        osbus_msg_set_prop_int(re, "n", n);
        osbus_msg_set_prop_int64(re, "result", 1);
        *reply = re;
        return true;
    }
    // n > 1
    *defer_reply = true;
    factorial_req_t *req = &factorial_array[n];
    req->async_reply = reply_handle;
    LOGT("%s req n=%d async_reply=%p\n", __func__, n, req->async_reply);

    // invoke async req
    osbus_msg_t *req_msg = osbus_msg_new_object();
    osbus_msg_set_prop_int64(req_msg, "n", n - 1);
    bool ret = osbus_method_invoke_async(handle,
            osbus_path_gbl(NULL, "factorial"), req_msg,
            factorial_async_invoke_handler, req, &req->async_invoke);
    osbus_msg_free(req_msg);
    if (!ret) return false;

    return true;
}


osbus_msg_policy_t provider_policy_ping[] = {
    { .name = "ping", .type = OSBUS_DATA_TYPE_INT64, .required = false },
};

osbus_msg_policy_t provider_policy_error[] = {
    { .name = "code", .type = OSBUS_DATA_TYPE_INT, .required = false },
};

osbus_msg_policy_t provider_policy_types[] = {
    { .name = "null",   .type = OSBUS_DATA_TYPE_NULL },
    { .name = "object", .type = OSBUS_DATA_TYPE_OBJECT },
    { .name = "array",  .type = OSBUS_DATA_TYPE_ARRAY },
    { .name = "bool",   .type = OSBUS_DATA_TYPE_BOOL },
    { .name = "int",    .type = OSBUS_DATA_TYPE_INT },
    { .name = "int64",  .type = OSBUS_DATA_TYPE_INT64 },
    { .name = "double", .type = OSBUS_DATA_TYPE_DOUBLE },
    { .name = "string", .type = OSBUS_DATA_TYPE_STRING },
    { .name = "binary", .type = OSBUS_DATA_TYPE_BINARY },
};

osbus_msg_policy_t provider_policy_async[] = {
    { .name = "delay", .type = OSBUS_DATA_TYPE_INT, .required = false },
    { .name = "arg", .type = OSBUS_DATA_TYPE_INT, .required = false },
};

osbus_msg_policy_t provider_policy_factorial[] = {
    { .name = "n", .type = OSBUS_DATA_TYPE_INT, .required = true },
};

osbus_method_t provider_method_list[] = {
    OSBUS_METHOD_ENTRY(provider, ping),
    OSBUS_METHOD_NO_POLICY(provider, echo),
    OSBUS_METHOD_NO_POLICY(provider, quit),
    { .name = "types", .handler_fn = provider_method_echo,
        .policy = provider_policy_types, .n_policy = ARRAY_LEN(provider_policy_types), },
    OSBUS_METHOD_ENTRY(provider, error),
    OSBUS_METHOD_ENTRY(provider, async),
    OSBUS_METHOD_ENTRY(provider, factorial),
};

bool mp1_method_hello1(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);
    osbus_msg_t *d = osbus_msg_new_object();
    osbus_msg_set_prop_string(d, "msg", "hello1");
    *reply = d;
    return true;
}

bool mp2_method_hello2(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle)
{
    print_msg(__func__, msg);
    osbus_msg_t *d = osbus_msg_new_object();
    osbus_msg_set_prop_string(d, "msg", "hello2");
    *reply = d;
    return true;
}

osbus_method_t mp1_method_list[] = { OSBUS_METHOD_NO_POLICY(mp1, hello1) };
osbus_method_t mp2_method_list[] = { OSBUS_METHOD_NO_POLICY(mp2, hello2) };

int method_provider(int argc, char *argv[])
{
    // connect multiple instances for testing
    osbus_handle_t mp1_handle = NULL;
    osbus_handle_t mp2_handle = NULL;

    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("method_provider", NULL))) {
        LOGE("bus init");
        return 1;
    }
    if (!osbus_connect(&mp1_handle, EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("mp1", NULL))) {
        return 1;
    }
    if (!osbus_connect(&mp2_handle, EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("mp2", NULL))) {
        return 1;
    }
    // test: multiple method registers on same object
    osbus_method_register(OSBUS_DEFAULT, provider_method_list, ARRAY_LEN(provider_method_list) - 1);
    osbus_method_register(OSBUS_DEFAULT, provider_method_list + ARRAY_LEN(provider_method_list) - 1, 1);
    osbus_method_register(mp1_handle, mp1_method_list, ARRAY_LEN(mp1_method_list));
    osbus_method_register(mp2_handle, mp2_method_list, ARRAY_LEN(mp2_method_list));

    ev_run(EV_DEFAULT, 0);

    if (mp1_handle) osbus_disconnect(mp1_handle);
    if (mp2_handle) osbus_disconnect(mp2_handle);
    return 0;
}

int64_t g_ping_t1;

void handle_ping_reply(osbus_msg_t *reply)
{
    int64_t t1, t2, t3;
    t1 = g_ping_t1;
    t3 = clock_mono_usec();
    if (osbus_msg_get_prop_int64(reply, "pong", &t2)) {
        printf("t1->t2: %3"PRId64" usec %.3f ms\n", t2 - t1, (double)(t2 - t1)/1000.0);
        printf("t2->t3: %3"PRId64" usec %.3f ms\n", t3 - t2, (double)(t3 - t2)/1000.0);
    }
    printf("t1->t3: %3"PRId64" usec %.3f ms\n", t3 - t1, (double)(t3 - t1)/1000.0);
}

osbus_msg_t *prep_msg(char *method, int argc, char *argv[])
{
    if (!strcmp(method, "ping")) {
        g_ping_t1 = clock_mono_usec();
        osbus_msg_t *d = osbus_msg_new_object();
        osbus_msg_set_prop_int64(d, "ping", g_ping_t1);
        return d;
    }
    if (!strcmp(method, "echo") && argc > 0) {
        return osbus_msg_from_json_string(argv[0]);
    }
    if (!strcmp(method, "error")) {
        osbus_msg_t *d = osbus_msg_new_object();
        osbus_msg_set_prop_int(d, "code", argc > 0 ? atoi(argv[0]) : 0);
        return d;
    }
    if (!strcmp(method, "quit")) {
        return NULL;
    }
    if (!strcmp(method, "async")) {
        osbus_msg_t *d = osbus_msg_new_object();
        if (argc > 0) osbus_msg_set_prop_int(d, "delay", atoi(argv[0]));
        if (argc > 1) osbus_msg_set_prop_int(d, "arg", atoi(argv[1]));
        return d;
    }
    if (!strcmp(method, "factorial")) {
        osbus_msg_t *d = osbus_msg_new_object();
        if (argc < 1) {
            printf("error: provide arg n\n");
            exit(1);
        }
        osbus_msg_set_prop_int(d, "n", atoi(argv[0]));
        return d;
    }
    return NULL;
}

int method_consumer(int argc, char *argv[])
{
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("method_consumer", NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_msg_t *reply;
    char *cmd = argc > 0 ? argv[0] : "ping";
    char *provider = "method_provider";
    char *dot = strchr(cmd, '.');
    if (dot) { provider = cmd; *dot = 0; cmd = dot + 1; }
    osbus_msg_t *d = prep_msg(cmd, argc - 1, argv + 1);

    bool ret = osbus_method_invoke(OSBUS_DEFAULT, osbus_path_gbl(provider, cmd), d, &reply);
    LOGT("osbus_method_invoke ret=%d err=%d %s", ret,
            osbus_error_get(OSBUS_DEFAULT), osbus_error_get_str(OSBUS_DEFAULT));
    print_msg_indent("reply", reply);
    if (!strcmp(cmd, "ping")) {
        handle_ping_reply(reply);
    }
    osbus_msg_free(d);
    osbus_msg_free(reply);

    return 0;
}

bool method_async_invoke_reply_handler(
        osbus_handle_t handle,
        char *method_name,
        bool status,
        osbus_msg_t *reply,
        void *user_data)
{
    char *cmd = user_data ?: "";
    LOGT("%s %s status=%d err=%d %s user_data: %s", __func__, method_name, status,
            osbus_error_get(handle), osbus_error_get_str(handle), cmd);
    print_msg_indent("reply", reply);
    if (!strcmp(cmd, "ping")) {
        handle_ping_reply(reply);
    }
    ev_break(EV_DEFAULT, EVBREAK_ONE);
    return true;
}

int method_consumer_async(int argc, char *argv[])
{
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("method_async", NULL))) {
        LOGE("bus init");
        return 1;
    }
    char *cmd = argc > 0 ? argv[0] : "ping";
    char *provider = "method_provider";
    char *dot = strchr(cmd, '.');
    if (dot) { provider = cmd; *dot = 0; cmd = dot + 1; }
    osbus_msg_t *d = prep_msg(cmd, argc - 1, argv + 1);
    osbus_async_invoke_t *async = NULL;
    bool ret = osbus_method_invoke_async(OSBUS_DEFAULT, osbus_path_gbl(provider, cmd), d,
            method_async_invoke_reply_handler, cmd, &async);
    LOGT("osbus_method_invoke_async ret=%d err=%d %s async handle: %p", ret,
            osbus_error_get(OSBUS_DEFAULT), osbus_error_get_str(OSBUS_DEFAULT), async);
    osbus_msg_free(d);
    if (!ret) {
        LOGE("%s %s", __func__, osbus_error_get_str(OSBUS_DEFAULT));
        return 1;
    }
    ev_run(EV_DEFAULT, 0);
    return 0;
}


bool topic_handler(
        osbus_handle_t handle,
        char *topic_path,
        osbus_msg_t *msg,
        void *user_data)
{
    printf("received topic %s:\n", topic_path);
    print_msg(__func__, msg);
    printf("user data: %s\n", (char*)user_data);
    return true;
}

int topic_listen(int argc, char *argv[])
{
    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", __func__, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_topic_listen(OSBUS_DEFAULT, osbus_path_gbl(prog_name, "topic"), topic_handler, "user data");
    ev_run(EV_DEFAULT, 0);
    return 0;
}

int topic_send(int argc, char *argv[])
{
    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", __func__, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_msg_t *msg = osbus_msg_new_object();
    osbus_msg_set_prop_string(msg, "msg", "test");
    osbus_topic_send(OSBUS_DEFAULT, osbus_path_gbl(prog_name, "topic"), msg);
    osbus_msg_free(msg);
    return 0;
}


void ev_periodic_event_pub(struct ev_loop *loop, ev_timer *w, int revents)
{
    osbus_msg_t *msg = osbus_msg_new_object();
    osbus_msg_set_prop_string(msg, "msg", "test");
    printf("publish event\n");
    if (!osbus_event_publish(OSBUS_DEFAULT, osbus_path_gbl("event_pub", "event"), "etype", msg)) {
        printf("publish error\n");
    }
    osbus_msg_set_prop_string(msg, "msg", "test2");
    if (!osbus_event_publish(OSBUS_DEFAULT, osbus_path_gbl("event_pub", "event2"), "etype", msg)) {
        printf("publish error\n");
    }
    osbus_msg_free(msg);
}

int event_pub(int argc, char *argv[])
{
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("event_pub", NULL))) {
        LOGE("bus init");
        return 1;
    }
    if (!osbus_event_register(OSBUS_DEFAULT, osbus_path_gbl("event_pub", "event"))) {
        LOGE("event register");
        return 1;
    }
    // register multiple for test
    if (!osbus_event_register(OSBUS_DEFAULT, osbus_path_gbl("event_pub", "event2"))) {
        LOGE("event register");
        return 1;
    }
    ev_timer mytimer;
    ev_timer_init(&mytimer, ev_periodic_event_pub, 0., 3.); /* note, only repeat used */
    ev_timer_start(EV_DEFAULT, &mytimer); /* start timer */
    ev_run(EV_DEFAULT, 0);
    return 0;
}

bool event_handler(
        osbus_handle_t handle,
        char *path,
        char *name,
        osbus_msg_t *msg,
        void *user_data)
{
    printf("received event: %s type: %s\n", path, name);
    print_msg(__func__, msg);
    printf("user data: %s\n", (char*)user_data);
    return true;
}

int event_sub(int argc, char *argv[])
{
    // connect multiple instances for testing
    osbus_handle_t es1_handle = NULL;
    osbus_handle_t es2_handle = NULL;

    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", __func__, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    if (!osbus_connect(&es1_handle, EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("es1", NULL))) {
        return 1;
    }
    if (!osbus_connect(&es2_handle, EV_DEFAULT, g_opt_bus_type, osbus_path_gbl("es2", NULL))) {
        return 1;
    }

    osbus_event_subscribe(OSBUS_DEFAULT, osbus_path_gbl("event_pub", "event"), event_handler, "<user-data>");
    osbus_event_subscribe(es2_handle, osbus_path_gbl("event_pub", "event2"), event_handler, "<user2>");
    ev_run(EV_DEFAULT, 0);

    if (es1_handle) osbus_disconnect(es1_handle);
    if (es2_handle) osbus_disconnect(es2_handle);
    return 0;
}

int dm_get(int argc, char *argv[])
{
    if (argc < 2) {
        printf("inv param %d\n", argc);
        return 1;
    }
    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", __func__, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_path_t path = osbus_path(OSBUS_NS_GLOBAL, argv[0], argv[1]);
    osbus_msg_t *msg = NULL;
    char path_str[OSBUS_NAME_SIZE];
    osbus_path_fmt(OSBUS_DEFAULT, path, path_str, sizeof(path_str));
    if (!osbus_dm_get(OSBUS_DEFAULT, path, &msg)) {
        printf("error getting %s\n", path_str);
        return 1;
    }
    printf("get %s = ", path_str);
    print_msg(NULL, msg);
    return 0;
}

int dm_set(int argc, char *argv[])
{
    if (argc < 3) {
        printf("inv param %d\n", argc);
        return 1;
    }
    char name[OSBUS_NAME_SIZE];
    snprintf(name, sizeof(name), "%s-%d", __func__, getpid());
    if (!osbus_init_ex(EV_DEFAULT, g_opt_bus_type, osbus_path_gbl(name, NULL))) {
        LOGE("bus init");
        return 1;
    }
    osbus_path_t path = osbus_path(OSBUS_NS_GLOBAL, argv[0], argv[1]);
    osbus_msg_t *msg = NULL;
    char path_str[OSBUS_NAME_SIZE];
    osbus_path_fmt(OSBUS_DEFAULT, path, path_str, sizeof(path_str));
    if (!(msg = osbus_msg_from_json_string(argv[2]))) {
        printf("invalid json value '%s' (use \"\" quotes for strings?)\n", argv[2]);
        return 1;
    }
    if (!osbus_dm_set(OSBUS_DEFAULT, path, msg)) {
        printf("error setting %s\n", path_str);
        return 1;
    }
    printf("set %s = ", path_str);
    print_msg(NULL, msg);
    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    prog_name = basename(argv[0]);
    log_severity_t opt_severity = LOG_SEVERITY_INFO;
    int opt;

    log_open(prog_name, 0);
    while ((opt = getopt(argc, argv, "vur")) != -1) {
        switch (opt) {
            case 'v':
                if (opt_severity < LOG_SEVERITY_TRACE) opt_severity++;
                break;
            case 'u':
                g_opt_bus_type = OSBUS_BUS_TYPE_UBUS;
                break;
            case 'r':
                g_opt_bus_type = OSBUS_BUS_TYPE_RBUS;
                break;
            default:
                usage(prog_name);
        }
    }
    log_severity_set(opt_severity);
    if (optind >= argc) {
        usage(prog_name);
    }

    backtrace_init();
    json_memdbg_init(EV_DEFAULT);

    char *cmd = argv[optind];
    int ac = argc - optind - 1;
    char **av = argv + optind + 1;
    if (!strcmp(cmd, "list")) {
        ret = cli_list(ac, av);
    } else if (!strcmp(cmd, "method_provider") || !strcmp(cmd, "mp")) {
        ret = method_provider(ac, av);
    } else if (!strcmp(cmd, "method_consumer") || !strcmp(cmd, "mc")) {
        ret = method_consumer(ac, av);
    } else if (!strcmp(cmd, "method_async") || !strcmp(cmd, "ma")) {
        ret = method_consumer_async(ac, av);
    } else if (!strcmp(cmd, "topic_listen") || !strcmp(cmd, "tl")) {
        ret = topic_listen(ac, av);
    } else if (!strcmp(cmd, "topic_send") || !strcmp(cmd, "ts")) {
        ret = topic_send(ac, av);
    } else if (!strcmp(cmd, "event_pub") || !strcmp(cmd, "ep")) {
        ret = event_pub(ac, av);
    } else if (!strcmp(cmd, "event_sub") || !strcmp(cmd, "es")) {
        ret = event_sub(ac, av);
    } else if (!strcmp(cmd, "get")) {
        ret = dm_get(ac, av);
    } else if (!strcmp(cmd, "set")) {
        ret = dm_set(ac, av);
    } else {
        usage(prog_name);
    }
    if (osbus_default_handle()) osbus_close();
    printf("done\n");
    return ret;
}

