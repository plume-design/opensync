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

#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/watchdog.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"
#include "os_file.h"
#include "osp_reboot.h"
#include "util.h"
#include "wpd_util.h"

/* ping the watchdog device every x seconds */
#define WPD_TIMEOUT_WDPING (5)

#ifndef CONFIG_WPD_WATCHDOG_DEVICE
#define CONFIG_WPD_WATCHDOG_DEVICE "/dev/watchdog"
#endif

/* stop pinging watchdog if there is no ping
 * from external applications for the last x seconds */
#ifndef CONFIG_WPD_PING_TIMEOUT
#define CONFIG_WPD_PING_TIMEOUT (60)
#endif

/* stop pinging watchdog if there is no ping
 * from external applications for the last x seconds - initial timeout */
#ifndef CONFIG_WPD_PING_INITIAL_TIMEOUT
#define CONFIG_WPD_PING_INITIAL_TIMEOUT (80)
#endif

/* set the HW watchdog to bite after x seconds
 * if there is no ping from wpd */
#ifndef CONFIG_WPD_WATCHDOG_TIMEOUT
#define CONFIG_WPD_WATCHDOG_TIMEOUT (30)
#endif

/* location of the PID file */
#ifndef CONFIG_WPD_PID_PATH
#define CONFIG_WPD_PID_PATH "/var/run/wpd.pid"
#endif

#define WPD_SIG_SET_AUTO (SIGUSR1)
#define WPD_SIG_SET_NOAUTO (SIGUSR2)
#define WPD_SIG_PING (SIGHUP)
#define WPD_SIG_KILL (SIGINT)

#define WPD_MAX_PROC_CNT (10)

enum wpd_op
{
    WPD_OP_INVALID,
    WPD_OP_DAEMON,
    WPD_OP_SET_AUTO,
    WPD_OP_SET_NOAUTO,
    WPD_OP_PING,
    WPD_OP_KILL,
};

enum wpd_mode
{
    WPD_MODE_NOAUTO,
    WPD_MODE_AUTO,
};

struct wpd_events
{
    ev_timer tmr_wd_ping;
    ev_timer tmr_ext_ping;
    ev_signal sig_set_auto;
    ev_signal sig_set_noauto;
    ev_signal sig_ping;
    ev_signal sig_kill;
};

struct wpd_ctx
{
    enum wpd_op op;
    enum wpd_mode mode;
    struct wpd_events ev;
    int wd_fd;
    int verbose;
    char proc_list[WPD_MAX_PROC_CNT][64];
    int proc_cnt;
};

static void wpd_add_proc_2_list(struct wpd_ctx *ctx, const char *arg)
{
    if (ctx->proc_cnt >= WPD_MAX_PROC_CNT)
    {
        LOGI("Too many arguments in proc list, supported only: %d", WPD_MAX_PROC_CNT);
        return;
    }

    STRSCPY(ctx->proc_list[ctx->proc_cnt], arg);
    ctx->proc_cnt++;
}

static void wpd_handle_proc_list(struct wpd_ctx *ctx)
{
    char cmd[126];
    int rc;
    int i;

    for (i = 0; i < ctx->proc_cnt; i++)
    {
        snprintf(cmd, sizeof(cmd), "pidof %s", ctx->proc_list[i]);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0) continue;

        snprintf(cmd, sizeof(cmd), "kill -SIGSEGV $(pidof %s)", ctx->proc_list[i]);
        LOGI("Sending segmentation fault signal into process %s", ctx->proc_list[i]);
        rc = system(cmd);
        if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
            LOGI("Killing %s failed [%d, %d]", ctx->proc_list[i], WIFEXITED(rc), WEXITSTATUS(rc));
    }
}

static void cb_timeout_wd(EV_P_ ev_timer *w, int revents)
{
    struct wpd_ctx *ctx = (struct wpd_ctx *)w->data;
    int rv;

    LOGD("cb ping");

    if (ctx->mode == WPD_MODE_AUTO)
    {
        /* restart the timer for external apps ping */
        ctx->ev.tmr_ext_ping.repeat = CONFIG_WPD_PING_TIMEOUT;
        ev_timer_again(loop, &ctx->ev.tmr_ext_ping);
    }

    rv = write(ctx->wd_fd, "w", sizeof("w"));
    if (rv < 0)
    {
        LOGE("Failed to ping the watchdog.");
    }
}

static void cb_timeout_mgr(EV_P_ ev_timer *w, int revents)
{
    struct wpd_ctx *ctx = (struct wpd_ctx *)w->data;
    int wd_timeout;
    int rv;

    LOGEM("Failed to get ping from managers. Watchdog will soon bite ...");

    /* Set the reboot reason before the watchdog kicks in */
    osp_unit_reboot_ex(OSP_REBOOT_WATCHDOG, "Watchdog ping timeout.", -1);

    sync();
    wpd_handle_proc_list(ctx);

    /* set wd timeout to 3 seconds */
    wd_timeout = 3;
    LOGN("Setting WD timeout to 3 seconds.");
    rv = ioctl(ctx->wd_fd, WDIOC_SETTIMEOUT, &wd_timeout);
    if (rv != 0)
    {
        LOGE("Failed to set watchdog timeout.");
    }

    sync();

    while (1)
    {
        /* wait for HW watchdog to kick in */
        sleep(1);
    }
}

static void cb_sig_set_auto(struct ev_loop *loop, ev_signal *w, int revents)
{
    struct wpd_ctx *ctx = (struct wpd_ctx *)w->data;

    LOGN("Mode switched to AUTONOMOUS");
    ctx->mode = WPD_MODE_AUTO;
}

static void cb_sig_set_noauto(struct ev_loop *loop, ev_signal *w, int revents)
{
    struct wpd_ctx *ctx = (struct wpd_ctx *)w->data;

    LOGN("Mode switched to NON-AUTONOMOUS");
    ctx->mode = WPD_MODE_NOAUTO;
}

static void cb_sig_ping(struct ev_loop *loop, ev_signal *w, int revents)
{
    struct wpd_ctx *ctx = (struct wpd_ctx *)w->data;

    /* restart the timeout for manager ping */
    LOGI("Got signal PING");

    ctx->ev.tmr_ext_ping.repeat = CONFIG_WPD_PING_TIMEOUT;
    ev_timer_again(loop, &ctx->ev.tmr_ext_ping);
}

static void cb_sig_kill(struct ev_loop *loop, ev_signal *w, int revents)
{
    LOGI("Got signal KILL");
    ev_break(loop, EVBREAK_ALL);
}

static int init_events(struct wpd_ctx *ctx, struct ev_loop *loop)
{
    ev_timer_init(&ctx->ev.tmr_wd_ping, cb_timeout_wd, 1, WPD_TIMEOUT_WDPING);
    ev_timer_start(loop, &ctx->ev.tmr_wd_ping);
    ctx->ev.tmr_wd_ping.data = ctx;

    ev_init(&ctx->ev.tmr_ext_ping, cb_timeout_mgr);
    ctx->ev.tmr_ext_ping.repeat = CONFIG_WPD_PING_INITIAL_TIMEOUT;
    ev_timer_again(loop, &ctx->ev.tmr_ext_ping);
    ctx->ev.tmr_ext_ping.data = ctx;

    ev_signal_init(&ctx->ev.sig_set_auto, cb_sig_set_auto, WPD_SIG_SET_AUTO);
    ev_signal_start(loop, &ctx->ev.sig_set_auto);
    ctx->ev.sig_set_auto.data = ctx;

    ev_signal_init(&ctx->ev.sig_set_noauto, cb_sig_set_noauto, WPD_SIG_SET_NOAUTO);
    ev_signal_start(loop, &ctx->ev.sig_set_noauto);
    ctx->ev.sig_set_noauto.data = ctx;

    ev_signal_init(&ctx->ev.sig_ping, cb_sig_ping, WPD_SIG_PING);
    ev_signal_start(loop, &ctx->ev.sig_ping);
    ctx->ev.sig_ping.data = ctx;

    ev_signal_init(&ctx->ev.sig_kill, cb_sig_kill, WPD_SIG_KILL);
    ev_signal_start(loop, &ctx->ev.sig_kill);
    ctx->ev.sig_kill.data = ctx;

    return 0;
}

static int run_daemon(struct wpd_ctx *ctx)
{
    struct ev_loop *loop = EV_DEFAULT;
    int wd_timeout;
    int rv;

    rv = init_events(ctx, loop);
    if (rv != 0)
    {
        LOGE("Failed to initialize and start events.");
        return -1;
    }

    /* open WD device and set timeout */
    ctx->wd_fd = open(CONFIG_WPD_WATCHDOG_DEVICE, O_RDWR | O_CLOEXEC);
    if (ctx->wd_fd < 0)
    {
        LOGE("Failed to open watchdog device.");
        return -1;
    }

    wd_timeout = CONFIG_WPD_WATCHDOG_TIMEOUT;
    LOGN("Setting WD timeout to %d seconds", wd_timeout);
    rv = ioctl(ctx->wd_fd, WDIOC_SETTIMEOUT, &wd_timeout);
    if (rv != 0)
    {
        LOGE("Failed to set watchdog timeout.");
        close(ctx->wd_fd);
        return -1;
    }

    /* run libev loop */
    ev_run(loop, 0);

    if (ctx->wd_fd >= 0)
    {
        close(ctx->wd_fd);
    }

    LOGN("Stopping wpd");
    return 0;
}

static void show_usage(void)
{
    const char *help = {"\n"
                        "wpd - Watchdog Proxy Daemon\n"
                        "\n"
                        "Usage:\n"
                        "  wpd -d, --daemon\n"
                        "  wpd -a, --set-auto\n"
                        "  wpd -n, --set-noauto\n"
                        "  wpd -p, --ping\n"
                        "  wpd -k, --kill\n"
                        "  wpd -v, --verbose\n"
                        "  wpd -x, --proc-name proc name\n"
                        "  wpd -h, --help\n"
                        "\n"};

    fprintf(stdout, "%s", help);
}

static int parse_args(struct wpd_ctx *ctx, int argc, char *argv[])
{
    const struct option long_opts[] = {
        {"daemon", no_argument, NULL, 'd'},
        {"set-auto", no_argument, NULL, 'a'},
        {"set-noauto", no_argument, NULL, 'n'},
        {"ping", no_argument, NULL, 'p'},
        {"kill", no_argument, NULL, 'k'},
        {"verbose", no_argument, NULL, 'v'},
        {"proc-name", required_argument, NULL, 'x'},
        {"help", no_argument, NULL, 'h'},
        {NULL, no_argument, NULL, 0}};

    if (argc == 1)
    {
        show_usage();
        return 0;
    }

    while (1)
    {
        int c;

        c = getopt_long(argc, argv, "danpkvx:h", long_opts, NULL);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
            case 'd':
                ctx->op = WPD_OP_DAEMON;
                break;

            case 'a':
                if (ctx->op == WPD_OP_INVALID)
                {
                    ctx->op = WPD_OP_SET_AUTO;
                }
                ctx->mode = WPD_MODE_AUTO;
                break;

            case 'n':
                if (ctx->op == WPD_OP_INVALID)
                {
                    ctx->op = WPD_OP_SET_NOAUTO;
                }
                ctx->mode = WPD_MODE_NOAUTO;
                break;

            case 'p':
                ctx->op = WPD_OP_PING;
                break;

            case 'k':
                ctx->op = WPD_OP_KILL;
                break;

            case 'v':
                ctx->verbose++;
                break;

            case 'x':
                wpd_add_proc_2_list(ctx, optarg);
                break;

            case 'h':
            default:
                show_usage();
                break;
        }
    }

    return 0;
}

static int send_signal(struct wpd_ctx *ctx)
{
    int rv;
    int sig;
    pid_t pid;

    switch (ctx->op)
    {
        case WPD_OP_SET_AUTO:
            LOGD("sig = SET AUTO");
            sig = WPD_SIG_SET_AUTO;
            break;
        case WPD_OP_SET_NOAUTO:
            LOGD("sig = SET NOAUTO");
            sig = WPD_SIG_SET_NOAUTO;
            break;
        case WPD_OP_PING:
            LOGD("sig = PING");
            sig = WPD_SIG_PING;
            break;
        case WPD_OP_KILL:
            LOGD("sig = KILL");
            sig = WPD_SIG_KILL;
            break;
        default:
            return -1;
    }

    pid = pid_get(CONFIG_WPD_PID_PATH);
    if (pid == 0)
    {
        LOGE("Failed to get PID.");
        return -1;
    }
    LOGD("pid = %d", pid);

    rv = kill(pid, sig);
    if (rv < 0)
    {
        LOGE("Sending signal %d to PID %d failed.", sig, pid);
        return rv;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int rv;
    struct wpd_ctx ctx;

    ctx.op = WPD_OP_INVALID;
    /* default mode is non-autonomous */
    ctx.mode = WPD_MODE_NOAUTO;
    ctx.wd_fd = -1;
    ctx.verbose = 0;
    ctx.proc_cnt = 0;
    memset(&ctx.proc_list, 0, sizeof(ctx.proc_list));
    memset(&ctx.ev, 0, sizeof(ctx.ev));

    log_open("WPD", LOG_OPEN_SYSLOG);
    log_severity_set(LOG_SEVERITY_INFO);

    rv = parse_args(&ctx, argc, argv);
    if (rv != 0)
    {
        return -1;
    }

    if (ctx.verbose >= 1)
    {
        log_severity_set(LOG_SEVERITY_DEBUG);
    }

    if (ctx.op == WPD_OP_DAEMON)
    {
        LOGN("Starting WPD (Watchdog Proxy Daemon)");

        rv = daemonize(CONFIG_WPD_PID_PATH);
        if (rv < 0)
        {
            return -1;
        }

        rv = run_daemon(&ctx);
        pid_remove(CONFIG_WPD_PID_PATH);
    }
    else if (
            (ctx.op == WPD_OP_PING) || (ctx.op == WPD_OP_SET_AUTO) || (ctx.op == WPD_OP_SET_NOAUTO)
            || (ctx.op == WPD_OP_KILL))
    {
        rv = send_signal(&ctx);
    }
    else
    {
        rv = -1;
    }

    return rv;
}
