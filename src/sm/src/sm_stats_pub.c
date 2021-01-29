#include <ev.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "dppline.h"
#include "ds_list.h"
#include "log.h"
#include "os_nif.h"
#include "stats_pub.h"

#include "sm_stats_pub.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define BUF_SIZE            1024
#define SOCK_MAX_PENDING    10

typedef struct sm_stats_pub_server
{
    int fd;
    ev_io accept_watcher;
} sm_stats_pub_server_t;

static sm_stats_pub_server_t g_server;

static int sm_stats_pub_survey_send(int client_fd, char *buf, int len)
{
    int res;
    int channel;
    stats_pub_survey_t *stats;
    stats_pub_pb_t *pb;
    char buffer[BUF_SIZE];
    char name[32];
    char *token;
    const char s[2] = ",";

    memcpy(buffer, buf, len);
    buffer[len] = 0;

    token = strtok(buffer, s);
    token = strtok(NULL, s);
    if (token == NULL) {
        LOGE("%s: Didn't find radio interface name", __func__);
        return -1;
    }
    memset(name, 0, sizeof(name));
    STRSCPY(name, token);

    token = strtok(NULL, s);
    if (token == NULL) {
        LOGE("%s: Didn't find radio channel", __func__);
        return -1;
    }
    channel = atoi(token);

    stats = sm_stats_pub_survey_get(name, channel);
    if (stats == NULL) {
        LOGW("%s: Couldn't find stats: [%s] dev, [%d] channel", __func__, name, channel);
        return -1;
    }

    pb = stats_pub_survey_pb_buffer_create(stats);
    if (pb == NULL) {
        LOGE("%s: Failed to create protobuf data", __func__);
        return -1;
    }

    res = write(client_fd, pb->buf, pb->len);
    if (res != (int)pb->len)
    {
        LOGE("%s: Failed to write data to the socket [%d]", __func__, client_fd);
        stats_pub_pb_free(pb);
        return -1;
    }

    stats_pub_pb_free(pb);
    return 0;
}

static int sm_stats_pub_device_send(int client_fd, char *buf, int len)
{
    int res;
    stats_pub_device_t *stats;
    stats_pub_pb_t *pb;

    stats = sm_stats_pub_device_get();
    if (stats == NULL) {
        LOGW("%s: Couldn't find stats", __func__);
        return -1;
    }

    pb = stats_pub_device_pb_buffer_create(stats);
    if (pb == NULL) {
        LOGE("%s: Failed to create protobuf data", __func__);
        return -1;
    }

    res = write(client_fd, pb->buf, pb->len);
    if (res != (int)pb->len)
    {
        LOGE("%s: Failed to write data to the socket [%d]", __func__, client_fd);
        stats_pub_pb_free(pb);
        return -1;
    }

    stats_pub_pb_free(pb);
    return 0;
}

static int sm_stats_pub_client_send(int client_fd, char *buf, int len)
{
    int res;
    stats_pub_client_t *stats;
    stats_pub_pb_t *pb;
    mac_address_t mac;
    char buffer[BUF_SIZE];
    char *token;
    const char s[2] = ",";

    memcpy(buffer, buf, len);
    buffer[len] = 0;

    token = strtok(buffer, s);
    token = strtok(NULL, s);
    if (token == NULL) {
        LOGE("%s: Didn't find client's MAC address", __func__);
        return -1;
    }

    if (!os_nif_macaddr_from_str((void*)&mac, token)) {
        LOGE("%s: Failed to create mac_address_t from MAC string", __func__);
        return -1;
    }

    stats = sm_stats_pub_client_get(mac);
    if (stats == NULL) {
        LOGW("%s: Couldn't find stats: [%s] mac", __func__, token);
        return -1;
    }

    pb = stats_pub_client_pb_buffer_create(stats);
    if (pb == NULL) {
        LOGE("%s: Failed to create protobuf data", __func__);
        return -1;
    }

    res = write(client_fd, pb->buf, pb->len);
    if (res != (int)pb->len)
    {
        LOGE("%s: Failed to write data to the socket [%d]", __func__, client_fd);
        stats_pub_pb_free(pb);
        return -1;
    }

    stats_pub_pb_free(pb);
    return 0;
}

static int sm_stats_pub_request_handle(int fd, char *buf, int len)
{
    if (strncmp(buf, "survey", strlen("survey")) == 0) {
        return sm_stats_pub_survey_send(fd, buf, len);
    }

    if (strncmp(buf, "device", strlen("device")) == 0) {
        return sm_stats_pub_device_send(fd, buf, len);
    }

    if (strncmp(buf, "client", strlen("client")) == 0) {
        return sm_stats_pub_client_send(fd, buf, len);
    }

    LOGW("%s: Unknown request is received [%s]", __func__, buf);
    return -1;
}

static void sm_stats_pub_read_cb(struct ev_loop *loop, struct ev_io *watcher, int event)
{
    char buffer[BUF_SIZE];
    int res;

    if ((event & EV_READ) == 0) {
        LOGW("%s: Received wrong event [%d]. Expected READ", __func__, event);
        return;
    }

    memset(buffer, 0, sizeof(buffer));
    res = read(watcher->fd, buffer, sizeof(buffer));
    if (res < 0) {
        LOGE("%s: Failed to read data from [%d] socket", __func__, watcher->fd);
        goto Release;
    }

    if (res == 0) {
        goto Release;
    }

    if (sm_stats_pub_request_handle(watcher->fd, buffer, res) != 0) {
        goto Release;
    }

    return;

Release:
    ev_io_stop(EV_DEFAULT, watcher);
    close(watcher->fd);
    free(watcher);
}

static void sm_stats_pub_accept_cb(struct ev_loop *loop, struct ev_io *watcher, int event)
{
    struct ev_io *client_watcher;
    int client_fd;

    if ((event & EV_READ) == 0) {
        LOGW("%s: Received wrong event [%d]. Expected READ", __func__, event);
        return;
    }

    client_fd = accept(watcher->fd, NULL, NULL);
    if (client_fd < 0) {
        LOGW("%s: Failed to accept client connection", __func__);
        return;
    }

    client_watcher = calloc(1, sizeof(struct ev_io));
    if (client_watcher == NULL) {
        LOGW("%s: Failed to allocate memory for client connection", __func__);
        return;
    }

    ev_io_init(client_watcher, sm_stats_pub_read_cb, client_fd, EV_READ);
    ev_io_start(EV_DEFAULT, client_watcher);
}

static int sm_stats_pub_server_start(void)
{
    struct sockaddr_un addr;
    char *server_path = STATS_PUB_SOCKET_PATH;

    if (g_server.fd > 0) {
        LOGW("%s: The server has already been run", __func__);
        return 0;
    }

    g_server.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_server.fd < 0) {
        LOGE("%s: Failed to create socket for server", __func__);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    STRSCPY(addr.sun_path, server_path);
    unlink(server_path);

    if (fcntl(g_server.fd, F_SETFL, fcntl(g_server.fd, F_GETFL) | O_NONBLOCK) != 0) {
        LOGE("%s: Failed to set O_NONBLOCK flag to the server socket [%d]", __func__, g_server.fd);
        goto Error;
    }

    if (bind(g_server.fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOGE("%s: Failed to bind to the server socket [%d]", __func__, g_server.fd);
        goto Error;
    }

    if (listen(g_server.fd, SOCK_MAX_PENDING) != 0) {
        LOGE("%s: Failed to listen to the server socket [%d]", __func__, g_server.fd);
        goto Error;
    }

    ev_io_init(&g_server.accept_watcher, sm_stats_pub_accept_cb, g_server.fd, EV_READ);
    ev_io_start(EV_DEFAULT, &g_server.accept_watcher);

    return 0;
Error:
    close(g_server.fd);
    g_server.fd = 0;
    return -1;
}

static void sm_stats_pub_server_stop(void)
{
    ev_io_stop(EV_DEFAULT, &g_server.accept_watcher);
    close(g_server.fd);
    g_server.fd = 0;
}

int sm_stats_pub_init(void)
{
    if (sm_stats_pub_server_start() != 0) {
        return -1;
    }

    if (sm_stats_pub_survey_init() != 0) {
        goto Error1;
    }

    if (sm_stats_pub_device_init() != 0) {
        goto Error2;
    }

    if (sm_stats_pub_client_init() != 0) {
        goto Error3;
    }

    return 0;

Error3:
    sm_stats_pub_device_uninit();
Error2:
    sm_stats_pub_survey_uninit();
Error1:
    sm_stats_pub_server_stop();
    return -1;
}

void sm_stats_pub_uninit(void)
{
    sm_stats_pub_client_uninit();
    sm_stats_pub_device_uninit();
    sm_stats_pub_survey_uninit();
    sm_stats_pub_server_stop();
}
