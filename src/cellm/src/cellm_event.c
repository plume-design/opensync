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
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "cellm_mgr.h"
#include "osn_cell_modem.h"

// Intervals and timeouts in seconds
#define CELLM_TIMER_INTERVAL            1
#define CELLM_STATE_UPDATE_INTERVAL     60
#define CELLM_MQTT_EVENT_INTERVAL       60 * 15
#define CELLM_LTE_L3_CHECK_INTERVAL     30
#define CELLM_MODEM_INFO_EVENT_INTERVAL 60 * 5
#define CELLM_LTE_HEALTHCHECK_INTERVAL  60 * 10
#define CELLM_WAN_L3_CHECK_INTERVAL     60

static void cellm_mqtt_periodic(time_t now, cellm_mgr_t *mgr)
{
    int res;

    if ((now - mgr->mqtt_periodic_ts) < mgr->mqtt_interval) return;

    res = cellm_build_mqtt_report(now);
    if (res)
    {
        LOGI("%s: cellm_build_mqtt_report: failed, res=%d", __func__, res);
    }

    mgr->mqtt_periodic_ts = now;
}

static void cellm_state_periodic(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->state_periodic_ts;
    if (elapsed < CELLM_STATE_UPDATE_INTERVAL) return;

    LOGD("%s: elapsed[%d] update_interval[%d]", __func__, (int)elapsed, (int)CELLM_STATE_UPDATE_INTERVAL);
    cellm_ovsdb_update_lte_state(mgr);
    mgr->state_periodic_ts = now;
}

static void cellm_check_lte_l3_state(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->lte_l3_state_periodic_ts;
    if (elapsed < CELLM_LTE_L3_CHECK_INTERVAL) return;

    cellm_ovsdb_check_lte_l3_state(mgr);
    mgr->lte_l3_state_periodic_ts = now;
}

static void cellm_check_wan_l3_state(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;
    int res;

    elapsed = now - mgr->wan_l3_state_periodic_ts;
    if (elapsed < CELLM_WAN_L3_CHECK_INTERVAL) return;

    if (mgr->wan_state == CELLM_WAN_STATE_DOWN && mgr->cellm_state_info->cellm_failover_active
        && !mgr->cellm_config_info->force_use_lte)
    {
        res = cellm_wan_healthcheck(mgr);
        if (!res)
        {
            mgr->wan_l3_reconnect_success++;
            if (mgr->wan_l3_reconnect_success >= WAN_L3_RECONNECT)
            {
                cellm_set_wan_route_preferred(mgr);
                cellm_set_wan_state(CELLM_WAN_STATE_UP);
                mgr->wan_l3_reconnect_success = 0;
            }
        }
    }

    mgr->wan_l3_state_periodic_ts = now;
}

static void cellm_log_modem_info(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->log_modem_info_ts;
    if (elapsed < CELLM_MODEM_INFO_EVENT_INTERVAL) return;

    osn_cell_dump_modem_info();
    mgr->log_modem_info_ts = now;
}

static void cellm_cell_healthcheck(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;
    int res;
    cell_serving_cell_info_t *srv_cell;

    srv_cell = &mgr->modem_info->srv_cell;

    elapsed = now - mgr->lte_healthcheck_ts;
    if (elapsed < CELLM_LTE_HEALTHCHECK_INTERVAL) return;

    if (mgr->cellm_config_info->if_name[0] == '\0') return;

    res = cellm_dns_connect_check(mgr->cellm_config_info->if_name);
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

    if (srv_cell->state == SERVING_CELL_NOCONN && srv_cell->mode == CELL_MODE_LTE)
    {
        LOGT("%s: lte_state[cELL_SERVING_CELL_NOCONN] lte_mode[CELL_MODE_LTE]", __func__);
    }
    else
    {
        LOGI("%s: lte_state[%d], lte_mode[%d]", __func__, srv_cell->state, srv_cell->mode);
        if (mgr->cellm_state == CELLM_STATE_UP)
        {
            cellm_set_state(CELLM_STATE_DOWN);
        }
    }

    if (mgr->cellm_state != CELLM_STATE_UP)
    {
        osn_cell_reset_modem();
        LOGT("%s: lte_state[%s]", __func__, cellm_get_state_name(mgr->cellm_state));
    }

    mgr->lte_healthcheck_ts = now;
}

/**
 * @brief periodic routine.
 */
static void cellm_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    cellm_mgr_t *mgr;
    time_t now;

    (void)loop;
    (void)watcher;
    (void)revents;

    mgr = cellm_get_mgr();

    now = time(NULL);

    if (!mgr->cellm_config_info->manager_enable || !mgr->cellm_config_info->modem_enable) return;

    if ((now - mgr->periodic_ts) < CELLM_TIMER_INTERVAL) return;

    LOGD("%s: modem_present[%d]", __func__, mgr->modem_info->modem_present);
    cellm_mqtt_periodic(now, mgr);
    cellm_state_periodic(now, mgr);
    cellm_check_lte_l3_state(now, mgr);
    cellm_log_modem_info(now, mgr);
    cellm_cell_healthcheck(now, mgr);
    cellm_check_wan_l3_state(now, mgr);
    mgr->periodic_ts = now;
}

/**
 * @brief periodic timer initialization
 */
void cellm_event_init()
{
    cellm_mgr_t *mgr = cellm_get_mgr();

    LOGI("Initializing LTEM event");
    ev_timer_init(&mgr->timer, cellm_event_cb, CELLM_TIMER_INTERVAL, CELLM_TIMER_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    mgr->mqtt_periodic_ts = time(NULL);
    mgr->state_periodic_ts = time(NULL);
    mgr->init_time = time(NULL);
    mgr->mqtt_interval = CELLM_MQTT_EVENT_INTERVAL;
    ev_timer_start(mgr->loop, &mgr->timer);
}
