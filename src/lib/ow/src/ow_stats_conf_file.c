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

#include <module.h>
#include <const.h>
#include <memutil.h>
#include <os_time.h>
#include <log.h>
#include <osw_module.h>
#include <ow_stats_conf.h>
#include <osw_etc.h>
#include <ev.h>

#define NOTE(fmt, ...) LOGI("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

struct ow_stats_conf_file {
    struct ow_stats_conf *conf;
    struct ev_loop *loop;
    ev_stat stat;
};

static void
ow_stats_conf_file_process_line(struct ow_stats_conf *c,
                                char *line)
{
    struct ow_stats_conf_entry *e = NULL;
    bool empty = true;

    while (line != NULL) {
        char *attr = strsep(&line, " \n"); if (attr == NULL) break;
        const char *k = strsep(&attr, "="); if (k == NULL) break;
        char *v = strsep(&attr, " \n"); if (v == NULL) break;

        NOTE("k='%s' v='%s'", k, v);

        if (strcmp(k, "id") == 0) {
            e = ow_stats_conf_get_entry(c, v);
        }
        else if (strcmp(k, "stats_type") == 0) {
            if (e == NULL) continue;
            if (strcmp(v, "survey") == 0) {
                ow_stats_conf_entry_set_stats_type(e, OW_STATS_CONF_STATS_TYPE_SURVEY);
                empty = false;
            }
            else if (strcmp(v, "neighbor") == 0) {
                ow_stats_conf_entry_set_stats_type(e, OW_STATS_CONF_STATS_TYPE_NEIGHBOR);
                empty = false;
            }
            else if (strcmp(v, "client") == 0) {
                ow_stats_conf_entry_set_stats_type(e, OW_STATS_CONF_STATS_TYPE_CLIENT);
                empty = false;
            }
        }
        else if (strcmp(k, "scan_type") == 0) {
            if (e == NULL) continue;
            if (strcmp(v, "on-chan") == 0) {
                ow_stats_conf_entry_set_scan_type(e, OW_STATS_CONF_SCAN_TYPE_ON_CHAN);
                empty = false;
            }
            else if (strcmp(v, "off-chan") == 0) {
                ow_stats_conf_entry_set_scan_type(e, OW_STATS_CONF_SCAN_TYPE_OFF_CHAN);
                empty = false;
            }
        }
        else if (strcmp(k, "radio_type") == 0) {
            if (e == NULL) continue;
            if (strcmp(v, "2.4G") == 0) {
                ow_stats_conf_entry_set_radio_type(e, OW_STATS_CONF_RADIO_TYPE_2G);
                empty = false;
            }
            else if (strcmp(v, "5G") == 0) {
                ow_stats_conf_entry_set_radio_type(e, OW_STATS_CONF_RADIO_TYPE_5G);
                empty = false;
            }
            else if (strcmp(v, "5GL") == 0) {
                ow_stats_conf_entry_set_radio_type(e, OW_STATS_CONF_RADIO_TYPE_5GL);
                empty = false;
            }
            else if (strcmp(v, "5GU") == 0) {
                ow_stats_conf_entry_set_radio_type(e, OW_STATS_CONF_RADIO_TYPE_5GU);
                empty = false;
            }
            else if (strcmp(v, "6G") == 0) {
                ow_stats_conf_entry_set_radio_type(e, OW_STATS_CONF_RADIO_TYPE_6G);
                empty = false;
            }
        }
        else if (strcmp(k, "sampling") == 0) {
            if (e == NULL) continue;
            ow_stats_conf_entry_set_sampling(e, atoi(v));
            empty = false;
        }
        else if (strcmp(k, "reporting") == 0) {
            if (e == NULL) continue;
            ow_stats_conf_entry_set_reporting(e, atoi(v));
            empty = false;
        }
        else if (strcmp(k, "channels") == 0) {
            const char *word;
            int max = 32;
            int chans[max];
            int n = 0;
            while (max-- > 0 && (word = strsep(&v, " ,\r\n")) != NULL)
                chans[n++] = atoi(word);
            ow_stats_conf_entry_set_channels(e, chans, n);
            empty = false;
        }
    }

    if (e != NULL && empty == true) {
        ow_stats_conf_entry_reset(e);
    }
}

static void
ow_stats_conf_file_process_path(struct ow_stats_conf_file *cf,
                                const char *path)
{
    NOTE("path='%s'", path);
    FILE *f = fopen(path, "rb");
    NOTE("path='%s' file=%p", path, f);
    if (f == NULL) return;
    char line[4096];
    while (fgets(line, sizeof(line), f) != NULL) {
        ow_stats_conf_file_process_line(cf->conf, line);
    }
    fclose(f);
}

static void
ow_stats_conf_file_stat_cb(EV_P_ ev_stat *arg, int events)
{
    struct ow_stats_conf_file *cf;
    cf = container_of(arg, struct ow_stats_conf_file, stat);
    ow_stats_conf_file_process_path(cf, arg->path);
}

static void
ow_stats_conf_file_start(EV_P_ const char *path)
{
    struct ow_stats_conf_file *cf = CALLOC(1, sizeof(*cf));

    LOGI("ow: stats: conf: file: path='%s'", path);
    cf->conf = ow_stats_conf_get();
    cf->loop = EV_A;
    ev_stat_init(&cf->stat, ow_stats_conf_file_stat_cb, path, 0);
    ev_stat_start(EV_A_ &cf->stat);
    ev_unref(EV_A);
}

OSW_MODULE(ow_stats_conf_file)
{
    const char *path = osw_etc_get("OW_STATS_CONF_FILE_PATH");
    if (path == NULL) return NULL;
    if (strlen(path) == 0) return NULL;
    OSW_MODULE_LOAD(ow_stats_conf);
    struct ev_loop *loop = OSW_MODULE_LOAD(osw_ev);
    ow_stats_conf_file_start(loop, path);
    return NULL;
}
