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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "log.h"
#include "util.h"
#include "ovsdb.h"

#include "pm.h"
#include "pm_tm.h"
#include "osp_reboot.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static struct osp_tm_ctx tm_ctx = { 0 };

static int pm_get_temperature(struct osp_tm_ctx *ctx, unsigned int temp_src, int *temperature);
static int pm_get_fan_rpm(struct osp_tm_ctx *ctx, unsigned int *fan_rpm);

static unsigned int pm_tbl_get_fanrpm(struct osp_tm_ctx *ctx, unsigned int state);
static int pm_tbl_get_temp_thresh(struct osp_tm_ctx *ctx, unsigned int state, unsigned int temp_src);
static unsigned int pm_tbl_get_radio_txchainmask(
        struct osp_tm_ctx *ctx,
        unsigned int state,
        unsigned int temp_src);
static unsigned int pm_get_highest_state(
        struct osp_tm_ctx *ctx,
        unsigned int temp_src,
        int temp,
        unsigned int prev_state);
static void pm_detect_fan_failure(
        struct osp_tm_ctx *ctx,
        unsigned int desired_fan_rpm,
        unsigned int fan_rpm);
static void pm_detect_over_temperature(struct osp_tm_ctx *ctx, unsigned int current_state);
static void pm_set_new_state(struct osp_tm_ctx *ctx, unsigned int new_state);
static int pm_calc_temp_moving_avg(struct osp_tm_ctx *ctx, unsigned int temp_src, int temperature);
static bool pm_is_temp_src_enabled(struct osp_tm_ctx *ctx, int idx);
static void pm_therm_cb(struct ev_loop *loop, ev_timer *timer, int revents);
static void pm_tm_reboot(struct osp_tm_ctx *ctx);

static int pm_get_temperature(struct osp_tm_ctx *ctx, unsigned int temp_src, int *temperature)
{
#ifdef PM_TM_DEBUG
    if (pm_tm_ovsdb_dbg_get_temperature(temp_src, temperature) == 0) {
        return 0;
    }
#endif

    return osp_tm_get_temperature(ctx->tgt_priv, temp_src, temperature);
}

static int pm_get_fan_rpm(struct osp_tm_ctx *ctx, unsigned int *fan_rpm)
{
#ifdef PM_TM_DEBUG
    if (pm_tm_ovsdb_dbg_get_fan_rpm(fan_rpm) == 0) {
        return 0;
    }
#endif

    return osp_tm_get_fan_rpm(ctx->tgt_priv, fan_rpm);
}

static unsigned int pm_tbl_get_fanrpm(struct osp_tm_ctx *ctx, unsigned int state)
{
    unsigned int fan_rpm;

    if (pm_tm_ovsdb_thermtbl_get_fan_rpm(state, &fan_rpm) != 0) {
        fan_rpm = ctx->therm_tbl[state].fan_rpm;
    }

    return fan_rpm;
}

static int pm_tbl_get_temp_thresh(struct osp_tm_ctx *ctx, unsigned int state, unsigned int temp_src)
{
    int temp;

    if (pm_tm_ovsdb_thermtbl_get_radio_temp(state, temp_src, &temp) != 0) {
        temp = ctx->therm_tbl[state].temp_thrld[temp_src];
    }

    return temp;
}

static unsigned int pm_tbl_get_radio_txchainmask(
        struct osp_tm_ctx *ctx,
        unsigned int state,
        unsigned int temp_src)
{
    unsigned int txchainmask;

    if (pm_tm_ovsdb_thermtbl_get_radio_txchainmask(state, temp_src, &txchainmask) != 0) {
        txchainmask = ctx->therm_tbl[state].radio_txchainmask[temp_src];
    }

    return txchainmask;
}

static unsigned int pm_get_highest_state(
        struct osp_tm_ctx *ctx,
        unsigned int temp_src,
        int temp,
        unsigned int prev_state)
{
    unsigned int highest_state = 0;
    unsigned int state;
    int hysteresis = CONFIG_PM_TM_TEMPERATURE_HYSTERESIS;

    for (state = 0; state < ctx->therm_state_cnt; state++)
    {
        if (state > prev_state) {
            hysteresis = 0;
        }

        if (temp < (pm_tbl_get_temp_thresh(ctx, state, temp_src) - hysteresis)) {
            break;
        }

        highest_state = state;
        continue;
    }

    return highest_state;
}

static void pm_detect_fan_failure(
        struct osp_tm_ctx *ctx,
        unsigned int desired_fan_rpm,
        unsigned int fan_rpm)
{
    int desired_fan_rpm_l;
    int desired_fan_rpm_h;

    desired_fan_rpm_l = desired_fan_rpm - CONFIG_PM_TM_FAN_RPM_TOLERANCE;
    desired_fan_rpm_h = desired_fan_rpm + CONFIG_PM_TM_FAN_RPM_TOLERANCE;

    if (((int)fan_rpm < desired_fan_rpm_l) || ((int)fan_rpm > desired_fan_rpm_h))
    {
        LOGE("TM: Fan cannot reach desired RPM: %d;%d", fan_rpm, desired_fan_rpm);
        ctx->fan_failure++;
        if (ctx->fan_failure > CONFIG_PM_TM_FAN_ERROR_PERIOD_TOLERANCE) {
            LOGE("TM: Setting HW error state");
            pm_tm_ovsdb_set_led_state(OSP_LED_ST_HWERROR, false);
        }
    }
    else
    {
        if (ctx->fan_failure != 0) {
            ctx->fan_failure = 0;
            pm_tm_ovsdb_set_led_state(OSP_LED_ST_HWERROR, true);
        }
    }
}

static void pm_detect_over_temperature(struct osp_tm_ctx *ctx, unsigned int current_state)
{
    if (current_state >= (ctx->therm_state_cnt - 1))
    {
        if (ctx->crit_temp_periods == 0) {
            pm_tm_ovsdb_set_led_state(OSP_LED_ST_THERMAL, false);
        }

        ctx->crit_temp_periods++;
        if (ctx->crit_temp_periods > CONFIG_PM_TM_CRITICAL_TEMPERATURE_PERIOD_TOLERANCE) {
            pm_tm_reboot(ctx);
        }
    }
    else
    {
        if (ctx->crit_temp_periods > 0) {
            ctx->crit_temp_periods = 0;
            pm_tm_ovsdb_set_led_state(OSP_LED_ST_THERMAL, true);
        }
    }
}

static void pm_set_new_state(struct osp_tm_ctx *ctx, unsigned int new_state)
{
    unsigned int temp_src;
    int rv;

    // set the new radio tx chainmask
    for (temp_src = 0; temp_src < ctx->temp_src_cnt; temp_src++)
    {
        unsigned int txchainmask_old;
        unsigned int txchainmask_new;

        txchainmask_old = pm_tbl_get_radio_txchainmask(ctx, ctx->prev_state, temp_src);
        txchainmask_new = pm_tbl_get_radio_txchainmask(ctx, new_state, temp_src);

        if (txchainmask_new != txchainmask_old)
        {
            const char *if_name = osp_tm_get_temp_src_name(ctx->tgt_priv, temp_src);

            // TODO: it might happen that radio will be torn down already

            LOGI("TM: Setting txchainmask on radio %d txchainmask %d", temp_src, txchainmask_new);
            rv = pm_tm_ovsdb_set_radio_txchainmask(if_name, txchainmask_new);
            if (rv != 0) {
                LOGE("TM: Could not set radio txchainmask; %s;%d", if_name, rv);
            }
        }
    }

    rv = pm_tm_ovsdb_set_state(new_state);
    if (rv != 0) {
        LOGE("TM: Could not update state in OVSDB");
    }
}

static int pm_calc_temp_moving_avg(struct osp_tm_ctx *ctx, unsigned int temp_src, int temperature)
{
    int *cur_temp_meas = &ctx->temp_avg_meas[temp_src][ctx->temp_avg_idx];
    int meas_cnt;
    int temp_avg;
    int i;

    if (*cur_temp_meas != 0) {
        ctx->temp_avg_sum[temp_src] -= *cur_temp_meas;
    }

    ctx->temp_avg_sum[temp_src] += temperature;
    *cur_temp_meas = temperature;

    meas_cnt = 0;
    for (i = 0; i < OSP_TM_TEMP_AVG_CNT; i++)
    {
        if (ctx->temp_avg_meas[temp_src][i] != 0) {
            meas_cnt++;
        }
    }

    if (meas_cnt > 0)
    {
        temp_avg = ctx->temp_avg_sum[temp_src] / meas_cnt;

        // division of integers always results in the floor()-ed value;
        // so instead of using floating point numbers, do a simple round()
        if ((ctx->temp_avg_sum[temp_src] % meas_cnt) >= (meas_cnt/2)) {
            temp_avg++;
        }
    }
    else {
        temp_avg = 0;
    }

    return temp_avg;
}

static bool pm_is_temp_src_enabled(struct osp_tm_ctx *ctx, int idx)
{
    const char *if_name;

    if_name = osp_tm_get_temp_src_name(ctx->tgt_priv, idx);
    if (if_name == NULL) {
        return false;
    }

    if (pm_tm_ovsdb_is_radio_enabled(if_name) == false) {
        return false;
    }

    return true;
}

static void pm_therm_cb(struct ev_loop *loop, ev_timer *timer, int revents)
{
    (void)loop;
    (void)revents;

    /*
     * loop through all temperature sources
     * read temperature
     * get the new highest thermal state
     * if new state is changed; set it
     * get the required fan rpm; set it
     */

    struct osp_tm_ctx *ctx = (struct osp_tm_ctx *)timer->data;
    unsigned int temp_src;
    char info_msg[128];
    char buf[32];
    int rv;

    unsigned int current_state = 0;
    unsigned int current_fan_rpm = 0;
    unsigned int fan_rpm = 0;

    strscpy(info_msg, "TM: Radio temperatures", sizeof(info_msg));

    // get the temperatures from temperature sources
    // get the highest thermal state
    for (temp_src = 0; temp_src < ctx->temp_src_cnt; temp_src++)
    {
        int temp;
        unsigned int state;

        if (pm_is_temp_src_enabled(ctx, temp_src) != true) {
            LOGN("TM: Temperature src %d is not enabled", temp_src);
            continue;
        }

        rv = pm_get_temperature(ctx, temp_src, &temp);
        if (rv != 0)
        {
            LOGE("TM: Could not get temperature from source %d:%s",
                 temp_src, osp_tm_get_temp_src_name(ctx->tgt_priv, temp_src));
            continue;
        }

        temp = pm_calc_temp_moving_avg(ctx, temp_src, temp);

        state = pm_get_highest_state(ctx, temp_src, temp, ctx->prev_state);
        if (state > current_state) {
            current_state = state;
        }

        snprintf(buf, sizeof(buf), " %s:%d",
            osp_tm_get_temp_src_name(ctx->tgt_priv, temp_src), temp);
        strscat(info_msg, buf, sizeof(info_msg));
    }

    // limit moving between states only 1 state at a time
    if (current_state > ctx->prev_state) {
        current_state = ctx->prev_state + 1;
    }
    else if (current_state < ctx->prev_state) {
        current_state = ctx->prev_state - 1;
    }

    // get the current fan RPM
    rv = pm_get_fan_rpm(ctx, &fan_rpm);
    if (rv < 0) {
        LOGE("TM: Could not get current fan RPM: %d", rv);
    }

    snprintf(buf, sizeof(buf), "; RPM:%d; therm_state:%u",
            fan_rpm, current_state);
    strscat(info_msg, buf, sizeof(info_msg));
    LOGI(info_msg);

    // check if fan RPM feedback is within some tolerance of the one we set
    pm_detect_fan_failure(ctx, ctx->prev_fan_rpm, fan_rpm);

    /*
     * check if we are off the scale with the temperature;
     * if the temperature is higher than the highest state,
     * for a couple of periods, reboot the device
     */
    pm_detect_over_temperature(ctx, current_state);

    current_fan_rpm = pm_tbl_get_fanrpm(ctx, current_state);
    if (current_fan_rpm != ctx->prev_fan_rpm) {
        LOGD("TM: Setting new fan RPM: %u", current_fan_rpm);
    }

    // has the thermal state changed?
    if (current_state != ctx->prev_state) {
        pm_set_new_state(ctx, current_state);
    }

    /*
     * set the required fan rpm speed
     * need to set it on every interval,
     * otherwise fan rpm watchdog will kick in
     * with highest possible RPM otherwise
     */
    rv = osp_tm_set_fan_rpm(ctx->tgt_priv, current_fan_rpm);
    if (rv != 0) {
        LOGE("TM: Could not set new fan RPM: %d:%d", current_fan_rpm,  rv);
    }

    // finally save the current states and fan RPM for the next run
    ctx->prev_fan_rpm = current_fan_rpm;
    ctx->prev_state = current_state;

    ctx->temp_avg_idx++;
    ctx->temp_avg_idx = ctx->temp_avg_idx % OSP_TM_TEMP_AVG_CNT;

    return;
}

static void pm_tm_reboot(struct osp_tm_ctx *ctx)
{
    (void)ctx;

    osp_unit_reboot_ex(OSP_REBOOT_THERMAL, "Critical temperature, rebooting due to overheating", 0);
}

bool pm_tm_init(void)
{
    LOGN("Initializing TM");

    if (pm_tm_ovsdb_init(&tm_ctx) != 0) {
        LOGE("Initializing TM (failed to initialize TM OVSDB)");
        return false;
    }

    // init timer for main thermal loop
    ev_timer_init(&tm_ctx.therm_timer, pm_therm_cb, 1,
        CONFIG_PM_TM_PERIOD_INTERVAL);
    ev_timer_start(EV_DEFAULT, &tm_ctx.therm_timer);
    tm_ctx.therm_timer.data = &tm_ctx;

    if (osp_tm_init(&tm_ctx.therm_tbl,
            &tm_ctx.therm_state_cnt,
            &tm_ctx.temp_src_cnt,
            &tm_ctx.tgt_priv) != 0) {
        LOGE("Initializing TM (failed to initialize OSP TM)");
        return false;
    }

    osp_tm_get_fan_rpm(tm_ctx.tgt_priv, &tm_ctx.prev_fan_rpm);

    return true;
}

bool pm_tm_deinit(void)
{
    osp_tm_deinit(&tm_ctx.tgt_priv);
    return true;
}
