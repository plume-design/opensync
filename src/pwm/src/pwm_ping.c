/*
 * Copyright (c) 2021, Sagemcom.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pwm_ping.h"
#include "pwm_ovsdb.h"
#include "pwm_wifi.h"

#define CMD_LEN 156
#define WIFI_IF_NAME "wl0.4"

static int g_ping_fail_count;
static ev_timer keep_alive_timer;
static pint_data_t ping_addr;
static bool timer_already_up = false;

static void pwm_ping_check_ip_alive()
{
    int err;
    char cmd[CMD_LEN];

    if (ping_addr.inet == AF_INET6) {
        snprintf(cmd, sizeof(cmd) - 1, "ping6 -c 1 -W 1 %s", ping_addr.ip);
    } else if (ping_addr.inet == AF_INET) {
        snprintf(cmd, sizeof(cmd) - 1, "ping -c 1 -W 1 %s", ping_addr.ip);
    } else {
        LOGE("Invalid inet family");
        return;
    }

    err = cmd_log(cmd);
    if (err) {
        LOGE("Ping to PW end-point %s failed", ping_addr.ip);
        g_ping_fail_count++;
    } else {
        g_ping_fail_count = 0;
    }
    return;
}

static void pwm_ping_keep_alive_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    if (!pwm_ovsdb_is_enabled()) {
        ev_timer_stop(EV_DEFAULT_ &keep_alive_timer);
        return;
    }
    pwm_ping_check_ip_alive();

    if (g_ping_fail_count >= PING_MAX_FAIL_COUNT)
    {
        //Ping failed 3 times
        LOGW("Tunnel connection broken");
        pwm_ovsdb_reset();
        //Disable the public wifi from the Wifi_VIF_Config table
        pwm_wifi_update_vif_config(WIFI_IF_NAME, false);
        ev_timer_stop(EV_DEFAULT_ &keep_alive_timer);
        return;
    }
}

bool pwm_ping_init_keep_alive(int inet, int keep_alive_timeout, const char *dip)
{
    if (!dip) {
        LOGE("[%s] Invalid argument", __func__);
        return false;
    }

    if (keep_alive_timeout < 5) {
        LOGE("[%s] Invalid keepalive argument = [%d]", __func__, keep_alive_timeout);
        return false;
    }

    ping_addr.inet = inet;
    ping_addr.ping_keep_alive_timeout = keep_alive_timeout;
    STRSCPY_WARN(ping_addr.ip, dip);

    pwm_ping_check_ip_alive();
    if (!timer_already_up)
    {
        g_ping_fail_count = 0;

        ev_timer_init(&keep_alive_timer, pwm_ping_keep_alive_timer_cb, ping_addr.ping_keep_alive_timeout, ping_addr.ping_keep_alive_timeout);
        ev_timer_start(EV_DEFAULT_ &keep_alive_timer);
        timer_already_up = true;
        LOGD("[%s] PWM tunnel keepalive timer added timeout [%d]", __func__, ping_addr.ping_keep_alive_timeout);
    }

    return true;
}

bool pwm_ping_stop_keep_alive(void)
{
    MEM_SET(&ping_addr, 0, sizeof(ping_addr));
    ev_timer_stop(EV_DEFAULT_ &keep_alive_timer);
    timer_already_up = false;
    return true;
}
