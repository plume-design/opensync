#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "dppline.h"
#include "log.h"
#include "stats_pub.h"

#include "sm_stats_pub.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static stats_pub_device_t g_device_stats;

int sm_stats_pub_device_update(dpp_device_record_t *rec)
{
    g_device_stats.timestamp = time(NULL);
    g_device_stats.mem_total = rec->mem_util.mem_total;
    g_device_stats.mem_used = rec->mem_util.mem_used;
    g_device_stats.cpu_util = rec->cpu_util.cpu_util;
    g_device_stats.cpu_util_1m = rec->load[DPP_DEVICE_LOAD_AVG_ONE];
    g_device_stats.cpu_util_5m = rec->load[DPP_DEVICE_LOAD_AVG_FIVE];
    g_device_stats.cpu_util_15m = rec->load[DPP_DEVICE_LOAD_AVG_FIFTEEN];

    LOGD("%s: CPU_util[%u] MEM_used[%u]", __func__, rec->cpu_util.cpu_util, rec->mem_util.mem_used);
    return 0;
}

stats_pub_device_t *sm_stats_pub_device_get(void)
{
    if (g_device_stats.timestamp == 0) {
        LOGW("%s: Device stats has not been updated yet", __func__);
        return NULL;
    }

    return &g_device_stats;
}

int sm_stats_pub_device_init(void)
{
    memset(&g_device_stats, 0, sizeof(g_device_stats));
    return 0;
}

void sm_stats_pub_device_uninit(void)
{
    memset(&g_device_stats, 0, sizeof(g_device_stats));
}
