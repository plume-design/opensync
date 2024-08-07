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

#include <limits.h>
#include <stdio.h>

#include "os_time.h"
#include "os_nif.h"
#include "dppline.h"
#include "target.h"
#include "log.h"

#include "qm_conn.h"
#include "sm.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define SM_QM_INTERVAL         5.0    /* Default (MAX) report interval in seconds -- float */
#define SM_QM_INTERVAL_MIN     0.1    /* Minimal report interval in seconds -- float */

/* Global MQTT instance */
static struct ev_timer  sm_mqtt_timer;
static double           sm_mqtt_timer_interval = SM_QM_INTERVAL;
static uint8_t          sm_mqtt_buf[STATS_MQTT_BUF_SZ];

static
bool sm_mqtt_publish(long mlen, void *mbuf)
{
    qm_response_t res;
    bool ret;
    ret = qm_conn_send_stats(mbuf, mlen, &res);
    return ret;
}

static
void sm_mqtt_timer_handler(struct ev_loop *loop, ev_timer *timer, int revents)
{
    (void)loop;
    (void)timer;
    (void)revents;

    static bool qm_err = false;
    uint32_t buf_len;

    // skip if empty queue
    if (dpp_get_queue_elements() <= 0) {
        return;
    }

    // Publish statistics report to MQTT
    LOG(DEBUG, "Total %d elements queued for transmission.\n", dpp_get_queue_elements());

    // Do not report any stats if QM is not running
    if (!qm_conn_get_status(NULL)) {
        if (!qm_err) {
            // don't repeat same error
            LOG(INFO, "Cannot connect to QM (QM not running?)");
        }
        qm_err = true;
        return;
    }
    qm_err = false;

    while (dpp_get_queue_elements() > 0)
    {
        bool ret;

#ifdef DPP_FAST_PACK
        uint8_t *buf = sm_mqtt_buf;

        ret = dpp_get_report2(&buf, sizeof(sm_mqtt_buf), &buf_len);
#else
        ret = dpp_get_report(sm_mqtt_buf, sizeof(sm_mqtt_buf), &buf_len);
#endif
        if (!ret)
        {
            LOGE("DPP: Get report failed.\n");
            break;
        }

        if (buf_len <= 0) continue;

        if (!sm_mqtt_publish(buf_len, sm_mqtt_buf))
        {
            LOGE("Publish report failed.\n");
            break;
        }
    }
}

/* sm_mqtt_timer interval must be <= 10% of the minimal reporting interval but in range:
 * SM_QM_INTERVAL_MIN ... SM_QM_INTERVAL seconds
 */
void sm_mqtt_interval_set(int interval)
{
    double sm_qm_interv;

    sm_qm_interv = (interval != 0) ? interval / 10.0 : SM_QM_INTERVAL;

    if (sm_qm_interv < SM_QM_INTERVAL_MIN) {
        sm_qm_interv = SM_QM_INTERVAL_MIN;
    }

    if (sm_qm_interv > SM_QM_INTERVAL) {
        sm_qm_interv = SM_QM_INTERVAL;
    }

    if (sm_qm_interv == sm_mqtt_timer_interval) {
        return;
    }

    LOGD("SM-QM timer interval is set to %f s", sm_qm_interv);
    sm_mqtt_timer_interval = sm_qm_interv;
    sm_mqtt_timer.repeat = sm_qm_interv;
    ev_timer_again(EV_DEFAULT, &sm_mqtt_timer);
}

bool sm_mqtt_init(void)
{
    // Start the MQTT report timer
    ev_timer_init(&sm_mqtt_timer, sm_mqtt_timer_handler,
            sm_mqtt_timer_interval, sm_mqtt_timer_interval);
    ev_timer_start(EV_DEFAULT, &sm_mqtt_timer);
    return true;
}

void sm_mqtt_stop(void)
{
    ev_timer_stop(EV_DEFAULT, &sm_mqtt_timer);
    sm_mqtt_timer_interval = SM_QM_INTERVAL;
    LOG(NOTICE, "Closing MQTT connection.");
}
