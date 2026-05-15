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
 * TR-181 Provider Demo Application
 *
 * Demonstrates registering a TR-181 object with parameters:
 *   Device.X_DEMO.Sample.text  - writable string
 *   Device.X_DEMO.Sample.count - read-only counter (increments every 10 seconds)
 *   Device.X_DEMO.Sample.Table.{i}.Name   - writable string per instance
 *   Device.X_DEMO.Sample.Table.{i}.Number - writable integer per instance
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include "os_tr181.h"
#include "log.h"

/* Default object path - can be overridden via command line */
#define DEFAULT_OBJECT "Device.X_DEMO.Sample."
#define MAX_INSTANCES  32

/* Instance data for table entries */
struct table_instance
{
    int instance_num;
    char *name;
    int number;
    int active; /* 1 if instance is allocated, 0 if free */
};

/* Application data */
struct app_data
{
    char *text_value;
    int count_value;
    time_t last_count_update;
    struct table_instance instances[MAX_INSTANCES];
    int next_instance_num;
};

static struct app_data app_state =
        {.text_value = NULL, .count_value = 0, .last_count_update = 0, .instances = {{0}}, .next_instance_num = 1};

static int running = 1;

void signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    running = 0;
}

/* Get callback for .text parameter */
os_tr181_error_t get_text_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    const char *text = data->text_value ? data->text_value : "default text";
    os_tr181_error_t err = os_val_set_str_dup(value, text);

    printf("[GET] %s = %s\n", param_path, text);
    return err;
}

/* Set callback for .text parameter */
os_tr181_error_t set_text_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    const char *str = os_val_get_str(value, NULL);
    if (!str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (data->text_value)
    {
        free(data->text_value);
    }
    data->text_value = strdup(str);
    if (!data->text_value)
    {
        fprintf(stderr, "[ERROR] Failed to allocate memory for text_value\n");
        return OS_TR181_ERROR;
    }

    printf("[SET] %s = %s\n", param_path, str);
    return OS_TR181_SUCCESS;
}

/* Get callback for .count parameter (read-only) */
os_tr181_error_t get_count_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    /* Return current counter value (updated by main loop) */
    os_tr181_error_t err = os_val_set_int(value, data->count_value);

    printf("[GET] %s = %d\n", param_path, data->count_value);
    return err;
}

/* Helper: Find instance by number */
static struct table_instance *find_instance(struct app_data *data, int instance_num)
{
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (data->instances[i].active && data->instances[i].instance_num == instance_num)
        {
            return &data->instances[i];
        }
    }
    return NULL;
}

/* Helper: Find or create instance by number */
static struct table_instance *find_or_create_instance(struct app_data *data, int instance_num)
{
    struct table_instance *inst;

    /* First try to find existing */
    inst = find_instance(data, instance_num);
    if (inst)
    {
        return inst;
    }

    /* Not found, create new one */
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (!data->instances[i].active)
        {
            data->instances[i].instance_num = instance_num;
            data->instances[i].name = strdup("");
            if (!data->instances[i].name)
            {
                return NULL;
            }
            data->instances[i].number = 0;
            data->instances[i].active = 1;
            return &data->instances[i];
        }
    }

    return NULL; /* No free slots */
}

/* Add instance callback */
os_tr181_error_t add_instance_cb(const char *object_path, int *instance_num, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    /* Find free slot */
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (!data->instances[i].active)
        {
            data->instances[i].instance_num = data->next_instance_num;
            data->instances[i].name = strdup("");
            if (!data->instances[i].name)
            {
                return OS_TR181_ERROR;
            }
            data->instances[i].number = 0;
            data->instances[i].active = 1;

            *instance_num = data->next_instance_num;
            data->next_instance_num++;

            printf("[ADD] Instance added: %s%d.\n", object_path, *instance_num);
            return OS_TR181_SUCCESS;
        }
    }

    fprintf(stderr, "[ERROR] No free instance slots\n");
    return OS_TR181_ERROR;
}

/* Delete instance callback */
os_tr181_error_t del_instance_cb(const char *object_path, int instance_num, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    struct table_instance *inst = find_instance(data, instance_num);

    if (!inst)
    {
        fprintf(stderr, "[ERROR] Instance not found: %d\n", instance_num);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    if (inst->name)
    {
        free(inst->name);
    }
    inst->active = 0;

    printf("[DEL] Instance deleted: %s%d.\n", object_path, instance_num);
    return OS_TR181_SUCCESS;
}

/* Get callback for .Table.{i}.Name */
os_tr181_error_t get_name_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;

    LOGT("%s path=%s instance_num=%d", __func__, param_path, instance_num);
    /* If instance_num is 0 or negative, this is a template parameter access
     * Return empty/default value */
    if (instance_num <= 0)
    {
        return os_val_set_str_ref(value, "");
    }

    /* Find or create instance data */
    inst = find_or_create_instance(data, instance_num);
    if (!inst)
    {
        fprintf(stderr, "[ERROR] Cannot create instance data for: %d\n", instance_num);
        return OS_TR181_ERROR;
    }

    os_tr181_error_t err = os_val_set_str_dup(value, inst->name);
    printf("[GET] %s = %s\n", param_path, inst->name);
    return err;
}

/* Set callback for .Table.{i}.Name */
os_tr181_error_t set_name_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;
    const char *str = os_val_get_str(value, NULL);

    if (!str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    LOGT("%s path=%s value=%s instance_num=%d", __func__, param_path, str, instance_num);
    /* Skip template access */
    if (instance_num <= 0)
    {
        return OS_TR181_SUCCESS;
    }

    /* Find or create instance data */
    inst = find_or_create_instance(data, instance_num);
    if (!inst)
    {
        fprintf(stderr, "[ERROR] Cannot create instance data for: %d\n", instance_num);
        return OS_TR181_ERROR;
    }

    if (inst->name)
    {
        free(inst->name);
    }
    inst->name = strdup(str);
    if (!inst->name)
    {
        fprintf(stderr, "[ERROR] Failed to allocate memory for instance name\n");
        return OS_TR181_ERROR;
    }

    printf("[SET] %s = %s\n", param_path, str);
    return OS_TR181_SUCCESS;
}

/* Get callback for .Table.{i}.Number */
os_tr181_error_t get_number_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;

    LOGT("%s path=%s instance_num=%d", __func__, param_path, instance_num);
    /* If instance_num is 0 or negative, this is a template parameter access
     * Return default value */
    if (instance_num <= 0)
    {
        return os_val_set_int(value, 0);
    }

    /* Find or create instance data */
    inst = find_or_create_instance(data, instance_num);
    if (!inst)
    {
        fprintf(stderr, "[ERROR] Cannot create instance data for: %d\n", instance_num);
        return OS_TR181_ERROR;
    }

    os_tr181_error_t err = os_val_set_int(value, inst->number);
    printf("[GET] %s = %d\n", param_path, inst->number);
    return err;
}

/* Set callback for .Table.{i}.Number */
os_tr181_error_t set_number_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;
    int num;

    os_tr181_error_t err = os_val_get_int(value, &num);
    if (err != OS_TR181_SUCCESS)
    {
        return err;
    }

    LOGT("%s path=%s value=%d instance_num=%d", __func__, param_path, num, instance_num);
    /* Skip template access */
    if (instance_num <= 0)
    {
        return OS_TR181_SUCCESS;
    }

    /* Find or create instance data */
    inst = find_or_create_instance(data, instance_num);
    if (!inst)
    {
        fprintf(stderr, "[ERROR] Cannot create instance data for: %d\n", instance_num);
        return OS_TR181_ERROR;
    }

    inst->number = num;

    printf("[SET] %s = %d\n", param_path, num);
    return OS_TR181_SUCCESS;
}

void usage(char *argv[])
{
    fprintf(stderr, "Usage: %s [-v] [-h] [object_path]\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v         Increase verbosity (can be repeated)\n");
    fprintf(stderr, "  -h         Show this help message\n");
    fprintf(stderr, "Example: %s -vv %s\n", argv[0], DEFAULT_OBJECT);
}

int main(int argc, char *argv[])
{
    os_tr181_handle_t *handle = NULL;
    int ret;
    const char *object_path = DEFAULT_OBJECT;
    char text_param[256];
    char count_param[256];
    log_severity_t log_level = LOG_SEVERITY_DEFAULT;
    int opt;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "vh")) != -1)
    {
        switch (opt)
        {
            case 'v':
                if (log_level < LOG_SEVERITY_LAST - 1)
                {
                    log_level++;
                }
                break;
            case 'h':
                usage(argv);
                return 0;
            default:
                usage(argv);
                return 1;
        }
    }

    log_open("TR181_SAMPLE", 0);
    log_severity_set(log_level);

    /* Parse positional arguments */
    if (optind < argc)
    {
        object_path = argv[optind];
        /* Ensure path ends with '.' */
        if (object_path[strlen(object_path) - 1] != '.')
        {
            fprintf(stderr, "Error: Object path must end with '.'\n");
            usage(argv);
            return 1;
        }
    }

    /* Build parameter paths */
    snprintf(text_param, sizeof(text_param), "%stext", object_path);
    snprintf(count_param, sizeof(count_param), "%scount", object_path);

    /* Build table paths */
    char table_path[256];
    char table_name_param[256];
    char table_number_param[256];
    snprintf(table_path, sizeof(table_path), "%sTable.", object_path);
    snprintf(table_name_param, sizeof(table_name_param), "%sTable.{i}.Name", object_path);
    snprintf(table_number_param, sizeof(table_number_param), "%sTable.{i}.Number", object_path);

    printf("TR-181 Registration Demo\n");
    printf("=========================\n\n");

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Print backend info */
    printf("Backend: %s\n", os_tr181_get_backend_name());

    /* Initialize library */
    ret = os_tr181_init(&handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize: %s\n", os_tr181_error_string(ret));
        return 1;
    }
    printf("Library initialized\n\n");

    /* Initialize app state */
    app_state.text_value = strdup("Hello World");
    if (!app_state.text_value)
    {
        fprintf(stderr, "Failed to allocate initial text_value\n");
        os_tr181_close(handle);
        return 1;
    }
    app_state.count_value = 0;
    app_state.last_count_update = time(NULL);

    /* Register object */
    printf("Registering object: %s\n", object_path);
    ret = os_tr181_register_object(handle, object_path);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register object: %s\n", os_tr181_error_string(ret));
        fprintf(stderr, "Hint: Try a custom namespace (e.g., MyApp.Sample.) instead of Device.\n");
        goto cleanup;
    }

    /* Register writable text parameter */
    printf("Registering parameter: %s (writable string)\n", text_param);
    ret = os_tr181_register_parameter(
            handle,
            text_param,
            OS_TR181_TYPE_STRING,
            1,
            get_text_cb,
            set_text_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register text parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register read-only count parameter */
    printf("Registering parameter: %s (read-only int)\n", count_param);
    ret = os_tr181_register_parameter(handle, count_param, OS_TR181_TYPE_INT, 0, get_count_cb, NULL, &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register count parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register table object with add/delete callbacks */
    printf("Registering table: %s\n", table_path);
    ret = os_tr181_register_table(handle, table_path, add_instance_cb, del_instance_cb, &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register table: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register table instance parameters (using {i} placeholder) */
    printf("Registering parameter: %s (writable string)\n", table_name_param);
    ret = os_tr181_register_parameter(
            handle,
            table_name_param,
            OS_TR181_TYPE_STRING,
            1,
            get_name_cb,
            set_name_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Name parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    printf("Registering parameter: %s (writable int)\n", table_number_param);
    ret = os_tr181_register_parameter(
            handle,
            table_number_param,
            OS_TR181_TYPE_INT,
            1,
            get_number_cb,
            set_number_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Number parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Publish all registered objects to the bus */
    printf("\nPublishing objects to bus...\n");
    ret = os_tr181_publish_objects(handle);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to publish objects: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    printf("\nRegistration complete!\n");
    printf("Object '%s' is now accessible via TR-181\n", object_path);
    printf("Parameters:\n");
    printf("  %s - writable string, current value: '%s'\n", text_param, app_state.text_value);
    printf("  %s - read-only counter, increments every 10 seconds\n", count_param);
    printf("  %s - multi-instance table\n", table_path);
    printf("    %s - writable string per instance\n", table_name_param);
    printf("    %s - writable int per instance\n\n", table_number_param);

    printf("Try accessing these parameters from another application:\n");
    printf("  Get: ./os_tr181_sample get %s\n", text_param);
    printf("  Set: ./os_tr181_sample set %s \"new value\"\n", text_param);
    printf("  Get: ./os_tr181_sample get %s\n", count_param);
    printf("  Subscribe: ./os_tr181_sample subscribe %s\n", count_param);
    printf("  Add instance: ./os_tr181_sample add %s\n", table_path);
    printf("  Get instances: ./os_tr181_sample instances %s\n", table_path);
    printf("  Set: ./os_tr181_sample set %sTable.1.Name \"Test\"\n", object_path);
    printf("  Set: ./os_tr181_sample set %sTable.1.Number 42\n", object_path);
    printf("  Delete: ./os_tr181_sample delete %s1.\n\n", table_path);

    printf("Processing requests (press Ctrl+C to exit)...\n");

    /* Process requests loop */
    while (running)
    {
        /* Update counter every 10 seconds and notify subscribers */
        time_t now = time(NULL);
        if (now - app_state.last_count_update >= 10)
        {
            int old_count = app_state.count_value;

            app_state.count_value++;
            app_state.last_count_update = now;

            printf("[UPDATE] Counter incremented to %d (notifying subscribers)\n", app_state.count_value);

            /* Notify subscribers of the change with proper type */
            os_tr181_val_t old_val = OS_VAL_INT(old_count);
            os_tr181_val_t new_val = OS_VAL_INT(app_state.count_value);

            ret = os_tr181_notify_changed(handle, count_param, &old_val, &new_val);
            if (ret != OS_TR181_SUCCESS)
            {
                fprintf(stderr, "Warning: Failed to notify change: %s\n", os_tr181_error_string(ret));
            }
        }

        ret = os_tr181_process_requests(handle, 1000); /* 1 second timeout */
        if (ret != OS_TR181_SUCCESS && ret != OS_TR181_ERROR_TIMEOUT)
        {
            fprintf(stderr, "Error processing requests: %s\n", os_tr181_error_string(ret));
        }
    }

cleanup:
    printf("\nCleaning up...\n");
    if (app_state.text_value)
    {
        free(app_state.text_value);
    }
    /* Free all instance data */
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (app_state.instances[i].active && app_state.instances[i].name)
        {
            free(app_state.instances[i].name);
        }
    }
    if (handle)
    {
        os_tr181_close(handle);
    }
    printf("Done.\n");

    return 0;
}
