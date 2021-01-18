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
 * Band Steering Manager - Event Handling
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
#define MODULE_ID LOG_MODULE_ID_EVENT


/*****************************************************************************/
static struct ev_loop *     _evloop = NULL;

static pthread_mutex_t      bm_cb_lock;
static ev_async             bm_cb_async;
static ds_dlist_t           bm_cb_queue;
static int                  bm_cb_queue_len = 0;

static bool                 _bsal_initialized = false;

typedef struct {
    bsal_event_t            event;

    ds_dlist_node_t         node;
} bm_cb_entry_t;

static c_item_t map_bsal_disc_sources[] = {
    C_ITEM_STR(BSAL_DISC_SOURCE_LOCAL,              "Local"),
    C_ITEM_STR(BSAL_DISC_SOURCE_REMOTE,             "Remote")
};

static c_item_t map_bsal_disc_types[] = {
    C_ITEM_STR(BSAL_DISC_TYPE_DISASSOC,             "Disassoc"),
    C_ITEM_STR(BSAL_DISC_TYPE_DEAUTH,               "Deauth")
};

static c_item_t map_bsal_rssi_changes[] = {
    C_ITEM_STR(BSAL_RSSI_UNCHANGED,                 "Unchanged"),
    C_ITEM_STR(BSAL_RSSI_HIGHER,                    "Higher"),
    C_ITEM_STR(BSAL_RSSI_LOWER,                     "Lower")
};

static c_item_t map_bsal_disc_reasons[] = {
    C_ITEM_STR(1,   "Unspecified"),
    C_ITEM_STR(2,   "Prev Auth Not Valid"),
    C_ITEM_STR(3,   "Leaving"),
    C_ITEM_STR(4,   "Due to Inactivity"),
    C_ITEM_STR(5,   "AP Busy"),
    C_ITEM_STR(6,   "C2F from non-auth STA"),
    C_ITEM_STR(7,   "C3F from non-assoc STA"),
    C_ITEM_STR(8,   "STA Left"),
    C_ITEM_STR(9,   "Assoc w/o Auth"),
    C_ITEM_STR(10,  "Power Caps Invalid"),
    C_ITEM_STR(11,  "Channel Not Valid"),
    C_ITEM_STR(13,  "Invalid IE"),
    C_ITEM_STR(14,  "MMIC Failure"),
    C_ITEM_STR(15,  "4Way Handshake Tmout"),
    C_ITEM_STR(16,  "GKEY Update Tmout"),
    C_ITEM_STR(17,  "IE in 4Way Differs"),
    C_ITEM_STR(18,  "Group Cipher Not Valid"),
    C_ITEM_STR(19,  "Pairwise Cipher Not Valid"),
    C_ITEM_STR(20,  "AKMP Not Valid"),
    C_ITEM_STR(21,  "Unsupported RSN IE Ver"),
    C_ITEM_STR(22,  "Invalid RSN IE Cap"),
    C_ITEM_STR(23,  "802.11x Auth Failed"),
    C_ITEM_STR(24,  "Cipher Suite Rejected"),
    C_ITEM_STR(25,  "TDLS Teardown Unreachable"),
    C_ITEM_STR(26,  "TDLS Teardown Unspecified"),
    C_ITEM_STR(34,  "Low ACK")
};

/*****************************************************************************/
static void     bm_events_handle_event(bsal_event_t *event );
/*****************************************************************************/

// Callback function for BSAL upon receiving steering events from the driver
static void
bm_events_bsal_event_cb(bsal_event_t *event)
{
    bm_cb_entry_t       *cb_entry;
    int                 ret = 0;

    pthread_mutex_lock( &bm_cb_lock );

    if( bm_cb_queue_len >= BM_CB_QUEUE_MAX ) {
        LOGW( "BM CB queue full! Ignoring event..." );
        goto exit;
    }

    if( !(cb_entry = calloc( 1, sizeof( *cb_entry ))) ) {
        LOGE( "Failed to allocate memory for BM CB queue object" );
        goto exit;
    }

    memcpy( &cb_entry->event, event, sizeof( cb_entry->event ) );

    ds_dlist_insert_tail( &bm_cb_queue, cb_entry );
    bm_cb_queue_len++;
    if (bm_cb_queue_len > 5)
        LOGT("bm_cm_queue_len++ (%d)", bm_cb_queue_len);
    ret = 1;

exit:
    pthread_mutex_unlock( &bm_cb_lock );
    if( ret && _evloop ) {
            ev_async_send( _evloop, &bm_cb_async );
    }

    return;
}

// Asynchronous callback to process events in CB queue
static void
bm_events_async_cb( EV_P_ ev_async *w, int revents )
{
    bm_cb_entry_t       *cb_entry;
    bsal_event_t        *event;

    while( true )
    {
        pthread_mutex_lock( &bm_cb_lock );
        cb_entry = ds_dlist_is_empty( &bm_cb_queue ) ? NULL : ds_dlist_remove_head( &bm_cb_queue );
        if ( cb_entry )
        {
            bm_cb_queue_len--;
            if (bm_cb_queue_len > 4)
                LOGT("bm_cm_queue_len-- (%d)", bm_cb_queue_len);
        }
        pthread_mutex_unlock( &bm_cb_lock );

        if ( !cb_entry )
        {
            break;
        }

        event = &cb_entry->event;

        bm_events_handle_event(event);

        free( cb_entry );
    }

    return;
}

static void
bm_events_handle_event(bsal_event_t *event)
{
    bm_client_stats_t           *stats;
    bm_client_times_t           *times;
    bm_client_t                 *client;
    bm_group_t                  *group = bm_group_find_by_ifname(event->ifname);
    time_t                      now = time(NULL);
    bool                        reject = false;
    radio_type_t                radio_type;
    char                        *bandstr;
    char                        *ifname = event->ifname;
    bm_client_reject_t          reject_detection;
    unsigned int                i;
    time_t                      last_probe;

    radio_type = bm_group_find_radio_type_by_ifname(event->ifname);
    bandstr = radio_get_name_from_type(radio_type);

    if (radio_type == RADIO_TYPE_NONE) {
        LOGD("bm_group_find_radio_type_by_ifname(%s) failed!", event->ifname);
        return;
    }

    if (!group) {
        LOGW("%s: could not find group!", event->ifname);
        return;
    }

    switch (event->type) {

    case BSAL_EVENT_PROBE_REQ:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.probe_req.client_addr))) {
            break;
        }

        times = &client->times;
        stats = bm_client_get_stats(client, event->ifname);
        if (WARN_ON(!stats))
            break;

        LOGD("[%s] %s: BSAL_EVENT_PROBE_REQ from %s, RSSI=%2u%s%s",
                                                    bandstr, ifname,
                                                    client->mac_addr,
                                                    event->data.probe_req.rssi,
                                                    event->data.probe_req.ssid_null ?
                                                                            ", null" : ", direct",
                                                    event->data.probe_req.blocked   ?
                                                                            ", BLOCKED" : "");
        last_probe = stats->probe.last;
        stats->probe.last = now;
        if (event->data.probe_req.ssid_null) {
            stats->probe.last_null = now;
            stats->probe.null_cnt++;
            if (event->data.probe_req.blocked) {
                stats->probe.null_blocked++;
            }
        }
        else {
            stats->probe.last_direct = now;
            stats->probe.direct_cnt++;
            if (event->data.probe_req.blocked) {
                stats->probe.direct_blocked++;
            }
        }

        // If the client is in 'away' mode of client steering,
        // check if it did an RSSI XING
        bm_client_cs_check_rssi_xing( client, event );

        if (event->data.probe_req.blocked) {
            stats->probe.last_blocked = now;

            if (client->state != BM_CLIENT_STATE_CONNECTED &&
                client->state != BM_CLIENT_STATE_BACKOFF) {
                if( client->steering_state != BM_CLIENT_CLIENT_STEERING ) {
                    if (!bm_client_bs_ifname_allowed(client, ifname)) {
                        bm_client_set_state(client, BM_CLIENT_STATE_STEERING);
                    }
                }

                reject_detection = bm_client_get_reject_detection( client );
                switch(reject_detection) {
                    case BM_CLIENT_REJECT_NONE:
                        break;

                    case BM_CLIENT_REJECT_AUTH_BLOCKED:
                        reject = true;
                        break;

                    case BM_CLIENT_REJECT_PROBE_ALL:
                        reject = true;
                        break;

                    case BM_CLIENT_REJECT_PROBE_NULL:
                        if (event->data.probe_req.ssid_null) {
                            reject = true;
                        }
                        break;

                    case BM_CLIENT_REJECT_PROBE_DIRECT:
                        if (!event->data.probe_req.ssid_null) {
                            reject = true;
                        }
                        break;
                }

                if (reject) {
                    bm_client_rejected(client, event);
                }
            }
        }

        switch (radio_type) {
            case RADIO_TYPE_2G:
                client->band_cap_2G = true;
                break;
            case RADIO_TYPE_5G:
            case RADIO_TYPE_5GL:
            case RADIO_TYPE_5GU:
                client->band_cap_5G = true;
                break;
            default:
                break;
        }

        /* This one managed by cloud */
        if (abs((int)stats->probe.last_snr - (int)event->data.probe_req.rssi) < client->preq_snr_thr) {
            LOGD("[%s] %s: %s skip preq report (%d %d)", bandstr, ifname, client->mac_addr,
                 stats->probe.last_snr, event->data.probe_req.rssi);
            break;
        }

        /* This is short time probe report filtering/limitation */
        if (abs((int)stats->probe.last_snr - (int)event->data.probe_req.rssi) <= BM_CLIENT_PREQ_SNR_TH &&
            (now - last_probe) < BM_CLIENT_PREQ_TIME_TH) {
            LOGD("[%s] %s: %s skip preq report (%d %d) short time", bandstr, ifname, client->mac_addr,
                 stats->probe.last_snr, event->data.probe_req.rssi);
            break;
        }

        stats->probe.last_snr = event->data.probe_req.rssi;
        bm_stats_add_event_to_report( client, event, PROBE, false );
        break;

    case BSAL_EVENT_CLIENT_CONNECT:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.connect.client_addr))) {
            break;
        }

        LOGN("[%s] %s: BSAL_EVENT_CLIENT_CONNECT %s", bandstr, ifname, client->mac_addr);
        bm_client_check_connected(client, group, event->ifname);
        break;

    case BSAL_EVENT_CLIENT_DISCONNECT:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.disconnect.client_addr))) {
            break;
        }

        times = &client->times;
        stats = bm_client_get_stats(client, event->ifname);
        if (WARN_ON(!stats))
            break;

        LOGN("[%s] %s: BSAL_EVENT_CLIENT_DISCONNECT %s, src=%s, type=%s, reason=%s",
                                    bandstr, ifname, client->mac_addr,
                                    c_get_str_by_key(map_bsal_disc_sources, event->data.disconnect.source),
                                    c_get_str_by_key(map_bsal_disc_types,   event->data.disconnect.type),
                                    c_get_str_by_key(map_bsal_disc_reasons, event->data.disconnect.reason));

        if (client->state != BM_CLIENT_STATE_CONNECTED) {
            break;
        }

        if (strcmp(client->ifname, event->ifname)) {
            LOGI("Client '%s': Client connected band is different, ignoring"\
                 " DISCONNECT event", client->mac_addr);
            break;
        }

        bm_client_disconnected(client);
        times->last_disconnect = now;
        stats->disconnects++;
        stats->last_disconnect.source = event->data.disconnect.source;
        stats->last_disconnect.type   = event->data.disconnect.type;
        stats->last_disconnect.reason = event->data.disconnect.reason;

        /* Unblock probe reporting */
        bm_client_reset_last_probe_snr(client);

        // Cancel any pending BSS TM Request retry task
        bm_kick_cancel_btm_retry_task( client );

        for (i = 0; i < ARRAY_SIZE(client->rrm_req); i++)
           evsched_task_cancel(client->rrm_req[i].rrm_task);

        bm_stats_add_event_to_report( client, event, DISCONNECT, false );
        break;

    case BSAL_EVENT_CLIENT_ACTIVITY:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.activity.client_addr))) {
            break;
        }

        LOGD("[%s] %s: BSAL_EVENT_CLIENT_ACTIVITY %s is now %s",
                            bandstr, ifname, client->mac_addr,
                            event->data.activity.active ? "ACTIVE" : "INACTIVE");
        bm_client_handle_ext_activity(client, ifname, event->data.activity.active);
        break;

    case BSAL_EVENT_CHAN_UTILIZATION:
        LOGI("[%s] %s: BSAL_EVENT_CHAN_UTILIZATION is now %2u%%",
                                                        bandstr, ifname, event->data.chan_util.utilization);
        break;

    case BSAL_EVENT_RSSI_XING:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.rssi_change.client_addr))) {
            break;
        }
        stats = bm_client_get_stats(client, event->ifname);
        if (WARN_ON(!stats))
            break;

        LOGT("[%s] %s: BSAL_EVENT_RSSI_XING %s, RSSI=%2u, low xing=%s, high xing=%s, inact xing=%s",
                            bandstr, ifname, client->mac_addr,
                            event->data.rssi_change.rssi,
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.low_xing),
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.high_xing),
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.inact_xing));

        if( !client->connected ) {
            // Ignore the event if the client is not connected
            LOGD("[%s] %s: XING client not connected", bandstr, ifname);
            break;
        }

        if (strcmp(client->ifname, event->ifname)) {
            LOGD("[%s] %s: XING client ifname (%s) different than event ifname", bandstr, event->ifname, client->ifname);
	    break;
	}

        switch(event->data.rssi_change.inact_xing) {

        case BSAL_RSSI_UNCHANGED:
            break;

        case BSAL_RSSI_HIGHER:
            stats->rssi.inact_higher++;
            break;

        case BSAL_RSSI_LOWER:
            stats->rssi.inact_lower++;
            break;
        }

        bm_client_handle_ext_xing(client, client->ifname, event);
        break;

    case BSAL_EVENT_RSSI:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.rssi.client_addr))) {
            break;
        }
        LOGT("[%s] %s: BSAL_EVENT_RSSI %s, RSSI=%2u",
                            bandstr, ifname, client->mac_addr, event->data.rssi.rssi);

        bm_kick_measurement(client->macaddr, event->data.rssi.rssi);
        break;

    case BSAL_EVENT_DEBUG_CHAN_UTIL:
        LOGT("[%s] %s: BSAL_EVENT_DEBUG_CHAN_UTIL is now %2u%%",
                                                      bandstr, ifname, event->data.chan_util.utilization);
        break;

    case BSAL_EVENT_DEBUG_RSSI:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.rssi.client_addr))) {
            break;
        }

        LOGT("[%s] %s: BSAL_EVENT_DEBUG_RSSI %s, RSSI=%2u",
                           bandstr, ifname, client->mac_addr, event->data.rssi.rssi);
        break;

    case BSAL_EVENT_STEER_CLIENT:
        {
            // add client to internal list for stat collection
            if (!(client = bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr))) {
                break;
            }
            LOGN("[%s] %s: BSAL_EVENT_STEER_CLIENT %s",
                    bandstr, ifname, client->mac_addr);
            break;
        }

    case BSAL_EVENT_STEER_SUCCESS:
        {
            client = bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr);
            if (!client) break;
            LOGN("[%s] %s: BSAL_EVENT_STEER_SUCCESS %s from: %d to: %d",
                    bandstr, ifname, client->mac_addr,
                    event->data.steer.from_ch, event->data.steer.to_ch);
            stats = bm_client_get_stats(client, event->ifname);
            if (WARN_ON(!stats))
                break;
            stats->steering_success_cnt++;
        }
        break;

    case BSAL_EVENT_STEER_FAILURE:
        {
            client = bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr);
            if (!client) break;
            LOGN("[%s] %s: BSAL_EVENT_STEER_FAILURE %s from: %d to: %d",
                    bandstr, ifname, client->mac_addr,
                    event->data.steer.from_ch, event->data.steer.to_ch);
            stats = bm_client_get_stats(client, event->ifname);
            if (WARN_ON(!stats))
                break;
            stats->steering_fail_cnt++;
        }
        break;

    case BSAL_EVENT_AUTH_FAIL:
        {
            if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.auth_fail.client_addr))) {
                break;
            }

            LOGN( "[%s] %s: BSAL_EVENT_AUTH_FAIL %s, RSSI=%2u, reason=%s %s",
                                                bandstr, ifname,
                                                client->mac_addr,
                                                event->data.auth_fail.rssi,
                                                c_get_str_by_key(map_bsal_disc_reasons, event->data.auth_fail.reason),
                                                ( event->data.auth_fail.bs_blocked == 1 )? "BLOCKED" : "" );

            times = &client->times;
            stats = bm_client_get_stats(client, event->ifname);
            if (WARN_ON(!stats))
                break;

            if( client->cs_reject_detection == BM_CLIENT_REJECT_AUTH_BLOCKED &&
                event->data.auth_fail.bs_blocked == 1 ) {
                bm_client_rejected( client, event );
                bm_stats_add_event_to_report( client, event, AUTH_BLOCK, false );
            }

            if (client->state == BM_CLIENT_STATE_CONNECTED) {
                bsal_event_t disconnect_event;

                LOGI("[%s]: %s in connected state %s (assume we lost last deauth/disassoc)", bandstr,
                     client->ifname, event->ifname);

                memset(&disconnect_event, 0, sizeof(disconnect_event));
                STRSCPY(disconnect_event.ifname, client->ifname);
                disconnect_event.data.disconnect.source = BSAL_DISC_SOURCE_REMOTE;
                disconnect_event.data.disconnect.type = BSAL_DISC_TYPE_DEAUTH;
                disconnect_event.data.disconnect.reason = 99;
                times->last_disconnect = now;
                stats->disconnects++;
                stats->last_disconnect.source = disconnect_event.data.disconnect.source;
                stats->last_disconnect.type   = disconnect_event.data.disconnect.type;
                stats->last_disconnect.reason = disconnect_event.data.disconnect.reason;

                bm_client_disconnected(client);
                bm_stats_add_event_to_report(client, &disconnect_event, DISCONNECT, false);
            }

            if (client->steering_state != BM_CLIENT_CLIENT_STEERING) {
                    bm_client_rejected(client, event);
            }
        }
        break;

    case BSAL_EVENT_ACTION_FRAME:
        bm_event_action_frame(ifname, event->data.action_frame.data, event->data.action_frame.data_len);
        break;

    case BSAL_EVENT_BTM_STATUS:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.btm_status.client_addr))) {
            break;
        }

        LOGN("[%s] %s: BSAL_EVENT_BTM_STATUS %s status %u", bandstr, ifname,
                                                            client->mac_addr,
                                                            event->data.btm_status.status);
        bm_stats_add_event_to_report(client, event, CLIENT_BTM_STATUS, false);
        break;

    default:
        LOGW("[%s] Unhandled event type %u", bandstr, event->type);
        break;

    }

    return;
}

static bool
bm_events_kick_client_upon_idle( bm_client_t *client )
{
    bm_client_kick_t kick_type = client->kick_type;

    if( !client->kick_upon_idle ) {
        return false;
    }

    if( !client->is_active ) {
        return false;
    }

    if( kick_type == BM_CLIENT_KICK_BSS_TM_REQ ) {
        return false;
    }

    if ((kick_type == BM_CLIENT_KICK_BTM_DISASSOC || kick_type == BM_CLIENT_KICK_BTM_DEAUTH) &&
         client->info->is_BTM_supported ) {
        return false;
    }

    return true;
}

static void
bm_events_xing_bs_sticky_task(void *arg)
{
    bm_client_t *client = arg;
    bsal_client_info_t info;

    /* Check client/snr info */
    if (!target_bsal_client_info(client->ifname, client->macaddr.addr, &info)) {
        if (!info.connected) {
            LOGI("[%s]: xing_bs '%s' not connected, skip sticky task", client->ifname, client->mac_addr);
            return;
        }

        if (info.snr > 0 && info.snr > client->lwm) {
            LOGI("[%s]: xing_bs '%s' snr %u higher %u skip sticky task", client->ifname, client->mac_addr,
                 info.snr, client->lwm);
            return;
        }

        LOGD("[%s]: xing_bs '%s' sticky task, kick still required (%u %u)", client->ifname, client->mac_addr,
             info.snr, client->lwm);
    }

    /* Finally issue kick */
    if (bm_events_kick_client_upon_idle(client)) {
        client->kick_info.kick_pending = true;
        client->kick_info.kick_type    = BM_STICKY_KICK;
        client->kick_info.rssi         = client->xing_bs_rssi;

        LOGN("[%s]: xing_bs '%s' is ACTIVE, handling LOW_RSSI_XING upon idle",
             client->ifname, client->mac_addr );
    } else {
        LOGN("[%s]: xing_bs '%s' handling LOW_RSSI_XING (STICKY)", client->ifname, client->mac_addr);
        client->cancel_btm = false;
        bm_kick(client, BM_STICKY_KICK, client->xing_bs_rssi);
    }
}

static void
bm_events_xing_bs_sticky_req(bm_client_t *client, int delay)
{
    evsched_task_cancel(client->xing_bs_sticky_task);
    if (!delay) {
        bm_events_xing_bs_sticky_task(client);
        return;
    }

    client->xing_bs_sticky_task = evsched_task(bm_events_xing_bs_sticky_task,
                                               client,
                                               EVSCHED_SEC(delay));
}

static void
bm_events_handle_rssi_xing_bs(bm_client_t *client, bsal_event_t *event)
{
    bm_client_stats_t *stats;
    time_t now;
    int delay;

    now = time(NULL);
    delay = 0;

    stats = bm_client_get_stats(client, event->ifname);
    if (WARN_ON(!stats))
        return;

    LOGI("[%s]: xing_bs '%s' snr %d (%d,%d)", event->ifname, client->mac_addr, event->data.rssi_change.rssi,
         event->data.rssi_change.low_xing, event->data.rssi_change.high_xing);

    if (client->backoff && !client->steer_during_backoff) {
        LOGI("[%s]: xing_bs '%s' skip XING event, don't steer during pre-assoc backoff ", event->ifname, client->mac_addr);
        return;
    }

    if (now < client->settling_skip_xing_till) {
        LOGI("[%s]: xing_bs '%s' skip XING event, settling duration left %lds", event->ifname, client->mac_addr,
             client->settling_skip_xing_till - now);
        return;
    }

    if (event->data.rssi_change.high_xing == BSAL_RSSI_HIGHER) {
        stats->rssi.higher++;
        if (!bm_client_bs_ifname_allowed(client, event->ifname) && client->hwm > 0 &&
            event->data.rssi_change.rssi >= client->hwm) {

            if( bm_events_kick_client_upon_idle( client ) ) {
                client->kick_info.kick_pending = true;
                client->kick_info.kick_type    = BM_STEERING_KICK;
                client->kick_info.rssi         = event->data.rssi_change.rssi;

                LOGN("[%s]: xing_bs '%s' is ACTIVE, handling HIGH_RSSI_XING upon idle",
                     event->ifname, client->mac_addr);
            } else {
                LOGN("[%s]: xing_bs '%s' handling HIGH_RSSI_XING (STERING)", event->ifname, client->mac_addr);
                client->cancel_btm = false;
                bm_kick(client, BM_STEERING_KICK, event->data.rssi_change.rssi);
            }
        }
    }
    else if (event->data.rssi_change.low_xing == BSAL_RSSI_LOWER) {
        stats->rssi.lower++;
        if (client->lwm > 0 && event->data.rssi_change.rssi <= client->lwm) {
            client->xing_bs_rssi = event->data.rssi_change.rssi;

            if (client->send_rrm_after_xing && client->info->rrm_caps.bcn_rpt_active) {
                /* Do it fast, scan only current channel */
                bm_client_send_rrm_req(client, BM_CLIENT_RRM_5G_ONLY, 0);
                delay = 1;
            }

            bm_events_xing_bs_sticky_req(client, delay);
        }
    }
    else {
        if( event->data.rssi_change.low_xing == BSAL_RSSI_HIGHER ||
            event->data.rssi_change.high_xing == BSAL_RSSI_LOWER ) {
            if( client->kick_info.kick_pending ) {
                LOGN("[%s]: xing_bs clearing pending steering request for Client '%s'",
                     event->ifname, client->mac_addr);
                client->kick_info.kick_pending = false;
                client->kick_info.rssi         = 0;
            }
            client->cancel_btm = true;
        }
    }
}

static void
bm_events_handle_rssi_xing_cs(bm_client_t *client, bsal_event_t *event)
{
    bm_client_stats_t *stats;

    stats = bm_client_get_stats(client, event->ifname);
    if (WARN_ON(!stats))
        return;

    LOGI("[%s]: xing_cs '%s' snr %d (%d,%d)", event->ifname, client->mac_addr, event->data.rssi_change.rssi,
         event->data.rssi_change.low_xing, event->data.rssi_change.high_xing);

    if( event->data.rssi_change.high_xing == BSAL_RSSI_HIGHER ) {
        stats->rssi.higher++;
        if( client->cs_hwm > 0 && event->data.rssi_change.rssi >= client->cs_hwm ) {
            LOGN("[%s]: xing_cs '%s' RSSI now above CS HWM, setting steering" \
                 " state to xing_high", event->ifname, client->mac_addr);

            if( client->cs_auto_disable) {
                client->cs_state = BM_CLIENT_CS_STATE_XING_DISABLED;
                bm_client_disable_client_steering( client );
            } else {
                // Don't update the cs_state if the client is already in the
                // required cs state.
                if( client->cs_state == BM_CLIENT_CS_STATE_STEERING ||
                    client->cs_state != BM_CLIENT_CS_STATE_XING_HIGH ) {
                    client->cs_state = BM_CLIENT_CS_STATE_XING_HIGH;
                    bm_client_update_cs_state( client );
                }
            }
        }
    }
    else if( event->data.rssi_change.low_xing == BSAL_RSSI_LOWER ) {
        if( client->cs_lwm > 0 && event->data.rssi_change.rssi <= client->cs_lwm ) {
            LOGN("[%s]: xing_cs '%s' RSSI now below CS LWM, setting steering" \
                 " state to xing_low", event->ifname, client->mac_addr );

            if( client->cs_auto_disable) {
                client->cs_state = BM_CLIENT_CS_STATE_XING_DISABLED;
                bm_client_disable_client_steering( client );
            } else {
                // Don't update the cs_state if the client is already in the
                // required cs state.
                if( client->cs_state == BM_CLIENT_CS_STATE_STEERING ||
                    client->cs_state != BM_CLIENT_CS_STATE_XING_LOW ) {
                    client->cs_state = BM_CLIENT_CS_STATE_XING_LOW;
                    bm_client_update_cs_state( client );
                }
            }
        }
    }
}

void
bm_events_handle_rssi_xing(bm_client_t *client, bsal_event_t *event)
{
    switch (client->steering_state) {
    case BM_CLIENT_CLIENT_STEERING:
        bm_events_handle_rssi_xing_cs(client, event);
        break;
    case BM_CLIENT_BAND_STEERING:
    case BM_CLIENT_STEERING_NONE:
    default:
        bm_events_handle_rssi_xing_bs(client, event);
        break;
    }
}

/*****************************************************************************/
bool
bm_events_init(struct ev_loop *loop)
{
    int         rc;

    if( _bsal_initialized ) {
        // BSAL has already been initialized, just [re]start async watcher
        ev_async_start( _evloop, &bm_cb_async );
        return true;
    }

    _evloop             = loop;

    // Initialize CB queue
    ds_dlist_init( &bm_cb_queue, bm_cb_entry_t, node );
    bm_cb_queue_len     = 0;

    // Initialize mutex lock for CB queue
    pthread_mutex_init( &bm_cb_lock, NULL );

    // Initialize async watcher
    ev_async_init( &bm_cb_async, bm_events_async_cb );
    ev_async_start( _evloop, &bm_cb_async );

    // Initialization of BSAL should be done last to avoid potential
    // race conditions since target_bsal_init() now spawns off a thread.
    if ((rc = target_bsal_init(bm_events_bsal_event_cb, loop)) < 0) {
        LOGE("Failed to initialize BSAL");
        return false;
    }
    _bsal_initialized   = true;

    return true;
}

bool
bm_events_cleanup(void)
{
    LOGI( "Events cleaning up" );

    ev_async_stop( _evloop, &bm_cb_async );
    pthread_mutex_destroy( &bm_cb_lock );

    target_bsal_cleanup();
    _bsal_initialized   = false;
    _evloop            = NULL;

    return true;
}
