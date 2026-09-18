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

/* libc */
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* 3rd party */
#include <ev.h>

/* opensync */
#include <log.h>
#include <manager_watchdog.h>
#include <memutil.h>
#include <os_backtrace.h>
#include <os_time.h>
#include <os_tr181.h>

/* ox */
#include <ox.h>
#include <ox_dm_bridging.h>
#include <ox_dm_ethernet.h>
#include <ox_dm_firmware.h>
#include <ox_dm_ip.h>

struct oxm_ctx
{
    log_severity_t initial_log_severity;
    int log_flags;
    int cmd_test_rstatus;
    const char *test_cmd;
    pid_t test_cmd_pid;
    ev_child cmd_test_child;
    bool opt_enable_deviceinfo;
    bool opt_enable_ethernet;
    bool opt_enable_bridging;
    bool opt_enable_ip;
};
typedef struct oxm_ctx oxm_ctx_t;

static os_tr181_error_t oxm_dm_deviceinfo_get_uptime(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    const uint64_t uptime = time_monotonic();
    return os_val_set_uint(value, uptime);
}

static void oxm_ctx_test_cmd_child_cb(EV_P_ ev_child *child, int flags)
{
    oxm_ctx_t *ctx = child->data;
    LOGI("Test command with pid %d exited with status %d", child->rpid, child->rstatus);
    ctx->cmd_test_rstatus = child->rstatus;
    ev_child_stop(EV_DEFAULT_ child);
    ev_break(EV_A_ EVBREAK_ALL);
}

static void oxm_ctx_init(oxm_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->initial_log_severity = LOG_SEVERITY_INFO;
    ctx->log_flags = LOG_OPEN_DEFAULT;
    ctx->opt_enable_deviceinfo = true; /* enable DeviceInfo by default */
    ctx->opt_enable_ethernet = true;   /* enable Ethernet by default */
    ctx->opt_enable_bridging = true;   /* enable Bridging by default */
    ctx->opt_enable_ip = true;         /* enable IP by default */
}

static void oxm_print_usage(oxm_ctx_t *ctx, int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d, --debug              set log severity to DEBUG\n");
    fprintf(stderr, "  -t, --trace              set log severity to TRACE\n");
    fprintf(stderr, "  -o, --stdout             log to stdout instead of syslog\n");
    fprintf(stderr, "  -s, --syslog             log to syslog instead of stdout\n");
    fprintf(stderr, "  -T, --test-cmd cmd       run test command 'cmd' and exit with the same status\n");
    fprintf(stderr, "      --enable-deviceinfo  enable DeviceInfo TR-181 object\n");
    fprintf(stderr, "      --disable-deviceinfo disable DeviceInfo TR-181 object\n");
    fprintf(stderr, "      --enable-ethernet    enable Ethernet.Interface/Link TR-181 objects\n");
    fprintf(stderr, "      --disable-ethernet   disable Ethernet.Interface/Link TR-181 objects\n");
    fprintf(stderr, "      --enable-bridging    enable Bridging.Bridge TR-181 object\n");
    fprintf(stderr, "      --disable-bridging   disable Bridging.Bridge TR-181 object\n");
    fprintf(stderr, "      --enable-ip          enable IP.Interface TR-181 object\n");
    fprintf(stderr, "      --disable-ip         disable IP.Interface TR-181 object\n");
}

static int oxm_ctx_parse_args(oxm_ctx_t *ctx, int argc, char **argv)
{
    enum
    {
        OPT_ENABLE_DEVICEINFO = 256,
        OPT_DISABLE_DEVICEINFO,
        OPT_ENABLE_ETHERNET,
        OPT_DISABLE_ETHERNET,
        OPT_ENABLE_BRIDGING,
        OPT_DISABLE_BRIDGING,
        OPT_ENABLE_IP,
        OPT_DISABLE_IP,
    };

    static const struct option long_opts[] = {
        {"debug", no_argument, NULL, 'd'},
        {"trace", no_argument, NULL, 't'},
        {"stdout", no_argument, NULL, 'o'},
        {"syslog", no_argument, NULL, 's'},
        {"test-cmd", required_argument, NULL, 'T'},
        {"enable-deviceinfo", no_argument, NULL, OPT_ENABLE_DEVICEINFO},
        {"disable-deviceinfo", no_argument, NULL, OPT_DISABLE_DEVICEINFO},
        {"enable-ethernet", no_argument, NULL, OPT_ENABLE_ETHERNET},
        {"disable-ethernet", no_argument, NULL, OPT_DISABLE_ETHERNET},
        {"enable-bridging", no_argument, NULL, OPT_ENABLE_BRIDGING},
        {"disable-bridging", no_argument, NULL, OPT_DISABLE_BRIDGING},
        {"enable-ip", no_argument, NULL, OPT_ENABLE_IP},
        {"disable-ip", no_argument, NULL, OPT_DISABLE_IP},
        {NULL, 0, NULL, 0},
    };

    int opt;

    while ((opt = getopt_long(argc, argv, "tdsoT:", long_opts, NULL)) != -1)
    {
        switch (opt)
        {
            case 'o':
                ctx->log_flags = LOG_OPEN_STDOUT;
                break;
            case 's':
                ctx->log_flags = LOG_OPEN_SYSLOG;
                break;
            case 'd':
                ctx->initial_log_severity = LOG_SEVERITY_DEBUG;
                break;
            case 't':
                ctx->initial_log_severity = LOG_SEVERITY_TRACE;
                break;
            case 'T':
                ctx->test_cmd = optarg;
                break;
            case OPT_ENABLE_DEVICEINFO:
                ctx->opt_enable_deviceinfo = true;
                break;
            case OPT_DISABLE_DEVICEINFO:
                ctx->opt_enable_deviceinfo = false;
                break;
            case OPT_ENABLE_ETHERNET:
                ctx->opt_enable_ethernet = true;
                break;
            case OPT_DISABLE_ETHERNET:
                ctx->opt_enable_ethernet = false;
                break;
            case OPT_ENABLE_BRIDGING:
                ctx->opt_enable_bridging = true;
                break;
            case OPT_DISABLE_BRIDGING:
                ctx->opt_enable_bridging = false;
                break;
            case OPT_ENABLE_IP:
                ctx->opt_enable_ip = true;
                break;
            case OPT_DISABLE_IP:
                ctx->opt_enable_ip = false;
                break;
            default:
                oxm_print_usage(ctx, argc, argv);
                return -1;
        }
    }

    return 0;
}

static void oxm_ctx_start_test_cmd(oxm_ctx_t *ctx)
{
    const pid_t oxm_pid = getpid();
    ctx->test_cmd_pid = fork();
    if (ctx->test_cmd_pid == 0)
    {
        const char *oxm_pid_str = strfmta("%d", (int)oxm_pid);
        setenv("OXM_PID", oxm_pid_str, 1);
        execl("/bin/sh", "sh", "-c", ctx->test_cmd, (char *)NULL);
        exit(EXIT_FAILURE);
    }
    else if (ctx->test_cmd_pid < 0)
    {
        LOGW("Failed to fork for test command '%s': %d (%s)", ctx->test_cmd, errno, strerror(errno));
        ctx->test_cmd_pid = -1;
    }
    else
    {
        ev_child_init(&ctx->cmd_test_child, oxm_ctx_test_cmd_child_cb, ctx->test_cmd_pid, 0);
        ev_child_start(EV_DEFAULT_ & ctx->cmd_test_child);
        ctx->cmd_test_child.data = ctx;
    }
}

int main(int argc, char **argv)
{
    oxm_ctx_t ctx;

    oxm_ctx_init(&ctx);
    const int parse_err = oxm_ctx_parse_args(&ctx, argc, argv);
    if (parse_err)
    {
        return parse_err;
    }

    backtrace_init();
    log_open("OXM", ctx.log_flags);
    log_register_dynamic_severity(EV_DEFAULT);
    log_severity_set(ctx.initial_log_severity);
    manager_watchdog_init(EV_DEFAULT_ CONFIG_MANAGER_WATCHDOG_TIMEOUT_SEC);

    if (ctx.test_cmd != NULL)
    {
        oxm_ctx_start_test_cmd(&ctx);
        if (ctx.test_cmd_pid <= 0)
        {
            return -1;
        }

        LOGI("Started test command '%s' with pid %d", ctx.test_cmd, ctx.test_cmd_pid);
    }

    assert(ovsdb_init_loop(EV_DEFAULT, "OXM"));

    ox_router_t router;
    MEMZERO(router);
    assert(ox_router_init(&router));

    LOGI("DeviceInfo: %s", ctx.opt_enable_deviceinfo ? "enabled" : "disabled");
    if (ctx.opt_enable_deviceinfo)
    {
        assert(os_tr181_register_object(router.tr181_handle, "Device.DeviceInfo.") == OS_TR181_SUCCESS);
        /* Add pre-defined routes for DeviceInfo */
        assert(ox_dm_deviceinfo_add_routes(&router));
        /* The app can easily supplement with custom parameters/methods as needed,
         * see eg. oxm_dm_deviceinfo_get_uptime() */
        assert(os_tr181_register_parameter(
                       router.tr181_handle,
                       "Device.DeviceInfo.UpTime",
                       OS_TR181_TYPE_UINT,
                       OS_TR181_ACCESS_READONLY,
                       oxm_dm_deviceinfo_get_uptime,
                       NULL,
                       NULL)
               == OS_TR181_SUCCESS);

        /* Additional DeviceInfo parameters with custom callbacks */
        assert(ox_dm_deviceinfo_register(&router));

        /* Firmware upgrade mapping: register TR181 schema */
        assert(ox_dm_firmware_register(&router));
    }
    assert(os_tr181_register_object(router.tr181_handle, "Device.WiFi.") == OS_TR181_SUCCESS);
    // assert(os_tr181_register_object(router.tr181_handle, "Device.WiFi.Radio.") == OS_TR181_SUCCESS);
    // assert(os_tr181_register_object(router.tr181_handle, "Device.WiFi.SSID.") == OS_TR181_SUCCESS);
    // assert(os_tr181_register_object(router.tr181_handle, "Device.WiFi.AccessPoint.") == OS_TR181_SUCCESS);

    /* Add pre-defined routes for WiFi. */
    assert(ox_router_add_routes(&router, g_ox_dm_wifi));

    LOGI("Ethernet: %s", ctx.opt_enable_ethernet ? "enabled" : "disabled");
    if (ctx.opt_enable_ethernet)
    {
        assert(os_tr181_register_object(router.tr181_handle, "Device.Ethernet.") == OS_TR181_SUCCESS);
        /* Add pre-defined routes for Ethernet.Interface/Link. */
        assert(ox_dm_ethernet_add_routes(&router));
    }

    LOGI("Bridging: %s", ctx.opt_enable_bridging ? "enabled" : "disabled");
    if (ctx.opt_enable_bridging)
    {
        assert(os_tr181_register_object(router.tr181_handle, "Device.Bridging.") == OS_TR181_SUCCESS);
        /* Add pre-defined routes for Bridging.Bridge/Bridge.Port. */
        assert(ox_dm_bridging_add_routes(&router));
    }

    LOGI("IP: %s", ctx.opt_enable_ip ? "enabled" : "disabled");
    if (ctx.opt_enable_ip)
    {
        assert(os_tr181_register_object(router.tr181_handle, "Device.IP.") == OS_TR181_SUCCESS);
        /* Add pre-defined routes for IP.Interface/IPv4Address. */
        assert(ox_dm_ip_add_routes(&router));
    }

    assert(os_tr181_attach_loop(router.tr181_handle, EV_DEFAULT) == OS_TR181_SUCCESS);
    assert(os_tr181_publish_objects(router.tr181_handle) == OS_TR181_SUCCESS);

    /* Firmware upgrade mapping: create instances + start OVSDB monitor (after publish) */
    if (ctx.opt_enable_deviceinfo)
    {
        assert(ox_dm_firmware_post_publish(&router));
    }

    ev_run(EV_DEFAULT_ 0);
    LOGI("Exiting");

    if (ctx.test_cmd != NULL)
    {
        const int rstatus = ctx.cmd_test_rstatus;
        LOGI("Test command exited with status %d", rstatus);
        if (!WIFEXITED(rstatus) || WEXITSTATUS(rstatus) != 0)
        {
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}
