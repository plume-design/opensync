#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "stats_pub.h"

#include "wbm_stats.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

int wbm_stats_health_get(wbm_stats_health_t *stats)
{
    stats_pub_device_t rec;

    if (stats_pub_device_get(&rec) != 0) {
        return -1;
    }

    stats->updated_ts = rec.timestamp;
    stats->cpu_util = rec.cpu_util;
    stats->cpu_util_1m = rec.cpu_util_1m;
    stats->cpu_util_5m = rec.cpu_util_5m;
    stats->cpu_util_15m = rec.cpu_util_15m;
    stats->mem_free = rec.mem_total - rec.mem_used;

    return 0;
}

int wbm_stats_radio_get(wbm_stats_radio_t *stats, char *radio_name, int radio_chan)
{
    stats_pub_survey_t rec;

    if (stats_pub_survey_get(&rec, radio_name, radio_chan) != 0) {
        return -1;
    }

    stats->updated_ts = rec.timestamp;
    stats->util = rec.busy;
    stats->activity = rec.busy_tx + rec.busy_self;
    stats->interf = rec.busy - (rec.busy_tx + rec.busy_self);
    stats->noise_floor = rec.noise_floor;

    return 0;
}

int wbm_stats_client_get(wbm_stats_client_t *stats, char *str_mac)
{
    stats_pub_client_t rec;

    if (stats_pub_client_get(&rec, str_mac) != 0) {
        return -1;
    }

    stats->updated_ts = rec.timestamp;
    stats->rate_rx = rec.rec.rate_rx;
    stats->rate_tx = rec.rec.rate_tx;
    stats->rssi = rec.rec.rssi - 95;

    return 0;
}
