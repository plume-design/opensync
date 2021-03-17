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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <ev.h>
#include <unistd.h>

#include "log.h"
#include "ovsdb.h"
#include "schema.h"

#include "monitor.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "os_util.h"
#include "util.h"

#include "target.h"
#include "pasync.h"
#include "dm.h"
#include "module.h"


#define ST_STATUS_OK    (0)
#define ST_STATUS_JSON  (-1)
#define ST_STATUS_READ  (-2)


#define ST_EXE              "iperf3"
#define ST_PATH             "/usr/bin/" ST_EXE
#define ST_DEBUG_JSON_PATH  "/tmp/debug_iperf_out.log"
#define ST_DEF_LEN          (10)  /* default speedtest duration, seconds */
#define ST_WAIT_TIMEOUT     (30)  /* st wait timeout in addition to duration */


#define ST_IS_IPERF3_S(st_ctx) \
        (strcmp(st_ctx->st_config.test_type, "IPERF3_S") == 0)

#define ST_IS_IPERF3_C(st_ctx) \
        (strcmp(st_ctx->st_config.test_type, "IPERF3_C") == 0)

#define ST_IS_DLUL(st_ctx) \
        (strcmp(st_ctx->st_config.st_dir, "DL_UL") == 0)

#define ST_IS_DL(st_ctx) \
        (strcmp(st_ctx->st_config.st_dir, "DL") == 0)
#define ST_IS_UL(st_ctx) \
        (strcmp(st_ctx->st_config.st_dir, "UL") == 0)

struct st_context
{
    struct schema_Wifi_Speedtest_Config st_config;
    struct schema_Wifi_Speedtest_Status st_status;

    bool is_reverse;
    unsigned run_cnt;
    unsigned run_cnt_max;
};

static ev_timer st_timeout;

static bool iperf_run_async(struct st_context *st_ctx, bool run_reverse);
static bool iperf_debug_log(const char *buff, size_t buff_sz);


static bool iperf_parse_json(
        json_t *js_root,
        struct st_context *st_ctx)
{
    double duration;
    double bandwidth;
    int is_reverse;
    int bytes;
    char *host_remote = NULL;
    char *proto = NULL;
    bool is_udp;
    json_error_t error;
    json_t *js;
    json_t *js_sec;
    json_t *js_bw;


    /* If "error" key present in json, an error occured, log the error string: */
    const char *str_error = NULL;
    if (json_unpack(js_root, "{s:s}", "error", &str_error) == 0)
    {
        LOG(ERR, "ST_IPERF: Iperf cmd failed with error string: '%s'", str_error);
        return false;
    }

    /* Remote host IP: */
    if (json_unpack_ex(js_root, &error, 0, "{s:{s:o}}", "start", "connected", &js) != 0)
    {
        LOG(ERR, "ST_IPERF: JSON parse error (start:connected): %d:%d: %s",
                error.line, error.column, error.text);
        return false;
    }
    if (json_is_array(js) && json_array_size(js) > 0)
    {
        json_t *js_conn = json_array_get(js, 0);
        json_unpack(js_conn, "{s:s}", "remote_host", &host_remote);
    }

    /* Determine if this is 'reverse' test and protocol (TCP/UDP): */
    if (json_unpack_ex(js_root, &error, 0, "{s:{s:o}}", "start", "test_start", &js) != 0)
    {
        LOG(ERR, "ST_IPERF: JSON parse error (start:test_start): %d:%d: %s",
                error.line, error.column, error.text);
        return false;
    }
    if (json_unpack_ex(js, &error, 0, "{s:i, s:s}",
            "reverse", &is_reverse,
            "protocol", &proto) != 0)
    {
        LOG(ERR, "ST_IPERF: JSON parse error (reverse, protocol): %d:%d: %s",
                error.line, error.column, error.text);
        return false;
    }

    /* iperf "reverse" test (server sends, client receives)? */
    st_ctx->is_reverse = is_reverse;

    /* UDP test? (instead of default TCP)? */
    if (strncmp(proto, "UDP", strlen("UDP")) == 0)
        is_udp = true;
    else
        is_udp = false;

    const char *key_end_sum = is_udp ? "sum" : "sum_received"; /* different json key for UDP tests: */
    if (json_unpack_ex(js_root, &error, 0, "{s:{s:o}}", "end", key_end_sum, &js) != 0)
    {
        LOG(ERR, "JSON parse error (end:sum_received/sum): %d:%d: %s",
                error.line, error.column, error.text);
        return false;
    }

    /* Test results: */
    double pkt_loss = 0.0;
    json_t *js_pkt_loss = NULL;
    double jitter = 0.0;
    json_t *js_jitter = NULL;
    if (json_unpack_ex(js, &error, 0, "{s:o, s:i, s:o, s?o, s?o}",
            "seconds", &js_sec,
            "bytes", &bytes,
            "bits_per_second", &js_bw,
            "lost_percent", &js_pkt_loss,
            "jitter_ms", &js_jitter) != 0)
    {
        LOG(ERR, "ST_IPERF: JSON parse error (seconds, bytes, bits_per_second): %d:%d: %s",
                error.line, error.column, error.text);
        return false;
    }
    if (!json_is_number(js_sec) || !json_is_number(js_bw))
    {
        LOG(ERR, "ST_IPERF: JSON parse error: end:sum_received: seconds|bits_per_second: not a number");
        return false;
    }
    /* iperf sometimes returning results in integer sometimes in "float" format: */
    duration = json_is_real(js_sec) ?
            json_real_value(js_sec) : (double)json_integer_value(js_sec);
    bandwidth = json_is_real(js_bw) ?
            json_real_value(js_bw) : (double) json_integer_value(js_bw);
    if (is_udp)
    {
        if (!json_is_number(js_pkt_loss) || !json_is_number(js_jitter))
        {
            LOG(ERR, "ST_IPERF: JSON parse error: end:sum: lost_percent|jitter_ms: not a number");
            return false;
        }
        pkt_loss = json_is_real(js_pkt_loss) ?
                json_real_value(js_pkt_loss) : (double) json_integer_value(js_pkt_loss);

        jitter = json_is_real(js_jitter) ?
                json_real_value(js_jitter) : (double) json_integer_value(js_jitter);
    }

    /* Determine if this is DL or UL speedtest: */
    bool is_UL = false;
    if (ST_IS_IPERF3_S(st_ctx) && is_reverse)
        is_UL = true;
    if (ST_IS_IPERF3_C(st_ctx) && !is_reverse)
        is_UL = true;


    LOG(INFO, "ST_IPERF: Parsed JSON results: %s: is_reverse=%d, direction=%s, "
               "proto=%s, remote_host=%s",
               st_ctx->st_config.test_type,
               is_reverse,
               (is_UL ? "UL" : "DL"),
               (proto ? proto : "?"),
               (host_remote ? host_remote : "?"));

    /* Fill up st_status with test results: */
    if (host_remote)
    {
        strscpy(st_ctx->st_status.host_remote, host_remote, sizeof(st_ctx->st_status.host_remote));
        st_ctx->st_status.host_remote_exists = true;
    }

    if (is_UL)
    {
        st_ctx->st_status.UL_duration = duration;
        st_ctx->st_status.UL_duration_exists = true;
        st_ctx->st_status.UL_bytes = bytes;
        st_ctx->st_status.UL_bytes_exists = true;
        st_ctx->st_status.UL = bandwidth / 1e6;  // [bits/s] --> [Mbit/s]
        st_ctx->st_status.UL_exists = true;
        if (is_udp)
        {
            st_ctx->st_status.UL_pkt_loss = pkt_loss;
            st_ctx->st_status.UL_pkt_loss_exists = true;
            st_ctx->st_status.UL_jitter = jitter;
            st_ctx->st_status.UL_jitter_exists = true;
        }
    }
    else
    {
        st_ctx->st_status.DL_duration = duration;
        st_ctx->st_status.DL_duration_exists = true;
        st_ctx->st_status.DL_bytes = bytes;
        st_ctx->st_status.DL_bytes_exists = true;
        st_ctx->st_status.DL = bandwidth / 1e6;  // [bits/s] --> [Mbit/s]
        st_ctx->st_status.DL_exists = true;
        if (is_udp)
        {
            st_ctx->st_status.DL_pkt_loss = pkt_loss;
            st_ctx->st_status.DL_pkt_loss_exists = true;
            st_ctx->st_status.DL_jitter = jitter;
            st_ctx->st_status.DL_jitter_exists = true;
        }
    }
    st_ctx->st_status.duration += duration;
    st_ctx->st_status.duration_exists = true;

    return true;
}

static bool iperf_parse_json_output(
        const char *json_buf,
        size_t buflen,
        struct st_context *st_ctx)
{
    json_t *js;
    json_error_t je;

    js = json_loadb(json_buf, buflen, JSON_DISABLE_EOF_CHECK, &je);
    if (js == NULL)
    {
        LOG(ERR, "ST_IPERF: JSON validation failed: '%s' (line=%d pos=%d)",
           je.text,
           je.line,
           je.position);

        iperf_debug_log(json_buf, buflen);
        return false;
    }
    if (!iperf_parse_json(js, st_ctx))
    {
        LOG(ERR, "ST_IPERF: Failed to parse json.");
        iperf_debug_log(json_buf, buflen);

        json_decref(js);
        return false;
    }
    json_decref(js);
    return true;
}

/* pasync API callback, invoked when speedtest command completes. */
static void iperf_on_st_completed_cb(pasync_ctx_t *ctx, void *buff, int buff_sz)
{
    struct st_context *st_ctx = (struct st_context *)ctx->data;
    int status = ST_STATUS_READ;
    pjs_errmsg_t err;
    ovs_uuid_t uuid = { '\0' };

    LOG(DEBUG, "ST_IPERF: %s: buff_size=%d", __func__, buff_sz);
    if (buff_sz > 0)
    {
        /* parse iperf results */
        if (iperf_parse_json_output(buff, (size_t)buff_sz, st_ctx)
                && (st_ctx->st_status.UL_exists || st_ctx->st_status.DL_exists))
        {
            status = ST_STATUS_OK;
        }
    }
    else
    {
        if (ctx->rc != 0)
            LOG(ERR, "ST_IPERF: Speedtest command failed with rc=%d", ctx->rc);
        if (buff_sz == 0)
            LOG(ERR, "ST_IPERF: No output from speedtest app received.");
    }

    /* If this was 1st run, execute 2nd run (to measure both DL and UL): */
    if (status == ST_STATUS_OK && st_ctx->run_cnt == 1
            && st_ctx->run_cnt < st_ctx->run_cnt_max)
    {
        LOG(DEBUG, "ST_IPERF: %s: ST 1st run completed. Execute 2nd run.",
                   st_ctx->st_config.test_type);

        if (ST_IS_IPERF3_C(st_ctx))
            sleep(2);

        if (!iperf_run_async(st_ctx, true))  // 2nd run: run with reverse_mode=true
        {
            goto st_end;
        }
        LOG(DEBUG, "ST_IPERF: %s: ST 2nd run executed.", st_ctx->st_config.test_type);

        return; // async process run.
    }

    /* 2nd run completed. All results ready. Insert into ovsdb. */
    st_ctx->st_status.status = status;
    st_ctx->st_status.status_exists = true;
    st_ctx->st_status.testid = ctx->id;
    st_ctx->st_status.testid_exists = true;

    strscpy(st_ctx->st_status.test_type, st_ctx->st_config.test_type,
            sizeof(st_ctx->st_status.test_type));
    st_ctx->st_status.test_type_exists = true;

    st_ctx->st_status.timestamp = time(NULL);
    st_ctx->st_status.timestamp_exists = true;

    LOG(NOTICE, "ST_IPERF: Speedtest results:"
                " DL: %f Mbit/s (bytes=%d, duration=%f s),"
                " UL: %f Mbit/s (bytes=%d, duration=%f s),"
                " duration: %f s, remote_host: %s,"
                " testid: %d, status=%d, timestamp=%d",
                 st_ctx->st_status.DL,
                 st_ctx->st_status.DL_bytes,
                 st_ctx->st_status.DL_duration,
                 st_ctx->st_status.UL,
                 st_ctx->st_status.UL_bytes,
                 st_ctx->st_status.UL_duration,
                 st_ctx->st_status.duration,
                 st_ctx->st_status.host_remote,
                 st_ctx->st_status.testid,
                 st_ctx->st_status.status,
                 st_ctx->st_status.timestamp);


    /* insert ST results into ovsdb row: */
    if (!ovsdb_sync_insert(SCHEMA_TABLE(Wifi_Speedtest_Status),
                           schema_Wifi_Speedtest_Status_to_json(&st_ctx->st_status, err),
                           &uuid))
    {
        LOG(ERR, "ST_IPERF: ovsdb: Wifi_Speedtest_Status: insert error: "
                 "ST results not written.");
    }
    else
    {
        LOG(NOTICE, "ST_IPERF: ovsdb: Wifi_Speedtest_Status: "
                    "ST results written. uuid=%s", uuid.uuid);
    }

st_end:
    ev_timer_stop(EV_DEFAULT, &st_timeout);
    /* signal that ST has completed: */
    dm_st_in_progress_set(false);

    /* free pasync user data context: */
    free(st_ctx);
}

static void iperf_on_timeout(struct ev_loop *loop, ev_timer *watcher, int revent)
{
    int rc;
    const char *cmd = "killall -KILL " ST_EXE;

    LOG(DEBUG, "ST_IPERF: %s called. st_in_progress=%d", __func__, dm_st_in_progress_get());

    if (dm_st_in_progress_get())
    {
        LOG(WARN, "ST_IPERF: maximum duration exceeded. Killing speedtest app.");

        LOG(DEBUG, "Running system cmd: %s", cmd);
        rc = system(cmd);
        if (!(WIFEXITED(rc) && WEXITSTATUS(rc) == 0))
            LOG(ERR, "Error executing system command: %s", cmd);

        dm_st_in_progress_set(false);
    }
}

/* Run iperf command in async manner. */
static bool iperf_run_async(struct st_context *st_ctx, bool run_reverse)
{
    char st_cmd[TARGET_BUFF_SZ];
    char arg_port[64] = { 0 };

    if (st_ctx->run_cnt >= st_ctx->run_cnt_max)
    {
        LOG(ERR, "ST_IPERF: Only %u runs of iperf allowed.", st_ctx->run_cnt_max);
        return false;
    }

    if (st_ctx->st_config.st_port_exists) // server port to listen on/connect to
    {
        snprintf(arg_port, sizeof(arg_port), " -p %d",
                 st_ctx->st_config.st_port);
    }

    /* Build speedtest command: */
    if (ST_IS_IPERF3_S(st_ctx))  // iperf server
    {
        char *arg_bind = "";

        if (st_ctx->st_config.st_server_exists) // bind to a specific interface
        {
            arg_bind = strfmta(" -B %s", st_ctx->st_config.st_server);
        }

        snprintf(st_cmd, sizeof(st_cmd), "%s -s -1 -i 0 -J %s%s",
                 ST_PATH,
                 arg_bind,
                 arg_port);
    }
    else if (ST_IS_IPERF3_C(st_ctx)) // iperf client
    {
        char arg_len[64] = { 0 };
        char arg_parallel[64] = { 0 };
        char arg_bw[64] = { 0 };
        const char *arg_reverse = "";
        const char *arg_udp = "";

        if (!st_ctx->st_config.st_server_exists) // connect to host
        {
            LOG(ERR, "ST_IPERF_C: st_server not specified");
            return false;
        }

        arg_reverse = run_reverse ? "-R" : "";
        if (st_ctx->st_config.st_len_exists) // time in seconds to transmit
        {
            snprintf(arg_len, sizeof(arg_len), " -t %d",
                     st_ctx->st_config.st_len);
        }
        if (st_ctx->st_config.st_parallel_exists) // number of parallel streams
        {
            snprintf(arg_parallel, sizeof(arg_parallel), " -P %d",
                     st_ctx->st_config.st_parallel);
        }
        if (st_ctx->st_config.st_udp_exists && st_ctx->st_config.st_udp) // use UDP rather than TCP
        {
            arg_udp = " -u";
        }
        if (st_ctx->st_config.st_bw_exists) // target bandwidth [bits/sec]
        {
            snprintf(arg_bw, sizeof(arg_bw), " -b %d",
                     st_ctx->st_config.st_bw);
        }

        snprintf(st_cmd, sizeof(st_cmd), "%s -i 0 -O 2 -J -c %s %s%s%s%s%s%s",
                 ST_PATH,
                 st_ctx->st_config.st_server,
                 arg_reverse,
                 arg_port,
                 arg_len,
                 arg_parallel,
                 arg_udp,
                 arg_bw);
    }
    else
    {
        LOG(ERR, "ST_IPERF: Unknown ST type: %s", st_ctx->st_config.test_type);
        return false;
    }

    LOG(DEBUG, "ST_IPERF: Executing speedtest: test_type=%s, run_cnt=%u, is_reverse=%d",
               st_ctx->st_config.test_type, st_ctx->run_cnt, run_reverse);
    LOG(DEBUG, "ST_IPERF: Running command: %s", st_cmd);

    /* Run speedtest: */
    if (!pasync_ropenx(EV_DEFAULT,
                       st_ctx->st_config.testid,
                       st_ctx,
                       st_cmd,
                       iperf_on_st_completed_cb))
    {
        LOG(ERR, "ST_IPERF: Failed executing command with pasync: %s", st_cmd);
        return false;
    }

    LOG(NOTICE, "ST_IPERF: speedtest %s(%d) started!",
                 st_ctx->st_config.test_type,
                 run_reverse);

    st_ctx->run_cnt++;
    dm_st_in_progress_set(true);
    return true;
}

/* Run iperf speedtest (server or client) according to config,
 * both UL and DL. */
bool iperf_run_speedtest(struct schema_Wifi_Speedtest_Config *st_config)
{
    const unsigned RUN_CNT_MAX = 2;
    struct st_context *st_ctx;
    bool reverse_mode = false;
    int timeout;


    if (dm_st_in_progress_get())
    {
        LOG(ERR, "ST_IPERF: Speedtest already in progress");
        return false;
    }

    /* Speedtest context: must be freed in pasync (on_st_completed_cb) callback. */
    st_ctx = calloc(1, sizeof(*st_ctx));
    st_ctx->st_config = *st_config;
    st_ctx->run_cnt_max = RUN_CNT_MAX;

    if (st_config->st_dir_exists)
    {
        if (ST_IS_DL(st_ctx) || ST_IS_UL(st_ctx))
        {
            st_ctx->run_cnt_max = 1;
            if (ST_IS_IPERF3_C(st_ctx) && ST_IS_DL(st_ctx))
            {
                reverse_mode = true;
            }
        }
    }

    if (!iperf_run_async(st_ctx, reverse_mode))
    {
        free(st_ctx);
        return false;
    }

    /* ST timeout: timeout for each direction + "wait timeout": */
    if (st_ctx->st_config.st_len_exists)
        timeout = st_ctx->run_cnt_max * st_ctx->st_config.st_len + ST_WAIT_TIMEOUT;
    else
        timeout = st_ctx->run_cnt_max * ST_DEF_LEN + ST_WAIT_TIMEOUT;

    ev_timer_init(&st_timeout, iperf_on_timeout, timeout, 0.0);
    ev_timer_start(EV_DEFAULT, &st_timeout);

    LOG(DEBUG, "ST_IPERF: %s: ST 1st run executed.", st_ctx->st_config.test_type);
    return true;
}

/*
 * Iperf may produce pretty huge JSON output which might (or will) get truncated
 * by system logger.
 *
 * This is for logging iperf json parse errors.
 */
static bool iperf_debug_log(const char *buff, size_t buff_sz)
{
    FILE *f;
    const char *file_path = ST_DEBUG_JSON_PATH;

    f = fopen(file_path, "w");
    if (f == NULL)
    {
        LOG(ERR, "Error opening file: %s", file_path);
        return false;
    }

    size_t len = fwrite(buff, 1, buff_sz, f);
    if (len == (size_t)buff_sz)
        LOG(DEBUG, "ST_IPERF: Wrote %zu bytes to %s", len, file_path);
    else
        LOG(DEBUG, "ST_IPERF: Error writing to %s: Wrote only %zu bytes", file_path, len);
    fclose(f);
    return true;
}

/* DM speedtest plugin module */

struct dm_st_plugin dm_st_plugin_iperf3_s =
{
    .st_name = "IPERF3_S",
    .st_run = iperf_run_speedtest,
};

struct dm_st_plugin dm_st_plugin_iperf3_c =
{
    .st_name = "IPERF3_C",
    .st_run = iperf_run_speedtest,
};

void dm_st_iperf_init(void *data)
{
    dm_st_plugin_register(&dm_st_plugin_iperf3_s);
    dm_st_plugin_register(&dm_st_plugin_iperf3_c);
}

void dm_st_iperf_fini(void *data)
{
    dm_st_plugin_unregister(&dm_st_plugin_iperf3_s);
    dm_st_plugin_unregister(&dm_st_plugin_iperf3_c);
}

MODULE(dm_st_iperf, dm_st_iperf_init, dm_st_iperf_fini)

