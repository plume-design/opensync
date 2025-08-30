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
 * ===========================================================================
 * WAN plug-in pipeline management module
 *
 * A plug-in pipeline is responsible for executing plug-ins on an interface.
 * There can be multiple independent pipelines running on a single interface.
 * Plug-ins can be executed in series or in parallel, depending on the plug-in
 * and pipeline masks.
 *
 * There can be multiple pipe-lines associated with a single interface as
 * plug-ins are capable of creating their own pipelines.
 * ===========================================================================
 */
#include <ev.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include "evx.h"
#include "memutil.h"
#include "os_random.h"
#include "os_time.h"
#include "osa_assert.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "util.h"

#include "wano.h"
#include "wano_internal.h"
#include "wano_wan.h"
#include "wano_plugin_stam.h"

#define WANO_PPLINE_RETRY_TIME      3       /* Retry time increment in seconds */
#define WANO_PPLINE_RETRY_MAX       5       /* Maximum values of retries (for calculations) */
#define WANO_PPLINE_RESTART_MIN     1.0     /* Pipeline restart debounce timer */
#define WANO_PPLINE_RESTART_MAX     5.0     /* Pipeline restart maximum debounce timeout */

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

/*
 * Structure describing a single plug-in running on the pipeline
 */
struct wano_ppline_plugin
{
    /** Plug-in object state */
    wano_plugin_state_t         wpp_state;
    /** Parent pipeline */
    wano_ppline_t              *wpp_ppline;
    /** Plug-in structure */
    struct wano_plugin         *wpp_plugin;
    /** Plug-in status */
    struct wano_plugin_status   wpp_status;
    /** Plug-in status update async watcher */
    ev_async                    wpp_status_async;
    /** Plug-in handle */
    wano_plugin_handle_t       *wpp_handle;
    /** Plug-in timeout timer */
    ev_timer                    wpp_timeout;
    /** Linked list node structure */
    ds_dlist_t                  wpp_dnode;
};

static ds_dlist_t g_wano_ppline_list = DS_DLIST_INIT(struct wano_ppline, wpl_dnode);

static ev_debounce_fn_t wano_ppline_restart_debounce_fn;
static void wano_ppline_plugin_init(wano_ppline_t *self);
static void wano_ppline_plugin_reset(wano_ppline_t *self, bool force);
static void wano_ppline_schedule(wano_ppline_t *self);
static bool wano_ppline_run(wano_ppline_t *self);
static struct wano_ppline_plugin *wano_ppline_plugin_new(wano_ppline_t *self, struct wano_plugin *wp);
static void wano_ppline_plugin_del(wano_ppline_t *self, struct wano_ppline_plugin *wpp);
static wano_inet_state_event_fn_t wano_ppline_inet_state_event_fn;
static wano_plugin_status_fn_t wano_ppline_plugin_status_fn;
static void wano_ppline_retry_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void wano_ppline_status_async_fn(struct ev_loop *, ev_async *ev, int revent);
static void wano_ppline_plugin_timeout_fn(struct ev_loop *loop, ev_timer *w, int revent);
static wano_connmgr_uplink_event_fn_t wano_ppline_cmu_event_fn;
static wano_ovs_port_event_fn_t wano_ppline_ovs_port_event_fn;
static void wano_ppline_check_freeze(wano_ppline_t *wpl);
static void wano_ppline_reset_ipv6(wano_ppline_t *self);
static void wano_ppline_map_iftype(const char *in_iftype, char *out_iftype, int out_size);

bool wano_ppline_init(
        wano_ppline_t *self,
        const char *ifname,
        const char *iftype,
        uint64_t emask)
{
    memset(self, 0, sizeof(*self));

    STRSCPY_WARN(self->wpl_ifname, ifname);
    STRSCPY_WARN(self->wpl_iftype, iftype);
    self->wpl_plugin_emask = emask;

    ds_dlist_init(&self->wpl_plugin_waitq, struct wano_ppline_plugin, wpp_dnode);
    ds_dlist_init(&self->wpl_event_list, wano_ppline_event_t, wpe_dnode);

    /* Subscribe to Wifi_Inet_State events */
    if (!wano_inet_state_event_init(
            &self->wpl_inet_state_event,
            self->wpl_ifname,
            wano_ppline_inet_state_event_fn))
    {
        LOG(ERR, "wano: %s: Error initializing inet_state_event object", self->wpl_ifname);
        return false;
    }

    wano_connmgr_uplink_event_init(&self->wpl_cmu_event, wano_ppline_cmu_event_fn);
    if (!wano_connmgr_uplink_event_start(&self->wpl_cmu_event, ifname))
    {
        LOG(WARN, "wano: %s: Error initializing connmgr_uplink_event object.", self->wpl_ifname);
    }

    wano_ovs_port_event_init(&self->wpl_ovs_port_event, wano_ppline_ovs_port_event_fn);
    if (!wano_ovs_port_event_start(&self->wpl_ovs_port_event, ifname))
    {
        LOG(WARN, "wano: %s: Error initializing wano_ovs_port_event object.",
                self->wpl_ifname);
    }

    ev_timer_init(&self->wpl_retry_timer, wano_ppline_retry_timer_fn, 0.0, 0.0);

    /* Initialize plug-ins */
    wano_ppline_plugin_init(self);

    self->wpl_init = true;

    ds_dlist_insert_tail(&g_wano_ppline_list, self);

    LOG(NOTICE, "wano: %s: Created WAN pipeline on interface.", ifname);

    return true;
}

/*
 * Dispose of a wano_ppline_t object
 */
void wano_ppline_fini(wano_ppline_t *self)
{
    struct wano_ppline_plugin *wpp;
    ds_dlist_iter_t iter;

    if (!self->wpl_init)
    {
        return;
    }

    self->wpl_init = false;
    ds_dlist_remove(&g_wano_ppline_list, self);

    wano_ovs_port_event_stop(&self->wpl_ovs_port_event);
    wano_connmgr_uplink_event_stop(&self->wpl_cmu_event);
    wano_inet_state_event_fini(&self->wpl_inet_state_event);

    ds_dlist_foreach_iter(&self->wpl_plugin_waitq, wpp, iter)
    {
        ds_dlist_iremove(&iter);
        wano_ppline_plugin_del(self, wpp);
    }

    /*
     * Remove any entries that this interface might have in the
     * Connection_Manager_Uplink table
     */
    (void)wano_connmgr_uplink_delete(self->wpl_ifname);
}

void wano_ppline_restart_debounce_fn(struct ev_loop *loop, ev_debounce *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    struct wano_ppline *wpl;

    LOG(NOTICE, "wano: Commencing general pipeline restart.");

    ds_dlist_foreach(&g_wano_ppline_list, wpl)
    {
        wano_ppline_state_do(
                &wpl->wpl_state,
                wano_ppline_exception_PPLINE_RESTART,
                NULL);
    }
}

void wano_ppline_restart_all(void)
{
    static ev_debounce restart_debounce;
    static bool restart_init = false;

    if (!restart_init)
    {
        ev_debounce_init2(
                &restart_debounce,
                wano_ppline_restart_debounce_fn,
                WANO_PPLINE_RESTART_MIN,
                WANO_PPLINE_RESTART_MAX);

        restart_init = true;
    }

    ev_debounce_start(EV_DEFAULT, &restart_debounce);
}

void wano_ppline_wan_set(wano_ppline_t *wpl, wano_wan_t *wan)
{
    wpl->wpl_wan = wan;
}

wano_wan_t *wano_ppline_wan_get(wano_ppline_t *wpl)
{
    return wpl->wpl_wan;
}

wano_ppline_t *wano_ppline_from_plugin_handle(const wano_plugin_handle_t *plugin)
{
    struct wano_ppline_plugin *wpl = plugin->wh_data;

    if (wpl == NULL) return NULL;

    return wpl->wpp_ppline;
}

wano_wan_t *wano_wan_from_plugin_handle(wano_plugin_handle_t *plugin)
{
    wano_ppline_t *wpl = wano_ppline_from_plugin_handle(plugin);
    if (wpl == NULL) return NULL;
    return wano_ppline_wan_get(wpl);
}

/*
 * Initialize a plug-in pipeline event object
 */
void wano_ppline_event_init(wano_ppline_event_t *self, wano_ppline_event_fn_t *fn)
{
    memset(self, 0, sizeof(*self));
    self->wpe_event_fn = fn;
}

/*
 * Start listening to plug-in pipeline @p wpp events
 */
void wano_ppline_event_start(wano_ppline_event_t *self, wano_ppline_t *wpp)
{
    if (self->wpe_ppline != NULL) return;

    self->wpe_ppline = wpp;
    ds_dlist_insert_tail(&wpp->wpl_event_list, self);
}

/*
 * Stop listening to plug-in pipeline @p wpp events
 */
void wano_ppline_event_stop(wano_ppline_event_t *self)
{
    if (self->wpe_ppline == NULL) return;

    ds_dlist_remove(&self->wpe_ppline->wpl_event_list, self);
    self->wpe_ppline = NULL;
}

void wano_ppline_event_dispatch(wano_ppline_t *self, enum wano_ppline_status status)
{
    wano_ppline_event_t *ppe;

    ds_dlist_foreach(&self->wpl_event_list, ppe)
    {
        ppe->wpe_event_fn(ppe, status);
    }
}

struct wano_ppline_plugin *wano_ppline_plugin_new(wano_ppline_t *self, struct wano_plugin *wp)
{
    struct wano_ppline_plugin *wpp;

    wpp = CALLOC(1, sizeof(struct wano_ppline_plugin));

    wpp->wpp_ppline = self;
    wpp->wpp_plugin = wp;
    wpp->wpp_handle = NULL;

    return wpp;
}

void wano_ppline_plugin_del(wano_ppline_t *self, struct wano_ppline_plugin *wpp)
{
    wano_plugin_state_do(&wpp->wpp_state, wano_plugin_exception_CANCEL, self);
    FREE(wpp);
}

/*
 * Initialize the plug-in list (waitq)
 */
void wano_ppline_plugin_init(wano_ppline_t *self)
{
    struct wano_plugin *wp;
    wano_plugin_iter_t wpi;

    /*
     * Populate the waitqueue
     */
    for (wp = wano_plugin_first(&wpi); wp != NULL; wp = wano_plugin_next(&wpi))
    {
        struct wano_ppline_plugin *wpp;

        /* Skip all plug-ins in the exclusion mask */
        if ((wp->wanp_mask & self->wpl_plugin_emask) != 0) continue;

        wpp = wano_ppline_plugin_new(self, wp);
        if (wpp == NULL)
        {
            LOG(WARN, "Error allocating plugin: %s", wp->wanp_name);
            continue;
        }

        ds_dlist_insert_tail(&self->wpl_plugin_waitq, wpp);

        wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_START, self);
    }

    if (ds_dlist_is_empty(&self->wpl_plugin_waitq))
    {
        LOG(WARN, "wano: %s: Plug-in pipeline is empty (mask = %"PRIx64").",
                self->wpl_ifname, self->wpl_plugin_emask);
    }
}

/*
 * Move plug-ins to the PENDING state. If `force` is false, plug-ins in a
 * running state (ACTIVE or DETACHED) are left intact. Other plug-ins are first
 * forced to the IDLE state.
 */
void wano_ppline_plugin_reset(wano_ppline_t *self, bool force)
{
    struct wano_ppline_plugin *wpp;

    ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
    {
        LOG(DEBUG, "%s: Pluing %s state: %s",
                self->wpl_ifname,
                wpp->wpp_plugin->wanp_name,
                wano_plugin_state_str(wano_plugin_state_get(&wpp->wpp_state)));
        switch (wano_plugin_state_get(&wpp->wpp_state))
        {
            case wano_plugin_ACTIVE:
            case wano_plugin_DETACHED:
                if (!force) continue;
                wano_plugin_state_do(&wpp->wpp_state, wano_plugin_exception_CANCEL, self);
                break;

            case wano_plugin_IDLE:
                break;

            default:
                wano_plugin_state_do(&wpp->wpp_state, wano_plugin_exception_CANCEL, self);
                break;
        }

        /* Skip plugins that match the exclude mask */
        if ((self->wpl_plugin_emask & wpp->wpp_plugin->wanp_mask) != 0) continue;
        wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_START, self);
    }
}

/*
 * Find all plug-ins that are eligible for running and schedule them.
 *
 * The function below finds all plug-ins in the PENDING state that do not
 * conflict with any currently running plug-in (by checking the run-mask)
 * and schedules them.
 *
 * Note that plug-ins that are in the SCHEDULED state are not started yet.
 * This happens after the IPv4/IPv6 interface reset.
 */
void wano_ppline_schedule(wano_ppline_t *self)
{
    struct wano_ppline_plugin *wpp;

    uint64_t rmask = 0;

    /* Calculate the initial run mask from ACTIVE plug-ins */
    ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
    {
        if (wano_plugin_state_get(&wpp->wpp_state) != wano_plugin_ACTIVE) continue;
        rmask |= wpp->wpp_plugin->wanp_mask;
    }

    ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
    {
        enum wano_plugin_state wpp_state = wano_plugin_state_get(&wpp->wpp_state);

        LOG(DEBUG, "%s: Processing plugin: %s (state = %s)",
                self->wpl_ifname,
                wpp->wpp_plugin->wanp_name,
                wano_plugin_state_str(wano_plugin_state_get(&wpp->wpp_state)));

        /* Skip plug-ins that are blocked on other plug-ins */
        if ((rmask & wpp->wpp_plugin->wanp_mask) != 0) continue;

        /* If a plug-in is pending schedule it now */
        if (wpp_state == wano_plugin_PENDING)
        {
            LOG(DEBUG, "%s: Scheduling plug-in: %s", self->wpl_ifname, wpp->wpp_plugin->wanp_name);
            wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_SCHEDULE, self);
        }

        /*
         * Pending plug-ins revert back to the IDLE state on error, so the
         * condition below covers both the cases of finished plug-ins and
         * plug-ins that failed to be scheduled.
         */
        if (wpp_state == wano_plugin_IDLE) continue;
        /* Detached plug-ins should not contribute to the run mask */
        if (wpp_state == wano_plugin_DETACHED) continue;
        rmask |= wpp->wpp_plugin->wanp_mask;
    }
}

/*
 * Execute scheduled plug-ins and return true if any plug-ins are running
 * at this moment.
 */
bool wano_ppline_run(wano_ppline_t *self)
{
    struct wano_ppline_plugin *wpp;

    bool scheduled = false;

    /*
     * Run scheduled plug-ins
     */
    ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
    {
        if (wano_plugin_state_get(&wpp->wpp_state) == wano_plugin_RUNNING) scheduled = true;
        if (wano_plugin_state_get(&wpp->wpp_state) != wano_plugin_SCHEDULED) continue;
        wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_RUN, self);
        scheduled = true;
    }

    return scheduled;
}

void wano_ppline_inet_state_event_fn(
        struct wano_inet_state_event *event,
        struct wano_inet_state *status)
{
    wano_ppline_t *self = CONTAINER_OF(event, wano_ppline_t, wpl_inet_state_event);
    if (self->wpl_carrier_exception && !status->is_port_state)
    {
        LOG(NOTICE, "wano: %s: Carrier loss detected.", self->wpl_ifname);
        self->wpl_carrier_exception = false;
        wano_ppline_state_do(&self->wpl_state, wano_ppline_exception_PPLINE_RESTART, NULL);
    }
    else
    {
        wano_ppline_state_do(&self->wpl_state, wano_ppline_do_INET_STATE_UPDATE, status);
    }
}

void wano_ppline_plugin_status_fn(wano_plugin_handle_t *ph, struct wano_plugin_status *ps)
{
    struct wano_ppline_plugin *wpp = ph->wh_data;
    wpp->wpp_status = *ps;
    ev_async_send(EV_DEFAULT, &wpp->wpp_status_async);
}

/*
 * Process plug-in events asynchronously. Note that if a plug-in sends several
 * events in a rapid succession, they may get compressed into a single event.
 */
void wano_ppline_status_async_fn(struct ev_loop *loop, ev_async *ev, int revent)
{
    (void)loop;
    (void)revent;

    char iftype[32];

    struct wano_ppline_plugin *wpp = CONTAINER_OF(ev, struct wano_ppline_plugin, wpp_status_async);
    wano_ppline_t *self = wpp->wpp_ppline;

    wano_ppline_map_iftype(self->wpl_iftype, iftype, sizeof(iftype));

    switch (wpp->wpp_status.ws_type)
    {
        case WANP_OK:
        {
            char *update_ifname = (wpp->wpp_status.ws_ifname[0] != '\0') ?
                wpp->wpp_status.ws_ifname : self->wpl_ifname;

            /* Move the plug-in to the ACTIVE state */
            wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_SUCCESS, self);

            if (!wano_dns_probe_run(update_ifname))
            {
                LOG(WARN, "wano: %s: WAN Plug-in fail: %s, wan probe failed",
                    update_ifname, wano_plugin_name(wpp->wpp_handle));
                /* Cancel plug-in and reschedule an update */
                wano_plugin_state_do(&wpp->wpp_state, wano_plugin_exception_CANCEL, self);
                wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
                break;
            }

            LOG(NOTICE, "wano: %s: WAN Plug-in success: %s",
                    self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            if (wpp->wpp_status.ws_ifname[0] != '\0')
            {
                /* Reset the .has_L3 field from the parent interface */
                if (!WANO_CONNMGR_UPLINK_DELETE_COLUMN(self->wpl_ifname, .has_L3 = WANO_TRI_TRUE))
                {
                    LOG(WARN, "wano: %s: Error deleting .has_L3 from Connection_Manager_Uplink).",
                            self->wpl_ifname);
                }
            }

            if (wpp->wpp_status.ws_iftype[0] != '\0')
            {
                STRSCPY(iftype, wpp->wpp_status.ws_iftype);
            }

            /* Use the interface name and type reported in the plug-in status */
            if (!WANO_CONNMGR_UPLINK_UPDATE(
                        update_ifname,
                        .if_type = iftype,
                        .has_L2 = WANO_TRI_TRUE,
                        .has_L3 = WANO_TRI_TRUE,
                        .loop = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplink (plugin interface) table (has_L3 = true).",
                        self->wpl_ifname);
            }

            self->wpl_has_l3 = true;

            /* Reset the retries count */
            self->wpl_retries = 0;

            /* Notify upper layers */
            wano_ppline_event_dispatch(self, WANO_PPLINE_OK);
            return;
        }

        case WANP_RESERVED:
        {
            LOG(NOTICE, "wano: %s: Interface reserved: %s", self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            if (wpp->wpp_status.ws_iftype[0] != '\0')
            {
                STRSCPY(iftype, wpp->wpp_status.ws_iftype);
            }

            /* Use the interface name and type reported in the plug-in status */
            if (!WANO_CONNMGR_UPLINK_UPDATE(
                        self->wpl_ifname,
                        .if_type = iftype,
                        .has_L2 = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplink (plugin interface) table (has_L3 = true).",
                        self->wpl_ifname);
            }

            /* Reset the .has_L3 field from the parent interface */
            if (!WANO_CONNMGR_UPLINK_DELETE_COLUMN(
                        self->wpl_ifname,
                        .has_L3 = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error deleting .has_L3 from Connection_Manager_Uplink).",
                        self->wpl_ifname);
            }

            self->wpl_has_l3 = true;

            /* Reset the retries count */
            self->wpl_retries = 0;

            /* Notify upper layers */
            wano_ppline_event_dispatch(self, WANO_PPLINE_OK);
            return;
        }

        case WANP_SKIP:
            LOG(INFO, "wano: %s: Plug-in requested skip: %s", self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));
            wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_SKIP, self);
            wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
            break;

        case WANP_BUSY:
            LOG(INFO, "wano: %s: Plug-in %s is busy, stopping timeout timer and resetting has_L3.", self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));
            /* Reset the .has_L3 field from the parent interface */
            if (!WANO_CONNMGR_UPLINK_DELETE_COLUMN(self->wpl_ifname, .has_L3 = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error deleting .has_L3 from Connection_Manager_Uplink -- WANP_BUSY).",
                        self->wpl_ifname);
            }
            /* Stop the timeout timer */
            ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);
            break;

        case WANP_ERROR:
            LOG(ERR, "wano: %s: Error detected running plug-in: %s. Skipping.",
                    self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));
            wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_ERROR, self);
            wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
            break;

        case WANP_DETACH:
            LOG(INFO, "wano: %s: Plug-in %s detached.",
                    self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);
            wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_DETACH, self);
            wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
            break;

        case WANP_RESTART:
            wano_ppline_state_do(&self->wpl_state, wano_ppline_exception_PPLINE_RESTART, NULL);
            break;

        case WANP_ABORT:
            /*
             * Abort processing of the current pipeline
             */
            wano_ppline_state_do(&self->wpl_state, wano_ppline_exception_PPLINE_ABORT, NULL);
            break;
    }
}

void wano_ppline_plugin_timeout_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    struct wano_ppline_plugin *wpp = CONTAINER_OF(w, struct wano_ppline_plugin, wpp_timeout);
    wano_ppline_t *self = wpp->wpp_ppline;

    LOG(NOTICE, "wano: %s: Plug-in timed out: %s. Terminating.",
            self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

    wano_plugin_state_do(&wpp->wpp_state, wano_plugin_do_TIMEOUT, self);
    wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
}

void wano_ppline_cmu_event_fn(wano_connmgr_uplink_event_t *ce, struct wano_connmgr_uplink_state *state)
{
    wano_ppline_t *wpl = CONTAINER_OF(ce, wano_ppline_t, wpl_cmu_event);
    wpl->wpl_uplink_bridge = (state->bridge[0] != '\0');

    wano_ppline_check_freeze(wpl);
}

void wano_ppline_ovs_port_event_fn(wano_ovs_port_event_t *pe, struct wano_ovs_port_state *state)
{
    wano_ppline_t *wpl = CONTAINER_OF(pe, wano_ppline_t, wpl_ovs_port_event);
    wpl->wpl_bridge = state->ps_exists;

    wano_ppline_check_freeze(wpl);
}

/*
 * Check if pipeline FREEZE/UNFREEZE events should be sent
 */
void wano_ppline_check_freeze(wano_ppline_t *wpl)
{
    /*
     * The pipeline should be frozen if the interface is bridged
     * (wpl->wpl_bridged is true when the interface is present in the Port
     * table) or the bridge column is set in the Connection_Manager_Uplink table
     * (wpl->wpl_uplink_bridged is true).
     */
    bool freeze = wpl->wpl_bridge || wpl->wpl_uplink_bridge;

    /*
     * If the pipeline is frozen (the current state is FREEZE) and the bridge name
     * is empty, we need to unfreeze the pipeline
     */
    if (!freeze && wano_ppline_state_get(&wpl->wpl_state) == wano_ppline_FREEZE)
    {
        wano_ppline_state_do(&wpl->wpl_state, wano_ppline_do_PPLINE_UNFREEZE, NULL);
    }
    /*
     * If the pipeline is not frozen and the bridge column is not empty, issue
     * a FREEZE exception so that WANO relinquishes control of the pipeline
     * as fast as possible
     */
    else if (freeze && wano_ppline_state_get(&wpl->wpl_state) != wano_ppline_FREEZE)
    {
        wano_ppline_state_do(&wpl->wpl_state, wano_ppline_exception_PPLINE_FREEZE, NULL);
    }
}

/*
 * Retry timer -- this is used in the IDLE state to restart the pipeline after a
 * backoff timer (logarithmic increase)
 */
void wano_ppline_retry_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)revent;

    wano_ppline_t *self = CONTAINER_OF(w, wano_ppline_t, wpl_retry_timer);

    wano_ppline_state_do(&self->wpl_state, wano_ppline_do_IDLE_TIMEOUT, NULL);
}

/*
 * Reset IPv6 settings. Delete any static, DHCPv6 client/server and Route
 * Advertising configuration. Do not delete the row in IP_Interface as it holds
 * a strong reference to QoS configuration which we *need* to retain
 * (Interface_QoS and Interface_Queue).
 */
void wano_ppline_reset_ipv6(wano_ppline_t *self)
{
    struct schema_IP_Interface ip_interface;
    ovsdb_table_t table_IP_Interface;
    ovsdb_table_t table_DHCPv6_Client;
    ovsdb_table_t table_DHCPv6_Server;
    ovsdb_table_t table_IPv6_RouteAdv;
    ovsdb_table_t table_IPv6_Address;
    int ii;
    int rc;

    OVSDB_TABLE_INIT(IP_Interface, _uuid);
    OVSDB_TABLE_INIT(DHCPv6_Client, _uuid);
    OVSDB_TABLE_INIT(DHCPv6_Server, _uuid);
    OVSDB_TABLE_INIT(IPv6_RouteAdv, _uuid);

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", self->wpl_ifname, &ip_interface))
    {
        /* No entry in IP_Interface -- we're good */
        LOG(INFO, "wano: %s: IP_Interface no entry for if_name.", self->wpl_ifname);
        return;
    }

    ovsdb_table_delete_where(&table_DHCPv6_Client, ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid));
    ovsdb_table_delete_where(&table_DHCPv6_Server, ovsdb_where_uuid("interface", ip_interface._uuid.uuid));
    ovsdb_table_delete_where(&table_IPv6_RouteAdv, ovsdb_where_uuid("interface", ip_interface._uuid.uuid));

    /* Delete non-autoconfigured IPv6 addresses */
    for (ii = 0; ii < ip_interface.ipv6_addr_len; ii++)
    {
        struct schema_IPv6_Address *addr;
        int addr_cnt;

        addr = ovsdb_table_select_where(&table_IPv6_Address, ovsdb_where_uuid("_uuid", ip_interface.ipv6_addr[ii].uuid), &addr_cnt);
        if (addr == NULL || addr_cnt < 1)
        {
            continue;
        }

        if (strcmp(addr->origin, "auto_configured") == 0) continue;

        rc = ovsdb_table_delete_where_with_parent(
                &table_IPv6_Address,
                ovsdb_where_uuid("_uuid", addr->_uuid.uuid),
                SCHEMA_TABLE(IP_Interface),
                ovsdb_where_uuid("_uuid", ip_interface._uuid.uuid),
                SCHEMA_COLUMN(IP_Interface, ipv6_addr));
        if (rc <= 0)
        {
            LOG(WARN, "wano: %s: Error deleting IPv6_Address uuid=%s",
                    self->wpl_ifname,
                    addr->_uuid.uuid);
        }
    }

    /* Delete IPv6_Prefixes */
    struct schema_IP_Interface ip_noprefix;

    memset(&ip_noprefix, 0, sizeof(ip_noprefix));
    ip_noprefix._partial_update = true;
    SCHEMA_UNSET_MAP(ip_noprefix.ipv6_prefix);

    rc = ovsdb_table_update_where(
            &table_IP_Interface,
            ovsdb_where_uuid("_uuid", ip_interface._uuid.uuid),
            &ip_noprefix);
    if (rc <= 0)
    {
        LOG(WARN, "wano: %s: Error clearing ipv6_prefix list from IP_Interface where uuid=%s",
                self->wpl_ifname,
                ip_interface._uuid.uuid);
    }
}

void wano_ppline_map_iftype(const char *in_iftype, char *out_iftype, int out_size)
{
    if (strcmp(in_iftype, "unmanaged") == 0)
    {
        strscpy(out_iftype, "eth", out_size);
    }
    else
    {
        strscpy(out_iftype, in_iftype, out_size);
    }
}

/*
 * ===========================================================================
 *  wano_ppline State Machine
 * ===========================================================================
 */
enum wano_ppline_state wano_ppline_state_INIT(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)action;
    (void)data;
    char if_type[32] = {0};

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    wano_ppline_event_dispatch(self, WANO_PPLINE_RESTART);

    /* Destroy current plug-ins if they are left over from a restart */
    wano_ppline_plugin_reset(self, true);

    /* Remove any entries in the Connection_Manager_Uplink table */
    if (wano_connmgr_uplink_delete(self->wpl_ifname))
    {
        LOG(INFO, "wano: %s: Stale Connection_Manager_Uplink entry was removed.", self->wpl_ifname);
    }

    self->wpl_immediate_timeout = clock_mono_double() + CONFIG_MANAGER_WANO_PLUGIN_IMMEDIATE_TIMEOUT;

    wano_ppline_map_iftype(self->wpl_iftype, if_type, sizeof(if_type));

    if (!WANO_CONNMGR_UPLINK_UPDATE(
                self->wpl_ifname,
                .if_type = if_type,
                .has_L2 = WANO_TRI_FALSE))
    {
        LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplink (iftype = %s, has_L2 = false)",
                self->wpl_ifname, self->wpl_iftype);
    }

    if (self->wpl_wan != NULL)
    {
        wano_wan_pause(self->wpl_wan, true);
    }

    self->wpl_has_l3 = false;

    return wano_ppline_START;
}

enum wano_ppline_state wano_ppline_state_START(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)action;
    (void)data;
    (void)state;

    return wano_ppline_IF_ENABLE;
}

/*
 * Re-enable the interface -- set Wifi_Inet_Config:enabled,network to true
 * and wait until Wifi_Inet_State reflects the change
 */
enum wano_ppline_state wano_ppline_state_IF_ENABLE(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    struct wano_inet_state *is = data;

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            LOG(INFO, "wano: %s: Enabling interface.", self->wpl_ifname);
            if (!WANO_INET_CONFIG_UPDATE(
                        self->wpl_ifname,
                        .if_type = self->wpl_iftype,
                        .enabled = WANO_TRI_TRUE,
                        .network = WANO_TRI_TRUE,
                        .nat = WANO_TRI_TRUE,
                        .ip_assign_scheme = "none"))
            {
                LOG(WARN, "wano: %s: Error writing Wifi_Inet_Config in IF_ENABLE.", self->wpl_ifname);
            }
            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            return 0;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (!is->is_enabled) break;
            if (!is->is_network) break;
            if (!is->is_nat) break;
            if (strcmp(is->is_ip_assign_scheme, "none") != 0) break;
            return wano_ppline_IF_CARRIER;

        default:
            break;
    }

    return 0;
}

/*
 * Wait until there's carrier on the interface
 */
enum wano_ppline_state wano_ppline_state_IF_CARRIER(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    struct wano_inet_state *is = data;

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            LOG(INFO, "wano: %s: Waiting for carrier.", self->wpl_ifname);
            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            break;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (!is->is_port_state)
            {
                break;
            }

            if (self->wpl_wan != NULL)
            {
                wano_wan_pause(self->wpl_wan, false);
            }

            self->wpl_carrier_exception = true;

            if (!WANO_CONNMGR_UPLINK_UPDATE(self->wpl_ifname, .has_L2 = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplinkg talbe (has_L2 = true).",
                        self->wpl_ifname);
            }

            LOG(INFO, "wano: %s: Carrier detected.", self->wpl_ifname);
            return wano_ppline_PLUGIN_SCHED;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_PLUGIN_SCHED(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)action;
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            LOG(DEBUG, "Scheduling plugins.");
            wano_ppline_schedule(self);
            break;

        default:
            break;
    }

    return wano_ppline_IF_IPV4_RESET;
}

enum wano_ppline_state wano_ppline_state_IF_IPV4_RESET(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)action;
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    struct wano_inet_state *is = data;
    struct wano_ppline_plugin *wpp;
    bool is_ipv4 = false;

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
            {
                if (wano_plugin_state_get(&wpp->wpp_state) == wano_plugin_SCHEDULED)
                {
                    is_ipv4 |= wpp->wpp_plugin->wanp_mask & WANO_PLUGIN_MASK_IPV4;
                }
            }

            if (!is_ipv4) return wano_ppline_IF_IPV6_RESET;

            LOG(INFO, "wano: %s: An IPv4 plugin is scheduled to run, resetting IPv4 settings.",
                    self->wpl_ifname);

            WANO_INET_CONFIG_UPDATE(self->wpl_ifname, .ip_assign_scheme = "none");
            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            break;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (strcmp(is->is_ip_assign_scheme, "none") != 0) break;
            if (osn_ip_addr_cmp(&is->is_ipaddr, &OSN_IP_ADDR_INIT) != 0
                && strcmp(self->wpl_iftype, "unmanaged") != 0) break;
            return wano_ppline_IF_IPV6_RESET;

        case wano_ppline_do_PLUGIN_UPDATE:
            return wano_ppline_PLUGIN_SCHED;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_IF_IPV6_RESET(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)state;
    (void)data;

    struct wano_ppline_plugin *wpp;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    bool is_ipv6 = false;

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
            {
                if (wano_plugin_state_get(&wpp->wpp_state) == wano_plugin_SCHEDULED)
                {
                    is_ipv6 |= wpp->wpp_plugin->wanp_mask & WANO_PLUGIN_MASK_IPV6;
                }
            }

            if (!is_ipv6) return wano_ppline_PLUGIN_RUN;

            LOG(INFO, "wano: %s: An IPv6 plugin is scheduled to run, resetting IPv6 settings.",
                    self->wpl_ifname);
            wano_ppline_reset_ipv6(self);
            return wano_ppline_PLUGIN_RUN;

        case wano_ppline_do_PLUGIN_UPDATE:
            return wano_ppline_PLUGIN_SCHED;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_PLUGIN_RUN(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)state;
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            if (wano_ppline_run(self))
            {
                break;
            }
            LOG(NOTICE, "wano: %s: Pipeline exhausted plug-ins.", self->wpl_ifname);
            return wano_ppline_IDLE;

        case wano_ppline_do_PLUGIN_UPDATE:
            return wano_ppline_PLUGIN_SCHED;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_IDLE(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)state;
    (void)data;

    int retries;
    double maxtime;
    double rtime;
    struct wano_ppline_plugin *wpp;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    bool active = false;

    /* Check if there are any active plug-ins */
    ds_dlist_foreach(&self->wpl_plugin_waitq, wpp)
    {
        if (wano_plugin_state_get(&wpp->wpp_state) != wano_plugin_ACTIVE) continue;
        active = true;
        break;
    }

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            if (!active)
            {
                /*
                 * This event signifies that there's no more work to do on the
                 * pipeline. Historically this is also what the "IDLE" state menat.
                 *
                 * With recent changes, the IDLE state is just a sleeping state
                 * so we need one extra cehck (!active) to figure out if we're
                 * really idle.
                 */
                wano_ppline_event_dispatch(self, WANO_PPLINE_IDLE);
            }

            wano_ppline_plugin_reset(self, false);

            /*
             * Update connection manager table, has_L3 must be false and loop
             * must be true. This is required by the Cloud/CM state machines.
             *
             * Since WANO can handle multiple WAN configurations, we should
             * wait until a WAN rollover occurs before we flag the interface
             * as "exhasuted" (has_L3 = false).
             */
            if (active)
            {
                LOG(INFO, "wano: %s: Active plug-ins present, not resetting .has_L3.", self->wpl_ifname);
            }
            else if (self->wpl_wan == NULL || wano_wan_rollover_get(self->wpl_wan) > 0)
            {
                if (!WANO_CONNMGR_UPLINK_UPDATE(
                        self->wpl_ifname,
                        .has_L3 = WANO_TRI_FALSE))
                {
                    LOG(WARN, "wano: %s: Error updating the Connection_Manager_Uplink table (has_L3 = false)",
                            self->wpl_ifname);
                }
            }
            else
            {
                LOG(INFO, "wano: %s: Not updating .has_L3 as WAN configuration not exhausted yet.",
                        self->wpl_ifname);
            }

            if (clock_mono_double() < self->wpl_immediate_timeout)
            {
                LOG(INFO, "wano: %s: Peforming immediate restarts for the next %0.2f seconds.",
                        self->wpl_ifname, self->wpl_immediate_timeout - clock_mono_double());
                /*
                 * This is a requirement for CM: After the plugin pipeline is
                 * exhausted, set has_L3 to false and restart provisioning. The
                 * START state only restarts the pipeline without touching the
                 * Connection_Manager_Uplink state
                 */
                return (active ? wano_ppline_PLUGIN_SCHED : wano_ppline_START);
            }

            /* Calculate retry timer */
            retries = (self->wpl_retries < WANO_PPLINE_RETRY_MAX) ? self->wpl_retries : WANO_PPLINE_RETRY_MAX;
            maxtime = WANO_PPLINE_RETRY_TIME << retries;
            /* Add the random component */
            rtime = maxtime * 0.5 * (os_random() % 1000) / 1000.0;
            /* Add the fixed component */
            rtime += maxtime * 0.5;

            /* Re-arm retry timer */
            ev_timer_stop(EV_DEFAULT, &self->wpl_retry_timer);
            ev_timer_set(&self->wpl_retry_timer, rtime, 0.0);
            ev_timer_start(EV_DEFAULT, &self->wpl_retry_timer);
            self->wpl_retries++;

            LOG(INFO, "wano: %s: Entered IDLE state, pipeline retry timer is %0.2f seconds.",
                    self->wpl_ifname, rtime);
            break;

        case wano_ppline_do_IDLE_TIMEOUT:
            LOG(INFO, "wano: %s: Idle timeout reached, restarting pipeline.", self->wpl_ifname);
            ev_timer_stop(EV_DEFAULT, &self->wpl_retry_timer);
            return (active ? wano_ppline_PLUGIN_SCHED : wano_ppline_START);

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_ABORT(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)state;
    (void)data;

    double maxtime;
    double rtime;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            wano_ppline_event_dispatch(self, WANO_PPLINE_ABORT);
            wano_ppline_plugin_reset(self, true);

            if (!WANO_CONNMGR_UPLINK_UPDATE(
                    self->wpl_ifname,
                    .has_L3 = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error updating the Connection_Manager_Uplink table (has_L3 = false)",
                        self->wpl_ifname);
            }

            /*
             * Disable NAT
             */
            if (!WANO_INET_CONFIG_UPDATE(
                        self->wpl_ifname,
                        .ip_assign_scheme = "none",
                        .nat = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error disabling NAT.", self->wpl_ifname);
            }

            /* Calculate retry timer */
            maxtime = WANO_PPLINE_RETRY_TIME << WANO_PPLINE_RETRY_MAX;
            /* Add the random component */
            rtime = maxtime * 0.5 * (os_random() % 1000) / 1000.0;
            /* Add the fixed component */
            rtime += maxtime * 0.5;

            /* Re-arm retry timer */
            ev_timer_stop(EV_DEFAULT, &self->wpl_retry_timer);
            ev_timer_set(&self->wpl_retry_timer, rtime, 0.0);
            ev_timer_start(EV_DEFAULT, &self->wpl_retry_timer);
            self->wpl_retries++;

            LOG(INFO, "wano: %s: Entered ABORT state, pipeline retry timer is %0.2f seconds.",
                    self->wpl_ifname, rtime);
            break;

        case wano_ppline_do_IDLE_TIMEOUT:
            LOG(INFO, "wano: %s: Abort timeout reached, restarting pipeline.", self->wpl_ifname);
            ev_timer_stop(EV_DEFAULT, &self->wpl_retry_timer);
            return wano_ppline_START;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_FREEZE(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)state;
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            LOG(NOTICE, "wano: %s: Pipeline frozen.", self->wpl_ifname);

            wano_ppline_event_dispatch(self, WANO_PPLINE_FREEZE);
            wano_ppline_plugin_reset(self, true);

            /*
             * If there was no successful WAN configuration found on the
             * interface, reset any lingering IPv4 configuration by setting
             * the ip_assign_scheme to none.
             */
            if (!self->wpl_has_l3 && !WANO_INET_CONFIG_UPDATE(
                        self->wpl_ifname,
                        .nat = WANO_TRI_FALSE,
                        .ip_assign_scheme = "none"))
            {
                LOG(WARN, "wano: %s: Error writing Wifi_Inet_Config in FREEZE.", self->wpl_ifname);
            }

            /* Pause WAN processing */
            if (self->wpl_wan != NULL)
            {
                wano_wan_pause(self->wpl_wan, true);
            }
            break;

        case wano_ppline_do_PPLINE_UNFREEZE:
            LOG(NOTICE, "wano: %s: Unfreezing pipeline.", self->wpl_ifname);

            /* Resume WAN processing */
            if (self->wpl_wan != NULL)
            {
                wano_wan_pause(self->wpl_wan, false);
            }
            return wano_ppline_INIT;

        default:
            break;
    }

    return 0;
}

enum wano_ppline_state wano_ppline_state_EXCEPTION(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    /* Reset the immediate restart timer */
    self->wpl_immediate_timeout = clock_mono_double() + CONFIG_MANAGER_WANO_PLUGIN_IMMEDIATE_TIMEOUT;

    /* Stop any pending timers */
    ev_timer_stop(EV_DEFAULT, &self->wpl_retry_timer);

    switch (action)
    {
        case wano_ppline_exception_PPLINE_RESTART:
            return wano_ppline_INIT;

        case wano_ppline_exception_PPLINE_ABORT:
            return wano_ppline_ABORT;

        case wano_ppline_exception_PPLINE_FREEZE:
            return wano_ppline_FREEZE;

        default:
            break;
    }

    return 0;
}

/*
 * ===========================================================================
 *  wano_plugin State Machine
 * ===========================================================================
 */
enum wano_plugin_state wano_plugin_state_IDLE(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    (void)action;
    (void)data;

    struct wano_ppline_plugin *wpp = CONTAINER_OF(state, struct wano_ppline_plugin, wpp_state);

    switch (action)
    {
        case wano_plugin_do_STATE_INIT:
            if (wpp->wpp_handle != NULL) wano_plugin_fini(wpp->wpp_handle);
            wpp->wpp_handle = NULL;

            /* If the plug-in reported its own interface for WAN, clear the uplink table */
            if (wpp->wpp_status.ws_ifname[0] != '\0' &&
                    strcmp(wpp->wpp_ppline->wpl_ifname, wpp->wpp_status.ws_ifname) != 0)
            {
                if (!wano_connmgr_uplink_delete(wpp->wpp_status.ws_ifname))
                {
                    LOG(ERR, "wano: %s: Error clearing Connection_Manager_Uplink.",
                            wpp->wpp_status.ws_ifname);
                }
                wpp->wpp_status.ws_ifname[0] = '\0';
            }

            ev_async_stop(EV_DEFAULT, &wpp->wpp_status_async);
            ev_async_init(&wpp->wpp_status_async, wano_ppline_status_async_fn);
            break;

        case wano_plugin_do_START:
            return wano_plugin_PENDING;

        default:
            break;
    }

    return 0;
}

enum wano_plugin_state wano_plugin_state_PENDING(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    switch (action)
    {
        case wano_plugin_do_SCHEDULE:
            return wano_plugin_SCHEDULED;

        default:
            break;
    }

    return 0;
}

enum wano_plugin_state wano_plugin_state_SCHEDULED(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    struct wano_ppline_plugin *wpp = CONTAINER_OF(state, struct wano_ppline_plugin, wpp_state);
    wano_ppline_t *self = data;

    switch (action)
    {
        case wano_plugin_do_STATE_INIT:
            /*
             * Initialize the plug-in; plug-ins that return a NULL instance
             * are skipped.
             */
            wpp->wpp_handle = wano_plugin_init(
                    wpp->wpp_plugin,
                    self->wpl_ifname,
                    wano_ppline_plugin_status_fn);
            if (wpp->wpp_handle == NULL)
            {
                LOG(INFO, "wano: %s: Skipping plug-in %s",
                        self->wpl_ifname,
                        wpp->wpp_plugin->wanp_name);
                return wano_plugin_IDLE;
            }
            wpp->wpp_handle->wh_data = wpp;
            break;

        case wano_plugin_do_RUN:
            return wano_plugin_RUNNING;

        default:
            break;
    }

    return 0;
}

enum wano_plugin_state wano_plugin_state_RUNNING(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    (void)action;
    (void)data;

    enum wano_plugin_state retval = 0;

    struct wano_ppline_plugin *wpp = CONTAINER_OF(state, struct wano_ppline_plugin, wpp_state);

    switch (action)
    {
        case wano_plugin_do_STATE_INIT:
            ev_timer_init(&wpp->wpp_timeout, wano_ppline_plugin_timeout_fn, CONFIG_MANAGER_WANO_PLUGIN_TIMEOUT, 0.0);
            ev_timer_start(EV_DEFAULT, &wpp->wpp_timeout);
            ev_async_start(EV_DEFAULT, &wpp->wpp_status_async);
            wano_plugin_run(wpp->wpp_handle);
            return 0;

        case wano_plugin_do_SUCCESS:
            retval = wano_plugin_ACTIVE;
            break;

        case wano_plugin_do_DETACH:
            retval = wano_plugin_DETACHED;
            break;

        case wano_plugin_do_SKIP:
        case wano_plugin_do_TIMEOUT:
        case wano_plugin_do_ERROR:
            retval = wano_plugin_IDLE;
            break;

        default:
            break;
    }

    ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);

    return retval;
}

enum wano_plugin_state wano_plugin_state_ACTIVE(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    return 0;
}

enum wano_plugin_state wano_plugin_state_DETACHED(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    switch (action)
    {
        case wano_plugin_do_SKIP:
            return wano_plugin_IDLE;

        default:
            break;
    }

    return 0;
}

enum wano_plugin_state wano_plugin_state_EXCEPTION(
        wano_plugin_state_t *state,
        enum wano_plugin_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    /* Any exception forces a transition to the IDLE state */
    return wano_plugin_IDLE;
}
