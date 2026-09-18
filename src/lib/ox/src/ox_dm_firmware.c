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

/* libc */
#include <stdbool.h>
#include <string.h>

/* 3rd party */
#include <jansson.h>

/* opensync */
#include <log.h>
#include <memutil.h>
#include <os_tr181.h>
#include <os_tr181_val.h>
#include <osp_reboot.h>
#include <ovsdb_sync.h>
#include <ovsdb_update.h>
#include <um.h>

/* ox */
#include <ox_dm_firmware.h>
#include <ox_types.h>

/* dm_schema */
#include <dm_schema.h>

#define LOG_PREFIX(fmt, ...) "ox_dm_firmware: " fmt, ##__VA_ARGS__

/* TR181 instance numbers for the two firmware image slots */
#define FW_INST_ACTIVE   1
#define FW_INST_INACTIVE 2
#define FW_INST_MAX      2

/* TR181 FirmwareImage.Status values */
#define FW_STATUS_NO_IMAGE        "NoImage"
#define FW_STATUS_DOWNLOADING     "Downloading"
#define FW_STATUS_AVAILABLE       "Available"
#define FW_STATUS_ACTIVE          "Active"
#define FW_STATUS_DOWNLOAD_FAILED "DownloadFailed"

/* TR181 FirmwareImage.Alias values */
#define FW_ALIAS_ACTIVE   "active"
#define FW_ALIAS_INACTIVE "inactive"

/* TR181 Download() / Activate() method argument names */
#define FW_ARG_URL           "URL"
#define FW_ARG_AUTO_ACTIVATE "AutoActivate"
#define FW_ARG_START         "Start"
#define FW_ARG_MODE          "Mode"
#define FW_ARG_USER_MESSAGE  "UserMessage"

/* TR181 Activate() Mode enum values */
#define FW_ARG_MODE_ANYTIME             "AnyTime"
#define FW_ARG_MODE_IMMEDIATELY         "Immediately"
#define FW_ARG_MODE_WHEN_IDLE           "WhenIdle"
#define FW_ARG_MODE_CONFIRMATION_NEEDED "ConfirmationNeeded"

/* --------------------------------------------------------------------------
 * Module context — lives for the lifetime of the process
 * -------------------------------------------------------------------------- */
typedef struct
{
    ox_router_t *router;
    ovsdb_update_monitor_t awlan_mon;
    /* Cached previous value used to compute old/new for notify_changed */
    int prev_upgrade_status;
    /* Set by Download(AutoActivate=true); cleared once upgrade_timer is written */
    bool auto_activate;
    /* Set by Download() or Activate(); triggers reboot when upgrade_status=31 */
    bool pending_reboot;
} fw_ctx_t;

static fw_ctx_t g_fw;

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/**
 * Map OVSDB upgrade_status integer to TR181 FirmwareImage.Status string.
 * See opensync/core/src/um/inc/um.h for status code definitions.
 */
static const char *fw_upgrade_status_to_tr181(int code)
{
    switch (code)
    {
        case UM_ERR_OK:
            return FW_STATUS_NO_IMAGE;
        case UM_STS_FW_DL_START:
            return FW_STATUS_DOWNLOADING; /* also covers Validating */
        case UM_STS_FW_DL_END:
            return FW_STATUS_AVAILABLE; /* download + MD5 OK */
        case UM_STS_FW_WR_START:
            return FW_STATUS_AVAILABLE; /* flash write in progress */
        case UM_STS_FW_WR_END:
            return FW_STATUS_AVAILABLE; /* flash written */
        case UM_STS_FW_BC_START:
            return FW_STATUS_AVAILABLE; /* boot config update */
        case UM_STS_FW_BC_END:
            return FW_STATUS_ACTIVE; /* committed, rebooting */
        default:
            return (code < 0) ? FW_STATUS_DOWNLOAD_FAILED : FW_STATUS_DOWNLOADING;
    }
}

/**
 * Read a string column from the single AWLAN_Node row into buf.
 * ovsdb_sync_select_where2 steals the where reference; do not decref it.
 */
static bool fw_awlan_read_str(const char *column, char *buf, size_t buf_size)
{
    bool ok = false;

    buf[0] = '\0';
    json_t *rows = ovsdb_sync_select_where2("AWLAN_Node", json_array());
    if (rows == NULL) return false;

    json_t *row = json_array_get(rows, 0);
    json_t *val = json_object_get(row, column);
    if (json_is_string(val))
    {
        strncpy(buf, json_string_value(val), buf_size - 1);
        buf[buf_size - 1] = '\0';
        ok = true;
    }
    json_decref(rows);
    return ok;
}

static bool fw_awlan_read_int(const char *column, int *out)
{
    json_t *rows = ovsdb_sync_select_where2("AWLAN_Node", json_array());
    if (rows == NULL) return false;

    json_t *val = json_object_get(json_array_get(rows, 0), column);
    const bool ok = json_is_integer(val);
    if (ok) *out = (int)json_integer_value(val);
    json_decref(rows);
    return ok;
}

static bool fw_awlan_read_upgrade_status(int *out)
{
    return fw_awlan_read_int("upgrade_status", out);
}

/* --------------------------------------------------------------------------
 * TR181 GET callbacks — all use {i} template paths.
 * os_tr181_parse_instance() extracts the instance number from param_path.
 * When called on the template itself (e.g. during schema introspection),
 * instance_num is <= 0; return a neutral default in that case.
 * -------------------------------------------------------------------------- */

static os_tr181_error_t fw_get_fw_image_count(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    return os_val_set_uint(value, FW_INST_MAX);
}

static os_tr181_error_t fw_get_active_fw_image(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    /* NO_DOT: TR-106 §3.2.3: path references stored in parameter values must not have a trailing dot */
    return os_val_set_str_dup(value, DM_FMT_NO_DOT(Device.DeviceInfo.FirmwareImage.i, FW_INST_ACTIVE));
}

/* --- FirmwareImage.{i}.Name --- */
static os_tr181_error_t fw_get_name(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    const int inst = os_tr181_parse_instance(param_path);

    if (inst == FW_INST_ACTIVE)
    {
        /* Active slot: Name reflects the currently running firmware version */
        char buf[256];
        fw_awlan_read_str("firmware_version", buf, sizeof(buf));
        return os_val_set_str_dup(value, buf);
    }
    /* Inactive slot or template access: no persistent slot name in OpenSync */
    return os_val_set_str_ref(value, "");
}

/* --- FirmwareImage.{i}.Status --- */
static os_tr181_error_t fw_get_status(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    const int inst = os_tr181_parse_instance(param_path);

    if (inst == FW_INST_ACTIVE) return os_val_set_str_ref(value, FW_STATUS_ACTIVE);

    if (inst == FW_INST_INACTIVE)
    {
        int code = 0;
        fw_awlan_read_upgrade_status(&code);
        return os_val_set_str_ref(value, fw_upgrade_status_to_tr181(code));
    }

    /* Template access during schema introspection */
    return os_val_set_str_ref(value, "");
}

/* --- FirmwareImage.{i}.Alias --- */
static os_tr181_error_t fw_get_alias(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    const int inst = os_tr181_parse_instance(param_path);

    if (inst == FW_INST_ACTIVE) return os_val_set_str_ref(value, FW_ALIAS_ACTIVE);
    if (inst == FW_INST_INACTIVE) return os_val_set_str_ref(value, FW_ALIAS_INACTIVE);

    return os_val_set_str_ref(value, "");
}

/* --- FirmwareImage.{i}.X_OPENSYNC_DecryptionPassword --- */
static os_tr181_error_t fw_get_decryption_password(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    /* Write-only semantics: always return empty string on read */
    return os_val_set_str_ref(value, "");
}

/* --------------------------------------------------------------------------
 * TR181 SET callbacks
 * -------------------------------------------------------------------------- */

static os_tr181_error_t fw_set_decryption_password(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    /* Only meaningful on the inactive instance */
    const int inst = os_tr181_parse_instance(param_path);
    if (inst != FW_INST_INACTIVE)
    {
        LOGE(LOG_PREFIX("DecryptionPassword set on unexpected instance %d"), inst);
        return OS_TR181_ERROR_INVALID;
    }

    char *str = NULL;
    if (os_val_to_str(value, &str) != OS_TR181_SUCCESS) return OS_TR181_ERROR_INVALID;

    /* ovsdb_sync_update_where steals both where and row references */
    json_t *row = json_object();
    json_object_set_new(row, "firmware_pass", json_string(str != NULL ? str : ""));
    FREE(str);

    const int n = ovsdb_sync_update_where("AWLAN_Node", json_array(), row);
    if (n <= 0)
    {
        LOGE(LOG_PREFIX("Failed to write firmware_pass to AWLAN_Node (n=%d)"), n);
        return OS_TR181_ERROR;
    }
    return OS_TR181_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Download() method callback — registered as FirmwareImage.{i}.Download()
 * -------------------------------------------------------------------------- */

static os_tr181_error_t fw_download_method_cb(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    /* Download is only valid on the inactive instance */
    const int inst = os_tr181_parse_instance(path);
    if (inst != FW_INST_INACTIVE)
    {
        LOGE(LOG_PREFIX("Download() called on unexpected instance %d"), inst);
        return OS_TR181_ERROR_INVALID;
    }

    /* URL is mandatory */
    const char *url = os_val_dict_get_string_or(args, FW_ARG_URL, NULL);
    if (url == NULL || url[0] == '\0')
    {
        LOGE(LOG_PREFIX("Download() called without URL argument"));
        return OS_TR181_ERROR_INVALID;
    }

    /* AutoActivate is mandatory per TR181. Default false so an omission results
     * in download-only (no flash). */
    const bool auto_activate = os_val_dict_get_bool_or(args, FW_ARG_AUTO_ACTIVATE, false);

    LOGI(LOG_PREFIX("Download() url=%s auto_activate=%s"), url, auto_activate ? "true" : "false");

    /* Store the flag; fw_awlan_monitor_cb will write upgrade_timer=1 when
     * upgrade_status reaches UM_STS_FW_DL_END (11 = "Available"). */
    g_fw.auto_activate = auto_activate;
    /* Reboot is only expected if OXM will trigger the flash (AutoActivate=true).
     * With AutoActivate=false the caller controls activation via Activate(). */
    g_fw.pending_reboot = auto_activate;

    /*
     * Write firmware_url to trigger download.  Reset timers to zero first so
     * um does not start flashing immediately on URL change.
     * ovsdb_sync_update_where steals both where and row references.
     */
    json_t *row = json_object();
    json_object_set_new(row, "upgrade_dl_timer", json_integer(0));
    json_object_set_new(row, "upgrade_timer", json_integer(0));
    json_object_set_new(row, "firmware_url", json_string(url));

    const int n = ovsdb_sync_update_where("AWLAN_Node", json_array(), row);
    if (n <= 0)
    {
        LOGE(LOG_PREFIX("Download(): failed to write AWLAN_Node (n=%d)"), n);
        g_fw.auto_activate = false;
        g_fw.pending_reboot = false;
        return OS_TR181_ERROR;
    }

    /* Method returns immediately; upgrade proceeds asynchronously via um daemon.
     * Status is tracked via FirmwareImage.2.Status (OVSDB monitor below). */
    return OS_TR181_SUCCESS;
}

/* Returns true if the Activate() Mode value is one we can honour.
 * ConfirmationNeeded cannot be mapped to OVSDB (no confirmation mechanism).
 * NULL or empty string means "not specified" and is accepted. */
static bool fw_activate_mode_is_supported(const char *mode)
{
    if (mode == NULL || mode[0] == '\0') return true;
    if (strcmp(mode, FW_ARG_MODE_ANYTIME) == 0) return true;
    if (strcmp(mode, FW_ARG_MODE_IMMEDIATELY) == 0) return true;
    if (strcmp(mode, FW_ARG_MODE_WHEN_IDLE) == 0) return true;
    /* ConfirmationNeeded not supported */
    if (strcmp(mode, FW_ARG_MODE_CONFIRMATION_NEEDED) == 0) return false;
    /* Unrecognised mode */
    return false;
}

/* --------------------------------------------------------------------------
 * Activate() method callback — registered as FirmwareImage.{i}.Activate()
 *
 * TR181 Activate() takes a TimeWindow sub-object; we support the simplified
 * flat form used by PrplOS: Activate(Start=<seconds>, ...).
 * Start=0 means "activate immediately" → upgrade_timer=1 (smallest non-zero
 * value that arms the um timer).
 * -------------------------------------------------------------------------- */
static os_tr181_error_t fw_activate_method_cb(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data)
{
    const int inst = os_tr181_parse_instance(path);
    if (inst != FW_INST_INACTIVE)
    {
        LOGE(LOG_PREFIX("Activate() called on instance %d (only instance %d is activatable)"), inst, FW_INST_INACTIVE);
        return OS_TR181_ERROR_INVALID;
    }

    /* Start is mandatory per TR181 but default to 0 (immediate) for leniency */
    const int start_sec = (int)os_val_dict_get_uint_or(args, FW_ARG_START, 0);

    /* log UserMessage if provided */
    const char *user_msg = os_val_dict_get_string_or(args, FW_ARG_USER_MESSAGE, NULL);
    if (user_msg != NULL && user_msg[0] != '\0')
    {
        LOGI(LOG_PREFIX("Activate() UserMessage: %s"), user_msg);
    }

    /* Check if specified Mode is supported */
    const char *mode = os_val_dict_get_string_or(args, FW_ARG_MODE, NULL);
    if (fw_activate_mode_is_supported(mode) == false)
    {
        LOGE(LOG_PREFIX("Activate() unsupported Mode=%s"), mode ?: "(null)");
        return OS_TR181_ERROR_INVALID;
    }

    /* um requires upgrade_timer > 0 to arm the flash timer; treat 0 as "now" */
    const int upgrade_timer = (start_sec > 0) ? start_sec : 1;

    LOGI(LOG_PREFIX("Activate() start_sec=%d → upgrade_timer=%d"), start_sec, upgrade_timer);

    json_t *row = json_object();
    json_object_set_new(row, "upgrade_timer", json_integer(upgrade_timer));

    const int n = ovsdb_sync_update_where("AWLAN_Node", json_array(), row);
    if (n <= 0)
    {
        LOGE(LOG_PREFIX("Activate(): failed to write AWLAN_Node (n=%d)"), n);
        return OS_TR181_ERROR;
    }

    /* Clear any pending auto_activate flag since Activate() is now explicit */
    g_fw.auto_activate = false;
    /* Activate() always expects a reboot to follow the flash */
    g_fw.pending_reboot = true;

    return OS_TR181_SUCCESS;
}

/* --------------------------------------------------------------------------
 * OVSDB AWLAN_Node change monitor
 * -------------------------------------------------------------------------- */

static void fw_awlan_monitor_cb(ovsdb_update_monitor_t *mon)
{
    if (mon->mon_type != OVSDB_UPDATE_MODIFY) return;

    os_tr181_handle_t *tr181 = g_fw.router->tr181_handle;

    /* --- upgrade_status → FirmwareImage.2.Status --- */
    json_t *new_status_j = json_object_get(mon->mon_json_new, "upgrade_status");
    if (json_is_integer(new_status_j))
    {
        const int new_code = (int)json_integer_value(new_status_j);
        const int old_code = g_fw.prev_upgrade_status;

        if (new_code != old_code)
        {
            const char *old_str = fw_upgrade_status_to_tr181(old_code);
            const char *new_str = fw_upgrade_status_to_tr181(new_code);

            LOGD(LOG_PREFIX("upgrade_status %d->%d (%s->%s)"), old_code, new_code, old_str, new_str);

            os_tr181_val_t old_val = OS_VAL_INIT();
            os_tr181_val_t new_val = OS_VAL_INIT();
            os_val_set_str_ref(&old_val, old_str);
            os_val_set_str_ref(&new_val, new_str);

            const os_tr181_error_t notify_err = os_tr181_notify_changed(
                    tr181,
                    DM_FMT(Device.DeviceInfo.FirmwareImage.i.Status, FW_INST_INACTIVE),
                    &old_val,
                    &new_val);
            if (notify_err != OS_TR181_SUCCESS)
                LOGW(LOG_PREFIX("Failed to notify Status change (%s->%s): %d"), old_str, new_str, notify_err);

            /* AutoActivate: trigger flash when download successfully completes */
            if (new_code == UM_STS_FW_DL_END && g_fw.auto_activate)
            {
                LOGI(LOG_PREFIX("AutoActivate: download complete, writing upgrade_timer=1"));
                json_t *row = json_object();
                json_object_set_new(row, "upgrade_timer", json_integer(1));
                const int n = ovsdb_sync_update_where("AWLAN_Node", json_array(), row);
                if (n <= 0) LOGE(LOG_PREFIX("AutoActivate: failed to write upgrade_timer (n=%d)"), n);
                g_fw.auto_activate = false;
            }

            /* Reboot after successful flash: UM_STS_FW_BC_END means boot config
             * committed — the last step before reboot.
             * Only reboot if this upgrade was initiated via TR181 (pending_reboot set). */
            if (new_code == UM_STS_FW_BC_END && g_fw.pending_reboot)
            {
                LOGI(LOG_PREFIX("Firmware upgrade complete, rebooting"));
                g_fw.pending_reboot = false;
                osp_unit_reboot_ex(OSP_REBOOT_UPGRADE, "Firmware upgrade via TR181", 0);
                /* osp_unit_reboot_ex does not return on success */
            }

            g_fw.prev_upgrade_status = new_code;
        }
    }
}

/* --------------------------------------------------------------------------
 * Phase 1: Register schema (call before os_tr181_publish_objects)
 * -------------------------------------------------------------------------- */

/* Helper macro: register a parameter and return false on failure */
#define FW_REG_PARAM(path, type, access, get_cb, set_cb)                                                \
    do                                                                                                  \
    {                                                                                                   \
        const os_tr181_error_t _e =                                                                     \
                os_tr181_register_parameter(tr181, (path), (type), (access), (get_cb), (set_cb), NULL); \
        if (_e != OS_TR181_SUCCESS)                                                                     \
        {                                                                                               \
            LOGE(LOG_PREFIX("Failed to register '%s': %d"), (path), _e);                                \
            return false;                                                                               \
        }                                                                                               \
    } while (0)

static os_tr181_error_t fw_table_add_cb(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data)
{
    (void)initial_values;
    (void)user_data;

    if (instance_num < 1 || instance_num > FW_INST_MAX)
    {
        LOGD(LOG_PREFIX("FirmwareImage add rejected: instance %d out of range [1..%d]"), instance_num, FW_INST_MAX);
        return OS_TR181_ERROR;
    }

    LOGD(LOG_PREFIX("FirmwareImage add: instance %d (%s)"), instance_num, object_path);
    return OS_TR181_SUCCESS;
}

static os_tr181_error_t fw_table_del_cb(const char *object_path, int instance_num, void *user_data)
{
    (void)user_data;

    LOGD(LOG_PREFIX("FirmwareImage delete rejected: instance %d (%s) is fixed"), instance_num, object_path);
    return OS_TR181_ERROR;
}

bool ox_dm_firmware_register(ox_router_t *router)
{
    os_tr181_handle_t *tr181 = router->tr181_handle;
    os_tr181_error_t err;

    memset(&g_fw, 0, sizeof(g_fw));
    g_fw.router = router;

    /* Device.DeviceInfo. is pre-registered by the caller (oxm_main.c) before
     * ox_dm_firmware_register() is invoked.  Register only the parameters
     * and sub-objects that belong to the firmware module. */

    /* --- Top-level DeviceInfo scalar parameters --- */

    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.FirmwareImageNumberOfEntries),
            OS_TR181_TYPE_UINT,
            OS_TR181_ACCESS_READONLY,
            fw_get_fw_image_count,
            NULL);

    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.ActiveFirmwareImage),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            fw_get_active_fw_image,
            NULL);

    /* BootFirmwareImage — the image selected for the next boot.
     * In practice identical to ActiveFirmwareImage: our reboot follows
     * immediately after boot-config commit so the two never diverge. */
    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.BootFirmwareImage),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            fw_get_active_fw_image,
            NULL);

    /* --- FirmwareImage multi-instance table ---
     * Instances are fixed (always FW_INST_MAX); no user-created/deleted instances.
     * add_cb accepts instances 1..FW_INST_MAX (pre-created in post_publish) and
     * rejects any request beyond that. del_cb rejects all deletions.
     */
    err = os_tr181_register_table(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage),
            fw_table_add_cb, /* add_cb */
            fw_table_del_cb, /* del_cb */
            NULL);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("Failed to register FirmwareImage table: %d"), err);
        return false;
    }

    /* --- Template parameter definitions (use {i} placeholder) ---
     * These define the schema; callbacks dispatch on the instance number
     * extracted from param_path using os_tr181_parse_instance().
     */
    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Name),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            fw_get_name,
            NULL);

    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Status),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            fw_get_status,
            NULL);

    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Alias),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            fw_get_alias,
            NULL);

    /* Vendor extension: AES-256-CBC decryption password → AWLAN_Node.firmware_pass.
     * Read always returns ""; set is only meaningful on instance 2. */
    FW_REG_PARAM(
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.X_OPENSYNC_DecryptionPassword),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READWRITE,
            fw_get_decryption_password,
            fw_set_decryption_password);

    /* --- Download() method template --- */
    // clang-format off
    static const os_tr181_param_schema_t fw_download_params[] = {
        { "URL",              OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN | OS_TR181_PARAM_MANDATORY },
        { "AutoActivate",     OS_TR181_TYPE_BOOL,   OS_TR181_PARAM_IN | OS_TR181_PARAM_MANDATORY },
        { "Username",         OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
        { "Password",         OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
        { "FileSize",         OS_TR181_TYPE_UINT,   OS_TR181_PARAM_IN },
        { "CheckSumAlgorithm",OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
        { "CheckSum",         OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
        { NULL, 0, 0 },
    };
    // clang-format on
    err = os_tr181_register_method(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Download),
            fw_download_method_cb,
            router,
            fw_download_params,
            OS_TR181_METHOD_ASYNC);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("Failed to register Download() method: %d"), err);
        return false;
    }

    /* --- Activate() method template ---
     * TR181 defines Activate() input as a TimeWindow.{i}. sub-object, but
     * Ambiorix/PrplOS exposes a flat argument list — we follow that convention. */
    // clang-format off
    static const os_tr181_param_schema_t fw_activate_params[] = {
        { "Start",      OS_TR181_TYPE_UINT,   OS_TR181_PARAM_IN | OS_TR181_PARAM_MANDATORY },
        { "End",        OS_TR181_TYPE_UINT,   OS_TR181_PARAM_IN | OS_TR181_PARAM_MANDATORY },
        { "Mode",       OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN | OS_TR181_PARAM_MANDATORY },
        { "UserMessage",OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
        { "MaxRetries", OS_TR181_TYPE_INT,    OS_TR181_PARAM_IN },
        { NULL, 0, 0 },
    };
    // clang-format on
    err = os_tr181_register_method(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage.i.Activate),
            fw_activate_method_cb,
            router,
            fw_activate_params,
            OS_TR181_METHOD_ASYNC);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("Failed to register Activate() method: %d"), err);
        return false;
    }

    LOGD(LOG_PREFIX("Schema registered successfully"));
    return true;
}

#undef FW_REG_PARAM

/* --------------------------------------------------------------------------
 * Phase 2: Post-publish (call after os_tr181_publish_objects)
 * -------------------------------------------------------------------------- */

bool ox_dm_firmware_post_publish(ox_router_t *router)
{
    os_tr181_handle_t *tr181 = router->tr181_handle;
    os_tr181_error_t err;
    int inst_num;

    /* Pre-create instance 1: active slot */
    err = os_tr181_add_instance_ex(
            tr181 /* force clang-format newline */,
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage),
            FW_INST_ACTIVE,
            FW_ALIAS_ACTIVE,
            NULL,
            &inst_num);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("Failed to create %s: %d"), DM_FMT(Device.DeviceInfo.FirmwareImage.i, FW_INST_ACTIVE), err);
        return false;
    }

    /* Pre-create instance 2: inactive / download-target slot */
    err = os_tr181_add_instance_ex(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.FirmwareImage),
            FW_INST_INACTIVE,
            FW_ALIAS_INACTIVE,
            NULL,
            &inst_num);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("Failed to create %s: %d"), DM_FMT(Device.DeviceInfo.FirmwareImage.i, FW_INST_INACTIVE), err);
        return false;
    }

    /* Seed cached state so the first MODIFY callback only fires notify_changed
     * for columns that actually change after startup. */
    fw_awlan_read_upgrade_status(&g_fw.prev_upgrade_status);

    /* Start OVSDB monitor: fires fw_awlan_monitor_cb on AWLAN_Node changes */
    const bool mon_ok = ovsdb_update_monitor(&g_fw.awlan_mon, fw_awlan_monitor_cb, "AWLAN_Node", OMT_MODIFY);
    if (!mon_ok)
    {
        LOGE(LOG_PREFIX("Failed to start AWLAN_Node monitor"));
        return false;
    }
    g_fw.awlan_mon.mon_data = &g_fw;

    LOGI(LOG_PREFIX("Initialised (upgrade_status=%d)"), g_fw.prev_upgrade_status);

    return true;
}
