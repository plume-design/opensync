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
    bsal_t                  bsal;
    bsal_event_t            event;

    ds_dlist_node_t         node;
} bm_cb_entry_t;


static c_item_t map_bsal_bands[] = {
    C_ITEM_STR(BSAL_BAND_24G,                       "2.4G"),
    C_ITEM_STR(BSAL_BAND_5G,                        "  5G")
};

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

static c_item_t map_steering_state[] = {
    C_ITEM_VAL(BSAL_BAND_24G,           BM_CLIENT_STATE_STEERING_2G),
    C_ITEM_VAL(BSAL_BAND_5G,            BM_CLIENT_STATE_STEERING_5G)
};

static c_item_t map_steering_type[] = {
    C_ITEM_VAL(BSAL_BAND_24G,           BM_CLIENT_STATE_STEERING_5G),
    C_ITEM_VAL(BSAL_BAND_5G,            BM_CLIENT_STATE_STEERING_2G)
};

/*****************************************************************************/
static void     bm_events_handle_event( bsal_t bsal, bsal_event_t *event );
static void     bm_events_handle_rssi_xing( bm_client_t *client, bsal_event_t *event );

/*****************************************************************************/
static char *
macstr(uint8_t *macaddr) {
    static char         buf[32];

    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",
                                        macaddr[0], macaddr[1], macaddr[2],
                                        macaddr[3], macaddr[4], macaddr[5]);
    return buf;
}

static char *
bm_events_get_ifname(bsal_t bsal, bsal_band_t band)
{
    bm_pair_t       *pair;

    if ((pair = bm_pair_find_by_bsal(bsal))) {
        return pair->ifcfg[band].ifname;
    }

    return "unknown";
}

// Callback function for BSAL upon receiving steering events from the driver
static void
bm_events_bsal_event_cb( bsal_t bsal, bsal_event_t *event )
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

    cb_entry->bsal  = bsal;
    memcpy( &cb_entry->event, event, sizeof( cb_entry->event ) );

    ds_dlist_insert_tail( &bm_cb_queue, cb_entry );
    bm_cb_queue_len++;
    ret = 1;

exit:
    pthread_mutex_unlock( &bm_cb_lock );
    if( ret && _evloop ) {
        if( !ev_async_pending( &bm_cb_async ) ) {
            ev_async_send( _evloop, &bm_cb_async );
        }
    }

    return;
}

// Asynchronous callback to process events in CB queue
static void
bm_events_async_cb( EV_P_ ev_async *w, int revents )
{
    ds_dlist_iter_t     qiter;
    bm_cb_entry_t       *cb_entry;
    bsal_event_t        *event;
    bsal_t              bsal;

    pthread_mutex_lock( &bm_cb_lock );

    cb_entry = ds_dlist_ifirst( &qiter, &bm_cb_queue );
    while( cb_entry ) {
        ds_dlist_iremove( &qiter );
        bm_cb_queue_len--;

        event = &cb_entry->event;
        bsal  = cb_entry->bsal;

        bm_events_handle_event( bsal, event );

        free( cb_entry );
        cb_entry = ds_dlist_inext( &qiter );
    }

    pthread_mutex_unlock( &bm_cb_lock );
    return;
}

static void
bm_events_handle_event(bsal_t bsal, bsal_event_t *event)
{
    bm_client_stats_t           *stats;
    bm_client_times_t           *times;
    bm_client_t                 *client;
    bsal_band_t                 sband;
    bm_pair_t                   *pair = bm_pair_find_by_bsal(bsal);
    uint32_t                    sstate;
    time_t                      now = time(NULL);
    bool                        reject = false;
    char                        *bandstr = c_get_str_by_key(map_bsal_bands, event->band);
    char                        *ifname = bm_events_get_ifname(bsal, event->band);
    bm_client_reject_t          reject_detection;

    switch (event->type) {

    case BSAL_EVENT_PROBE_REQ:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.probe_req.client_addr))) {
            break;
        }
        stats = &client->stats[event->band];
        times = &client->times;

        LOGT("[%s] %s: BSAL_EVENT_PROBE_REQ from %s, RSSI=%2u%s%s",
                                                    bandstr, ifname,
                                                    macstr(event->data.probe_req.client_addr),
                                                    event->data.probe_req.rssi,
                                                    event->data.probe_req.ssid_null ?
                                                                            ", null" : ", direct",
                                                    event->data.probe_req.blocked   ?
                                                                            ", BLOCKED" : "");

        times->probe[event->band].last = now;
        if (event->data.probe_req.ssid_null) {
            times->probe[event->band].last_null = now;
            stats->probe.null_cnt++;
            if (event->data.probe_req.blocked) {
                stats->probe.null_blocked++;
            }
        }
        else {
            times->probe[event->band].last_direct = now;
            stats->probe.direct_cnt++;
            if (event->data.probe_req.blocked) {
                stats->probe.direct_blocked++;
            }
        }

        // If the client is in 'away' mode of client steering,
        // check if it did an RSSI XING
        bm_client_cs_check_rssi_xing( client, event );

        if (event->data.probe_req.blocked) {
            times->probe[event->band].last_blocked = now;

            if (client->state != BM_CLIENT_STATE_CONNECTED &&
                client->state != BM_CLIENT_STATE_BACKOFF) {
                if( client->steering_state != BM_CLIENT_CLIENT_STEERING ) {
                    if (c_get_value_by_key(map_steering_type, event->band, &sstate)) {
                        bm_client_set_state(client, (bm_client_state_t)sstate);
                    }
                }

                reject_detection = bm_client_get_reject_detection( client );
                switch(reject_detection) {
                    case BM_CLIENT_REJECT_NONE:
                        break;

                    case BM_CLIENT_REJECT_AUTH_BLOCKED:
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

        bm_stats_add_event_to_report( client, event, PROBE, false );
        break;

    case BSAL_EVENT_CLIENT_CONNECT:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.connect.client_addr))) {
            break;
        }
        stats = &client->stats[event->band];
        times = &client->times;

        LOGN("[%s] %s: BSAL_EVENT_CLIENT_CONNECT %s",
                                             bandstr, ifname, macstr(event->data.connect.client_addr));

        if( client->steering_state == BM_CLIENT_CLIENT_STEERING ) {
            bm_client_cs_connect( client, event->band );
        } else {
            // Check for successful band steer?
            if (pair && c_get_value_by_key(map_steering_state, event->band, &sstate)) {
                if (client->state == (bm_client_state_t)sstate) {
                    if (event->band == BSAL_BAND_24G) {
                        sband = BSAL_BAND_5G;
                    }
                    else {
                        sband = BSAL_BAND_24G;
                    }
                    if ((now - client->times.probe[sband].last_blocked) <= pair->success_threshold) {
                        bm_client_success(client, event->band);
                    }
                }
            }
        }

        // last_connect and connect stats incremented in bm_client_connected()
        bm_client_connected(client, bsal, event->band, event);
        break;

    case BSAL_EVENT_CLIENT_DISCONNECT:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.disconnect.client_addr))) {
            break;
        }
        stats = &client->stats[event->band];
        times = &client->times;

        LOGN("[%s] %s: BSAL_EVENT_CLIENT_DISCONNECT %s, src=%s, type=%s, reason=%s",
                                    bandstr, ifname,
                                    macstr(event->data.disconnect.client_addr),
                                    c_get_str_by_key(map_bsal_disc_sources, event->data.disconnect.source),
                                    c_get_str_by_key(map_bsal_disc_types,   event->data.disconnect.type),
                                    c_get_str_by_key(map_bsal_disc_reasons, event->data.disconnect.reason));

        if (client->state != BM_CLIENT_STATE_CONNECTED) {
            break;
        }

        bm_client_disconnected(client);
        times->last_disconnect = now;
        stats->disconnects++;
        stats->last_disconnect.source = event->data.disconnect.source;
        stats->last_disconnect.type   = event->data.disconnect.type;
        stats->last_disconnect.reason = event->data.disconnect.reason;

        bm_stats_add_event_to_report( client, event, DISCONNECT, false );
        break;

    case BSAL_EVENT_CLIENT_ACTIVITY:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.activity.client_addr))) {
            break;
        }
        stats = &client->stats[event->band];
        times = &client->times;

        LOGD("[%s] %s: BSAL_EVENT_CLIENT_ACTIVITY %s is now %s",
                            bandstr, ifname, macstr(event->data.activity.client_addr),
                            event->data.activity.active ? "ACTIVE" : "INACTIVE");

        if (client->state != BM_CLIENT_STATE_CONNECTED) {
            LOGW( "Client '%s': Received ACTIVITY event when disconnected," \
                  " changing state to connected", client->mac_addr );
            bm_client_connected(client, bsal, event->band, event);
        }

        client->active = event->data.activity.active;

        times->last_activity_change = now;
        stats->activity_changes++;

        bm_stats_add_event_to_report( client, event, ACTIVITY, false );
        break;

    case BSAL_EVENT_CHAN_UTILIZATION:
        LOGI("[%s] %s: BSAL_EVENT_CHAN_UTILIZATION is now %2u%%",
                                                        bandstr, ifname, event->data.chan_util.utilization);
        break;

    case BSAL_EVENT_RSSI_XING:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.rssi_change.client_addr))) {
            break;
        }
        stats = &client->stats[event->band];

        LOGT("[%s] %s: BSAL_EVENT_RSSI_XING %s, RSSI=%2u, low xing=%s, high xing=%s, inact xing=%s",
                            bandstr, ifname, macstr(event->data.rssi_change.client_addr),
                            event->data.rssi_change.rssi,
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.low_xing),
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.high_xing),
                            c_get_str_by_key(map_bsal_rssi_changes, event->data.rssi_change.inact_xing));

        if( !client->connected ) {
            // Ignore the event if the client is not connected
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

        bm_events_handle_rssi_xing( client, event );

        break;

    case BSAL_EVENT_RSSI:
        LOGT("[%s] %s: BSAL_EVENT_RSSI %s, RSSI=%2u",
                            bandstr, ifname, macstr(event->data.rssi.client_addr), event->data.rssi.rssi);

        bm_kick_measurement(*(os_macaddr_t *)&event->data.rssi.client_addr, event->data.rssi.rssi);
        break;

    case BSAL_EVENT_DEBUG_CHAN_UTIL:
        LOGD("[%s] %s: BSAL_EVENT_DEBUG_CHAN_UTIL is now %2u%%",
                                                      bandstr, ifname, event->data.chan_util.utilization);
        break;

    case BSAL_EVENT_DEBUG_RSSI:
        if (!(client = bm_client_find_by_macaddr(*(os_macaddr_t *)&event->data.rssi.client_addr))) {
            break;
        }

        LOGD("[%s] %s: BSAL_EVENT_DEBUG_RSSI %s, RSSI=%2u",
                           bandstr, ifname, macstr(event->data.rssi.client_addr), event->data.rssi.rssi);
        break;

    case BSAL_EVENT_STEER_CLIENT:
        {
            LOGD("[%s] %s: BSAL_EVENT_STEER_CLIENT %s",
                    bandstr, ifname, macstr(event->data.steer.client_addr));
            // add client to internal list for stat collection
            bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr);
            break;
        }

    case BSAL_EVENT_STEER_SUCCESS:
        {
            LOGD("[%s] %s: BSAL_EVENT_STEER_SUCCESS %s from: %d to: %d",
                    bandstr, ifname, macstr(event->data.steer.client_addr),
                    event->data.steer.from_ch, event->data.steer.to_ch);
            client = bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr);
            if (!client) break;
            stats = &client->stats[event->band];
            stats->steering_success_cnt++;
        }
        break;

    case BSAL_EVENT_STEER_FAILURE:
        {
            LOGD("[%s] %s: BSAL_EVENT_STEER_FAILURE %s from: %d to: %d",
                    bandstr, ifname, macstr(event->data.steer.client_addr),
                    event->data.steer.from_ch, event->data.steer.to_ch);
            client = bm_client_find_or_add_by_macaddr((os_macaddr_t*)&event->data.steer.client_addr);
            if (!client) break;
            stats = &client->stats[event->band];
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
                                                macstr( event->data.auth_fail.client_addr ),
                                                event->data.auth_fail.rssi,
                                                c_get_str_by_key(map_bsal_disc_reasons, event->data.auth_fail.reason),
                                                ( event->data.auth_fail.bs_blocked == 1 )? "BLOCKED" : "" );

            if( client->cs_reject_detection == BM_CLIENT_REJECT_AUTH_BLOCKED &&
                event->data.auth_fail.bs_blocked == 1 ) {
                bm_client_rejected( client, event );
            }
        }
        break;

    default:
        LOGW("[%s] Unhandled event type %u", bandstr, event->type);
        break;

    }

    return;
}

static void
bm_events_handle_rssi_xing( bm_client_t *client, bsal_event_t *event )
{
    bm_client_stats_t *stats = &client->stats[event->band];

    if( client->steering_state != BM_CLIENT_CLIENT_STEERING ) {
        if (event->data.rssi_change.high_xing == BSAL_RSSI_HIGHER) {
            stats->rssi.higher++;
            if (event->band == BSAL_BAND_24G && client->hwm > 0 && event->data.rssi_change.rssi >= client->hwm) {
                bm_kick(client, BM_STEERING_KICK, event->data.rssi_change.rssi);
            }
        }
        else if (event->data.rssi_change.low_xing == BSAL_RSSI_LOWER) {
            stats->rssi.lower++;
            if (client->lwm > 0 && event->data.rssi_change.rssi <= client->lwm) {
                bm_kick(client, BM_STICKY_KICK, event->data.rssi_change.rssi);
            }
        }
    } else {
        if( event->data.rssi_change.high_xing == BSAL_RSSI_HIGHER ) {
            stats->rssi.higher++;
            if( client->cs_hwm > 0 && event->data.rssi_change.rssi >= client->cs_hwm ) {
                LOGN( "Client '%s' RSSI now above CS HWM, setting steering" \
                        " state to xing_high", client->mac_addr );

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
                LOGN( "Client '%s' RSSI now below CS LWM, setting steering" \
                        " state to xing_low", client->mac_addr );

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

    return;
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
    // race conditions since bsal_init() now spawns off a thread.
    if ((rc = bsal_init(bm_events_bsal_event_cb)) < 0) {
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

    bsal_event_cleanup();
    _bsal_initialized   = false;
    _evloop            = NULL;

    return true;
}
