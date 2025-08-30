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

#include <inttypes.h>

#include "log.h"
#include "const.h"
#include "osp_ps.h"
#include "ovsdb_table.h"
#include "evx.h"
#include "cm2_uplink_event.h"

// Generate the PJS structures
#include "cm2_uplink_event_pjs.h"
#include "pjs_gen_h.h"

#include "cm2_uplink_event_pjs.h"
#include "pjs_gen_c.h"

#define CM2_UPLINK_EVENT_STORE "uplink_event"
#define CM2_UPLINK_EVENT_KEY   "uplink_event"


static const char * const cm2_uplink_event_type_map [] =
{
    [CM2_UPLINK_UNKNOWN]        = "UNKNOWN",
    [CM2_UPLINK_LINK]           = "LINK",
    [CM2_UPLINK_ALL_ROUTER]     = "IPALL_ROUTER",
    [CM2_UPLINK_ALL_INTERNET]   = "IPALL_INTERNET",
    [CM2_UPLINK_NTP]            = "NTP",
};

// Global list of uplink events
static struct cm2_uplink_events g_cm2_uplink_event;

// OVSDB Table Handler: Uplink Events
static ovsdb_table_t table_Uplink_Events;

// Debouncer for persistent storage updates
ev_debounce cm2_uplink_event_update_ev;

// Static variables for active events. This holds info about last outage event
static bool cur_connected[ARRAY_LEN(cm2_uplink_event_type_map)];
static int cur_timestamp[ARRAY_LEN(cm2_uplink_event_type_map)];

const char* cm2_uplink_event_type_str(enum cm2_uplink_event_type type);
enum cm2_uplink_event_type cm2_uplink_event_type_enum(char *str);

bool cm2_uplink_event_add_ovsdb (struct cm2_uplink_event *event)
{
    struct schema_Uplink_Events uplink_evnt = {0};

    SCHEMA_SET_STR(uplink_evnt.type, event->type);
    SCHEMA_SET_INT(uplink_evnt.timestamp, event->timestamp);
    SCHEMA_SET_BOOL(uplink_evnt.connected, event->connected);

    ovsdb_table_insert(&table_Uplink_Events, &uplink_evnt);

    return true;
}

bool cm2_uplink_event_store (struct cm2_uplink_events *cm2_uplink)
{
    pjs_errmsg_t perr;
    ssize_t rstrsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *rjson = NULL;
    char *rstr = NULL;

    LOGD("cm2_uplink: Adding events to pstore");
    // Open persistent storage in read-write mode
    ps = osp_ps_open(CM2_UPLINK_EVENT_STORE, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOGE("cm2_uplink: Error opening \"%s\" persistent store.", CM2_UPLINK_EVENT_STORE);
        goto exit;
    }

    // Convert the uplink event structure to JSON
    rjson = cm2_uplink_events_to_json(cm2_uplink, perr);
    if (rjson == NULL)
    {
        LOGE("cm2_uplink: Error converting cm2_uplink_events structure to JSON: %s", perr);
        goto exit;
    }

    LOGD("cm2_uplink: ns_events_len = %d", cm2_uplink->events_len);
    for (int i = 0; i < cm2_uplink->events_len; i++)
    {
        LOGD("cm2_uplink: events[%d].timestamp= %d", i, cm2_uplink->events[i].timestamp);
        LOGD("cm2_uplink: events[%d].type= %s", i, cm2_uplink->events[i].type);
        LOGD("cm2_uplink: events[%d].connected= %s", i, cm2_uplink->events[i].connected ? "connected":"disconnected");
    }

    // Convert the uplink events structure to string
    rstr = json_dumps(rjson, JSON_COMPACT);
    if (rstr == NULL)
    {
        LOGE("cm2_uplink: Error converting JSON to string.");
        goto exit;
    }

    // Store the string representation to persistent storage
    rstrsz = (ssize_t)strlen(rstr) + 1;
    if (osp_ps_set(ps, CM2_UPLINK_EVENT_KEY, rstr, (size_t)rstrsz) < rstrsz)
    {
        LOGE("cm2_uplink: Error storing uplink events: %s", rstr);
        goto exit;
    }

    retval = true;

exit:
    if (rstr != NULL) json_free(rstr);
    if (rjson != NULL) json_decref(rjson);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}

bool cm2_uplink_event_load (struct cm2_uplink_events *event)
{
    pjs_errmsg_t perr;
    ssize_t rstrsz;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *rjson = NULL;
    char *rstr = NULL;

    memset(event, 0, sizeof(*event));

    ps = osp_ps_open(CM2_UPLINK_EVENT_STORE, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        LOGN("cm2_uplink: Unable to open \"%s\" store.", CM2_UPLINK_EVENT_STORE);
        goto exit;
    }

    // Load and parse the uplink events structure
    rstrsz = osp_ps_get(ps, CM2_UPLINK_EVENT_KEY, NULL, 0);
    if (rstrsz < 0)
    {
        LOGE("cm2_uplink: Error fetching \"%s\" key size.", CM2_UPLINK_EVENT_KEY);
        goto exit;
    }
    else if (rstrsz == 0)
    {
        LOGD("cm2_uplink: Looks like events dont exist in pstore");
        retval = true;
        goto exit;
    }

    // Fetch the "uplink_event" data
    rstr = MALLOC((size_t)rstrsz);
    if (osp_ps_get(ps, CM2_UPLINK_EVENT_KEY, rstr, (size_t)rstrsz) != rstrsz)
    {
        LOGE("cm2_uplink: Error retrieving persistent \"%s\" key.", CM2_UPLINK_EVENT_KEY);
        goto exit;
    }

    // Convert it to JSON
    rjson = json_loads(rstr, 0, NULL);
    if (rjson == NULL)
    {
        LOGE("cm2_uplink: Error parsing JSON: %s", rstr);
        goto exit;
    }

    // Convert it to C
    if (!cm2_uplink_events_from_json(event, rjson, false, perr))
    {
        memset(event, 0, sizeof(*event));
        LOGE("cm2_uplink: Error parsing uplink_event record: %s", perr);
        goto exit;
    }

    retval = true;

exit:
    if (rstr != NULL) FREE(rstr);
    if (rjson != NULL) json_decref(rjson);
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}


static void callback_Uplink_Events (
        ovsdb_update_monitor_t *mon,
        struct schema_Uplink_Events *old,
        struct schema_Uplink_Events *new)
{
    int i;
    (void)new;
    // We're interested only in OVSDB_UPDATE_DEL events; these events will
    // update the uplink events persistent store data
    if (mon->mon_type != OVSDB_UPDATE_DEL)
    {
        return;
    }

    // Find the removed event based on timestamp
    for (i = 0; i < g_cm2_uplink_event.events_len; i++)
    {
        if (g_cm2_uplink_event.events[i].timestamp == old->timestamp) break;
    }

    if (i >= g_cm2_uplink_event.events_len)
    {
        LOGE("cm2_uplink: Unable to remove uplink event with timestamp %"PRId64, old->timestamp);
        return;
    }

    // Remove the record at position i
    if ((i + 1) < g_cm2_uplink_event.events_len)
    {
        memmove(&g_cm2_uplink_event.events[i],
                &g_cm2_uplink_event.events[i + 1],
                (g_cm2_uplink_event.events_len - i - 1) * sizeof(g_cm2_uplink_event.events[0]));
    }

    g_cm2_uplink_event.events_len--;

    ev_debounce_start(EV_DEFAULT, &cm2_uplink_event_update_ev);
}


// Update the cm2_uplink_event persistent storage with new data after
// a certain delay
void cm2_uplink_event_update_fn(struct ev_loop *loop, ev_debounce *w, int revents)
{
    (void)loop;
    (void)w;
    (void)revents;

    if (!cm2_uplink_event_store(&g_cm2_uplink_event))
    {
        LOGE("cm2_uplink: Unable store cm2_uplink_event during delayed update.");
        return;
    }
}

const char* cm2_uplink_event_type_str(enum cm2_uplink_event_type type)
{
    const char *ret = cm2_uplink_event_type_map[CM2_UPLINK_UNKNOWN];

    if (type < ARRAY_LEN(cm2_uplink_event_type_map) && cm2_uplink_event_type_map[type] != NULL)
    {
        ret = cm2_uplink_event_type_map[type];
    }

    return ret;
}

enum cm2_uplink_event_type cm2_uplink_event_type_enum(char *str)
{
    int len = ARRAY_LEN(cm2_uplink_event_type_map);

    for (int i = 0; i < len; i++)
    {
        if (strcmp(str, cm2_uplink_event_type_map[i]) == 0)
        {
            return i;
        }
    }
    return CM2_UPLINK_UNKNOWN;
}


/***
 *
 * Public API
 *****/

void cm2_uplink_event_init (void)
{
    LOGI("cm2_uplink: Initializing uplink events logging");

    // Init ovsdb tables
    OVSDB_TABLE_INIT_NO_KEY(Uplink_Events);
    OVSDB_TABLE_MONITOR(Uplink_Events, false);

    // Initialize the debouncer, minimum update interval is 300ms, maximum
    // is 2 seconds
    ev_debounce_init2(&cm2_uplink_event_update_ev, cm2_uplink_event_update_fn, 0.3, 2.0);

    // Init all elements of cur_connected to true
    for (int i = 0; i < ARRAY_LEN(cur_connected); i++)
    {
        cur_connected[i] = true;
    }

    // Read data from pstore
    if (cm2_uplink_event_load(&g_cm2_uplink_event) == false)
    {
        LOGE("net_status: Unable to access uplink events data.");
        return;
    }

    enum cm2_uplink_event_type event_type = CM2_UPLINK_UNKNOWN;
    // Populate ovsdb with current uplink events data
    for (int i = 0; i < g_cm2_uplink_event.events_len; i++)
    {
        cm2_uplink_event_add_ovsdb(&g_cm2_uplink_event.events[i]);
        LOGD("cm2_uplink: events[%d].timestamp= %d", i, g_cm2_uplink_event.events[i].timestamp);
        LOGD("cm2_uplink: events[%d].type= %s", i, g_cm2_uplink_event.events[i].type);
        LOGD("cm2_uplink: events[%d].connected= %s", i, g_cm2_uplink_event.events[i].connected ? "connected":"disconnected");

        event_type = cm2_uplink_event_type_enum(g_cm2_uplink_event.events[i].type);
        if (cur_timestamp[event_type] < g_cm2_uplink_event.events[i].timestamp)
        {
            // Save last event to cur_ data. This enables tracking if device rebooted while active event
            cur_connected[event_type] = g_cm2_uplink_event.events[i].connected;
            cur_timestamp[event_type] = g_cm2_uplink_event.events[i].timestamp;
        }
    }
}

bool cm2_uplink_event_add_event (int timestamp, bool connected, enum cm2_uplink_event_type type)
{
    // Check if any outage is already active. Allow only one outage at the time.
    // While outage is active cm2 might try and reconfigure interfaces which might
    // tirggere outer outages. Don't log those since they happened because of cm2
    // reconfig.
    if (connected == false)
    {
        for (int i = 0; i < ARRAY_LEN(cur_connected); i++)
        {
            if (cur_connected[i] == false)
            {
                LOGD("cm2_uplink: Already active outage event %s", cm2_uplink_event_type_str(i));
                return true;
            }
        }
    }

    if (cur_connected[type] != connected &&
        cur_timestamp[type] < timestamp)
    {
        cur_connected[type] = connected;
        cur_timestamp[type] = timestamp;
    }
    else
    {
        // None of the state changed, nothing to do, return.
        return true;
    }

    LOGI("cm2_uplink: %s changed to %s at %d", cm2_uplink_event_type_str(type),
                                               connected ? "connected" : "disconnected",
                                               timestamp);

    // Update the uplink event structure
    if (g_cm2_uplink_event.events_len >= ARRAY_LEN(g_cm2_uplink_event.events))
    {
        LOGW("cm2_uplink: Maximum number of uplink events reached, discarding oldest entry.");
        memmove(&g_cm2_uplink_event.events[0],
                &g_cm2_uplink_event.events[1],
                sizeof(g_cm2_uplink_event.events) - sizeof(g_cm2_uplink_event.events[0]));
        g_cm2_uplink_event.events_len--;
    }

    // Append uplink event
    int rec_len = g_cm2_uplink_event.events_len;

    g_cm2_uplink_event.counter++;

    STRSCPY(g_cm2_uplink_event.events[rec_len].type, cm2_uplink_event_type_str(type));
    g_cm2_uplink_event.events[rec_len].timestamp = timestamp;
    g_cm2_uplink_event.events[rec_len].connected = connected;
    g_cm2_uplink_event.events_len = rec_len + 1;

    if (!cm2_uplink_event_store(&g_cm2_uplink_event))
    {
        LOGE("cm2_uplink: Unable to store uplink event.");
    }

    cm2_uplink_event_add_ovsdb(&g_cm2_uplink_event.events[rec_len]);

    return true;
}

bool cm2_uplink_event_is_wan_iface(const char *if_name_check)
{
    char *if_name;
    char *iflist;
    char *pif;

#ifdef CONFIG_MANAGER_WANO_IFACE_LIST
    iflist = (char[]){ CONFIG_MANAGER_WANO_IFACE_LIST };
#else
    return true;
#endif

    while ((pif = strsep(&iflist, " ")) != NULL)
    {
        if_name = strsep(&pif, ":");

        if (if_name == NULL || if_name[0] == '\0') continue;

        if (strcmp(if_name, if_name_check) == 0) return true;

    }

    return false;
}

void cm2_uplink_event_close(void)
{
    ev_debounce_stop(EV_DEFAULT, &cm2_uplink_event_update_ev);
    LOGI("cm2_uplink: Stop uplink event logging");
}
