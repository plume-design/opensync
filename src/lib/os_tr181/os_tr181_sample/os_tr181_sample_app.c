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

/*
 * TR-181 Wrapper Application
 *
 * Sample application demonstrating the use of os_tr181 wrapper library.
 * The library provides a unified API across different TR-181 backends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/time.h>
#include "os_tr181.h"
#include "os_tr181_val.h"
#include "log.h"
#include <ev.h>

/* Global flag for runtime event loop selection (default: libev) */
static bool use_polling_mode = false; /* false = libev (default), true = manual polling */

/* State for async invoke callback */
struct async_invoke_state
{
    int callback_received;
    os_tr181_error_t status;
    os_tr181_val_t result;
    struct ev_loop *loop; /* Event loop to break when callback received */
};

void print_usage(const char *progname)
{
    printf("Usage: %s [-v] [-r] [-d] [-p] <command> [arguments]\n\n", progname);
    printf("Options:\n");
    printf("  -v                               - Increase verbosity (can be repeated)\n");
    printf("  -r                               - Recursive listing (for 'list' command)\n");
    printf("  -d                               - Fetch detailed type info (slower, for 'list' command)\n");
    printf("  -p                               - Use polling mode instead of libev (default: libev)\n\n");
    printf("Commands:\n");
    printf("  get <parameter>                  - Get a TR-181 parameter value\n");
    printf("  set <parameter> <value> [type]   - Set a TR-181 parameter value\n");
    printf("  list <path>                      - List parameters under a path\n");
    printf("  invoke <method> [json_args]      - Invoke a TR-181 method (synchronous)\n");
    printf("  invoke-async <method> [json_args] [timeout_sec] - Invoke a TR-181 method (asynchronous)\n");
    printf("  subscribe <path> [timeout_sec]   - Subscribe to parameter changes\n");
    printf("  add <object> [index] [alias] [json_values] - Add instance to multi-instance object\n");
    printf("  addwait <object> [timeout_sec]   - Add instance and wait for confirmation\n");
    printf("  delete <instance>                - Delete object instance\n");
    printf("  instances <object>               - List instance numbers\n");
    printf("\nParameter types:\n");
    printf("  string, int, uint, bool, datetime, base64\n");
    printf("\nExamples:\n");
    printf("  %s get Device.DeviceInfo.ModelName\n", progname);
    printf("  %s set Device.WiFi.SSID.1.SSID MyNetwork string\n", progname);
    printf("  %s set Device.WiFi.Radio.1.Enable true bool\n", progname);
    printf("  %s set Device.IP.Diagnostics.IPPing.Host example.com\n", progname);
    printf("  %s list Device.WiFi.SSID.1.\n", progname);
    printf("  %s invoke 'Device.WiFi.Reset()'\n", progname);
    printf("  %s invoke 'Device.X_DEMO.Sample.Reset()' '{\"type\":\"counter\"}'\n", progname);
    printf("  %s invoke-async 'Device.X_DEMO.Sample.DiagnosticsTest()' '{\"test_name\":\"ping\",\"duration\":5}' 30\n",
           progname);
    printf("  %s subscribe Device.WiFi.Radio.1.Enable 60\n", progname);
    printf("  %s subscribe Device.WiFi.Radio.1. 60  # Wildcard - all params under path\n", progname);
    printf("  %s add Device.WiFi.SSID.\n", progname);
    printf("  %s add Device.X_DEMO.Sample.Table. Item1 '{\"Name\":\"test\",\"Number\":42}'\n", progname);
    printf("  %s addwait Device.WiFi.SSID. %d\n", progname, OS_TR181_DEFAULT_TIMEOUT_MS / 1000);
    printf("  %s delete Device.WiFi.SSID.3.\n", progname);
    printf("  %s instances Device.WiFi.SSID.\n", progname);
    printf("\nUsing: %s\n", os_tr181_get_backend_name());
}

/* libev callback for timeout */
static void timeout_cb(EV_P_ ev_timer *w, int revents)
{
    (void)w;
    (void)revents;
    /* Timeout expired - break the event loop */
    ev_break(EV_A_ EVBREAK_ALL);
}

/* libev callback for progress dots */
static void progress_cb(EV_P_ ev_timer *w, int revents)
{
    (void)w;
    (void)revents;
    printf(".");
    fflush(stdout);
}

/* Signal callback for libev - called when SIGINT received */
static void sigint_cb(EV_P_ ev_signal *w, int revents)
{
    (void)w;
    (void)revents;
    printf("\nReceived SIGINT, exiting...\n");
    ev_break(EV_A_ EVBREAK_ALL);
}

/*
 * Process events using libev with optional completion flag and progress dots
 *
 * Parameters:
 *   handle - TR181 library handle
 *   timeout_sec - Maximum time to wait in seconds
 *   state - Optional pointer to async_invoke_state (NULL for timeout-only waiting).
 *           If provided, the async callback will set callback_received AND call ev_break().
 *
 * Returns: 0 on success (state->callback_received set or timeout), 1 on error
 */
static int process_events_with_timeout_libev(
        os_tr181_handle_t *handle,
        int timeout_sec,
        struct async_invoke_state *state)
{
    os_tr181_error_t ret;
    struct ev_loop *loop = EV_DEFAULT;
    ev_timer timeout_watcher;
    ev_timer progress_watcher;
    ev_signal signal_watcher;

    /* Set loop pointer in state so callback can call ev_break() */
    if (state)
    {
        state->loop = loop;
    }

    /* Attach to libev loop */
    ret = os_tr181_attach_loop(handle, loop);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to attach to libev loop: %s\n", os_tr181_error_string(ret));
        return 1;
    }

    /* Set up signal watcher for SIGINT (Ctrl-C) */
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);

    /* Set up timeout timer - will call ev_break() when timeout expires */
    ev_timer_init(&timeout_watcher, timeout_cb, timeout_sec, 0.0);
    ev_timer_start(loop, &timeout_watcher);

    /* Set up progress timer (1 second intervals) */
    ev_timer_init(&progress_watcher, progress_cb, 1.0, 1.0);
    ev_timer_start(loop, &progress_watcher);

    /* Run event loop - will exit when ev_break() is called by:
     * 1. Signal handler (SIGINT)
     * 2. Timeout callback
     * 3. Async invoke callback (if state provided)
     */
    ev_run(loop, 0);

    /* Cleanup */
    ev_signal_stop(loop, &signal_watcher);
    ev_timer_stop(loop, &timeout_watcher);
    ev_timer_stop(loop, &progress_watcher);
    os_tr181_detach_loop(handle);

    /* Clear loop pointer in state if we set it */
    if (state)
    {
        state->loop = NULL;
    }

    /* Check if we exited due to timeout (callback_received not set) */
    if (state && !state->callback_received)
    {
        fprintf(stderr, "\nTimeout after %d seconds\n", timeout_sec);
        return 1;
    }

    return 0;
}

/*
 * Process events using manual polling with optional completion flag and progress dots
 *
 * Parameters:
 *   handle - TR181 library handle
 *   timeout_sec - Maximum time to wait in seconds
 *   state - Optional pointer to async_invoke_state (NULL for timeout-only waiting)
 *
 * Returns: 0 on success (state->callback_received set or timeout), 1 on error
 */
static int process_events_with_timeout_poll(
        os_tr181_handle_t *handle,
        int timeout_sec,
        struct async_invoke_state *state)
{
    os_tr181_error_t ret;
    struct timeval start;
    long last_dot_sec = 0;

    gettimeofday(&start, NULL);

    while (!state || !state->callback_received)
    {
        /* Process events */
        ret = os_tr181_process_requests(handle, 100); /* 100ms poll */
        if (ret != OS_TR181_SUCCESS && ret != OS_TR181_ERROR_TIMEOUT)
        {
            fprintf(stderr, "Error processing requests: %s\n", os_tr181_error_string(ret));
            return 1;
        }

        /* Check timeout */
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_sec = now.tv_sec - start.tv_sec;

        /* Print progress dot every second */
        if (elapsed_sec > last_dot_sec)
        {
            printf(".");
            fflush(stdout);
            last_dot_sec = elapsed_sec;
        }

        if (elapsed_sec >= timeout_sec)
        {
            if (state && !state->callback_received)
            {
                fprintf(stderr, "\nTimeout after %d seconds\n", timeout_sec);
                return 1;
            }
            break;
        }
    }

    return 0;
}

/* Process events with optional completion flag and progress dots */
static int process_events_with_timeout(os_tr181_handle_t *handle, int timeout_sec, struct async_invoke_state *state)
{
    printf("Event loop mode: %s\n", use_polling_mode ? "polling" : "libev");
    if (use_polling_mode)
    {
        return process_events_with_timeout_poll(handle, timeout_sec, state);
    }
    else
    {
        return process_events_with_timeout_libev(handle, timeout_sec, state);
    }
}

/* Event callback for subscribe command */
static void event_callback(const char *path, const os_tr181_val_t *value, void *user_data)
{
    char *value_str = NULL;

    printf("\n[EVENT] Parameter changed:\n");
    printf("  Path: %s\n", path);

    printf("  Type: %s\n", os_tr181_type_to_string(value->type));

    /* Display container values as JSON, scalars as plain string */
    if (os_val_is_container_type(value->type))
    {
        char *json_str = NULL;
        if (os_val_to_json_string(value, &json_str) == OS_TR181_SUCCESS)
        {
            printf("  Value: %s\n", json_str);
            free(json_str);
        }
    }
    else if (os_val_to_str(value, &value_str) == OS_TR181_SUCCESS)
    {
        printf("  Value: %s\n", value_str);
        free(value_str);
    }
    if (user_data)
    {
        printf("  User Data: %s\n", (char *)user_data);
    }
    fflush(stdout);
}

int cmd_get(os_tr181_handle_t *handle, const char *param_name)
{
    os_tr181_val_t val = OS_VAL_INIT();
    char *str_value = NULL;
    os_tr181_error_t ret;

    printf("Getting parameter: %s\n", param_name);

    ret = os_tr181_get_val(handle, param_name, &val);

    if (ret == OS_TR181_SUCCESS)
    {
        /* Convert value to string for display */
        if (os_val_to_str(&val, &str_value) == OS_TR181_SUCCESS)
        {
            printf("Parameter: %s\n", param_name);
            printf("Value: %s\n", str_value);
            printf("Type: %s\n", os_tr181_type_to_string(val.type));
            free(str_value);
        }
        os_val_free(&val);
    }
    else
    {
        fprintf(stderr, "Failed to get parameter: %s\n", os_tr181_error_string(ret));
    }

    return ret;
}

int cmd_set(os_tr181_handle_t *handle, const char *param_name, const char *value, const char *type_str)
{
    int ret;
    os_tr181_param_type_t type = os_tr181_parse_type(type_str);
    os_tr181_val_t val = OS_VAL_INIT();

    printf("Setting parameter: %s = %s (type: %s)\n", param_name, value, os_tr181_type_to_string(type));

    /* Parse string value into os_tr181_val_t with the specified type */
    ret = os_val_from_str(&val, value, type);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to parse value: %s\n", os_tr181_error_string(ret));
        return ret;
    }

    ret = os_tr181_set_val(handle, param_name, &val);
    os_val_free(&val);

    if (ret == OS_TR181_SUCCESS)
    {
        printf("Parameter set successfully\n");
    }
    else
    {
        fprintf(stderr, "Failed to set parameter: %s\n", os_tr181_error_string(ret));
    }

    return ret;
}

static const char *access_flags_to_string(uint32_t access_flags)
{
    /* Check if it's a key */
    if (access_flags & OS_TR181_ACCESS_KEY_FLAG)
    {
        if (access_flags & OS_TR181_ACCESS_WRITE_ONCE_FLAG)
        {
            return "(write-once key)";
        }
        else if (access_flags & OS_TR181_ACCESS_WRITE_FLAG)
        {
            return "(writable key)";
        }
        else
        {
            return "(read-only key)";
        }
    }
    else if (access_flags & OS_TR181_ACCESS_WRITE_FLAG)
    {
        if (access_flags & OS_TR181_ACCESS_WRITE_ONCE_FLAG)
        {
            return "(write-once)"; /* non-standard for non-keys */
        }
        else
        {
            return "(writable)";
        }
    }
    else
    {
        return "(read-only)";
    }
}

int cmd_list(os_tr181_handle_t *handle, const char *path, bool recursive, bool details)
{
    os_tr181_param_info_t *params = NULL;
    int count = 0;
    int ret;
    int i;
    uint32_t flags = 0;

    if (recursive) flags |= OS_TR181_LIST_RECURSIVE;
    if (details) flags |= OS_TR181_LIST_DETAILS;

    printf("Listing parameters under: %s%s%s\n",
           path,
           recursive ? " (recursive)" : " (next level only)",
           details ? " (with details)" : "");

    ret = os_tr181_list(handle, path, flags, &params, &count);

    if (ret == OS_TR181_SUCCESS)
    {
        if (count > 0)
        {
            printf("Found %d parameter(s):\n", count);
            for (i = 0; i < count; i++)
            {
                const char *access_mode = access_flags_to_string(params[i].access_flags);

                printf("  [%d] %s [%s] %s\n",
                       i + 1,
                       params[i].name,
                       os_tr181_type_to_string(params[i].type),
                       access_mode);
            }
            os_tr181_free_list(params, count);
        }
        else
        {
            printf("No parameters found\n");
        }
    }
    else
    {
        fprintf(stderr, "Failed to list parameters: %s\n", os_tr181_error_string(ret));
    }

    return ret;
}

int cmd_subscribe(os_tr181_handle_t *handle, const char *path, int timeout_sec)
{
    int ret;
    char user_data[256];
    os_tr181_sub_handle_t sub_handle = NULL;

    snprintf(user_data, sizeof(user_data), "Subscription for %s", path);

    printf("Subscribing to: %s\n", path);

    /* Check if path ends with '.' for wildcard */
    size_t path_len = strlen(path);
    if (path_len > 0 && path[path_len - 1] == '.')
    {
        printf("Mode: Wildcard (all parameters under path)\n");
    }
    else
    {
        printf("Mode: Specific parameter\n");
    }

    /* Subscribe to parameter changes */
    ret = os_tr181_subscribe(handle, path, event_callback, user_data, &sub_handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to subscribe: %s\n", os_tr181_error_string(ret));
        return ret;
    }

    printf("Timeout: %d s\n", timeout_sec);
    printf("Waiting for events (press Ctrl+C to stop)...\n\n");

    /* Process events until timeout (no done flag - wait full timeout) */
    process_events_with_timeout(handle, timeout_sec, NULL);

    /* Unsubscribe */
    printf("\nUnsubscribing...\n");
    ret = os_tr181_unsubscribe(handle, sub_handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to unsubscribe: %s\n", os_tr181_error_string(ret));
    }

    return OS_TR181_SUCCESS;
}

/* Helper to detect if string is a JSON object structure */
static bool is_str_json_structure(const char *str)
{
    size_t len = strlen(str);
    return (len >= 2 && str[0] == '{' && str[len - 1] == '}');
}

/* Helper to parse optional arguments for cmd_add: [index] [alias] [json_values] in any order */
static int parse_add_optional_args(
        int cmd_argc,
        char **cmd_argv,
        int start_idx,
        uint32_t *index_out,
        const char **alias_out,
        os_tr181_val_t *values_out,
        os_tr181_val_t **values_ptr_out)
{
    *index_out = 0;
    *alias_out = NULL;
    *values_ptr_out = NULL;
    int index_found = 0;
    int alias_found = 0;

    /* Parse all remaining arguments */
    for (int i = start_idx; i < cmd_argc; i++)
    {
        char *arg = cmd_argv[i];

        /* Check if it's JSON (starts with '{') */
        if (is_str_json_structure(arg))
        {
            if (*values_ptr_out != NULL)
            {
                fprintf(stderr, "Error: Duplicate JSON values argument\n");
                return -1;
            }
            if (os_val_from_json_string(values_out, arg) == OS_TR181_SUCCESS)
            {
                *values_ptr_out = values_out;
            }
            else
            {
                fprintf(stderr, "Error: Invalid JSON format: %s\n", arg);
                return -1;
            }
        }
        /* Check if it's all digits (index) */
        else
        {
            char *endptr;
            long num = strtol(arg, &endptr, 10);

            if (*endptr == '\0' && num > 0)
            {
                /* All digits, valid positive number → treat as index */
                if (index_found)
                {
                    fprintf(stderr, "Error: Duplicate index argument\n");
                    return -1;
                }
                *index_out = (uint32_t)num;
                index_found = 1;
            }
            else
            {
                /* Contains non-digits or invalid number → treat as alias */
                if (alias_found)
                {
                    fprintf(stderr, "Error: Duplicate alias argument\n");
                    return -1;
                }
                *alias_out = arg;
                alias_found = 1;
            }
        }
    }

    return 0;
}

int cmd_add(os_tr181_handle_t *handle, int cmd_argc, char **cmd_argv)
{
    os_tr181_error_t ret;

    if (cmd_argc < 2)
    {
        fprintf(stderr, "Error: object path required\n");
        return 1;
    }

    const char *object_path = cmd_argv[1];
    uint32_t index = 0;
    const char *alias_value = NULL;
    os_tr181_val_t values = OS_VAL_INIT();
    os_tr181_val_t *values_ptr = NULL;
    int instance_num = 0;
    int result = 0;

    /* Parse optional arguments: [index] [alias] [json_values] */
    if (parse_add_optional_args(cmd_argc, cmd_argv, 2, &index, &alias_value, &values, &values_ptr) != 0)
    {
        result = 1;
        goto cleanup;
    }

    /* Call _ex API with parsed arguments */
    ret = os_tr181_add_instance_ex(handle, object_path, index, alias_value, values_ptr, &instance_num);

    if (ret == OS_TR181_SUCCESS)
    {
        printf("Added instance: %d\n", instance_num);
        printf("Full path: %s%d.\n", object_path, instance_num);
        if (alias_value)
        {
            printf("Alias: %s\n", alias_value);
        }
    }
    else
    {
        fprintf(stderr, "Failed to add instance: %s\n", os_tr181_error_string(ret));
        result = 1;
    }

cleanup:
    os_val_free(&values);
    return result;
}

int cmd_invoke(os_tr181_handle_t *handle, const char *method_path, const char *json_args)
{
    os_tr181_val_t args = OS_VAL_INIT();
    os_tr181_val_t *args_ptr = NULL;
    os_tr181_val_t result = OS_VAL_INIT();
    os_tr181_error_t ret;
    char *result_json = NULL;

    printf("Invoking method: %s\n", method_path);

    /* Parse arguments if provided */
    if (json_args != NULL && strlen(json_args) > 0)
    {
        ret = os_val_from_json_string(&args, json_args);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to parse JSON arguments: %s\n", os_tr181_error_string(ret));
            return 1;
        }
        printf("Arguments: %s\n", json_args);
        args_ptr = &args;
    }
    else
    {
        printf("No arguments\n");
    }

    /* Invoke the method */
    ret = os_tr181_invoke(handle, method_path, args_ptr, &result, 0);

    /* Free arguments */
    if (args_ptr != NULL)
    {
        os_val_free(&args);
    }

    if (ret == OS_TR181_SUCCESS)
    {
        printf("Method invoked successfully\n");

        /* Convert result to JSON and print */
        ret = os_val_to_json_string(&result, &result_json);
        if (ret == OS_TR181_SUCCESS && result_json != NULL)
        {
            printf("Result:\n%s\n", result_json);
            free(result_json);
        }
        else
        {
            fprintf(stderr, "Failed to convert result to JSON\n");
        }
        os_val_free(&result);
        return 0;
    }
    else
    {
        fprintf(stderr, "Failed to invoke method: %s\n", os_tr181_error_string(ret));
        return 1;
    }
}

/* Async invoke callback */
static void async_invoke_callback(
        const char *method_name,
        os_tr181_error_t status,
        const os_tr181_val_t *result,
        void *user_data)
{
    struct async_invoke_state *state = (struct async_invoke_state *)user_data;

    printf("\n[ASYNC] Response received for method: %s\n", method_name);

    state->status = status;
    state->callback_received = 1;

    if (status == OS_TR181_SUCCESS && result)
    {
        /* Copy result for display in main function */
        os_val_copy(&state->result, result);
    }

    /* Break the event loop if we're in libev mode */
    if (state->loop)
    {
        ev_break(state->loop, EVBREAK_ALL);
    }
}

int cmd_invoke_async(os_tr181_handle_t *handle, const char *method_path, const char *json_args, int timeout_sec)
{
    os_tr181_val_t args = OS_VAL_INIT();
    os_tr181_val_t *args_ptr = NULL;
    os_tr181_error_t ret;
    char *result_json = NULL;
    struct async_invoke_state state = {0};

    if (timeout_sec <= 0)
    {
        timeout_sec = 30; /* Default 30 seconds */
    }

    printf("Invoking method (async): %s\n", method_path);

    /* Parse arguments if provided */
    if (json_args != NULL && strlen(json_args) > 0)
    {
        ret = os_val_from_json_string(&args, json_args);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to parse JSON arguments: %s\n", os_tr181_error_string(ret));
            return 1;
        }
        printf("Arguments: %s\n", json_args);
        args_ptr = &args;
    }
    else
    {
        printf("No arguments\n");
    }

    printf("Timeout: %d seconds\n", timeout_sec);

    /* Invoke the method asynchronously */
    ret = os_tr181_invoke_async(handle, method_path, args_ptr, async_invoke_callback, &state, timeout_sec, NULL);

    /* Free arguments */
    if (args_ptr != NULL)
    {
        os_val_free(&args);
    }

    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to invoke method (async): %s\n", os_tr181_error_string(ret));
        return 1;
    }

    printf("Method invoked successfully (async)\n");
    printf("Waiting for response...\n");

    struct timeval start;
    gettimeofday(&start, NULL);

    /* Process events until callback received or timeout */
    if (process_events_with_timeout(handle, timeout_sec, &state) != 0)
    {
        return 1; /* Timeout or error */
    }

    /* Calculate elapsed time in milliseconds */
    struct timeval now;
    gettimeofday(&now, NULL);
    long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;

    /* Display result */
    if (state.status == OS_TR181_SUCCESS)
    {
        printf("\nAsync response received (after %ld ms):\n", elapsed_ms);
        printf("  Status: SUCCESS\n");

        /* Convert result to JSON and print */
        ret = os_val_to_json_string(&state.result, &result_json);
        if (ret == OS_TR181_SUCCESS && result_json != NULL)
        {
            printf("  Result: %s\n", result_json);
            free(result_json);
        }

        os_val_free(&state.result);
        return 0;
    }
    else
    {
        printf("\nAsync response received with error (after %ld ms):\n", elapsed_ms);
        printf("  Status: %s\n", os_tr181_error_string(state.status));
        os_val_free(&state.result);
        return 1;
    }
}

int main(int argc, char *argv[])
{
    os_tr181_handle_t *handle = NULL;
    log_severity_t log_level = LOG_SEVERITY_DEFAULT;
    bool recursive = false;
    bool details = false;
    int ret = 0;
    int opt;

    /* Parse command line options */
    while ((opt = getopt_long(argc, argv, "vrdp", NULL, NULL)) != -1)
    {
        switch (opt)
        {
            case 'v':
                if (log_level < LOG_SEVERITY_LAST - 1)
                {
                    log_level++;
                }
                break;
            case 'r':
                recursive = true;
                break;
            case 'd':
                details = true;
                break;
            case 'p':
                use_polling_mode = true;
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    log_open("TR181_SAMPLE", 0);
    log_severity_set(log_level);

    /* Check if we have a command after options */
    if (optind >= argc)
    {
        print_usage(argv[0]);
        return 1;
    }

    /* Initialize library */
    ret = os_tr181_init(&handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize TR-181 library: %s\n", os_tr181_error_string(ret));
        return 1;
    }

    /* Process command - shift argv to point at command after options */
    char **cmd_argv = &argv[optind];
    int cmd_argc = argc - optind;

    if (strcmp(cmd_argv[0], "get") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: parameter name required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            ret = cmd_get(handle, cmd_argv[1]);
        }
    }
    else if (strcmp(cmd_argv[0], "set") == 0)
    {
        if (cmd_argc < 3)
        {
            fprintf(stderr, "Error: parameter name and value required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            const char *type = (cmd_argc >= 4) ? cmd_argv[3] : NULL;
            ret = cmd_set(handle, cmd_argv[1], cmd_argv[2], type);
        }
    }
    else if (strcmp(cmd_argv[0], "list") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            ret = cmd_list(handle, cmd_argv[1], recursive, details);
        }
    }
    else if (strcmp(cmd_argv[0], "invoke") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: method path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            const char *json_args = (cmd_argc >= 3) ? cmd_argv[2] : NULL;
            ret = cmd_invoke(handle, cmd_argv[1], json_args);
        }
    }
    else if (strcmp(cmd_argv[0], "invoke-async") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: method path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            const char *json_args = (cmd_argc >= 3) ? cmd_argv[2] : NULL;
            int timeout = (cmd_argc >= 4) ? atoi(cmd_argv[3]) : 30; /* Default 30 seconds */
            ret = cmd_invoke_async(handle, cmd_argv[1], json_args, timeout);
        }
    }
    else if (strcmp(cmd_argv[0], "subscribe") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            int timeout = (cmd_argc >= 3) ? atoi(cmd_argv[2]) : 60; /* Default 60 seconds */
            ret = cmd_subscribe(handle, cmd_argv[1], timeout);
        }
    }
    else if (strcmp(cmd_argv[0], "add") == 0)
    {
        ret = cmd_add(handle, cmd_argc, cmd_argv);
    }
    else if (strcmp(cmd_argv[0], "addwait") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: object path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            int instance_num = 0;
            int timeout = (cmd_argc >= 3) ? atoi(cmd_argv[2]) : OS_TR181_DEFAULT_TIMEOUT_MS / 1000;

            printf("Adding instance and waiting for confirmation (timeout: %d s)...\n", timeout);
            ret = os_tr181_add_instance_wait(handle, cmd_argv[1], &instance_num, timeout * 1000);

            if (ret == OS_TR181_SUCCESS)
            {
                printf("Instance created and confirmed: %d\n", instance_num);
                printf("Full path: %s%d.\n", cmd_argv[1], instance_num);
            }
            else if (ret == OS_TR181_ERROR_TIMEOUT)
            {
                fprintf(stderr, "Timeout: Instance may have been created but not confirmed within %d ms\n", timeout);
                ret = 1;
            }
            else
            {
                fprintf(stderr, "Failed to add instance: %s\n", os_tr181_error_string(ret));
                ret = 1;
            }
        }
    }
    else if (strcmp(cmd_argv[0], "delete") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: instance path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            ret = os_tr181_delete_instance(handle, cmd_argv[1]);
            if (ret == OS_TR181_SUCCESS)
            {
                printf("Deleted instance: %s\n", cmd_argv[1]);
            }
            else
            {
                fprintf(stderr, "Failed to delete instance: %s\n", os_tr181_error_string(ret));
                ret = 1;
            }
        }
    }
    else if (strcmp(cmd_argv[0], "instances") == 0)
    {
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: object path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            int *instances = NULL;
            int count = 0;
            ret = os_tr181_get_instances(handle, cmd_argv[1], &instances, &count);
            if (ret == OS_TR181_SUCCESS)
            {
                os_tr181_sort_instances(instances, count);
                printf("Found %d instance(s) under %s:\n", count, cmd_argv[1]);
                for (int i = 0; i < count; i++)
                {
                    printf("  %d\n", instances[i]);
                }
                free(instances);
            }
            else
            {
                fprintf(stderr, "Failed to get instances: %s\n", os_tr181_error_string(ret));
                ret = 1;
            }
        }
    }
    else
    {
        fprintf(stderr, "Error: unknown command '%s'\n", cmd_argv[0]);
        print_usage(argv[0]);
        ret = 1;
    }

    /* Cleanup */
    os_tr181_close(handle);

    return (ret == OS_TR181_SUCCESS) ? 0 : 1;
}
