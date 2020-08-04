#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stats_pub.pb-c.h"
#include "stats_pub.h"

/**
 * @brief Allocates and sets a stats public client protobuf.
 *
 * Uses the stats public client info to fill a dynamically allocated
 * stats public client protobuf.
 * The caller is responsible for freeing the returned pointer.
 *
 * @param stats is used to fill up the protobuf
 * @return a pointer to the stats public client protobuf structure
 */
static StsPub__Client* stats_pub_client_pb_struct_create(stats_pub_client_t *stats)
{
    StsPub__Client *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) {
        return NULL;
    }

    sts_pub__client__init(pb);

    pb->timestamp       = stats->timestamp;
    pb->bytes_tx        = stats->rec.bytes_tx;
    pb->bytes_rx        = stats->rec.bytes_rx;
    pb->frames_tx       = stats->rec.frames_tx;
    pb->frames_rx       = stats->rec.frames_rx;
    pb->retries_rx      = stats->rec.retries_rx;
    pb->retries_tx      = stats->rec.retries_tx;
    pb->errors_rx       = stats->rec.errors_rx;
    pb->errors_tx       = stats->rec.errors_tx;
    pb->rate_rx         = stats->rec.rate_rx;
    pb->rate_tx         = stats->rec.rate_tx;
    pb->rssi            = stats->rec.rssi;

    return pb;
}

/**
 * @brief Generates a stats public client serialized protobuf
 *
 * Uses the information pointed by the input parameter to generate
 * a serialized stats public client buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see stats_pub_pb_free() for this purpose.
 *
 * @param stats is used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
stats_pub_pb_t* stats_pub_client_pb_buffer_create(stats_pub_client_t *stats)
{
    stats_pub_pb_t *serialized;
    StsPub__Client *pb;
    size_t len;
    void *buf;

    if (stats == NULL) {
        return NULL;
    }

    serialized = calloc(1, sizeof(*serialized));
    if (serialized == NULL) {
        return NULL;
    }

    pb = stats_pub_client_pb_struct_create(stats);
    if (pb == NULL) {
        goto Error;
    }

    len = sts_pub__client__get_packed_size(pb);
    if (len == 0) {
        goto Error;
    }

    buf = malloc(len);
    if (buf == NULL) {
        goto Error;
    }

    serialized->len = sts_pub__client__pack(pb, buf);
    serialized->buf = buf;

    free(pb);
    return serialized;

Error:
    free(pb);
    free(serialized);
    return NULL;
}

/**
 * @brief Generates a stats public client structure
 *
 * Uses the information pointed by the input parameter to generate
 * a stats public client structure.
 * The caller is responsible for freeing to the returned data,
 *
 * @param pb is used to fill up the structure.
 * @return a pointer to the data structure.
 */
stats_pub_client_t* stats_pub_client_struct_create(char *buffer, int len)
{
    stats_pub_client_t *stats;
    StsPub__Client *pb;

    pb = sts_pub__client__unpack(NULL, len, (const uint8_t *)buffer);
    if (pb == NULL) {
        return NULL;
    }

    stats = malloc(sizeof(*stats));
    if (stats == NULL) {
        goto Exit;
    }

    stats->timestamp         = pb->timestamp;
    stats->rec.bytes_tx      = pb->bytes_tx;
    stats->rec.bytes_rx      = pb->bytes_rx;
    stats->rec.frames_tx     = pb->frames_tx;
    stats->rec.frames_rx     = pb->frames_rx;
    stats->rec.retries_rx    = pb->retries_rx;
    stats->rec.retries_tx    = pb->retries_tx;
    stats->rec.errors_rx     = pb->errors_rx;
    stats->rec.errors_tx     = pb->errors_tx;
    stats->rec.rate_rx       = pb->rate_rx;
    stats->rec.rate_tx       = pb->rate_tx;
    stats->rec.rssi          = pb->rssi;

Exit:
    sts_pub__client__free_unpacked(pb, NULL);
    return stats;
}
