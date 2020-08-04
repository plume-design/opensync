#ifndef WBM_STATS_H_INCLUDED
#define WBM_STATS_H_INCLUDED

#include <stdint.h>

#include "osn_types.h"

typedef struct wbm_stats_health
{
    uint32_t    updated_ts;
    uint32_t    cpu_util;
    double      cpu_util_1m;
    double      cpu_util_5m;
    double      cpu_util_15m;
    uint32_t    mem_free;
} wbm_stats_health_t;

typedef struct wbm_stats_radio
{
    uint32_t    updated_ts;
    uint8_t     util;
    uint8_t     activity;
    uint8_t     interf;
    int32_t     noise_floor;
} wbm_stats_radio_t;

typedef struct wbm_stats_client
{
    uint32_t    updated_ts;
    uint32_t    rate_rx;           /* Rx rate in Mbps */
    uint32_t    rate_tx;           /* Tx rate in Mbps */
    int32_t     rssi;
} wbm_stats_client_t;

int wbm_stats_health_get(wbm_stats_health_t *stats);
int wbm_stats_radio_get(wbm_stats_radio_t *stats, char *radio_name, int radio_chan);
int wbm_stats_client_get(wbm_stats_client_t *stats, char *str_mac);

#endif /* WBM_STATS_H_INCLUDED */
