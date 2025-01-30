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

#include "cellm_mgr.h"
#include "log.h"

static void cellm_info_mqtt_periodic(time_t now, cellm_mgr_t *mgr)
{
    int res;

    if ((now - mgr->mqtt_periodic_ts) < mgr->mqtt_interval) return;

    LOGI("%s: mqtt_interval[%d]", __func__, (int)mgr->mqtt_interval);

    res = cellm_info_build_mqtt_report(now);
    if (res)
    {
        LOGE("%s: cellm_info_build_mqtt_report: failed, res=%d", __func__, res);
    }

    mgr->mqtt_periodic_ts = now;
}

static void cellm_info_log_modem_info(time_t now, cellm_mgr_t *mgr)
{
    time_t elapsed;

    elapsed = now - mgr->log_modem_info_ts;
    if (elapsed < CELLM_MODEM_INFO_INTERVAL) return;

    osn_cell_dump_modem_info();
    mgr->log_modem_info_ts = now;
}

/**
 * @brief periodic routine.
 */
static void cellm_info_event_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    cellm_mgr_t *mgr;
    time_t now;

    (void)loop;
    (void)watcher;
    (void)revents;

    mgr = cellm_get_mgr();

    now = time(NULL);

    if ((now - mgr->periodic_ts) < CELLM_TIMER_INTERVAL) return;

    LOGD("%s: modem_present[%d]", __func__, mgr->modem_info->modem_present);
    cellm_info_mqtt_periodic(now, mgr);
    cellm_info_log_modem_info(now, mgr);
    mgr->periodic_ts = now;
}

/**
 * @brief periodic timer initialization
 */
void cellm_info_event_init()
{
    cellm_mgr_t *mgr = cellm_get_mgr();

    LOGI("Initializing CELLM event");
    ev_timer_init(&mgr->timer, cellm_info_event_cb, CELLM_TIMER_INTERVAL, CELLM_TIMER_INTERVAL);
    mgr->timer.data = NULL;
    mgr->periodic_ts = time(NULL);
    mgr->mqtt_periodic_ts = time(NULL);
    mgr->init_time = time(NULL);
    mgr->mqtt_interval = CELLM_MQTT_INTERVAL;
    ev_timer_start(mgr->loop, &mgr->timer);
}
