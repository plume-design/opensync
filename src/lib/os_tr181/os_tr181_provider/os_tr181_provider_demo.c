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
 *   Device.X_DEMO.Sample.count - read-only counter (increments at configurable interval)
 *   Device.X_DEMO.Sample.Enable - writable boolean
 *   Device.X_DEMO.Sample.Table.{i}.Alias  - write-once key (unique identifier)
 *   Device.X_DEMO.Sample.Table.{i}.Name   - writable string per instance
 *   Device.X_DEMO.Sample.Table.{i}.Number - writable integer per instance
 *   Device.X_DEMO.Sample.Table.{i}.Numbers.{i}.Value - read-only integer per (sub)instance
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/time.h>
#include <ev.h>
#include "os_tr181.h"
#include "log.h"

/* Default object path - can be overridden via command line */
#define DEFAULT_OBJECT "Device.X_DEMO.Sample."
#define SECOND_OBJECT  "Device.X_SECOND."
#define MAX_INSTANCES  32

/* USP event emitted each time the counter increments */
#define DEMO_EVENT "Device.X_DEMO.Sample.CounterUpdate!"

/* USP event emitted each time a Table instance Name parameter changes */
#define DEMO_TABLE_EVENT_SUFFIX "Table.{i}.TableEvent!"

#define DEFAULT_DIAGNOSTICS_DURATION_SEC 5
#define DEFAULT_COUNT_UPDATE_INTERVAL_MS 10000

/* Global flag for runtime event loop selection (default: libev) */
static bool use_polling_mode = false; /* false = libev (default), true = manual polling */

/* Instance data for table entries */
struct table_instance
{
    int instance_num;
    char alias[64]; /* Alias key parameter (write-once) */
    char *name;
    int number;
    int active; /* 1 if instance is allocated, 0 if free */
};

/* Pending async diagnostics test */
struct pending_test
{
    os_tr181_async_method_ctx_t *async_ctx;
    struct timeval complete_time; /* when to complete */
    struct timeval start_time;    /* when started */
    char *test_name;
    struct pending_test *next;
    ev_timer timer; /* one-shot timer for libev mode */
};

/* Application data */
struct app_data
{
    char *text_value;
    char *second_txt_value;
    int num_value;
    int count_value;
    bool enable_value;
    uint64_t last_count_update_msec;
    struct table_instance instances[MAX_INSTANCES];
    struct pending_test *pending_tests;  /* List of pending async diagnostics tests */
    struct ev_loop *ev_loop;             /* Non-NULL in libev mode, NULL in polling mode */
    os_tr181_handle_t *handle;           /* Library handle, set after init (for event emission) */
    char table_event[OS_TR181_PATH_MAX]; /* Table event template path */
};

static struct app_data app_state = {
    .text_value = NULL,
    .count_value = 0,
    .enable_value = false,
    .last_count_update_msec = 0,
    .instances = {{0}},
    .pending_tests = NULL,
    .ev_loop = NULL};

static int running = 1;

/* Context for libev periodic timer callback */
struct periodic_timer_ctx
{
    os_tr181_handle_t *handle;
    const char *count_param;
    int count_update_interval;
};

/* Forward declarations */
static void send_test_response(struct pending_test *test);
static void test_timer_cb(EV_P_ ev_timer *w, int revents);
static void process_pending_tests(void);
static void update_counter(os_tr181_handle_t *handle, const char *count_param, uint64_t count_update_interval);
static struct table_instance *find_or_create_instance(struct app_data *data, int instance_num);

void usage(char *argv[])
{
    fprintf(stderr, "Usage: %s [-v] [-h] [-p] [-i MILLISECONDS] [object_path [second_object_path]]\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v         Increase verbosity (can be repeated)\n");
    fprintf(stderr, "  -h         Show this help message\n");
    fprintf(stderr, "  -p         Use polling mode instead of libev (default: libev)\n");
    fprintf(stderr,
            "  -i MILLISECONDS Update interval for count parameter (default: %d, 0 to disable)\n",
            DEFAULT_COUNT_UPDATE_INTERVAL_MS);
    fprintf(stderr, "Example: %s -vv %s\n", argv[0], DEFAULT_OBJECT);
    fprintf(stderr, "Example: %s -vv %s %s\n", argv[0], DEFAULT_OBJECT, SECOND_OBJECT);
}

void signal_handler(int signum)
{
    printf("\nReceived signal %d, shutting down...\n", signum);
    running = 0;
}

uint64_t time_ms()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_usec / 1000;
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

/* Get callback for second object's .txt parameter */
os_tr181_error_t get_second_txt_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    const char *txt = data->second_txt_value ? data->second_txt_value : "";
    printf("[GET] %s = %s\n", param_path, txt);
    return os_val_set_str_dup(value, txt);
}

/* Set callback for second object's .txt parameter */
os_tr181_error_t set_second_txt_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    const char *str = os_val_get_str(value, NULL);
    if (!str)
    {
        return OS_TR181_ERROR_INVALID;
    }
    free(data->second_txt_value);
    data->second_txt_value = strdup(str);
    if (!data->second_txt_value)
    {
        return OS_TR181_ERROR;
    }
    printf("[SET] %s = %s\n", param_path, str);
    return OS_TR181_SUCCESS;
}

/* Get callback for .num parameter */
os_tr181_error_t get_num_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    os_tr181_error_t err = os_val_set_int(value, data->num_value);
    printf("[GET] %s = %d\n", param_path, data->num_value);
    return err;
}

/* Set callback for .num parameter */
os_tr181_error_t set_num_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    data->num_value = os_val_get_int_or(value, 0);
    printf("[SET] %s = %d\n", param_path, data->num_value);
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

/* Get callback for .Enable parameter */
os_tr181_error_t get_enable_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    os_tr181_error_t err = os_val_set_bool(value, data->enable_value);

    printf("[GET] %s = %s\n", param_path, data->enable_value ? "true" : "false");
    return err;
}

/* Set callback for .Enable parameter */
os_tr181_error_t set_enable_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;

    bool enable_val;
    os_tr181_error_t err = os_val_get_bool(value, &enable_val);
    if (err != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "[SET] %s - Failed to get bool value: %s\n", param_path, os_tr181_error_string(err));
        return OS_TR181_ERROR_INVALID;
    }

    data->enable_value = enable_val;

    printf("[SET] %s = %s\n", param_path, enable_val ? "true" : "false");
    return OS_TR181_SUCCESS;
}

/* Get callback for Table.{i}.Alias parameter (write-once key) */
os_tr181_error_t get_alias_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;

    LOGT("%s path=%s instance_num=%d", __func__, param_path, instance_num);

    /* If instance_num is 0 or negative, this is a template parameter access */
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

    printf("[GET] %s = %s\n", param_path, inst->alias);
    return os_val_set_str_ref(value, inst->alias);
}

/* Set callback for Table.{i}.Alias parameter (write-once key) */
os_tr181_error_t set_alias_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    struct table_instance *inst;
    os_tr181_error_t err;
    const char *str_val;

    LOGT("%s path=%s instance_num=%d", __func__, param_path, instance_num);

    if (instance_num <= 0)
    {
        return OS_TR181_ERROR_INVALID;
    }

    inst = find_or_create_instance(data, instance_num);
    if (!inst)
    {
        fprintf(stderr, "[ERROR] Cannot create instance data for: %d\n", instance_num);
        return OS_TR181_ERROR;
    }

    str_val = os_val_get_str(value, &err);
    if (err != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "[SET] %s - Failed to get string value: %s\n", param_path, os_tr181_error_string(err));
        return OS_TR181_ERROR_INVALID;
    }

    /* Store alias value - write-once enforcement handled by platform */
    snprintf(inst->alias, sizeof(inst->alias), "%s", str_val);
    printf("[SET] %s = %s\n", param_path, str_val);
    return OS_TR181_SUCCESS;
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
            data->instances[i].alias[0] = '\0'; /* Initialize empty alias */
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
os_tr181_error_t add_instance_cb(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    char *json_str = NULL;

    /* Instance number is pre-assigned by the library */
    printf("[ADD] instance: %s%d.\n", object_path, instance_num);

    if (initial_values != NULL)
    {
        if (os_val_to_json_string(initial_values, &json_str) != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "[ERROR] Failed to convert initial values to JSON string\n");
        }
        else
        {
            printf("[ADD] Initial values for new instance %d: %s\n", instance_num, json_str);
            free(json_str);
        }
    }

    /* Check if instance already exists */
    if (find_instance(data, instance_num))
    {
        fprintf(stderr, "[ERROR] Instance %d already exists\n", instance_num);
        return OS_TR181_ERROR;
    }

    /* Find free slot */
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        if (!data->instances[i].active)
        {
            data->instances[i].instance_num = instance_num;
            data->instances[i].name = strdup("");
            if (!data->instances[i].name)
            {
                return OS_TR181_ERROR;
            }
            data->instances[i].number = 0;
            data->instances[i].active = 1;

            return OS_TR181_SUCCESS;
        }
    }

    fprintf(stderr, "[ERROR] Cannot add instance: Table limit reached (%d instances max)\n", MAX_INSTANCES);
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

/* Add subinstance callback for .Table.{i}.Numbers.{i} */
os_tr181_error_t add_subinstance_cb(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data)
{
    (void)object_path;
    (void)initial_values;
    (void)user_data;
    (void)instance_num;

    return OS_TR181_SUCCESS;
}

/* Delete subinstance callback for .Table.{i}.Numbers.{i} */
os_tr181_error_t del_subinstance_cb(const char *object_path, int instance_num, void *user_data)
{
    (void)object_path;
    (void)user_data;
    (void)instance_num;

    return OS_TR181_SUCCESS;
}

/* The .Table.{i}.Numbers.{i}.Value is merely echoing the instance number for
 * demonstration purposes of nesting alone. */
os_tr181_error_t get_subinst_value_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    (void)user_data;
    int instance_num = os_tr181_parse_instance(param_path);
    return os_val_set_int(value, instance_num);
}

/* MethodA — registered on Table.{i}.Numbers.{i}.MethodA() */
os_tr181_error_t table_numbers_method_a_cb(
        os_tr181_handle_t *handle,
        const char *method_path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    (void)handle;
    (void)args;
    (void)result;
    (void)async_ctx;
    (void)user_data;

    printf("[METHOD] %s invoked (instance method on nested table)\n", method_path);
    return OS_TR181_SUCCESS;
}

/* MethodB — registered on Table.{i}.Numbers.{i}.ObjectB.MethodB() */
os_tr181_error_t table_numbers_objectb_method_b_cb(
        os_tr181_handle_t *handle,
        const char *method_path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    (void)handle;
    (void)args;
    (void)result;
    (void)async_ctx;
    (void)user_data;

    printf("[METHOD] %s invoked (method on singleton child of instance)\n", method_path);
    return OS_TR181_SUCCESS;
}

/* Get callback for .Table.{i}.Name */
os_tr181_error_t get_inst_name_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
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
os_tr181_error_t set_inst_name_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
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

    /* Emit TableEvent! for this specific instance */
    if (data->handle && data->table_event[0])
    {
        /* Build concrete event path: substitute {i} with actual instance number */
        char event_path[OS_TR181_PATH_MAX];
        const char *tmpl = data->table_event;
        const char *placeholder = strstr(tmpl, "{i}");
        if (placeholder)
        {
            snprintf(
                    event_path,
                    sizeof(event_path),
                    "%.*s%d%s",
                    (int)(placeholder - tmpl),
                    tmpl,
                    instance_num,
                    placeholder + 3); /* skip "{i}" */
        }
        else
        {
            snprintf(event_path, sizeof(event_path), "%s", tmpl);
        }

        os_tr181_val_t event_data = OS_VAL_INIT();
        os_val_set_dict(&event_data);
        os_val_dict_set_string(&event_data, "name", str);
        os_val_dict_set_int(&event_data, "instance", instance_num);

        os_tr181_error_t emit_ret = os_tr181_emit_event(data->handle, event_path, &event_data);
        if (emit_ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Warning: Failed to emit %s: %s\n", event_path, os_tr181_error_string(emit_ret));
        }
        os_val_free(&event_data);
    }

    return OS_TR181_SUCCESS;
}

/* Get callback for .Table.{i}.Number */
os_tr181_error_t get_inst_number_cb(const char *param_path, os_tr181_val_t *value, void *user_data)
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
os_tr181_error_t set_inst_number_cb(const char *param_path, const os_tr181_val_t *value, void *user_data)
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

/* Method callback for .Reset() */
os_tr181_error_t reset_method_cb(
        os_tr181_handle_t *handle,
        const char *method_path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    const char *reset_type = "full"; /* Default to full reset */
    bool has_type_param = false;

    (void)handle;    /* Unused for now */
    (void)async_ctx; /* Unused for now - sync response only */

    printf("[METHOD] %s invoked\n", method_path);

    /* Check if arguments provided */
    if (args && args->type == OS_TR181_TYPE_DICT)
    {
        /* Check for 'type' parameter */
        const os_tr181_val_t *type_val = os_val_dict_get(args, "type");
        if (type_val)
        {
            const char *type_str = os_val_get_str(type_val, NULL);
            if (type_str)
            {
                has_type_param = true;
                reset_type = type_str;
                printf("[METHOD]   type = %s\n", reset_type);

                /* Validate type parameter */
                if (strcmp(reset_type, "counter") != 0 && strcmp(reset_type, "text") != 0
                    && strcmp(reset_type, "full") != 0)
                {
                    printf("[METHOD]   ERROR: Invalid type '%s' (must be 'counter', 'text', or 'full')\n", reset_type);
                    return OS_TR181_ERROR_INVALID;
                }
            }
        }

        /* Check for unexpected parameters */
        if (os_val_dict_size(args) > (has_type_param ? 1 : 0))
        {
            printf("[METHOD]   ERROR: Unexpected parameters (only 'type' is accepted)\n");
            return OS_TR181_ERROR_INVALID;
        }
    }

    /* Perform reset based on type */
    if (strcmp(reset_type, "counter") == 0)
    {
        /* Reset only counter */
        data->count_value = 0;
        data->last_count_update_msec = time_ms();
        printf("[METHOD]   Counter reset to 0\n");
    }
    else if (strcmp(reset_type, "text") == 0)
    {
        /* Reset only text */
        if (data->text_value)
        {
            free(data->text_value);
        }
        data->text_value = strdup("Hello World");
        printf("[METHOD]   Text reset to default\n");
    }
    else /* "full" or any other value */
    {
        /* Full reset - both counter and text */
        data->count_value = 0;
        data->last_count_update_msec = time_ms();
        if (data->text_value)
        {
            free(data->text_value);
        }
        data->text_value = strdup("Hello World");
        printf("[METHOD]   Full reset performed\n");
    }

    /* Build result dictionary */
    os_val_set_dict(result);

    os_tr181_val_t status_val = OS_VAL_INIT();
    os_val_set_str_dup(&status_val, "success");
    os_val_dict_set(result, "status", &status_val);
    os_val_free(&status_val);

    os_tr181_val_t type_val = OS_VAL_INIT();
    os_val_set_str_dup(&type_val, reset_type);
    os_val_dict_set(result, "reset_type", &type_val);
    os_val_free(&type_val);

    os_tr181_val_t count_val = OS_VAL_INIT();
    os_val_set_int(&count_val, data->count_value);
    os_val_dict_set(result, "count", &count_val);
    os_val_free(&count_val);

    os_tr181_val_t text_val = OS_VAL_INIT();
    os_val_set_str_dup(&text_val, data->text_value);
    os_val_dict_set(result, "text", &text_val);
    os_val_free(&text_val);

    printf("[METHOD]   Result: status=success, reset_type=%s\n", reset_type);
    return OS_TR181_SUCCESS;
}

/* Method callback for .DiagnosticsTest() - demonstrates async response */
os_tr181_error_t diagnostics_test_method_cb(
        os_tr181_handle_t *handle,
        const char *method_path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    struct app_data *data = (struct app_data *)user_data;
    const char *test_name = "generic";
    int duration_sec = DEFAULT_DIAGNOSTICS_DURATION_SEC;

    (void)handle; /* Unused */
    (void)result; /* Not used for async response */

    printf("[METHOD] %s invoked (async)\n", method_path);

    /* Parse arguments */
    if (args && args->type == OS_TR181_TYPE_DICT)
    {
        /* Get optional test_name */
        const os_tr181_val_t *name_val = os_val_dict_get(args, "test_name");
        if (name_val)
        {
            const char *name_str = os_val_get_str(name_val, NULL);
            if (name_str)
            {
                test_name = name_str;
                printf("[METHOD]   test_name = %s\n", test_name);
            }
        }

        /* Get optional duration */
        const os_tr181_val_t *dur_val = os_val_dict_get(args, "duration");
        if (dur_val)
        {
            int dur = 0;
            os_tr181_error_t err = os_val_get_int(dur_val, &dur);
            if (err == OS_TR181_SUCCESS && dur > 0 && dur <= 300) /* Limit to 5 minutes max */
            {
                duration_sec = dur;
                printf("[METHOD]   duration = %d seconds\n", duration_sec);
            }
            else
            {
                printf("[METHOD]   WARNING: Invalid duration, using default %d\n", DEFAULT_DIAGNOSTICS_DURATION_SEC);
            }
        }
    }

    /* Create pending test entry */
    struct pending_test *test = calloc(1, sizeof(struct pending_test));
    if (!test)
    {
        fprintf(stderr, "[METHOD]   ERROR: Failed to allocate pending test\n");
        return OS_TR181_ERROR;
    }

    test->async_ctx = async_ctx;
    test->test_name = strdup(test_name);
    if (!test->test_name)
    {
        fprintf(stderr, "[METHOD]   ERROR: Failed to duplicate test name\n");
        free(test);
        return OS_TR181_ERROR;
    }

    /* Calculate completion time */
    gettimeofday(&test->start_time, NULL);
    test->complete_time = test->start_time;
    test->complete_time.tv_sec += duration_sec;

    /* Add to pending list */
    test->next = data->pending_tests;
    data->pending_tests = test;

    /* In libev mode, start a one-shot timer for precise completion */
    if (app_state.ev_loop)
    {
        ev_timer_init(&test->timer, test_timer_cb, (double)duration_sec, 0.0);
        test->timer.data = test;
        ev_timer_start(app_state.ev_loop, &test->timer);
    }

    printf("[METHOD]   Test scheduled to complete in %d seconds\n", duration_sec);
    printf("[METHOD]   Returning DEFERRED - response will be sent later\n");

    /* Return DEFERRED to indicate async response */
    return OS_TR181_ERROR_DEFERRED;
}

/* Send async response for a completed diagnostics test (does not free the test) */
static void send_test_response(struct pending_test *test)
{
    struct timeval now;
    gettimeofday(&now, NULL);

    long elapsed_ms = (now.tv_sec - test->start_time.tv_sec) * 1000 + (now.tv_usec - test->start_time.tv_usec) / 1000;

    printf("[ASYNC] Completing diagnostics test '%s' (elapsed %ld ms)\n", test->test_name, elapsed_ms);

    /* Build result dictionary */
    os_tr181_val_t result = OS_VAL_INIT();
    os_val_set_dict(&result);

    os_tr181_val_t status_val = OS_VAL_INIT();
    os_val_set_str_dup(&status_val, "complete");
    os_val_dict_set(&result, "status", &status_val);
    os_val_free(&status_val);

    os_tr181_val_t elapsed_val = OS_VAL_INIT();
    os_val_set_int(&elapsed_val, (int)elapsed_ms);
    os_val_dict_set(&result, "elapsed_time_ms", &elapsed_val);
    os_val_free(&elapsed_val);

    os_tr181_val_t result_val = OS_VAL_INIT();
    os_val_set_str_dup(&result_val, "passed");
    os_val_dict_set(&result, "result", &result_val);
    os_val_free(&result_val);

    os_tr181_val_t name_val = OS_VAL_INIT();
    os_val_set_str_dup(&name_val, test->test_name);
    os_val_dict_set(&result, "test_name", &name_val);
    os_val_free(&name_val);

    /* Send async response */
    os_tr181_error_t ret = os_tr181_method_respond(test->async_ctx, OS_TR181_SUCCESS, &result);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "[ASYNC]   WARNING: Failed to send async response: %s\n", os_tr181_error_string(ret));
    }
    else
    {
        printf("[ASYNC]   Response sent successfully\n");
    }

    os_val_free(&result);
}

/* libev one-shot timer callback: fires when a test's duration has elapsed */
static void test_timer_cb(EV_P_ ev_timer *w, int revents)
{
    (void)EV_A;
    (void)revents;

    struct pending_test *test = (struct pending_test *)w->data;

    /* Remove from pending list */
    struct pending_test **pp = &app_state.pending_tests;
    while (*pp && *pp != test)
        pp = &(*pp)->next;
    if (*pp) *pp = test->next;

    send_test_response(test);
    free(test->test_name);
    free(test);
}

/* Process pending async diagnostics tests and complete them when ready (polling mode) */
static void process_pending_tests(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);

    struct pending_test **pp = &app_state.pending_tests;
    while (*pp)
    {
        struct pending_test *test = *pp;

        /* Check if test is complete */
        if (now.tv_sec > test->complete_time.tv_sec
            || (now.tv_sec == test->complete_time.tv_sec && now.tv_usec >= test->complete_time.tv_usec))
        {
            send_test_response(test);

            /* Remove from list and free */
            *pp = test->next;
            free(test->test_name);
            free(test);
        }
        else
        {
            pp = &test->next;
        }
    }
}

/* Update counter at configured interval and notify subscribers */
static void update_counter(os_tr181_handle_t *handle, const char *count_param, uint64_t count_update_interval)
{
    if (count_update_interval <= 0)
    {
        return;
    }

    int64_t now_ms = time_ms();
    if (now_ms - app_state.last_count_update_msec >= count_update_interval)
    {
        int old_count = app_state.count_value;

        app_state.count_value++;
        app_state.last_count_update_msec = now_ms;

        printf("[UPDATE] Counter incremented to %d (notifying subscribers)\n", app_state.count_value);

        /* Notify subscribers of the parameter value change */
        os_tr181_val_t old_val = OS_VAL_INT(old_count);
        os_tr181_val_t new_val = OS_VAL_INT(app_state.count_value);

        os_tr181_error_t ret = os_tr181_notify_changed(handle, count_param, &old_val, &new_val);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Warning: Failed to notify change: %s\n", os_tr181_error_string(ret));
        }

        /* Emit USP event with the new counter value */
        os_tr181_val_t event_data = OS_VAL_INIT();
        os_val_set_dict(&event_data);
        os_val_dict_set_int(&event_data, "count", app_state.count_value);
        os_val_dict_set_string(&event_data, "status", "updated");

        ret = os_tr181_emit_event(handle, DEMO_EVENT, &event_data);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Warning: Failed to emit event: %s\n", os_tr181_error_string(ret));
        }
        os_val_free(&event_data);
    }
}

/* Signal callback for libev - called when SIGINT/SIGTERM received */
static void libev_signal_cb(EV_P_ ev_signal *w, int revents)
{
    (void)revents;
    printf("\nReceived signal %d, shutting down...\n", w->signum);
    running = 0;
    ev_break(EV_A_ EVBREAK_ALL);
}

/* libev timer callback for periodic tasks (counter updates) */
static void periodic_cb(EV_P_ ev_timer *w, int revents)
{
    (void)EV_A;
    (void)revents;

    struct periodic_timer_ctx *ctx = (struct periodic_timer_ctx *)w->data;

    /* Update counter at configured interval and notify subscribers */
    update_counter(ctx->handle, ctx->count_param, ctx->count_update_interval);
}

/*
 * Run libev event loop for processing requests and managing async operations
 *
 * Parameters:
 *   handle - Library handle
 *   count_param - Counter parameter path for notifications
 *   count_update_interval - Interval for counter updates in milliseconds
 *
 * Returns: OS_TR181_SUCCESS on success, error code on failure
 */
static os_tr181_error_t run_event_loop_libev(
        os_tr181_handle_t *handle,
        const char *count_param,
        int count_update_interval)
{
    os_tr181_error_t ret;
    struct ev_loop *loop = EV_DEFAULT;
    ev_timer periodic_watcher;
    ev_signal sigint_watcher;
    ev_signal sigterm_watcher;
    struct periodic_timer_ctx timer_ctx;

    printf("Using libev\n");

    /* Attach to libev loop */
    ret = os_tr181_attach_loop(handle, loop);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to attach to libev loop: %s\n", os_tr181_error_string(ret));
        return ret;
    }

    app_state.ev_loop = loop;

    /* Set up signal watchers for SIGINT and SIGTERM */
    ev_signal_init(&sigint_watcher, libev_signal_cb, SIGINT);
    ev_signal_start(loop, &sigint_watcher);
    ev_signal_init(&sigterm_watcher, libev_signal_cb, SIGTERM);
    ev_signal_start(loop, &sigterm_watcher);

    /* Set up context for periodic timer callback */
    timer_ctx.handle = handle;
    timer_ctx.count_param = count_param;
    timer_ctx.count_update_interval = count_update_interval;

    /* Set up periodic timer for counter updates and async test management (1 second interval) */
    ev_timer_init(&periodic_watcher, periodic_cb, 1.0, 1.0);
    periodic_watcher.data = &timer_ctx; /* Pass context to callback */
    ev_timer_start(loop, &periodic_watcher);

    /* Run event loop - will exit when ev_break() is called by signal handler */
    ev_run(loop, 0);

    /* Cleanup libev */
    ev_signal_stop(loop, &sigint_watcher);
    ev_signal_stop(loop, &sigterm_watcher);
    ev_timer_stop(loop, &periodic_watcher);

    /* Stop any pending test timers (tests not yet completed when loop exited) */
    for (struct pending_test *t = app_state.pending_tests; t; t = t->next)
        ev_timer_stop(loop, &t->timer);

    app_state.ev_loop = NULL;
    os_tr181_detach_loop(handle);

    return OS_TR181_SUCCESS;
}

/*
 * Run manual polling event loop for processing requests and managing async operations
 *
 * Parameters:
 *   handle - Library handle
 *   count_param - Counter parameter path for notifications
 *   count_update_interval - Interval for counter updates in milliseconds
 *
 * Returns: OS_TR181_SUCCESS on success, error code on failure
 */
static os_tr181_error_t run_event_loop_poll(
        os_tr181_handle_t *handle,
        const char *count_param,
        int count_update_interval)
{
    os_tr181_error_t ret;

    printf("Using manual polling event loop\n");

    /* Process requests loop */
    while (running)
    {
        /* Check for completed async diagnostics tests */
        process_pending_tests();

        /* Update counter at configured interval and notify subscribers */
        update_counter(handle, count_param, count_update_interval);

        ret = os_tr181_process_requests(handle, 1000); /* 1 second timeout */
        if (ret != OS_TR181_SUCCESS && ret != OS_TR181_ERROR_TIMEOUT)
        {
            fprintf(stderr, "Error processing requests: %s\n", os_tr181_error_string(ret));
        }
    }

    return OS_TR181_SUCCESS;
}

int main(int argc, char *argv[])
{
    os_tr181_handle_t *handle = NULL;
    int ret;
    const char *object_path = DEFAULT_OBJECT;
    const char *second_object_path = NULL;
    char text_param[OS_TR181_PATH_MAX];
    char num_param[OS_TR181_PATH_MAX];
    char count_param[OS_TR181_PATH_MAX];
    char enable_param[OS_TR181_PATH_MAX];
    log_severity_t log_level = LOG_SEVERITY_DEFAULT;
    int count_update_interval = DEFAULT_COUNT_UPDATE_INTERVAL_MS;
    int opt;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "vhpi:")) != -1)
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
            case 'p':
                use_polling_mode = true;
                break;
            case 'i':
                count_update_interval = atoi(optarg);
                if (count_update_interval < 0)
                {
                    fprintf(stderr, "Error: Invalid interval value: %s\n", optarg);
                    usage(argv);
                    return 1;
                }
                break;
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
        int len = strlen(object_path);
        /* Ensure path ends with '.' */
        if (len == 0 || object_path[len - 1] != '.')
        {
            fprintf(stderr, "Error: Object path must end with '.'\n");
            usage(argv);
            return 1;
        }
    }

    if (optind + 1 < argc)
    {
        second_object_path = argv[optind + 1];
        int len = strlen(second_object_path);
        if (len == 0 || second_object_path[len - 1] != '.')
        {
            fprintf(stderr, "Error: Second object path must end with '.'\n");
            usage(argv);
            return 1;
        }
    }

    /* Build parameter paths */
    snprintf(text_param, sizeof(text_param), "%stext", object_path);
    snprintf(num_param, sizeof(num_param), "%snum", object_path);
    snprintf(count_param, sizeof(count_param), "%scount", object_path);
    snprintf(enable_param, sizeof(enable_param), "%sEnable", object_path);

    /* Build table paths */
    char table_path[OS_TR181_PATH_MAX];
    char table_alias_param[OS_TR181_PATH_MAX];
    char table_name_param[OS_TR181_PATH_MAX];
    char table_number_param[OS_TR181_PATH_MAX];
    char table_numbers_path[OS_TR181_PATH_MAX];
    char table_numbers_value[OS_TR181_PATH_MAX];
    char table_numbers_method_a[OS_TR181_PATH_MAX];
    char table_numbers_objectb_path[OS_TR181_PATH_MAX];
    char table_numbers_objectb_method_b[OS_TR181_PATH_MAX];
    snprintf(table_path, sizeof(table_path), "%sTable.", object_path);
    snprintf(table_alias_param, sizeof(table_alias_param), "%sTable.{i}.Alias", object_path);
    snprintf(table_name_param, sizeof(table_name_param), "%sTable.{i}.Name", object_path);
    snprintf(table_number_param, sizeof(table_number_param), "%sTable.{i}.Number", object_path);
    snprintf(table_numbers_path, sizeof(table_numbers_path), "%sTable.{i}.Numbers.", object_path);
    snprintf(table_numbers_value, sizeof(table_numbers_value), "%sTable.{i}.Numbers.{i}.Value", object_path);
    snprintf(table_numbers_method_a, sizeof(table_numbers_method_a), "%sTable.{i}.Numbers.{i}.MethodA()", object_path);
    snprintf(
            table_numbers_objectb_path,
            sizeof(table_numbers_objectb_path),
            "%sTable.{i}.Numbers.{i}.ObjectB.",
            object_path);
    snprintf(
            table_numbers_objectb_method_b,
            sizeof(table_numbers_objectb_method_b),
            "%sTable.{i}.Numbers.{i}.ObjectB.MethodB()",
            object_path);

    /* Table event template path */
    char table_event_param[OS_TR181_PATH_MAX];
    snprintf(table_event_param, sizeof(table_event_param), "%s%s", object_path, DEMO_TABLE_EVENT_SUFFIX);

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
    app_state.handle = handle;
    snprintf(app_state.table_event, sizeof(app_state.table_event), "%s", table_event_param);
    app_state.text_value = strdup("Hello World");
    if (!app_state.text_value)
    {
        fprintf(stderr, "Failed to allocate initial text_value\n");
        os_tr181_close(handle);
        return 1;
    }
    app_state.count_value = 0;
    app_state.last_count_update_msec = time_ms();

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
            OS_TR181_ACCESS_READWRITE | OS_TR181_ACCESS_PERSISTENT_FLAG,
            get_text_cb,
            set_text_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register text parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register writable num parameter */
    printf("Registering parameter: %s (writable number)\n", num_param);
    ret = os_tr181_register_parameter(
            handle,
            num_param,
            OS_TR181_TYPE_INT,
            OS_TR181_ACCESS_READWRITE | OS_TR181_ACCESS_PERSISTENT_FLAG,
            get_num_cb,
            set_num_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register num parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register read-only count parameter */
    printf("Registering parameter: %s (read-only int)\n", count_param);
    ret = os_tr181_register_parameter(
            handle,
            count_param,
            OS_TR181_TYPE_INT,
            OS_TR181_ACCESS_READONLY,
            get_count_cb,
            NULL,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register count parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register writable Enable parameter */
    printf("Registering parameter: %s (writable bool)\n", enable_param);
    ret = os_tr181_register_parameter(
            handle,
            enable_param,
            OS_TR181_TYPE_BOOL,
            OS_TR181_ACCESS_READWRITE,
            get_enable_cb,
            set_enable_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Enable parameter: %s\n", os_tr181_error_string(ret));
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

    /* Register table instance Alias parameter (write-once key) */
    printf("Registering parameter: %s (write-once key)\n", table_alias_param);
    ret = os_tr181_register_parameter(
            handle,
            table_alias_param,
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_KEY_FLAG | OS_TR181_ACCESS_WRITEONCE,
            get_alias_cb,
            set_alias_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Alias parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register table instance parameters (using {i} placeholder) */
    printf("Registering parameter: %s (writable string)\n", table_name_param);
    ret = os_tr181_register_parameter(
            handle,
            table_name_param,
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READWRITE | OS_TR181_ACCESS_PERSISTENT_FLAG,
            get_inst_name_cb,
            set_inst_name_cb,
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
            OS_TR181_ACCESS_READWRITE | OS_TR181_ACCESS_PERSISTENT_FLAG,
            get_inst_number_cb,
            set_inst_number_cb,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Number parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register table object with add/delete callbacks, the nested one */
    printf("Registering table: %s\n", table_numbers_path);
    ret = os_tr181_register_table(handle, table_numbers_path, add_subinstance_cb, del_subinstance_cb, &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register table: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register the read-only Value parameter under the nested table */
    printf("Registering parameter: %s (read-only int)\n", table_numbers_value);
    ret = os_tr181_register_parameter(
            handle,
            table_numbers_value,
            OS_TR181_TYPE_INT,
            OS_TR181_ACCESS_READONLY,
            get_subinst_value_cb,
            NULL,
            &app_state);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register parameter: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register MethodA directly on the nested table instance template.
     * Tests that method_invoke_cb delivers the concrete instance path
     * (e.g. "...Numbers.2.MethodA()") rather than the template path. */
    printf("Registering method: %s\n", table_numbers_method_a);
    ret = os_tr181_register_method(handle, table_numbers_method_a, table_numbers_method_a_cb, &app_state, NULL, 0);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register method: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register ObjectB — a singleton object nested inside the Numbers.{i} instance.
     * Tests that method_invoke_cb resolves instance numbers when the method's
     * direct parent is a singleton child of an instance (not an instance itself). */
    printf("Registering object: %s\n", table_numbers_objectb_path);
    ret = os_tr181_register_object(handle, table_numbers_objectb_path);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register object: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    printf("Registering method: %s\n", table_numbers_objectb_method_b);
    ret = os_tr181_register_method(
            handle,
            table_numbers_objectb_method_b,
            table_numbers_objectb_method_b_cb,
            &app_state,
            NULL,
            0);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register method: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register method */
    char reset_method[OS_TR181_PATH_MAX];
    snprintf(reset_method, sizeof(reset_method), "%sReset()", object_path);
    printf("Registering method: %s\n", reset_method);
    const os_tr181_param_schema_t reset_params[] = {
        {"type", OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN},
        {"status", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
        {"reset_type", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
        {"count", OS_TR181_TYPE_INT, OS_TR181_PARAM_OUT},
        {"text", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
        {NULL, 0, 0}};
    ret = os_tr181_register_method(handle, reset_method, reset_method_cb, &app_state, reset_params, 0);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register Reset method: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register async diagnostics test method */
    char diagnostics_method[OS_TR181_PATH_MAX];
    snprintf(diagnostics_method, sizeof(diagnostics_method), "%sDiagnosticsTest()", object_path);
    printf("Registering method: %s\n", diagnostics_method);
    const os_tr181_param_schema_t diag_params[] = {
        {"test_name", OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN},
        {"duration", OS_TR181_TYPE_INT, OS_TR181_PARAM_IN},
        {"status", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
        {"elapsed_time_ms", OS_TR181_TYPE_INT, OS_TR181_PARAM_OUT},
        {"result", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
        {NULL, 0, 0}};
    ret = os_tr181_register_method(
            handle,
            diagnostics_method,
            diagnostics_test_method_cb,
            &app_state,
            diag_params,
            OS_TR181_METHOD_ASYNC);
    if (ret != OS_TR181_SUCCESS)
    {
        fprintf(stderr, "Failed to register DiagnosticsTest method: %s\n", os_tr181_error_string(ret));
        goto cleanup;
    }

    /* Register second root object (optional) */
    if (second_object_path)
    {
        char second_txt_param[OS_TR181_PATH_MAX];
        snprintf(second_txt_param, sizeof(second_txt_param), "%stxt", second_object_path);

        printf("Registering second object: %s\n", second_object_path);
        ret = os_tr181_register_object(handle, second_object_path);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to register second object: %s\n", os_tr181_error_string(ret));
            goto cleanup;
        }

        app_state.second_txt_value = strdup("second object text");
        if (!app_state.second_txt_value)
        {
            fprintf(stderr, "Failed to allocate second_txt_value\n");
            goto cleanup;
        }

        printf("Registering parameter: %s (writable string)\n", second_txt_param);
        ret = os_tr181_register_parameter(
                handle,
                second_txt_param,
                OS_TR181_TYPE_STRING,
                OS_TR181_ACCESS_READWRITE,
                get_second_txt_cb,
                set_second_txt_cb,
                &app_state);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to register second object txt parameter: %s\n", os_tr181_error_string(ret));
            goto cleanup;
        }
    }

    /* Register USP event with argument schema */
    {
        const os_tr181_param_schema_t event_params[] = {
            {"count", OS_TR181_TYPE_INT, OS_TR181_PARAM_OUT},
            {"status", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
            {NULL, 0, 0}};
        printf("Registering event: %s\n", DEMO_EVENT);
        ret = os_tr181_register_event(handle, DEMO_EVENT, event_params);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to register event %s: %s\n", DEMO_EVENT, os_tr181_error_string(ret));
            goto cleanup;
        }
    }

    /* Register per-instance table event — emitted when Table.{i}.Name is changed */
    {
        const os_tr181_param_schema_t table_event_params[] = {
            {"name", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT},
            {"instance", OS_TR181_TYPE_INT, OS_TR181_PARAM_OUT},
            {NULL, 0, 0}};
        printf("Registering event: %s\n", table_event_param);
        ret = os_tr181_register_event(handle, table_event_param, table_event_params);
        if (ret != OS_TR181_SUCCESS)
        {
            fprintf(stderr, "Failed to register event %s: %s\n", table_event_param, os_tr181_error_string(ret));
            goto cleanup;
        }
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
    printf("  %s - writable integer, current value: %d\n", num_param, app_state.num_value);
    printf("  %s - read-only counter, increments every %d milliseconds%s\n",
           count_param,
           count_update_interval,
           count_update_interval > 0 ? "" : " (updates disabled)");
    printf("  %s - writable boolean, current value: %s\n", enable_param, app_state.enable_value ? "true" : "false");
    printf("  %s - multi-instance table\n", table_path);
    printf("    %s - writable string per instance\n", table_name_param);
    printf("    %s - writable int per instance\n", table_number_param);
    printf("Methods:\n");
    printf("  %s - reset counter and/or text\n", reset_method);
    printf("    Arguments: Optional {\"type\": \"counter\"|\"text\"|\"full\"}\n");
    printf("  %s - async diagnostics test\n", diagnostics_method);
    printf("    Arguments: Optional {\"test_name\": \"string\", \"duration\": int_seconds}\n");
    printf("Events:\n");
    printf("  %s - emitted on every counter increment\n", DEMO_EVENT);
    printf("    Arguments: {\"count\": int, \"status\": string}\n");
    printf("  %s - emitted when Table.{i}.Name is changed\n", table_event_param);
    printf("    Arguments: {\"name\": string, \"instance\": int}\n\n");

    printf("Try accessing these parameters from another application:\n");
    printf("  Get: ./os_tr181_sample get %s\n", text_param);
    printf("  Set: ./os_tr181_sample set %s \"new value\"\n", text_param);
    printf("  Get: ./os_tr181_sample get %s\n", count_param);
    printf("  Get: ./os_tr181_sample get %s\n", enable_param);
    printf("  Set: ./os_tr181_sample set %s true bool\n", enable_param);
    printf("  Set: ./os_tr181_sample set %s false bool\n", enable_param);
    printf("  Subscribe: ./os_tr181_sample subscribe %s\n", count_param);
    printf("  Subscribe event: ./os_tr181_sample subscribe '%s'\n", DEMO_EVENT);
    printf("  Add instance: ./os_tr181_sample add %s\n", table_path);
    printf("  Get instances: ./os_tr181_sample instances %s\n", table_path);
    printf("  Set: ./os_tr181_sample set %sTable.1.Name \"Test\"\n", object_path);
    printf("  Subscribe table event: ./os_tr181_sample subscribe '%sTable.1.TableEvent!'\n", object_path);
    printf("  Set: ./os_tr181_sample set %sTable.1.Number 42 int\n", object_path);
    printf("  Delete: ./os_tr181_sample delete %s1.\n", table_path);
    printf("  Method: ./os_tr181_sample invoke '%s'\n", reset_method);
    printf("  Method: ./os_tr181_sample invoke '%s' '{\"type\":\"counter\"}'\n", reset_method);
    printf("  Async Method: ./os_tr181_sample invoke-async '%s'\n", diagnostics_method);
    printf("  Async Method: ./os_tr181_sample invoke-async '%s' '{\"test_name\":\"ping\",\"duration\":3}'\n",
           diagnostics_method);

    printf("Processing requests (press Ctrl+C to exit)...\n");

    if (use_polling_mode)
    {
        ret = run_event_loop_poll(handle, count_param, count_update_interval);
    }
    else
    {
        ret = run_event_loop_libev(handle, count_param, count_update_interval);
    }

    if (ret != OS_TR181_SUCCESS)
    {
        goto cleanup;
    }

cleanup:
    printf("\nCleaning up...\n");

    /* Clean up pending tests */
    struct pending_test *test = app_state.pending_tests;
    while (test)
    {
        struct pending_test *next = test->next;
        printf("  Canceling pending test: %s\n", test->test_name);
        free(test->test_name);
        /* Note: async_ctx is owned by the library, we don't free it */
        free(test);
        test = next;
    }

    if (app_state.text_value)
    {
        free(app_state.text_value);
    }
    free(app_state.second_txt_value);
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
