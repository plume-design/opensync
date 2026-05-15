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
#include "os_tr181.h"
#include "log.h"

/* Event callback for subscribe command */
static void event_callback(const char *param_path, const os_tr181_val_t *new_value, void *user_data)
{
    char *value_str = NULL;

    printf("\n[EVENT] Parameter changed:\n");
    printf("  Path: %s\n", param_path);

    /* Convert value to string for display */
    if (os_val_to_str(new_value, &value_str) == OS_TR181_SUCCESS)
    {
        printf("  New Value: %s\n", value_str);
        free(value_str);
    }

    printf("  Type: %s\n", os_tr181_type_to_string(new_value->type));
    if (user_data)
    {
        printf("  User Data: %s\n", (char *)user_data);
    }
    fflush(stdout);
}

void print_usage(const char *progname)
{
    printf("Usage: %s [-v] <command> [arguments]\n\n", progname);
    printf("Options:\n");
    printf("  -v                               - Increase verbosity (can be repeated)\n");
    printf("  -r                               - Recursive listing (for 'list' command)\n\n");
    printf("Commands:\n");
    printf("  get <parameter>                  - Get a TR-181 parameter value\n");
    printf("  set <parameter> <value> [type]   - Set a TR-181 parameter value\n");
    printf("  list <path>                      - List parameters under a path\n");
    printf("  subscribe <path> [timeout_sec]   - Subscribe to parameter changes\n");
    printf("  add <object>                     - Add instance to multi-instance object\n");
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
    printf("  %s subscribe Device.WiFi.Radio.1.Enable 60\n", progname);
    printf("  %s subscribe Device.WiFi.Radio.1. 60  # Wildcard - all params under path\n", progname);
    printf("  %s add Device.WiFi.SSID.\n", progname);
    printf("  %s addwait Device.WiFi.SSID. %d\n", progname, OS_TR181_DEFAULT_TIMEOUT_MS / 1000);
    printf("  %s delete Device.WiFi.SSID.3.\n", progname);
    printf("  %s instances Device.WiFi.SSID.\n", progname);
    printf("\nUsing: %s\n", os_tr181_get_backend_name());
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

int cmd_list(os_tr181_handle_t *handle, const char *path, bool recursive)
{
    os_tr181_param_info_t *params = NULL;
    int count = 0;
    int ret;
    int i;

    printf("Listing parameters under: %s%s\n", path, recursive ? " (recursive)" : " (next level only)");

    ret = os_tr181_list(handle, path, recursive, &params, &count);

    if (ret == OS_TR181_SUCCESS)
    {
        if (count > 0)
        {
            printf("Found %d parameter(s):\n", count);
            for (i = 0; i < count; i++)
            {
                printf("  [%d] %s [%s] %s\n",
                       i + 1,
                       params[i].name,
                       os_tr181_type_to_string(params[i].type),
                       params[i].writable ? "(writable)" : "(read-only)");
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
    time_t start_time, current_time;
    int elapsed_sec = 0;
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

    /* Wait for events in a loop */
    start_time = time(NULL);
    while (1)
    {
        /* Calculate remaining timeout */
        current_time = time(NULL);
        elapsed_sec = (int)(current_time - start_time);

        if (timeout_sec > 0 && elapsed_sec >= timeout_sec)
        {
            printf("\nTimeout reached after %d s\n", elapsed_sec);
            break;
        }

        /* Wait for events (use smaller chunks for better responsiveness) */
        int wait_timeout = (timeout_sec > 0) ? ((timeout_sec - elapsed_sec < 1) ? (timeout_sec - elapsed_sec) : 1) : 1;

        ret = os_tr181_wait_event(handle, wait_timeout * 1000);

        if (ret != OS_TR181_SUCCESS && ret != OS_TR181_ERROR_TIMEOUT)
        {
            fprintf(stderr, "Error waiting for events: %s\n", os_tr181_error_string(ret));
            break;
        }
    }

    /* Unsubscribe */
    printf("\nUnsubscribing...\n");
    ret = os_tr181_unsubscribe(handle, sub_handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to unsubscribe: %s\n", os_tr181_error_string(ret));
    }

    return OS_TR181_SUCCESS;
}

int main(int argc, char *argv[])
{
    os_tr181_handle_t *handle = NULL;
    log_severity_t log_level = LOG_SEVERITY_DEFAULT;
    bool recursive = false;
    int ret = 0;
    int opt;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "vr")) != -1)
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
            ret = cmd_list(handle, cmd_argv[1], recursive);
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
        if (cmd_argc < 2)
        {
            fprintf(stderr, "Error: object path required\n");
            print_usage(argv[0]);
            ret = 1;
        }
        else
        {
            int instance_num = 0;
            ret = os_tr181_add_instance(handle, cmd_argv[1], &instance_num);
            if (ret == OS_TR181_SUCCESS)
            {
                printf("Added instance: %d\n", instance_num);
                printf("Full path: %s%d.\n", cmd_argv[1], instance_num);
            }
            else
            {
                fprintf(stderr, "Failed to add instance: %s\n", os_tr181_error_string(ret));
                ret = 1;
            }
        }
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
