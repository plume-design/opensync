#ifndef STATS_PUB_H_INCLUDED
#define STATS_PUB_H_INCLUDED

#include <stdint.h>

#include "dpp_client.h"

#define STATS_PUB_SOCKET_PATH "/tmp/stats_pub.sock"

/* API for clients */

typedef struct stats_pub_survey
{
    uint32_t    timestamp;         /* Timestamp in seconds */
    uint32_t    busy;              /* Busy = Rx + Tx + Interference */
    uint32_t    busy_tx;           /* Tx */
    uint32_t    busy_rx;           /* Rx = Rx_obss + Rx_self_success + Rx_self_err */
    uint32_t    busy_self;         /* Rx_self_success */
    uint32_t    busy_ext;          /* 40MHz extension channel busy */
    int32_t     noise_floor;       /* Noise floor on the channel */
} stats_pub_survey_t;

typedef struct stats_pub_device
{
    uint32_t    timestamp;         /* Timestamp in seconds */
    uint32_t    mem_total;         /* Total amount of RAM memory */
    uint32_t    mem_used;          /* Amount of used RAM memory */
    uint32_t    cpu_util;          /* Momental CPU usage */
    double      cpu_util_1m;       /* Average CPU usage for last 1 min */
    double      cpu_util_5m;       /* Average CPU usage for last 5 min */
    double      cpu_util_15m;      /* Average CPU usage for last 15 min */
} stats_pub_device_t;

typedef struct stats_pub_client
{
    uint32_t    timestamp;         /* Timestamp in seconds */
    dpp_client_stats_t rec;
} stats_pub_client_t;

int stats_pub_survey_get(stats_pub_survey_t *stats, char *name, int chan);
int stats_pub_device_get(stats_pub_device_t *stats);
int stats_pub_client_get(stats_pub_client_t *stats, char *mac);

/* API for SM server */

typedef struct stats_pub_pb
{
    size_t      len;               /* Length of the serialized protobuf */
    void        *buf;              /* Allocated pointer for serialized data */
} stats_pub_pb_t;

void stats_pub_pb_free(stats_pub_pb_t *pb);

stats_pub_pb_t*         stats_pub_survey_pb_buffer_create(stats_pub_survey_t *stats);
stats_pub_survey_t*     stats_pub_survey_struct_create(char *buffer, int len);
stats_pub_pb_t*         stats_pub_device_pb_buffer_create(stats_pub_device_t *stats);
stats_pub_device_t*     stats_pub_device_struct_create(char *buffer, int len);
stats_pub_pb_t*         stats_pub_client_pb_buffer_create(stats_pub_client_t *stats);
stats_pub_client_t*     stats_pub_client_struct_create(char *buffer, int len);

#endif /* STATS_PUB_H_INCLUDED */
