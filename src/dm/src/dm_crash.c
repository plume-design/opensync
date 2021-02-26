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
 * ===========================================================================
 *  OpenSync process crash reports implementation
 * ===========================================================================
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "module.h"
#include "target.h"
#include "log.h"
#include "osp_unit.h"
#include "os_util.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "json_util.h"
#include "qm_conn.h"
#include "ev.h"
#include "evx.h"

#define AWLAN_MQTT_TOPIC_KEY       "Crash.Reports"
#define LTAG                       "crash_reports: "
#define BACKOFF_INTERVAL            30

MODULE(dm_crash, dm_crash_init, dm_crash_fini)

enum cr_status
{
    cr_ok_sent,          // ok, crash report sent
    cr_err_mqtt_topic,   // mqtt topic not configured
    cr_err_qm_mqtt,      // QM or MQTT error
    cr_err_general       // general error
};

static ovsdb_table_t table_AWLAN_Node;
static ev_stat crash_reports_stat;
static ev_debounce crash_reports_debounce;
static ev_debounce backoff_debounce;

static struct
{
    char crash_reports_topic[TARGET_BUFF_SZ];   // MQTT topic for crash reports
    char location_id[TARGET_BUFF_SZ];
    char node_id[TARGET_BUFF_SZ];
} awlan_config;

static time_t qm_mqtt_backoff;          // QM or MQTT backoff on errors


static bool crash_report_send_to_qm(char *topic, void *data, int data_size)
{
    qm_response_t res;
    bool ret;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic, data, data_size, &res);
    if (!ret)
    {
        LOG(ERROR, LTAG"Error sending crash report to MQTT topic %s: response: %u, error: %u",
                   topic, res.response, res.error);
        return false;
    }
    return true;
}

static bool crash_report_add_meta_data(json_t *json)
{
    char buff[TARGET_BUFF_SZ];

    // node ID:
    if (*awlan_config.node_id == '\0')
    {
        LOG(WARN, LTAG"nodeId not yet configured in AWLAN_Node");
        return false;
    }
    json_object_set_new(json, "nodeId", json_string(awlan_config.node_id));

    // location ID:
    if (*awlan_config.location_id == '\0')
    {
        LOG(WARN, LTAG"locationId not yet configured in AWLAN_Node");
        return false;
    }
    json_object_set_new(json, "locationId", json_string(awlan_config.location_id));

    // firmware version:
    if (!osp_unit_sw_version_get(buff, sizeof(buff)))
        STRSCPY_WARN(buff, "<NA>");
    json_object_set_new(json, "firmwareVersion", json_string(buff));

    // model:
    if (!osp_unit_model_get(buff, sizeof(buff)))
        STRSCPY_WARN(buff, "<NA>");
    json_object_set_new(json, "model", json_string(buff));

    return true;
}

static enum cr_status crash_report_send(const char *crash_report_filename)
{
    enum cr_status rv = cr_err_general;
    char buffer[8*1024];
    json_t *json = NULL;
    FILE *f = NULL;

    if (*awlan_config.crash_reports_topic == '\0')
    {
        LOG(INFO, LTAG"Crash Reports MQTT topic not configured");
        rv = cr_err_mqtt_topic;
        return false;
    }

    // Load the crash report from tmp file:
    f = fopen(crash_report_filename, "r");
    if (f == NULL)
    {
        LOG(ERROR, LTAG"Error opening file: %s", crash_report_filename);
        goto error_out;
    }

    json = json_object();
    // Add meta data (headers) to json:
    if (!crash_report_add_meta_data(json))
    {
        LOG(ERR, LTAG"Error adding headers to crash report json.");
        goto error_out;
    }

    // Add crash report data to json:
    while (fgets(buffer, sizeof(buffer), f) != NULL)
    {
        char *key;
        char *value;

        key = strtok(buffer, " ");
        if (key == NULL)
        {
            LOG(ERR, LTAG"No key found in crash report tmp file %s", crash_report_filename);
            goto error_out;
        }
        value = strtok(NULL, "\n");
        if (value == NULL)
        {
            LOG(ERR, LTAG"No value for key=%s found in crash report tmp file %s", key, crash_report_filename);
            goto error_out;
        }

        if (strcmp(key, "timestamp") == 0)
            json_object_set_new(json, key, json_integer(atoll(value)));
        else
            json_object_set_new(json, key, json_string(value));
    }

    if (!json_gets(json, buffer, sizeof(buffer), 0))
    {
        LOG(ERR, LTAG"Error dumping JSON string");
        goto error_out;
    }
    LOG(INFO, LTAG"Crash report JSON (len=%zd): %s", strlen(buffer), buffer);

    // Send json osync crash report to mqtt via qm:
    if (!crash_report_send_to_qm(awlan_config.crash_reports_topic, buffer, strlen(buffer)))
    {
        LOG(ERROR, LTAG"Error sending crash report to QM");
        rv = cr_err_qm_mqtt;
        goto error_out;
    }
    LOG(INFO, LTAG"Sent crash report %s to MQTT topic %s", crash_report_filename,
            awlan_config.crash_reports_topic);

    rv = cr_ok_sent;
error_out:
    if (f != NULL) fclose(f);
    json_decref(json);
    return rv;
}

static void handle_new_crash_reports(void)
{
    struct dirent *dir;
    enum cr_status cr_status;
    char crash_file[PATH_MAX];
    DIR *d;

    LOG(TRACE, LTAG"%s", __func__);

    d = opendir(CRASH_REPORTS_TMP_DIR);
    if (d == NULL)
    {
        if (errno != ENOENT) LOG(ERROR, LTAG"Error opening directory %s: %s",
                                        CRASH_REPORTS_TMP_DIR, strerror(errno));
        return;
    }

    /* Check for new crash reports tmp files: */
    while ((dir = readdir(d)) != NULL)
    {
        if (!(dir->d_type == DT_REG
                && strncmp(dir->d_name, "crashed_", strlen("crashed_")) == 0) )
            continue;

        snprintf(crash_file, sizeof(crash_file), "%s/%s", CRASH_REPORTS_TMP_DIR, dir->d_name);
        LOG(INFO, LTAG"Found new crash report: %s", crash_file);

        // Check if we're in back off:
        if (qm_mqtt_backoff != 0)
        {
            time_t now = time(NULL);
            bool in_backoff = ((now - qm_mqtt_backoff) < BACKOFF_INTERVAL);

            if (in_backoff)
            {
                LOG(DEBUG, LTAG"New crash reports detected. "
                           "However in back off since %ld seconds", (now - qm_mqtt_backoff));
                break;
            }
            else
            {
                LOG(DEBUG, LTAG"Out of back off since %ld seconds",
                           (now - qm_mqtt_backoff - BACKOFF_INTERVAL));
                qm_mqtt_backoff = 0;
            }
        }

        /* Send this crash report to the Cloud: */
        cr_status = crash_report_send(crash_file);
        if (cr_status == cr_ok_sent)
        {
            // This crash report sent successfully, delete the crash file:
            unlink(crash_file);
        }
        else if (cr_status == cr_err_mqtt_topic || cr_status == cr_err_qm_mqtt)
        {
            LOG(INFO, LTAG"MQTT topic or QM/MQTT error: "
                           "Skip processing all existing crash reports. Will try later.");
            qm_mqtt_backoff = time(NULL); // go into back off
            ev_debounce_start(EV_DEFAULT, &backoff_debounce);
            break;
        }
        else
        {
            LOG(WARN, LTAG"Unhandled general error. Deleting this crash report.");
            unlink(crash_file);
        }
    }
    closedir(d);
}

static void crash_reports_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    handle_new_crash_reports();
}

static void crash_reports_stat_callback(struct ev_loop *loop, ev_stat *watcher, int revents)
{
    LOG(TRACE, LTAG"%s", __func__);

    ev_debounce_start(EV_DEFAULT, &crash_reports_debounce);
}

static void crash_reports_start(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    static bool started = false;

    if (started)
        return;

    LOG(DEBUG, LTAG"%s", __func__);

    /* Monitor CRASH_REPORTS_TMP_DIR for changes. When a crash happens, a
     * crash report is written into a temporary directory inside this dir. */
    ev_stat_init(&crash_reports_stat, crash_reports_stat_callback, CRASH_REPORTS_TMP_DIR, 0.0);
    ev_debounce_init(&crash_reports_debounce, crash_reports_debounce_fn, 1.5);
    ev_stat_start(loop, &crash_reports_stat);

    ev_debounce_init(&backoff_debounce, crash_reports_debounce_fn, BACKOFF_INTERVAL+2.0);

    /* When starting we need to initially check if there are any crash reports
     * already present (This could happen for instance when DM would be the one
     * crashing. Use a backoff debouncer for this purpose. */
    ev_debounce_start(EV_DEFAULT, &backoff_debounce);

    LOG(INFO, LTAG"Started monitoring crash reports tmp dir: %s", CRASH_REPORTS_TMP_DIR);
    started = true;
}

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *config)
{
    const char *mqtt_topic = NULL;
    const char *location_id = NULL;
    const char *node_id = NULL;
    int i;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;

    // Record crash reports MQTT topic from mqtt_topics:
    for (i = 0; i < config->mqtt_topics_len; i++)
    {
        const char *key = config->mqtt_topics_keys[i];
        const char *value = config->mqtt_topics[i];

        if (strcmp(key, AWLAN_MQTT_TOPIC_KEY) == 0 && *value != '\0')
        {
            mqtt_topic = value;
            break;
        }
    }

    // Record nodeId and locationId from mqtt_headers:
    for (i = 0; i < config->mqtt_headers_len; i++)
    {
        const char *key = config->mqtt_headers_keys[i];
        const char *value = config->mqtt_headers[i];

        if (strcmp(key, "locationId") == 0 && *value != '\0')
            location_id = value;
        if (strcmp(key, "nodeId") == 0 && *value != '\0')
            node_id = value;
    }

    if (location_id != NULL)
        STRSCPY_WARN(awlan_config.location_id, location_id);
    if (node_id != NULL)
        STRSCPY_WARN(awlan_config.node_id, node_id);

    if (mqtt_topic != NULL && strcmp(mqtt_topic, awlan_config.crash_reports_topic) != 0)
    {
        STRSCPY_WARN(awlan_config.crash_reports_topic, mqtt_topic);
        LOG(INFO, LTAG"Configured Crash.Reports MQTT topic: '%s'", awlan_config.crash_reports_topic);

        crash_reports_start();
    }
}

void dm_crash_init(void *data)
{
    LOG(INFO, LTAG"Init.");

    // Monitor OVSDB AWLAN_Node::mqtt_topics::Crash_Reports
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
}

void dm_crash_fini(void *data)
{
    LOG(INFO, LTAG"Finishing.");
}

