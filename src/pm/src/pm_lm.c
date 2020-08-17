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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "target.h"
#include "json_util.h"

#define MODULE_ID                               LOG_MODULE_ID_OVSDB

// dummy configs for OVS table initialization
#define PM_LM_CONFIG_DUMMY_NAME                 "lm"
#define PM_LM_CONFIG_DUMMY_PERIODICITY          "weekly"
#define PM_LM_CONFIG_DUMMY_UPLOAD_TOKEN         ""
#define PM_LM_CONFIG_DUMMY_UPLOAD_LOCATION      ""


typedef struct
{
    ev_timer          timer;
    struct ev_loop    *loop;
    const char        *log_state_file;
} pm_lm_state_t;

pm_lm_state_t   g_state;

ovsdb_table_t table_AW_LM_Config;
ovsdb_table_t table_AW_Debug;


static void pm_lm_do_log_pull(
        const char *upload_token,
        const char *upload_location,
        const char *upload_method)
{
    // check presence of necessary data in order to execute log-pull
    if (!strlen(upload_token) || !strlen(upload_location)) {
        LOGI("LM: Log-pull procedure not executed (up_token or up_location empty)");
        return;
    }

    LOGN("LM: Run log-pull procedure.");

    if (!target_log_pull_ext(upload_location, upload_token, upload_method)) {
        LOGE("LM: Log-pull procedure failed!");
    }
}

static void callback_AW_LM_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_AW_LM_Config *old_rec,
        struct schema_AW_LM_Config *config)
{
    if (mon->mon_type == OVSDB_UPDATE_MODIFY ||
        mon->mon_type == OVSDB_UPDATE_NEW) {
        /* AW_LM_Config::name is used to define logpull upload method. */
        pm_lm_do_log_pull(config->upload_token, config->upload_location, config->name);
    }
}

static void pm_lm_update_logging_state_file(void)
{
    // On each AW_Debug table update we just transfer new contents
    // to a file that is used by logging library to handle dynamic log levels.
    // To do this we are using cached table and internal state and than
    // we join them into single JSON file. Example:

    json_t *loggers_json = json_object();

    // get data from AW_Debug table
    ovsdb_cache_row_t *row;
    ovsdb_table_t     *table = &table_AW_Debug;
    ds_tree_foreach(&table->rows, row)
    {
        struct schema_AW_Debug *aw_debug = (void *)row->record;

        // check if current logger is already in our loggers JSON
        // in case it is not, just crate a new object
        json_t *logger = json_object_get(loggers_json, aw_debug->name);
        if (!logger) {
            logger = json_object();
            json_object_set_new(loggers_json, aw_debug->name, logger);
        }

        json_object_set_new(logger, "log_severity",
                            json_string(aw_debug->log_severity));
    }

    // dump created JSON to a file
    // the first output to a tmp file then move over
    // this prevents double file change events with first event being an empty file
    char tmp_filename[256];
    snprintf(tmp_filename, sizeof(tmp_filename), "%s.tmp", g_state.log_state_file);
    if (json_dump_file(loggers_json, tmp_filename, JSON_INDENT(1))) {
        LOGE("LM: Failed to dump loggers state to file %s", tmp_filename);
    }
    if (rename(tmp_filename, g_state.log_state_file)) {
        LOGE("LM: Failed rename %s -> %s", tmp_filename, g_state.log_state_file);
    }

    json_decref(loggers_json);
}

static void callback_AW_Debug(
        ovsdb_update_monitor_t *mon,
        struct schema_AW_Debug *old_rec,
        struct schema_AW_Debug *record,
        ovsdb_cache_row_t *row)
{
    if (mon->mon_type == OVSDB_UPDATE_MODIFY ||
        mon->mon_type == OVSDB_UPDATE_NEW ||
        mon->mon_type == OVSDB_UPDATE_DEL)
    {
        pm_lm_update_logging_state_file();
    }
}

static bool pm_lm_ovsdb_set_severity(const char *logger_name, const char *severity)
{
    struct schema_AW_Debug aw_debug;

    MEMZERO(aw_debug);
    STRSCPY(aw_debug.name, logger_name);
    STRSCPY(aw_debug.log_severity, severity);

    if (!ovsdb_table_upsert_simple(&table_AW_Debug, SCHEMA_COLUMN(AW_Debug, name),
                                   (char*)logger_name, &aw_debug, NULL))
    {
        LOGE("LM: Upsert into AW_Debug failed! [name:=%s, log_severity:=%s]",
             aw_debug.name, aw_debug.log_severity);
        return false;
    }
    return true;
}

static int pm_lm_ovsdb_init_AW_Debug_table(void)
{
    // init state table from persistent file
    LOGI("LM: Initializing AW_Debug table");

    json_t *loggers = json_load_file(g_state.log_state_file, 0, NULL);
    if (!loggers) {
        LOGI("LM: Unable to read data from persistent file \"%s\"!",
             g_state.log_state_file);
        return 0;
    }

    const char *logger_name;
    json_t *logger_config;
    json_object_foreach(loggers, logger_name, logger_config)
    {
        // upsert log severity in AW_Debug, if it exists for current logger.
        json_t *severity = json_object_get(logger_config, "log_severity");
        if (severity && json_is_string(severity) && json_string_value(severity)) {
            pm_lm_ovsdb_set_severity(logger_name, json_string_value(severity));
        }
    }

    json_decref(loggers);

    return 0;
}

static int pm_lm_ovsdb_init_AW_LM_Config_table(void)
{
    struct schema_AW_LM_Config aw_lm_config;

    LOGI("LM: Initializing AW_LM_Config table");

    MEMZERO(aw_lm_config);
    SCHEMA_SET_STR(aw_lm_config.name,            PM_LM_CONFIG_DUMMY_NAME);
    SCHEMA_SET_STR(aw_lm_config.periodicity,     PM_LM_CONFIG_DUMMY_PERIODICITY);
    SCHEMA_SET_STR(aw_lm_config.upload_token,    PM_LM_CONFIG_DUMMY_UPLOAD_TOKEN);
    SCHEMA_SET_STR(aw_lm_config.upload_location, PM_LM_CONFIG_DUMMY_UPLOAD_LOCATION);

    if (!ovsdb_table_upsert(&table_AW_LM_Config, &aw_lm_config, false)) {
        return -1;
    }

    return 0;
}

static int pm_lm_ovsdb_init(void)
{
    // initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AW_LM_Config);
    OVSDB_TABLE_INIT_NO_KEY(AW_Debug);

    if (pm_lm_ovsdb_init_AW_Debug_table()) {
        LOGE("LM: Failed to initialize AW_Debug table)");
        return -1;
    }

    if (pm_lm_ovsdb_init_AW_LM_Config_table()) {
        LOGE("LM: Failed to initialize AW_LM_Config table)");
        return -1;
    }

    // initialize OVSDB monitor callbacks after DB is initialized
    OVSDB_TABLE_MONITOR(AW_LM_Config, false);

    if (g_state.log_state_file)
    {
        // monitor this table only in case its data
        // should be mirrored to log_state_file
        OVSDB_CACHE_MONITOR(AW_Debug, false);
    }

    return 0;
}

bool pm_lm_init(int argc, char ** argv)
{
    LOGN("Initializing LM");

    MEMZERO(g_state);
    const char *path = CONFIG_TARGET_PATH_LOG_STATE;
    // if empty string set to NULL to disable dynamic log
    if (path != NULL && *path == 0) path = NULL;
    g_state.log_state_file = path;

    if (pm_lm_ovsdb_init()) {
        LOGE("LM: Failed to initialize LM tables");
        return false;
    }

    return true;
}
