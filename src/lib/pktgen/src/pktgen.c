#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <errno.h>

#include "log.h"
#include "util.h"

#include "pktgen.h"

#define BUF_LEN_MAX                 512
#define PKTGEN_DEVICE_FILE_PATH     "/proc/net/pktgen/"
#define PKTGEN_CNTRL_FILE           PKTGEN_DEVICE_FILE_PATH "pgctrl"
#define PKTGEN_THREAD_FILE_0        PKTGEN_DEVICE_FILE_PATH "kpktgend_0"

static pid_t g_pktgen_blast_pid = 0;

static int pgctrl_command(char *file, char *format, ...)
{
    va_list argptr;
    char cmd[BUF_LEN_MAX];
    int pgctrl_fd;
    ssize_t res;

    pgctrl_fd = open(file, O_WRONLY);
    if (pgctrl_fd < 0) {
        LOGE("%s: Failed to open %s", __func__, file);
        return -1;
    }

    va_start(argptr, format);
    vsnprintf(cmd, BUF_LEN_MAX, format, argptr);
    va_end(argptr);

    res = write(pgctrl_fd, cmd, strlen(cmd));
    close(pgctrl_fd);

    if (res < 0) {
        LOGE("%s: Failed to execute command: [%s].", __func__, cmd);
        return -1;
    }

    return 0;
}

static int start_wifi_blast(void)
{
    pid_t pid;

    pid = fork();
    if (pid == 0)
    {
        int res;

        res = pgctrl_command(PKTGEN_CNTRL_FILE, "start\n");
        if (res < 0) {
            LOGE("%s: Failed to start pktgen traffic.", __func__);
        }
        exit(0);
    }
    if (pid < 0)
    {
        LOGE("%s: Failed to start pktgen traffic. Fork is failed.", __func__);
        return -1;
    }

    g_pktgen_blast_pid = pid;
    return 0;
}

int pktgen_is_running(void)
{
    FILE *fp;
    char str[BUF_LEN_MAX];
    char run_if[32];
    int retval = 0;

    fp = fopen(PKTGEN_THREAD_FILE_0, "r");
    if (fp == NULL)
    {
        LOGD("%s: Failed to open [%s] file. Pktgen is not running", __func__, PKTGEN_THREAD_FILE_0);
        return retval;
    }

    if (fgets(str, BUF_LEN_MAX, fp) != NULL)
    {
        if (sscanf(str, "Running: %s", run_if) == 1)
        {
            LOGD("%s: Packetgen is already running on interface [%s].", __func__, run_if);
            retval = 1;
        }
    }

    fclose(fp);
    return retval;
}

static int pktgen_configure(char *if_name, char *dest_mac, int packet_size)
{
    char dev_path[PATH_MAX];
    int ret;

    ret = pgctrl_command(PKTGEN_CNTRL_FILE, "reset\n");
    if (ret < 0) {
        LOGE("%s: Failed to reset. Trying to continue.", __func__);
    }

    ret = pgctrl_command(PKTGEN_THREAD_FILE_0, "add_device %s\n", if_name);
    if (ret < 0) {
        LOGE("%s: Failed to configure new device.", __func__);
        return -1;
    }

    snprintf(dev_path, PATH_MAX, "%s%s", PKTGEN_DEVICE_FILE_PATH, if_name);
    ret = pgctrl_command(dev_path, "queue_map_min 2\n");
    if (ret < 0) {
        LOGE("%s: Failed to configure queue_map_min.", __func__);
        return -1;
    }

    ret = pgctrl_command(dev_path, "queue_map_max 2\n");
    if (ret < 0) {
        LOGE("%s: Failed to configure queue_map_max.", __func__);
        return -1;
    }

    // Set unlimited ("0") number of packets to be sent
    ret = pgctrl_command(dev_path, "count 0\n");
    if (ret < 0) {
        LOGE("%s: Failed to configure packets number.", __func__);
        return -1;
    }

    ret = pgctrl_command(dev_path, "pkt_size %d\n", packet_size);
    if (ret < 0) {
        LOGE("%s: Failed to configure pkt_size.", __func__);
        return -1;
    }

    ret = pgctrl_command(dev_path, "dst_mac %s\n", dest_mac);
    if (ret < 0) {
        LOGE("%s: Failed to configure dst_mac.", __func__);
        return -1;
    }

    LOGD("%s: PktGen Configured successfully..", __func__);
    return 0;
}

static int pktgen_config_is_valid(char *if_name, char *dest_mac, int packet_size)
{
    return (packet_size > 0)
           && (strlen(if_name) > 0)
           && (strlen(dest_mac) > 0);
}

enum pktgen_status pktgen_start_wifi_blast(char *if_name, char *dest_mac, int packet_size)
{
    if (g_pktgen_blast_pid != 0) {
        LOGE("%s: Previous blast is already in progress. Skipping current request.", __func__);
        return PKTGEN_STATUS_BUSY;
    }

    if (pktgen_is_running()) {
        LOGE("%s: Pktgen is already running. Skipping current request.", __func__);
        return PKTGEN_STATUS_BUSY;
    }

    if (!pktgen_config_is_valid(if_name, dest_mac, packet_size)) {
        LOGE("%s: Failed: invalid config is received.", __func__);
        return PKTGEN_STATUS_FAILED;
    }

    if (pktgen_configure(if_name, dest_mac, packet_size) != 0) {
        LOGE("%s: Failed to configure the pktgen.", __func__);
        return PKTGEN_STATUS_FAILED;
    }

    // Start Blasting, START is a blocking call, hence calling it as a process.
    LOGD("%s: Creating packet blasting process...", __func__);
    if (start_wifi_blast() != 0) {
        LOGE("%s: Failed to create pthread for pktgen.", __func__);
        return PKTGEN_STATUS_FAILED;
    }

    return PKTGEN_STATUS_SUCCEED;
}

int pktgen_stop_wifi_blast(void)
{
    int pgctrl_ret;
    int status;

    if (g_pktgen_blast_pid == 0) {
        LOGW("%s: Pktgen process is not running!", __func__);
        return 0;
    }

    kill(g_pktgen_blast_pid, SIGKILL);
    LOGD("%s: pktgen process is interrupted.", __func__);
    waitpid(g_pktgen_blast_pid, &status, WNOHANG);
    g_pktgen_blast_pid = 0;
    LOGD("%s: pktgen process is exited with status: 0x%X", __func__, status);

    pgctrl_ret = pgctrl_command(PKTGEN_CNTRL_FILE, "stop\n");
    if (pgctrl_ret < 0) {
        return -1;
    }

    return 0;
}

int pktgen_init(void)
{
    struct stat buffer;
    int file_exists = 0;

    // Check if pktgen control file exists. If exists system supports pktgen
    file_exists = stat(PKTGEN_CNTRL_FILE, &buffer);
    if (file_exists != 0) {
        LOGE("%s: Missing pktgen support. Failed to init pktgen.", __func__);
        return -1;
    }

    return 0;
}

int pktgen_uninit(void)
{
    int pgctrl_ret;

    // Reset all pktgen counters and configurations.
    pgctrl_ret = pgctrl_command(PKTGEN_CNTRL_FILE, "reset\n");
    if (pgctrl_ret != 0) {
        return -1;
    }

    return 0;
}
