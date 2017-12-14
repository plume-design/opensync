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

/*
 * Band Steering Manager - Kicking Logic
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <linux/types.h>

#include "bm.h"


/*****************************************************************************/
#define MODULE_ID LOG_MODULE_ID_KICK

#define INST_RSSI_SAMPLE_CNT        5


/*****************************************************************************/
typedef struct {
    os_macaddr_t        macaddr;
    bsal_band_t         band;
    bsal_t              bsal;

    bm_kick_type_t      type;
    uint8_t             rssi;
    bool                measuring;

    ds_list_t           dsl_node;
} bm_kick_t;

static ds_list_t        bm_kick_queue;

static c_item_t map_bsal_kick_type[] = {
    C_ITEM_VAL(BM_CLIENT_KICK_DISASSOC,         BSAL_DISC_TYPE_DISASSOC),
    C_ITEM_VAL(BM_CLIENT_KICK_DEAUTH,           BSAL_DISC_TYPE_DEAUTH)
};


/*****************************************************************************/
static bm_kick_t *      bm_kick_find_by_macaddr(os_macaddr_t macaddr);
static bm_kick_t *      bm_kick_find_by_macstr(char *mac_str);
static void             bm_kick_free(bm_kick_t *kick);
static void             bm_kick_task_queue_run(void *arg);
static void             bm_kick_queue_start(void);


/*****************************************************************************/
static bm_client_kick_t
bm_kick_get_kick_type( bm_client_t *client, bm_kick_type_t kick )
{
    bm_client_kick_t kick_type = BM_CLIENT_KICK_NONE;

    switch( kick )
    {
        case BM_STEERING_KICK:
            kick_type = client->kick_type;
            break;

        case BM_STICKY_KICK:
        case BM_FORCE_KICK:
            kick_type = client->sc_kick_type;
            break;

        default:
            break;
    }

    return kick_type;
}

static uint8_t
bm_kick_get_kick_reason( bm_client_t *client, bm_kick_type_t kick )
{
    uint8_t kick_reason = 0;

    switch( kick )
    {
        case BM_STEERING_KICK:
            kick_reason = client->kick_reason;
            break;

        case BM_STICKY_KICK:
        case BM_FORCE_KICK:
            kick_reason = client->sc_kick_reason;
            break;

        default:
            break;
    }

    return kick_reason;
}

static uint16_t
bm_kick_get_debounce_period( bm_client_t *client, bm_kick_type_t kick )
{
    uint16_t kick_debounce_period = 0;

    switch( kick )
    {
        case BM_STEERING_KICK:
            kick_debounce_period = client->kick_debounce_period;
            break;

        case BM_STICKY_KICK:
        case BM_FORCE_KICK:
            kick_debounce_period = client->sc_kick_debounce_period;
            break;

        default:
            break;
    }

    return kick_debounce_period;
}

static char *
bm_kick_get_kick_type_str(bm_kick_type_t type)
{
    char *str = NULL;

    switch( type )
    {
        case BM_STEERING_KICK:
            str = "STEERING";
            break;

        case BM_STICKY_KICK:
            str = "STICKY";
            break;

        case BM_FORCE_KICK:
            str = "FORCE";
            break;

        default:
            str = "NONE";
            break;
    }

    return str;
}

static bm_kick_t *
bm_kick_find_by_macaddr(os_macaddr_t macaddr)
{
    bm_kick_t       *kick;

    ds_list_foreach(&bm_kick_queue, kick) {
        if (memcmp(&macaddr, &kick->macaddr, sizeof(macaddr)) == 0) {
            return kick;
        }
    }

    return NULL;
}

static bm_kick_t *
bm_kick_find_by_macstr(char *mac_str)
{
    os_macaddr_t        macaddr;

    if (!os_nif_macaddr_from_str(&macaddr, mac_str)) {
        LOGE("Failed to parse mac address: '%s'", mac_str);
        return NULL;
    }

    return bm_kick_find_by_macaddr(macaddr);
}

static void
bm_kick_free(bm_kick_t *kick)
{
    evsched_task_cancel_by_find(NULL, kick, EVSCHED_FIND_BY_ARG);
    free(kick);
    return;
}

static void
bm_kick_task_kick(void *arg)
{
    bm_client_stats_t   *stats;
    bm_client_times_t   *times;
    bm_client_t         *client;
    bm_kick_t           *kick = arg;
    bm_client_kick_t    kick_type;
    uint32_t            bsal_kick_type;
    uint8_t             kick_reason;
    int                 ret;

    if (kick != ds_list_head(&bm_kick_queue)) {
        LOGW("bm_kick_task_kick() called with entry not head of queue!");
        return;
    }
    ds_list_remove_head(&bm_kick_queue);

    do {
        client = bm_client_find_by_macaddr(kick->macaddr);
        if (!client || !client->connected ||
                           client->pair->bsal != kick->bsal || client->band != kick->band) {
            // No longer in same state, don't kick
            break;
        }
        stats = &client->stats[client->band];
        times = &client->times;

        kick_type   = bm_kick_get_kick_type( client, kick->type );
        kick_reason = bm_kick_get_kick_reason( client, kick->type );

        if (kick->type != BM_FORCE_KICK && kick->rssi == 0) {
            LOGW("Client '%s' RSSI measurement failed, not kicking...", client->mac_addr);
            break;
        }

        if (!c_get_value_by_key(map_bsal_kick_type, kick_type, &bsal_kick_type)) {
            LOGE("Client '%s' kick type %u failed to map to BSAL",
                                                      client->mac_addr, kick_type);
            break;
        }

        if (kick->type == BM_STICKY_KICK) {
            if (kick->rssi > client->lwm) {
                LOGD("Client '%s' measured RSSI %u is higer then LWM %u, not kicking...",
                                                client->mac_addr, kick->rssi, client->lwm);
                break;
            }
            LOGI("Client '%s' measured RSSI %u is lower then LWM %u, kicking...",
                                                client->mac_addr, kick->rssi, client->lwm);
        }
        else if (kick->type == BM_STEERING_KICK) {
            if (kick->rssi <= client->hwm) {
                LOGD("Client '%s' measured RSSI %u is lower then HWM %u, not kicking...",
                                                client->mac_addr, kick->rssi, client->hwm);
                break;
            }
            LOGI("Client '%s' measured RSSI %u is higher then HWM %u, kicking...",
                                                client->mac_addr, kick->rssi, client->hwm);
        } else if (kick->type == BM_FORCE_KICK) {
            LOGI("Client '%s' being forced kicked...", client->mac_addr );
        }
        else {
            break;
        }

        ret = bsal_client_disconnect(kick->bsal, kick->band,
                                     (uint8_t *)&kick->macaddr,
                                     bsal_kick_type,
                                     kick_reason);
        if (ret < 0) {
            LOGE("Client '%s' BSAL kick failed, ret = %d", client->mac_addr, ret);
        }
        else {
            times->last_kick = time(NULL);
            switch(kick->type) {

            case BM_STICKY_KICK:
            case BM_FORCE_KICK:
                stats->sticky_kick_cnt++;
                break;

            case BM_STEERING_KICK:
                stats->steering_kick_cnt++;
                break;

            }
        }
    } while(0);

    bm_kick_free(kick);

    if( ds_list_head( &bm_kick_queue ) ) {
        // Continue processing other kicks in the queue
        bm_kick_queue_start();
    }

    return;
}

static void
bm_kick_task_queue_run(void *arg)
{
    bm_client_t         *client;
    bm_kick_t           *kick;
    bool                remove = false;
    int                 ret;

    (void)arg;

    if ((kick = ds_list_head(&bm_kick_queue)) == NULL) {
        // Finished
        return;
    }

    do {
        // Make sure client is still in same state
        client = bm_client_find_by_macaddr(kick->macaddr);
        if (!client || !client->connected ||
                        client->pair->bsal != kick->bsal || client->band != kick->band) {
            // No longer in same state, don't kick
            remove = true;
            break;
        }

        if( kick->type == BM_FORCE_KICK ) {
            // Don't do an instant measurement for force kick
            evsched_task(bm_kick_task_kick, kick, EVSCHED_ASAP);
            break;
        }

        LOGD("Starting measurement for '%s'", client->mac_addr);

        // Kick off an instant measurement for this entry
        ret = bsal_client_measure(kick->bsal, kick->band,
                                    (uint8_t *)&kick->macaddr, INST_RSSI_SAMPLE_CNT);
        if (ret == -ENOSYS) {
            // BSAL library doesn't support instant measurement, just use last RSSI
            evsched_task(bm_kick_task_kick, kick, EVSCHED_ASAP);
            break;
        }
        else if (ret < 0) {
            LOGE("Failed to perform instant measrement for '%s', ret = %d",
                                                               client->mac_addr, ret);
            remove = true;
            break;
        }

        kick->measuring = true;
    } while(0);

    if (remove) {
        bm_kick_free(ds_list_remove_head(&bm_kick_queue));
        evsched_task_reschedule();
    }

    return;
}

static void
bm_kick_queue_start(void)
{
    evsched_task(bm_kick_task_queue_run, NULL, EVSCHED_ASAP);
    return;
}


/*****************************************************************************/
bool
bm_kick_init(void)
{
    LOGI("Kick Initializing");

    ds_list_init(&bm_kick_queue, bm_kick_t, dsl_node);
    return true;
}

bool
bm_kick_cleanup(void)
{
    ds_list_iter_t      iter;
    bm_kick_t           *kick;

    LOGI("Kick cleaning up");

    kick = ds_list_ifirst(&iter, &bm_kick_queue);
    while(kick) {
        ds_list_iremove(&iter);
        bm_kick_free(kick);

        kick = ds_list_inext(&iter);
    }

    return true;
}

bool
bm_kick_cleanup_by_bsal(bsal_t bsal)
{
    ds_list_iter_t      iter;
    bm_kick_t           *kick;
    bool                check_restart = false;

    kick = ds_list_ifirst(&iter, &bm_kick_queue);
    while(kick) {
        if (kick->bsal == bsal) {
            if (kick->measuring) {
                check_restart = true;
            }
            ds_list_iremove(&iter);
            bm_kick_free(kick);
        }

        kick = ds_list_inext(&iter);
    }

    if (check_restart) {
        if (ds_list_head(&bm_kick_queue)) {
            LOGW("Measuring entry removed -- restarting queue");
            bm_kick_queue_start();
        }
    }

    return true;
}

bool
bm_kick_cleanup_by_client(bm_client_t *client)
{
    ds_list_iter_t      iter;
    bm_kick_t           *kick;
    os_macaddr_t        macaddr;

    if (!os_nif_macaddr_from_str(&macaddr, client->mac_addr)) {
        LOGE("Failed to parse mac address: '%s'", client->mac_addr);
        return false;
    }

    kick = ds_list_ifirst(&iter, &bm_kick_queue);
    while(kick) {
        if (memcmp(&macaddr, &kick->macaddr, sizeof(macaddr)) == 0 &&
                                                    kick->measuring == false) {
            ds_list_iremove(&iter);
            bm_kick_free(kick);
        }

        kick = ds_list_inext(&iter);
    }

    return true;
}

bool
bm_kick(bm_client_t *client, bm_kick_type_t type, uint8_t rssi)
{
    bm_client_times_t   *times;
    bm_kick_t           *kick;
    bm_client_kick_t     kick_type;
    uint16_t             kick_debounce_period;

    if (!client->connected) {
        LOGW("Ignoring kick of '%s' because client is not connected",
                client->mac_addr);
        return false;
    }

    kick_type = bm_kick_get_kick_type( client, type );
    if (kick_type == BM_CLIENT_KICK_NONE) {
        LOGW( "Ignoring kick of '%s' because kick type is NONE",
                client->mac_addr );
        return false;
    }

    times = &client->times;
    kick_debounce_period = bm_kick_get_debounce_period( client, type );
    if( (( time(NULL) - times->last_connect ) <= kick_debounce_period ) ||
         ( kick_debounce_period == BM_KICK_MAGIC_DEBOUNCE_PERIOD ) ) {
        LOGW("Ignoring kick of '%s' -- reconnected within debounce period",
                client->mac_addr);
        return false;
    }

    if (type == BM_STEERING_KICK) {
        if (client->state != BM_CLIENT_STATE_CONNECTED ||
                                                client->band != BSAL_BAND_24G) {
            return false;
        }
    }

    if ((kick = bm_kick_find_by_macstr(client->mac_addr))) {
        // Already queued up -- so just update it
        kick->type = type;
        kick->rssi = rssi;
        kick->bsal = client->pair->bsal;
        kick->band = client->band;
        return true;
    }

    if (!(kick = (bm_kick_t *)calloc(1, sizeof(*kick)))) {
        LOGE("Failed to allocate memory for kick queue entry");
        return false;
    }

    if (!(os_nif_macaddr_from_str(&kick->macaddr, client->mac_addr))) {
        LOGE("Failed to parse mac address: '%s' for kick entry", client->mac_addr);
        free(kick);
        return false;
    }

    kick->type = type;
    kick->rssi = rssi;
    kick->bsal = client->pair->bsal;
    kick->band = client->band;
    ds_list_insert_tail(&bm_kick_queue, kick);

    if (ds_list_head(&bm_kick_queue) == kick) {
        // Kick off queue now
        bm_kick_queue_start();
    }

    LOGD("Queued %s kick for '%s'", bm_kick_get_kick_type_str( type ),
                                    client->mac_addr);
    return true;
}

void
bm_kick_measurement(os_macaddr_t macaddr, uint8_t rssi)
{
    bm_kick_t           *kick;

    if (!(kick = ds_list_head(&bm_kick_queue))) {
        return;
    }

    if (!kick->measuring ||
                memcmp(&macaddr, &kick->macaddr, sizeof(macaddr)) != 0) {
        return;
    }

    kick->measuring = false;
    kick->rssi = rssi;
    evsched_task(bm_kick_task_kick, kick, EVSCHED_ASAP);
    return;
}
