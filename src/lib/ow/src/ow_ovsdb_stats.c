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

#include <ovsdb.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <schema_consts.h>
#include "ow_stats_conf.h"

static ovsdb_table_t table_Wifi_Stats_Config;

static enum ow_stats_conf_radio_type
ow_ovsdb_stats_xlate_radio(const struct schema_Wifi_Stats_Config *c)
{
    const char *s = c->radio_type;
    if (strcmp(s, SCHEMA_CONSTS_RADIO_TYPE_STR_2G) == 0) return OW_STATS_CONF_RADIO_TYPE_2G;
    if (strcmp(s, SCHEMA_CONSTS_RADIO_TYPE_STR_5G) == 0) return OW_STATS_CONF_RADIO_TYPE_5G;
    if (strcmp(s, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) == 0) return OW_STATS_CONF_RADIO_TYPE_5GL;
    if (strcmp(s, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) == 0) return OW_STATS_CONF_RADIO_TYPE_5GU;
    if (strcmp(s, SCHEMA_CONSTS_RADIO_TYPE_STR_6G) == 0) return OW_STATS_CONF_RADIO_TYPE_6G;
    return OW_STATS_CONF_RADIO_TYPE_UNSPEC;
}

static enum ow_stats_conf_scan_type
ow_ovsdb_stats_xlate_scan(const struct schema_Wifi_Stats_Config *c)
{
    const char *s = c->survey_type;
    if (strcmp(s, SCHEMA_CONSTS_SCAN_TYPE_ON_CHAN) == 0) return OW_STATS_CONF_SCAN_TYPE_ON_CHAN;
    if (strcmp(s, SCHEMA_CONSTS_SCAN_TYPE_OFF_CHAN) == 0) return OW_STATS_CONF_SCAN_TYPE_OFF_CHAN;
    if (strcmp(s, SCHEMA_CONSTS_SCAN_TYPE_FULL) == 0) return OW_STATS_CONF_SCAN_TYPE_FULL;
    return OW_STATS_CONF_SCAN_TYPE_UNSPEC;
}

static enum ow_stats_conf_stats_type
ow_ovsdb_stats_xlate_stats(const struct schema_Wifi_Stats_Config *c)
{
    const char *s = c->stats_type;
    if (strcmp(s, SCHEMA_CONSTS_REPORT_TYPE_NEIGHBOR) == 0) return OW_STATS_CONF_STATS_TYPE_NEIGHBOR;
    if (strcmp(s, SCHEMA_CONSTS_REPORT_TYPE_CLIENT) == 0) return OW_STATS_CONF_STATS_TYPE_CLIENT;
    if (strcmp(s, SCHEMA_CONSTS_REPORT_TYPE_SURVEY) == 0) return OW_STATS_CONF_STATS_TYPE_SURVEY;
    return OW_STATS_CONF_STATS_TYPE_UNSPEC;
}

static void
ow_ovsdb_stats_setup(struct ow_stats_conf_entry *e,
                     const struct schema_Wifi_Stats_Config *c)
{
    const enum ow_stats_conf_radio_type radio = ow_ovsdb_stats_xlate_radio(c);
    const enum ow_stats_conf_scan_type scan = ow_ovsdb_stats_xlate_scan(c);
    const enum ow_stats_conf_stats_type stats = ow_ovsdb_stats_xlate_stats(c);
    const int sampling = c->sampling_interval;
    const int reporting = c->reporting_interval;
    const int report_limit = c->reporting_count;
    const int *channels = c->channel_list;
    const unsigned int dwell = c->survey_interval_ms;
    const size_t n_channels = c->channel_list_len;
    int delay = 0;
    int util = 0;

    int i;
    for (i = 0; i < c->threshold_len; i++) {
        const char *key = c->threshold_keys[i];
        int *out = NULL;
        if (strcmp(key, "max_delay") == 0) out = &delay;
        else if (strcmp(key, "util") == 0) out = &util;
        if (out != NULL) *out = c->threshold[i];
    }

    WARN_ON(c->reporting_count != 0); /* FIXME */
    if (c->report_type_exists == true) WARN_ON(strcmp(c->report_type, "raw") != 0); /* FIXME */

    ow_stats_conf_entry_set_radio_type(e, radio);
    ow_stats_conf_entry_set_scan_type(e, scan);
    ow_stats_conf_entry_set_stats_type(e, stats);
    ow_stats_conf_entry_set_dwell_time(e, dwell);
    ow_stats_conf_entry_set_reporting_limit(e, report_limit);
    ow_stats_conf_entry_set_holdoff_busy(e, util);
    ow_stats_conf_entry_set_holdoff_delay(e, delay);
    ow_stats_conf_entry_set_channels(e, channels, n_channels);
    if (sampling > 0) ow_stats_conf_entry_set_sampling(e, sampling);
    if (reporting > 0) ow_stats_conf_entry_set_reporting(e, reporting);
}

static void
callback_Wifi_Stats_Config(ovsdb_update_monitor_t *mon,
                           struct schema_Wifi_Stats_Config *old,
                           struct schema_Wifi_Stats_Config *cconf,
                           ovsdb_cache_row_t *row)
{
    const struct schema_Wifi_Stats_Config *c = (const void *)row->record;
    const char *id = c->_uuid.uuid;
    struct ow_stats_conf *conf = ow_stats_conf_get();
    struct ow_stats_conf_entry *e = ow_stats_conf_get_entry(conf, id);

    if (WARN_ON(e == NULL)) return;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_stats_setup(e, c);
            break;
        case OVSDB_UPDATE_DEL:
            ow_stats_conf_entry_reset(e);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

void
ow_ovsdb_stats_init(ovsdb_table_t *vconft)
{
    OVSDB_TABLE_INIT(Wifi_Stats_Config, _uuid);
    OVSDB_CACHE_MONITOR(Wifi_Stats_Config, true);
}
