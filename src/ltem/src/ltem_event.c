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

#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include "ltem_mgr.h"
#include "log.h"
#include "neigh_table.h"

// Intervals and timeouts in seconds
#define LTEM_TIMER_INTERVAL   1
#define LTEM_STATE_UPDATE_INTERVAL 60
#define LTEM_MQTT_INTERVAL     60 * 15
#define LTEM_L3_CHECK_INTERVAL 30
#define LTEM_MODEM_INFO_INTERVAL 60 * 5
#define LTEM_LTE_HEALTHCHECK_INTERVAL 60 * 10

static void
ltem_mqtt_periodic(time_t now, ltem_mgr_t *mgr)
{
    int res;

    if ((now - mgr->mqtt_periodic_ts) < mgr->mqtt_interval) return;

    res = ltem_build_mqtt_report(now);
    if (res)
    {
        LOGI("%s: ltem_build_mqtt_report: failed, res=%d", __func__, res);
    }

    mgr->mqtt_periodic_ts = now;
}

static void
ltem_state_periodic(time_t now, ltem_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->state_periodic_ts;
    if (elapsed < LTEM_STATE_UPDATE_INTERVAL) return;

    LOGD("%s: elapsed[%d] update_interval[%d]", __func__, (int)elapsed, (int)LTEM_STATE_UPDATE_INTERVAL);
    ltem_ovsdb_update_lte_state(mgr);
    mgr->state_periodic_ts = now;
}

static void
ltem_check_l3_state(time_t now, ltem_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->l3_state_periodic_ts;
    if (elapsed < LTEM_L3_CHECK_INTERVAL) return;

    ltem_ovsdb_check_l3_state(mgr);
    mgr->l3_state_periodic_ts = now;
}

static void
ltem_log_modem_info(time_t now, ltem_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->lte_log_modem_info_ts;
    if (elapsed < LTEM_MODEM_INFO_INTERVAL) return;

    osn_lte_dump_modem_info();
    mgr->lte_log_modem_info_ts = now;
}

static void
ltem_lte_healthcheck(time_t now, ltem_mgr_t *mgr)
{
    time_t elapsed;
    int res;
    lte_serving_cell_info_t *srv_cell;

    srv_cell = &mgr->modem_info->srv_cell;

    elapsed = now - mgr->lte_healthcheck_ts;
    if (elapsed < LTEM_LTE_HEALTHCHECK_INTERVAL) return;

    if (mgr->lte_config_info->if_name[0] == '\0') return;

    res = ltem_dns_connect_check(mgr->lte_config_info->if_name);
    if (res)
    {
        mgr->modem_info->healthcheck_failures++;
        LOGI("%s: Failed", __func__);
    }
    else
    {
        mgr->modem_info->last_healthcheck_success = now;
        LOGI("%s: Success", __func__);
    }

    if (srv_cell->state == LTE_SERVING_CELL_NOCONN && srv_cell->mode == LTE_CELL_MODE_LTE)
    {
        LOGT("%s: lte_state[LTE_SERVING_CELL_NOCONN] lte_mode[LTE_CELL_MODE_LTE]", __func__);
    }
    else
    {
        LOGI("%s: lte_state[%d], lte_mode[%d]", __func__, srv_cell->state, srv_cell->mode);
        if (mgr->lte_state == LTEM_LTE_STATE_UP)
        {
            ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
        }
    }

    if (mgr->lte_state != LTEM_LTE_STATE_UP)
    {
        osn_lte_reset_modem();
        LOGT("%s: lte_state[%s]", __func__, ltem_get_lte_state_name(mgr->lte_state));
    }

    mgr->lte_healthcheck_ts = now;
}

/**
 * @brief periodic routine.
 */
static void
ltem_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    ltem_mgr_t *mgr;
    time_t now;

    (void)loop;
    (void)watcher;
    (void)revents;

    mgr = ltem_get_mgr();

    now = time(NULL);

    if (!mgr->lte_config_info->manager_enable || !mgr->lte_config_info->modem_enable) return;

    if ((now - mgr->periodic_ts) < LTEM_TIMER_INTERVAL) return;

    LOGD("%s: modem_present[%d]", __func__, mgr->modem_info->modem_present);
    ltem_mqtt_periodic(now, mgr);
    ltem_state_periodic(now, mgr);
    ltem_check_l3_state(now, mgr);
    ltem_log_modem_info(now, mgr);
    ltem_lte_healthcheck(now, mgr);

    mgr->periodic_ts = now;
}

/**
 * @brief periodic timer initialization
 */
void
ltem_event_init()
{
    ltem_mgr_t *mgr = ltem_get_mgr();

    LOGI("Initializing LTEM event");
    ev_timer_init(&mgr->timer, ltem_event_cb,
                  LTEM_TIMER_INTERVAL, LTEM_TIMER_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    mgr->mqtt_periodic_ts = time(NULL);
    mgr->state_periodic_ts = time(NULL);
    mgr->init_time = time(NULL);
    mgr->mqtt_interval = LTEM_MQTT_INTERVAL;
    ev_timer_start(mgr->loop, &mgr->timer);
}
