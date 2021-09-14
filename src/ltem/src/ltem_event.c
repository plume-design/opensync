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

static void
ltem_mqtt_periodic(time_t now, ltem_mgr_t *mgr)
{
    int res;

    if ((now - mgr->mqtt_periodic_ts) < mgr->mqtt_interval) return;

    res = ltem_build_mqtt_report(now);
    LOGI("%s: ltem_build_mqtt_report, res=%d", __func__, res);

    mgr->mqtt_periodic_ts = now;
}

static void
ltem_state_periodic(time_t now, ltem_mgr_t *mgr)
{
    time_t elapsed;

    LOGD("%s: modem_present[%d]", __func__, mgr->lte_state_info->modem_present);
    elapsed = now - mgr->state_periodic_ts;
    if (elapsed < LTEM_STATE_UPDATE_INTERVAL) return;

    LOGD("%s: elapsed[%d] update_interval[%d]", __func__, (int)elapsed, (int)LTEM_STATE_UPDATE_INTERVAL);
    ltem_ovsdb_update_lte_state(mgr);
    mgr->state_periodic_ts = now;
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

    ltem_mqtt_periodic(now, mgr);
    ltem_state_periodic(now, mgr);

    if (mgr->wan_state != LTEM_WAN_STATE_UNKNOWN && mgr->lte_state == LTEM_LTE_STATE_UP)
    {
        if (mgr->lte_state_info->lte_failover_active)
        {
            ltem_ovsdb_cmu_check_lte(mgr);
        }
    }

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
