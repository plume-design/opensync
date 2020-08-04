#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stats_pub.pb-c.h"
#include "stats_pub.h"

/**
 * @brief Allocates and sets a stats public device protobuf.
 *
 * Uses the stats public device info to fill a dynamically allocated
 * stats public device protobuf.
 * The caller is responsible for freeing the returned pointer.
 *
 * @param stats is used to fill up the protobuf
 * @return a pointer to the stats public device protobuf structure
 */
static StsPub__Device* stats_pub_device_pb_struct_create(stats_pub_device_t *stats)
{
    StsPub__Device *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) {
        return NULL;
    }

    sts_pub__device__init(pb);

    pb->timestamp = stats->timestamp;
    pb->mem_total = stats->mem_total;
    pb->mem_used = stats->mem_used;
    pb->cpu_util = stats->cpu_util;
    pb->cpu_util_1m = stats->cpu_util_1m;
    pb->cpu_util_5m = stats->cpu_util_5m;
    pb->cpu_util_15m = stats->cpu_util_15m;

    return pb;
}

/**
 * @brief Generates a stats public device serialized protobuf
 *
 * Uses the information pointed by the input parameter to generate
 * a serialized stats public device buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see stats_pub_pb_free() for this purpose.
 *
 * @param stats is used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
stats_pub_pb_t* stats_pub_device_pb_buffer_create(stats_pub_device_t *stats)
{
    stats_pub_pb_t *serialized;
    StsPub__Device *pb;
    size_t len;
    void *buf;

    if (stats == NULL) {
        return NULL;
    }

    serialized = calloc(1, sizeof(*serialized));
    if (serialized == NULL) {
        return NULL;
    }

    pb = stats_pub_device_pb_struct_create(stats);
    if (pb == NULL) {
        goto Error;
    }

    len = sts_pub__device__get_packed_size(pb);
    if (len == 0) {
        goto Error;
    }

    buf = malloc(len);
    if (buf == NULL) {
        goto Error;
    }

    serialized->len = sts_pub__device__pack(pb, buf);
    serialized->buf = buf;

    free(pb);
    return serialized;

Error:
    free(pb);
    free(serialized);
    return NULL;
}

/**
 * @brief Generates a stats public device structure
 *
 * Uses the information pointed by the input parameter to generate
 * a stats public device structure.
 * The caller is responsible for freeing to the returned data,
 *
 * @param pb is used to fill up the structure.
 * @return a pointer to the data structure.
 */
stats_pub_device_t* stats_pub_device_struct_create(char *buffer, int len)
{
    stats_pub_device_t *stats;
    StsPub__Device *pb;

    pb = sts_pub__device__unpack(NULL, len, (const uint8_t *)buffer);
    if (pb == NULL) {
        return NULL;
    }

    stats = malloc(sizeof(*stats));
    if (stats == NULL) {
        goto Exit;
    }

    stats->timestamp = pb->timestamp;
    stats->mem_total = pb->mem_total;
    stats->mem_used = pb->mem_used;
    stats->cpu_util = pb->cpu_util;
    stats->cpu_util_1m = pb->cpu_util_1m;
    stats->cpu_util_5m = pb->cpu_util_5m;
    stats->cpu_util_15m = pb->cpu_util_15m;
Exit:
    sts_pub__device__free_unpacked(pb, NULL);
    return stats;
}
