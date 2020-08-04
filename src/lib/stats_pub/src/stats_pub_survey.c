#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stats_pub.pb-c.h"
#include "stats_pub.h"

/**
 * @brief Allocates and sets a stats public survey protobuf.
 *
 * Uses the stats public survey info to fill a dynamically allocated
 * stats public survey protobuf.
 * The caller is responsible for freeing the returned pointer.
 *
 * @param stats is used to fill up the protobuf
 * @return a pointer to the stats public survey protobuf structure
 */
static StsPub__Survey* stats_pub_survey_pb_struct_create(stats_pub_survey_t *stats)
{
    StsPub__Survey *pb;

    pb = calloc(1, sizeof(*pb));
    if (pb == NULL) {
        return NULL;
    }

    sts_pub__survey__init(pb);

    pb->timestamp = stats->timestamp;
    pb->busy = stats->busy;
    pb->busy_tx = stats->busy_tx;
    pb->busy_rx = stats->busy_rx;
    pb->busy_self = stats->busy_self;
    pb->busy_ext = stats->busy_ext;
    pb->noise_floor = stats->noise_floor;

    return pb;
}

/**
 * @brief Generates a stats public survey serialized protobuf
 *
 * Uses the information pointed by the input parameter to generate
 * a serialized stats public survey buffer.
 * The caller is responsible for freeing to the returned serialized data,
 * @see stats_pub_pb_free() for this purpose.
 *
 * @param stats is used to fill up the protobuf.
 * @return a pointer to the serialized data.
 */
stats_pub_pb_t* stats_pub_survey_pb_buffer_create(stats_pub_survey_t *stats)
{
    stats_pub_pb_t *serialized;
    StsPub__Survey *pb;
    size_t len;
    void *buf;

    if (stats == NULL) {
        return NULL;
    }

    serialized = calloc(1, sizeof(*serialized));
    if (serialized == NULL) {
        return NULL;
    }

    pb = stats_pub_survey_pb_struct_create(stats);
    if (pb == NULL) {
        goto Error;
    }

    len = sts_pub__survey__get_packed_size(pb);
    if (len == 0) {
        goto Error;
    }

    buf = malloc(len);
    if (buf == NULL) {
        goto Error;
    }

    serialized->len = sts_pub__survey__pack(pb, buf);
    serialized->buf = buf;

    free(pb);
    return serialized;

Error:
    free(pb);
    free(serialized);
    return NULL;
}

/**
 * @brief Generates a stats public survey structure
 *
 * Uses the information pointed by the input parameter to generate
 * a stats public survey structure.
 * The caller is responsible for freeing to the returned data,
 *
 * @param pb is used to fill up the structure.
 * @return a pointer to the data structure.
 */
stats_pub_survey_t* stats_pub_survey_struct_create(char *buffer, int len)
{
    stats_pub_survey_t *stats;
    StsPub__Survey *pb;

    pb = sts_pub__survey__unpack(NULL, len, (const uint8_t *)buffer);
    if (pb == NULL) {
        return NULL;
    }

    stats = malloc(sizeof(*stats));
    if (stats == NULL) {
        goto Exit;
    }

    stats->timestamp = pb->timestamp;
    stats->busy = pb->busy;
    stats->busy_tx = pb->busy_tx;
    stats->busy_rx = pb->busy_rx;
    stats->busy_self = pb->busy_self;
    stats->busy_ext = pb->busy_ext;
    stats->noise_floor = pb->noise_floor;

Exit:
    sts_pub__survey__free_unpacked(pb, NULL);
    return stats;
}
