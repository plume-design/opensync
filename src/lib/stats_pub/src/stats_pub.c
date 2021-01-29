#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "log.h"
#include "util.h"

#include "stats_pub.h"

static int stats_pub_connect_sm(int *sock_fd)
{
    struct sockaddr_un addr;
    struct timeval tv;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("%s: Failed to create socket for client", __func__);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    STRSCPY(addr.sun_path, STATS_PUB_SOCKET_PATH);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        LOGE("%s: Failed to connect to the socket", __func__);
        goto Error;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        LOGW("%s: Failed to set socket read timeout", __func__);
        goto Error;
    }

    *sock_fd = fd;
    return 0;

Error:
    close(fd);
    return -1;
}

int stats_pub_survey_get(stats_pub_survey_t *stats, char *name, int chan)
{
    char buffer[1024];
    int fd;
    int res;
    int req_len;
    int retval = -1;
    stats_pub_survey_t *rec = NULL;

    req_len = snprintf(buffer, sizeof(buffer), "%s,%s,%d", "survey", name, chan);
    if ((req_len <= 0) || (req_len >= (int)sizeof(buffer))) {
        LOGE("%s: Failed to prepare survey request", __func__);
        return -1;
    }

    if (stats_pub_connect_sm(&fd) != 0) {
        return -1;
    }

    res = write(fd, buffer, req_len + 1);
    if (res != (req_len + 1)) {
        LOGE("%s: Failed to write data to the socket", __func__);
        goto Exit;
    }

    memset(buffer, 0, sizeof(buffer));
    res = read(fd, buffer, sizeof(buffer));
    if (res < 0) {
        LOGE("%s: Failed to read data from the socket", __func__);
        goto Exit;
    }

    if (res == 0) {
        LOGW("%s: No data is read from the socket", __func__);
        goto Exit;
    }

    rec = stats_pub_survey_struct_create(buffer, res);
    if (rec == NULL) {
        LOGE("%s: Failed to convert protobuf data", __func__);
        goto Exit;
    }
    memcpy(stats, rec, sizeof(*stats));

    retval = 0;
Exit:
    free(rec);
    close(fd);
    return retval;
}

int stats_pub_device_get(stats_pub_device_t *stats)
{
    char buffer[1024];
    int fd;
    int res;
    int req_len;
    int retval = -1;
    stats_pub_device_t *rec = NULL;

    if (stats_pub_connect_sm(&fd) != 0) {
        return -1;
    }

    req_len = strlen("device") + 1;
    res = write(fd, "device", req_len);
    if (res != req_len) {
        LOGE("%s: Failed to write data to the socket", __func__);
        goto Exit;
    }

    memset(buffer, 0, sizeof(buffer));
    res = read(fd, buffer, sizeof(buffer));
    if (res < 0) {
        LOGE("%s: Failed to read data from the socket", __func__);
        goto Exit;
    }

    if (res == 0) {
        LOGW("%s: No data is read from the socket", __func__);
        goto Exit;
    }

    rec = stats_pub_device_struct_create(buffer, res);
    if (rec == NULL) {
        LOGE("%s: Failed to convert protobuf data", __func__);
        goto Exit;
    }
    memcpy(stats, rec, sizeof(*stats));

    retval = 0;
Exit:
    free(rec);
    close(fd);
    return retval;
}

int stats_pub_client_get(stats_pub_client_t *stats, char *mac)
{
    char buffer[1024];
    int fd;
    int res;
    int req_len;
    int retval = -1;
    stats_pub_client_t *rec = NULL;

    req_len = snprintf(buffer, sizeof(buffer), "%s,%s", "client", mac);
    if ((req_len <= 0) || (req_len >= (int)sizeof(buffer))) {
        LOGE("%s: Failed to prepare cleint request", __func__);
        return -1;
    }

    if (stats_pub_connect_sm(&fd) != 0) {
        return -1;
    }

    res = write(fd, buffer, req_len + 1);
    if (res != (req_len + 1)) {
        LOGE("%s: Failed to write data to the socket", __func__);
        goto Exit;
    }

    memset(buffer, 0, sizeof(buffer));
    res = read(fd, buffer, sizeof(buffer));
    if (res < 0) {
        LOGE("%s: Failed to read data from the socket", __func__);
        goto Exit;
    }

    if (res == 0) {
        LOGW("%s: No data is read from the socket", __func__);
        goto Exit;
    }

    rec = stats_pub_client_struct_create(buffer, res);
    if (rec == NULL) {
        LOGE("%s: Failed to convert protobuf data", __func__);
        goto Exit;
    }
    memcpy(stats, rec, sizeof(*stats));

    retval = 0;
Exit:
    free(rec);
    close(fd);
    return retval;
}

/**
 * @brief Frees the pointer to serialized data and container
 *
 * Frees the dynamically allocated pointer to serialized data (pb->buf)
 * and the container (pb).
 *
 * @param pb a pointer to a serialized data container
 * @return none
 */
void stats_pub_pb_free(stats_pub_pb_t *pb)
{
     if (pb == NULL) {
         return;
     }

     free(pb->buf);
     free(pb);
}
