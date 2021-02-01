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
#include <limits.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "module.h"
#include "log.h"
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

static char crash_reports_topic[128];   // MQTT topic for crash reports
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

static enum cr_status crash_report_send(const char *crash_report_filename)
{
    enum cr_status rv = cr_err_general;
    struct stat statbuf;
    char *crash_report = NULL;
    FILE *f = NULL;

    if (*crash_reports_topic == '\0')
    {
        LOG(INFO, LTAG"Crash Reports MQTT topic not configured");
        rv = cr_err_mqtt_topic;
        return false;
    }

    // Load the crash report (in json format) from tmp file:
    f = fopen(crash_report_filename, "r");
    if (f == NULL || fstat(fileno(f), &statbuf) == -1)
    {
        LOG(ERROR, LTAG"Error opening file: %s", crash_report_filename);
        goto error_out;
    }
    crash_report = malloc(statbuf.st_size + 1);

    if (fread(crash_report, 1, statbuf.st_size, f) < (size_t)statbuf.st_size)
    {
        LOG(ERROR, LTAG"Error reading from file: %s", crash_report_filename);
        goto error_out;
    }
    crash_report[statbuf.st_size] = '\0';
    LOG(DEBUG, LTAG"Loaded crash report from file %s: %s", crash_report_filename, crash_report);

    // Send json osync crash report to mqtt via qm:
    if (!crash_report_send_to_qm(crash_reports_topic, crash_report, statbuf.st_size))
    {
        LOG(ERROR, LTAG"Error sending crash report to QM");
        rv = cr_err_qm_mqtt;
        goto error_out;
    }
    LOG(INFO, LTAG"Sent crash report %s to MQTT topic %s", crash_report_filename, crash_reports_topic);

    rv = cr_ok_sent;
error_out:
    if (f != NULL) fclose(f);
    free(crash_report);
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
    int i;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;

    for (i = 0; i < config->mqtt_topics_len; i++)
    {
        const char *key = config->mqtt_topics_keys[i];
        const char *topic = config->mqtt_topics[i];

        if (strcmp(key, AWLAN_MQTT_TOPIC_KEY) == 0 && *topic != '\0')
        {
            mqtt_topic = topic;
            break;
        }
    }

    if (mqtt_topic != NULL && strcmp(mqtt_topic, crash_reports_topic) != 0)
    {
        strscpy(crash_reports_topic, mqtt_topic, sizeof(crash_reports_topic));
        LOG(INFO, LTAG"Configured Crash.Reports MQTT topic: '%s'", crash_reports_topic);

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

