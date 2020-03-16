/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

/* libc */
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/if_packet.h>
#include <sys/types.h>
#include <dirent.h>

/* other */
#include <ev.h>
#include <wpa_ctrl.h>

/* opensync */
#include <const.h>
#include <log.h>
#include <util.h>
#include <opensync-ctrl.h>

/* local */
#define WPA_CTRL_LEVEL_WARNING 4
#define WPA_CTRL_LEVEL_ERROR 5

#define MODULE_ID LOG_MODULE_ID_CTRL

static void
ctrl_close(struct ctrl *ctrl)
{
    if (ctrl->io.cb)
        ev_io_stop(EV_DEFAULT_ &ctrl->io);
    if (ctrl->retry.cb)
        ev_timer_stop(EV_DEFAULT_ &ctrl->retry);
    if (ctrl->watchdog.cb)
        ev_timer_stop(EV_DEFAULT_ &ctrl->watchdog);
    if (!ctrl->wpa)
        return;

    wpa_ctrl_detach(ctrl->wpa);
    wpa_ctrl_close(ctrl->wpa);
    ctrl->wpa = NULL;
    LOGI("%s: closed", ctrl->bss);

    if (ctrl->closed)
        ctrl->closed(ctrl);
}

static void
ctrl_process(struct ctrl *ctrl)
{
    const char *str;
    size_t len;
    int level;

    /* Example events:
     *
     * <3>AP-STA-CONNECTED 60:b4:f7:f0:0a:19
     * <3>AP-STA-CONNECTED-PWD 60:b4:f7:f0:0a:19 passphrase
     * <3>AP-STA-DISCONNECTED 60:b4:f7:f0:0a:19
     * <3>CTRL-EVENT-CONNECTED - Connection to 00:1d:73:73:88:ea completed [id=0 id_str=]
     * <3>CTRL-EVENT-DISCONNECTED bssid=00:1d:73:73:88:ea reason=3 locally_generated=1
     */
    LOGD("%s: %s", ctrl->bss, ctrl->reply);

    if (WARN_ON(!(str = index(ctrl->reply, '>'))))
        return;
    if (WARN_ON(sscanf(ctrl->reply, "<%d>", &level) != 1))
        return;

    len = ctrl->reply_len;
    len -= str - ctrl->reply;
    str++;
    ctrl->cb(ctrl, level, str, len);

    switch (level) {
        case WPA_CTRL_LEVEL_WARNING:
            LOGW("%s: received '%s'", ctrl->bss, str);
            break;
        case WPA_CTRL_LEVEL_ERROR:
            LOGE("%s: received '%s'", ctrl->bss, str);
            break;
    }

    if (!strncmp(str, WPA_EVENT_TERMINATING, strlen(WPA_EVENT_TERMINATING))) {
        ctrl_close(ctrl);
        ev_timer_again(EV_DEFAULT_ &ctrl->retry);
    }
}

static void
ctrl_ev_cb(EV_P_ struct ev_io *io, int events)
{
    struct ctrl *ctrl = container_of(io, struct ctrl, io);
    int err;

    ctrl->reply_len = sizeof(ctrl->reply) - 1;
    err = wpa_ctrl_recv(ctrl->wpa, ctrl->reply, &ctrl->reply_len);
    ctrl->reply[ctrl->reply_len] = 0;
    if (err < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        ctrl_close(ctrl);
        ev_timer_again(EV_A_ &ctrl->retry);
        return;
    }

    ctrl_process(ctrl);
}

static void
ctrl_cleanup(void)
{
    const char *prefix = "wpa_ctrl_";
    const char *fmt = "wpa_ctrl_%d-%d";
    const char *procdir = "/proc";
    const char *sockdir = "/tmp";
    char unlinkpath[128];
    char procpath[128];
    struct dirent *d;
    DIR *dir;
    int counter;
    int pid;

    dir = opendir(sockdir);
    if (WARN_ON(!dir))
        return;

    while ((d = readdir(dir))) {
        if (strstr(d->d_name, prefix) != d->d_name)
            continue;
        if (sscanf(d->d_name, fmt, &pid, &counter) != 2)
            continue;

        LOGD("%s: checking pid %d", d->d_name, pid);
        snprintf(procpath, sizeof(procpath), "%s/%d", procdir, pid);
        if (access(procpath, X_OK) == 0)
            continue;

        LOGI("%s: cleaning up stale socket file", d->d_name);
        snprintf(unlinkpath, sizeof(unlinkpath), "%s/%s", sockdir, d->d_name);
        unlink(unlinkpath);
    }

    closedir(dir);
}

static void
ctrl_once(void)
{
    static int done;
    if (done) return;
    ctrl_cleanup();
    done = 1;
}

static int
ctrl_open(struct ctrl *ctrl)
{
    int fd;

    ctrl_once();

    if (ctrl->wpa)
        return 0;

    ctrl->wpa = wpa_ctrl_open(ctrl->sockpath);
    if (!ctrl->wpa)
        goto err;

    if (wpa_ctrl_attach(ctrl->wpa) < 0)
        goto err_close;

    fd = wpa_ctrl_get_fd(ctrl->wpa);
    if (fd < 0)
        goto err_detach;

    ev_io_init(&ctrl->io, ctrl_ev_cb, fd, EV_READ);
    ev_io_start(EV_DEFAULT_ &ctrl->io);
    LOGI("%s: opened", ctrl->bss);

    if (ctrl->opened)
        ctrl->opened(ctrl);

    ev_timer_again(EV_DEFAULT_ &ctrl->watchdog);
    return 0;

err_detach:
    wpa_ctrl_detach(ctrl->wpa);
err_close:
    wpa_ctrl_close(ctrl->wpa);
err:
    ctrl->wpa = NULL;
    return -1;
}

static void
ctrl_stat_cb(EV_P_ ev_stat *stat, int events)
{
    struct ctrl *ctrl = container_of(stat, struct ctrl, stat);
    LOGI("%s: file state changed", ctrl->bss);
    ctrl_open(ctrl);
}

static void
ctrl_retry_cb(EV_P_ ev_timer *timer, int events)
{
    struct ctrl *ctrl = container_of(timer, struct ctrl, retry);
    LOGD("%s: retrying", ctrl->bss);
    if (ctrl_open(ctrl) == 0)
        ev_timer_stop(EV_DEFAULT_ &ctrl->retry);
}

static void
ctrl_watchdog_cb(EV_P_ ev_timer *timer, int events)
{
    struct ctrl *ctrl = container_of(timer, struct ctrl, watchdog);
    const char *pong = "PONG";
    const char *ping = "PING";
    char reply[1024];
    size_t len = sizeof(reply);
    int err;

    LOGD("%s: pinging", ctrl->bss);
    err = ctrl_request(ctrl, ping, strlen(ping), reply, &len);
    if (err == 0 && len > strlen(pong) && !strncmp(reply, pong, strlen(pong)))
        return;

    LOGI("%s: ping timeout", ctrl->bss);
    ctrl_close(ctrl);
    ev_timer_again(EV_A_ &ctrl->retry);
}

int
ctrl_enable(struct ctrl *ctrl)
{
    if (ctrl->wpa)
        return 0;

    if (!ctrl->stat.cb) {
        ev_stat_init(&ctrl->stat, ctrl_stat_cb, ctrl->sockpath, 0.);
        ev_stat_start(EV_DEFAULT_ &ctrl->stat);
    }

    if (!ctrl->retry.cb)
        ev_timer_init(&ctrl->retry, ctrl_retry_cb, 0., 5.);

    if (!ctrl->watchdog.cb)
        ev_timer_init(&ctrl->watchdog, ctrl_watchdog_cb, 0., 30.);

    return ctrl_open(ctrl);
}

int
ctrl_disable(struct ctrl *ctrl)
{
    ev_stat_stop(EV_DEFAULT_ &ctrl->stat);
    ctrl_close(ctrl);
    return 0;
}

int
ctrl_running(struct ctrl *ctrl)
{
    return access(ctrl->sockpath, R_OK) == 0;
}

static void
ctrl_msg_cb(char *buf, size_t len)
{
    struct ctrl *ctrl = container_of(buf, struct ctrl, reply);
    LOGD("%s: unsolicited message: len=%zu msg=%s", ctrl->bss, len, buf);
    ctrl_process(ctrl);
}

int
ctrl_request(struct ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len)
{
    int err;

    if (!ctrl->wpa)
        return -1;
    if (WARN_ON(*reply_len < 2))
        return -1;

    (*reply_len)--;
    ctrl->reply_len = sizeof(ctrl->reply);
    err = wpa_ctrl_request(ctrl->wpa, cmd, cmd_len, ctrl->reply, &ctrl->reply_len, ctrl_msg_cb);
    LOGD("%s: cmd='%s' err=%d", ctrl->bss, cmd, err);
    if (err < 0)
        return err;

    if (ctrl->reply_len > *reply_len)
        ctrl->reply_len = *reply_len;

    *reply_len = ctrl->reply_len;
    memcpy(reply, ctrl->reply, *reply_len);
    reply[*reply_len] = 0;
    LOGD("%s: reply='%s'", ctrl->bss, reply);
    return 0;
}
