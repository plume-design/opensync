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

#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>

#include "sm.h"
#include "osp_temp.h"
#include "osp_tm.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define DEVICE_THERMAL_TIMER_SEC                60

/* new part */
typedef struct
{
    bool                            initialized;

    /* Internal structure used to lower layer radio selection */
    ev_timer                        report_timer;
    ev_timer                        thermal_report_timer;

    /* Structure containing cloud request timer params */
    sm_stats_request_t              request;
    /* Structure pointing to upper layer device storage */
    dpp_device_report_data_t        report;

    /* Reporting start timestamp used for reporting timestamp calculation */
    uint64_t                        report_ts;
} sm_device_ctx_t;

/* Common place holder for all neighbor stat report contexts */
static sm_device_ctx_t              g_sm_device_ctx;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static
bool dpp_device_report_timer_set(
        ev_timer                   *timer,
        bool                        enable)
{
    if (enable) {
        ev_timer_again(EV_DEFAULT, timer);
    }
    else {
        ev_timer_stop(EV_DEFAULT, timer);
    }

    return true;
}


static
bool dpp_device_report_timer_restart(
        ev_timer                   *timer)
{
    sm_device_ctx_t                *device_ctx =
        (sm_device_ctx_t *) timer->data;
    sm_stats_request_t             *request_ctx =
        &device_ctx->request;

    if (request_ctx->reporting_count) {
        request_ctx->reporting_count--;

        LOG(DEBUG,
            "Updated device reporting count=%d",
            request_ctx->reporting_count);

        /* If reporting_count becomes zero, then stop reporting */
        if (0 == request_ctx->reporting_count) {
            dpp_device_report_timer_set(timer, false);
            dpp_device_report_timer_set(&device_ctx->thermal_report_timer, false);

            LOG(DEBUG,
                "Stopped device reporting (count expired)");
            return true;
        }
    }

    return true;
}

static
bool sm_device_temp_list_clear (
        ds_dlist_t                 *temp_list)
{
    dpp_device_temp_t              *record = NULL;
    ds_dlist_iter_t                 record_iter;

    for (   record = ds_dlist_ifirst(&record_iter, temp_list);
            record != NULL;
            record = ds_dlist_inext(&record_iter))
    {
        ds_dlist_iremove(&record_iter);
        dpp_device_temp_record_free(record);
        record = NULL;
    }

    return true;
}

static
bool sm_device_thermal_list_clear (
        ds_dlist_t                 *thermal_list)
{
    dpp_device_thermal_record_t    *record = NULL;
    ds_dlist_iter_t                 record_iter;

    for (   record = ds_dlist_ifirst(&record_iter, thermal_list);
            record != NULL;
            record = ds_dlist_inext(&record_iter))
    {
        ds_dlist_iremove(&record_iter);
        dpp_device_thermal_record_free(record);
        record = NULL;
    }

    return true;
}

static
void sm_device_report (EV_P_ ev_timer *w, int revents)
{
    bool                           rc;
    int                            temp_src_ix;
    int                            temp_val;

    sm_device_ctx_t                *device_ctx =
        (sm_device_ctx_t *) w->data;
    dpp_device_report_data_t       *report_ctx =
        &device_ctx->report;
    sm_stats_request_t             *request_ctx =
        &device_ctx->request;
    ev_timer                       *report_timer =
        &device_ctx->report_timer;

    dpp_device_report_timer_restart(report_timer);

    /* Get device stats */
    rc =
        target_stats_device_get (
                &report_ctx->record);
    if (true != rc) {
        return;
    }

    LOG(DEBUG,
        "Sending device stats load %0.2f %0.2f %0.2f\n",
        report_ctx->record.load[DPP_DEVICE_LOAD_AVG_ONE],
        report_ctx->record.load[DPP_DEVICE_LOAD_AVG_FIVE],
        report_ctx->record.load[DPP_DEVICE_LOAD_AVG_FIFTEEN]);

    /* Get radio temperature stats */
    ds_tree_t                      *radios = sm_radios_get();
    sm_radio_state_t               *radio;
    dpp_device_temp_t              *temp;
    char                           *radio_name;
    ds_tree_foreach(radios, radio)
    {
        temp = NULL;
        temp =
            dpp_device_temp_record_alloc();
        if (NULL == temp) {
            goto clean;
        }

        radio_name = radio_get_name_from_type((&radio->config)->type);
        temp_src_ix = osp_temp_get_idx_from_band(radio_name);
        if (temp_src_ix == -1)
        {
            dpp_device_temp_record_free(temp);
            continue;
        }

        rc = osp_temp_get_temperature(temp_src_ix, &temp_val);
        if (rc != 0)
        {
            dpp_device_temp_record_free(temp);
            continue;
        }

        /* Assign values to device temperature config struct */
        temp->value = (int32_t)temp_val;
        temp->type = (&radio->config)->type;

        LOG(DEBUG,
            "Sending device stats %s temp %d\n",
            radio_name,
            temp->value);

        /* Add temperature config to report */
        ds_dlist_insert_tail(&report_ctx->temp, temp);
    }

    /* Report_timestamp is base-timestamp + relative start time offset */
    report_ctx->timestamp_ms =
        request_ctx->reporting_timestamp - device_ctx->report_ts +
        get_timestamp();

    report_ctx->record.populated = true;

    LOG(INFO,
        "uptime=%u, mem=%u/%u, cpu=%u, file_handles=%u/%u",
        report_ctx->record.uptime,
        report_ctx->record.mem_util.mem_used,
        report_ctx->record.mem_util.mem_total,
        report_ctx->record.cpu_util.cpu_util,
        report_ctx->record.used_file_handles,
        report_ctx->record.total_file_handles);

    LOG(INFO,
        "Sending device report at '%s'",
        sm_timestamp_ms_to_date(report_ctx->timestamp_ms));

    dpp_put_device(report_ctx);

clean:
    /* Clear temperature list */
    sm_device_temp_list_clear(&report_ctx->temp);
    sm_device_thermal_list_clear(&report_ctx->thermal_records);

    SM_SANITY_CHECK_TIME(report_ctx->timestamp_ms,
                         &request_ctx->reporting_timestamp,
                         &device_ctx->report_ts);
}


static
void sm_device_thermal_report (EV_P_ ev_timer *w, int revents)
{
    bool                            rc;
    bool                            thermal_valid = false;
    int                             log_pos;
    char                            log_msg[256];

    sm_device_ctx_t                *device_ctx =
        (sm_device_ctx_t *) w->data;

    dpp_device_report_data_t       *report_ctx =
        &device_ctx->report;
    sm_stats_request_t             *request_ctx =
        &device_ctx->request;
    dpp_device_thermal_record_t    *thermal_record;
    uint32_t                        fan_rpm;
    uint16_t                        fan_duty_cycle;
    int                             thermal_state;
    int                             target_rpm;
    int                             led_state;
    thermal_record = dpp_device_thermal_record_alloc();

    log_pos = snprintf(log_msg, sizeof(log_msg), "Sending device stats: ");

    rc = target_stats_device_fanrpm_get(&fan_rpm);
    if(true == rc)
    {
        thermal_record->fan_rpm = fan_rpm;
        thermal_valid = true;
        log_pos += snprintf(log_msg + log_pos,
                            sizeof(log_msg) - log_pos,
                            "fan_rpm=%u, ", thermal_record->fan_rpm);
    }
    else
    {
        thermal_record->fan_rpm = -1;
    }

    rc = target_stats_device_fandutycycle_get(&fan_duty_cycle);
    if(true == rc)
    {
        thermal_record->fan_duty_cycle = (int16_t)fan_duty_cycle;
        thermal_valid = true;
        log_pos += snprintf(log_msg + log_pos,
                            sizeof(log_msg) - log_pos,
                            "fan_duty_cycle=%u, ", thermal_record->fan_duty_cycle);
    }
    else
    {
        thermal_record->fan_duty_cycle = -1;
    }

    rc = osp_tm_ovsdb_get_thermal_state(&thermal_state);
    if(true == rc)
    {
        thermal_record->thermal_state = (int16_t)thermal_state;
        thermal_valid = true;
        log_pos += snprintf(log_msg + log_pos,
                            sizeof(log_msg) - log_pos,
                            "thermal_state=%u, ", thermal_record->thermal_state);
    }
    else
    {
        thermal_record->thermal_state = -1;
    }

    rc = osp_tm_get_fan_rpm_from_thermal_state(thermal_state, &target_rpm);
    if(true == rc)
    {
        thermal_record->target_rpm = (int16_t)target_rpm;
        thermal_valid = true;
        log_pos += snprintf(log_msg + log_pos,
                            sizeof(log_msg) - log_pos,
                            "target_rpm=%u, ", thermal_record->target_rpm);
    }
    else
    {
        thermal_record->target_rpm = -1;
    }

    for (int pos = 0; pos < DPP_DEVICE_LED_COUNT; pos++)
    {
        led_state = osp_tm_get_led_state(pos);

        thermal_record->led_states[pos].position = (int16_t)pos;
        thermal_record->led_states[pos].value = (int16_t)led_state;
        thermal_valid = true;
        log_pos += snprintf(log_msg + log_pos,
                            sizeof(log_msg) - log_pos,
                            "led_position=%d led_state=%u, ", (int16_t)pos, (int16_t)led_state);
    }

    /* Remove the trailing comma and space if there are any in the final log message */
    if (log_pos > 2 && log_msg[log_pos - 2] == ',' && log_msg[log_pos - 1] == ' ') {
        log_msg[log_pos - 2] = '\0';
    }
    LOG(INFO, "%s", log_msg);

    /* Insert thermal data only if data is valid, some devices like PIRANHAV1 doesn't
     * support thermal management */
    if (thermal_valid == true)
    {
        thermal_record->timestamp_ms = request_ctx->reporting_timestamp - device_ctx->report_ts + get_timestamp();
        ds_dlist_insert_tail(&report_ctx->thermal_records, thermal_record);
    }
    else {
        dpp_device_thermal_record_free(thermal_record);
    }
}


/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/
bool sm_device_report_request(
        sm_stats_request_t         *request)
{
    sm_device_ctx_t                *device_ctx =
        &g_sm_device_ctx;
    sm_stats_request_t             *request_ctx =
        &device_ctx->request;
    dpp_device_report_data_t       *report_ctx =
        &device_ctx->report;
    ev_timer                       *report_timer =
        &device_ctx->report_timer;
    ev_timer                       *thermal_report_timer=
        &device_ctx->thermal_report_timer;


    if (NULL == request) {
        LOG(ERR,
            "Initializing device reporting "
            "(Invalid request config)");
        return false;
    }

    /* Initialize global stats only once */
    if (!device_ctx->initialized) {
        memset(request_ctx, 0, sizeof(*request_ctx));
        memset(report_ctx, 0, sizeof(*report_ctx));

        LOG(INFO,
            "Initializing device reporting");

        /* Initialize report device temp list */
        ds_dlist_init(
                &report_ctx->temp,
                dpp_device_temp_t,
                node);

        ds_dlist_init(
                &report_ctx->thermal_records,
                dpp_device_thermal_record_t,
                node);

        /* Initialize event lib timers and pass the global
           internal cache
         */
        ev_init (report_timer, sm_device_report);
        report_timer->data = device_ctx;

        ev_init (thermal_report_timer, sm_device_thermal_report);
        thermal_report_timer->data = device_ctx;
        device_ctx->initialized = true;
    }

    /* Store and compare every request parameter ...
       memcpy would be easier but we want some debug info
     */
    REQUEST_VAL_UPDATE("device", reporting_count, "%d");
    REQUEST_VAL_UPDATE("device", reporting_interval, "%d");
    REQUEST_VAL_UPDATE("device", sampling_interval, "%d");
    REQUEST_VAL_UPDATE("device", reporting_timestamp, "%"PRIu64"");

    /* Restart timers with new parameters */
    dpp_device_report_timer_set(report_timer, false);
    dpp_device_report_timer_set(thermal_report_timer, false);

    if (request_ctx->reporting_interval) {
        device_ctx->report_ts = get_timestamp();
        report_timer->repeat = request_ctx->reporting_interval == -1 ?
           1 : request_ctx->reporting_interval;

        dpp_device_report_timer_set(report_timer, true);
        if(request_ctx->sampling_interval == 0)
        {
            thermal_report_timer->repeat = DEVICE_THERMAL_TIMER_SEC;
        }
        else
        {
            thermal_report_timer->repeat = request_ctx->sampling_interval;
        }
        dpp_device_report_timer_set(thermal_report_timer, true);

        LOG(INFO, "Started device reporting");
    }
    else {
        LOG(INFO, "Stopped device reporting");
        sm_device_temp_list_clear(&report_ctx->temp);
        sm_device_thermal_list_clear(&report_ctx->thermal_records);
        memset(request_ctx, 0, sizeof(*request_ctx));
    }

    return true;
}
