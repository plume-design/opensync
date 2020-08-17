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
#include <inttypes.h>
#include <unistd.h>

#include <ev.h>

#include "osa_assert.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "wano.h"
#include "wano_internal.h"

/*
 * Structure describing a single plug-in running on the pipeline
 */
struct wano_ppline_plugin
{
    /** True if the plug-in has been started (wano_plugin_run() was called) */
    bool                        wpp_running;
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

static void wano_ppline_start_queues(wano_ppline_t *self);
static void wano_ppline_stop_queues(wano_ppline_t *self);
static void wano_ppline_schedule(wano_ppline_t *self);
static bool wano_ppline_runq_add(wano_ppline_t *self, struct wano_ppline_plugin *wpp);
static bool wano_ppline_runq_start(wano_ppline_t *self);
static void __wano_ppline_runq_stop(wano_ppline_t *self, struct wano_ppline_plugin *wpp);
static void wano_ppline_runq_del(wano_ppline_t *self, struct wano_ppline_plugin *wpp);
static void wano_ppline_runq_flush(wano_ppline_t *self);
static wano_inet_state_event_fn_t wano_ppline_inet_state_event_fn;
static wano_plugin_status_fn_t wano_ppline_plugin_status_fn;
static void wano_ppline_status_async_fn(struct ev_loop *, ev_async *ev, int revent);
static void wano_ppline_plugin_timeout_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void wano_ppline_reset_ipv6(wano_ppline_t *self);

bool wano_ppline_init(
        wano_ppline_t *self,
        const char *ifname,
        const char *iftype,
        uint64_t emask,
        wano_ppline_status_fn_t *status_fn)
{
    memset(self, 0, sizeof(*self));

    STRSCPY_WARN(self->wpl_ifname, ifname);
    STRSCPY_WARN(self->wpl_iftype, iftype);
    self->wpl_plugin_emask = emask;
    self->wpl_status_fn = status_fn;

    ds_dlist_init(&self->wpl_plugin_waitq, struct wano_ppline_plugin, wpp_dnode);
    ds_dlist_init(&self->wpl_plugin_runq, struct wano_ppline_plugin, wpp_dnode);

    /* Subscribe to Wifi_Inet_State events */
    if (!wano_inet_state_event_init(
            &self->wpl_inet_state_event,
            self->wpl_ifname,
            wano_ppline_inet_state_event_fn))
    {
        LOG(ERR, "wano: %s: Error initializing inet_state_event object", self->wpl_ifname);
        return false;
    }

    self->wpl_init = true;

    LOG(NOTICE, "wano: %s: Created WAN pipeline on interface.", ifname);

    return true;
}

/*
 * Dispose of a wano_ppline_t object
 */
void wano_ppline_fini(wano_ppline_t *self)
{
    if (!self->wpl_init)
    {
        return;
    }

    self->wpl_init = false;

    wano_inet_state_event_fini(&self->wpl_inet_state_event);

    wano_ppline_stop_queues(self);
}

/*
 * Initialize the plug-in run and wait queue
 */
void wano_ppline_start_queues(wano_ppline_t *self)
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
        if (wp->wanp_mask & self->wpl_plugin_emask) continue;

        wpp = calloc(1, sizeof(struct wano_ppline_plugin));
        ASSERT(wpp != NULL, "failed to allocate wano_ppline_plugin");

        wpp->wpp_ppline = self;
        wpp->wpp_plugin = wp;

        ds_dlist_insert_tail(&self->wpl_plugin_waitq, wpp);
    }

    if (ds_dlist_is_empty(&self->wpl_plugin_waitq))
    {
        LOG(WARN, "wano: %s: Plug-in pipeline is empty (mask = %"PRIx64").",
                self->wpl_ifname, self->wpl_plugin_emask);
    }
}

void wano_ppline_stop_queues(wano_ppline_t *self)
{
    ds_dlist_iter_t iter;
    struct wano_ppline_plugin *wpp;

    /*
     * Free the waiting queue
     */
    ds_dlist_foreach_iter(&self->wpl_plugin_waitq, wpp, iter)
    {
        ds_dlist_iremove(&iter);
        free(wpp);
    }

    /*
     * Free the running queue and terminate all currently running plug-ins
     */
    wano_ppline_runq_flush(self);

    self->wpl_plugin_rmask = 0;
}

/*
 * Schedule next WAN plug-in on the interface.
 *
 * @return
 * Return false if there's no more work present on the pipeline (all plug-ins
 * were executed and the wait queue is empty).
 */
void wano_ppline_schedule(wano_ppline_t *self)
{
    struct wano_ppline_plugin *wpp;
    ds_dlist_iter_t iter;

    /*
     * Scan the current wait queue
     */
    ds_dlist_foreach_iter(&self->wpl_plugin_waitq, wpp, iter)
    {
        /* Check if a plug-in can run according to the current run mask */
        if (self->wpl_plugin_rmask & wpp->wpp_plugin->wanp_mask)
        {
            LOG(DEBUG, "wano: %s: Blocked plug-in %s",
                    self->wpl_ifname,
                    wpp->wpp_plugin->wanp_name);
            continue;
        }

        ds_dlist_iremove(&iter);

        if (!wano_ppline_runq_add(self, wpp))
        {
            /* Plug-in was unable to start, continue */
            free(wpp);
            continue;
        }

        self->wpl_plugin_imask |= wpp->wpp_plugin->wanp_mask;
    }
}

/*
 * Add plug-in to the run-queue
 */
bool wano_ppline_runq_add(wano_ppline_t *self, struct wano_ppline_plugin *wpp)
{
    wpp->wpp_running = false;

    wpp->wpp_handle = wano_plugin_init(
            wpp->wpp_plugin,
            self->wpl_ifname,
            wano_ppline_plugin_status_fn);

    if (wpp->wpp_handle == NULL)
    {
        LOG(INFO, "wano: %s: Skipping plug-in %s",
                self->wpl_ifname,
                wpp->wpp_plugin->wanp_name);
        return false;
    }

    wpp->wpp_handle->wh_data = wpp;

    self->wpl_plugin_rmask |= wpp->wpp_plugin->wanp_mask;
    ds_dlist_insert_head(&self->wpl_plugin_runq, wpp);

    return true;
}

/*
 * Scan the run-queue and start all plug-ins that are not yet started
 */
bool wano_ppline_runq_start(wano_ppline_t *self)
{
    struct wano_ppline_plugin *wpp;

    if (ds_dlist_is_empty(&self->wpl_plugin_runq))
    {
        return false;
    }

    ds_dlist_foreach(&self->wpl_plugin_runq, wpp)
    {
        if (wpp->wpp_running)
        {
            continue;
        }

        LOG(INFO, "wano: %s: Starting plug-in %s",
                    self->wpl_ifname,
                    wpp->wpp_plugin->wanp_name);

        ev_timer_init(&wpp->wpp_timeout, wano_ppline_plugin_timeout_fn, CONFIG_MANAGER_WANO_PLUGIN_TIMEOUT, 0.0);
        ev_timer_start(EV_DEFAULT, &wpp->wpp_timeout);

        ev_async_init(&wpp->wpp_status_async, wano_ppline_status_async_fn);
        ev_async_start(EV_DEFAULT, &wpp->wpp_status_async);

        wano_plugin_run(wpp->wpp_handle);

        wpp->wpp_running = true;
    }

    return true;
}

/*
 * Remove a plug-in on the run queue; if the plug-in is running, terminate it
 */
void __wano_ppline_runq_stop(wano_ppline_t *self, struct wano_ppline_plugin *wpp)
{
    self->wpl_plugin_rmask &= ~wpp->wpp_plugin->wanp_mask;

    /* If the plug-in reported its own interface for WAN, clear the uplink table */
    if (wpp->wpp_status.ws_ifname[0] != '\0')
    {
        if (!wano_connection_manager_uplink_delete(wpp->wpp_status.ws_ifname))
        {
            LOG(ERR, "wano: %s: Error clearing Connection_Manager_Uplink.",
                    wpp->wpp_status.ws_ifname);
        }
    }

    if (wpp->wpp_handle != NULL)
    {
        wano_plugin_fini(wpp->wpp_handle);
        wpp->wpp_handle = NULL;
    }

    /* Stop the async status handler */
    ev_async_stop(EV_DEFAULT, &wpp->wpp_status_async);

    /* Stop the timeout handler */
    ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);
}

void wano_ppline_runq_del(wano_ppline_t *self, struct wano_ppline_plugin *wpp)
{
    /* Remove the plug-in from the runqueue and clear the run mask */
    __wano_ppline_runq_stop(self, wpp);
    ds_dlist_remove(&self->wpl_plugin_runq, wpp);
}

static void wano_ppline_runq_flush(wano_ppline_t *self)
{
    ds_dlist_iter_t iter;
    struct wano_ppline_plugin *wpp;

    ds_dlist_foreach_iter(&self->wpl_plugin_runq, wpp, iter)
    {
        ds_dlist_iremove(&iter);
        __wano_ppline_runq_stop(self, wpp);
        free(wpp);
    }
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

    /* Update local copy of the status and schedule an async event */
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

    struct wano_ppline_plugin *wpp = CONTAINER_OF(ev, struct wano_ppline_plugin, wpp_status_async);
    wano_ppline_t *self = wpp->wpp_ppline;
    char *ifname = self->wpl_ifname;
    char *iftype = self->wpl_iftype;

    switch (wpp->wpp_status.ws_type)
    {
        case WANP_OK:
            LOG(INFO, "wano: %s: WAN Plug-in success: %s",
                    self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            /* Stop the timeout timer */
            ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);

            /* Use the interface name and type reported in the plug-in status */
            if (wpp->wpp_status.ws_ifname[0] != '\0')
            {
                ifname = wpp->wpp_status.ws_ifname;
            }

            if (wpp->wpp_status.ws_iftype[0] != '\0')
            {
                iftype =  wpp->wpp_status.ws_iftype;
            }

            /* Update the connection manager table */
            if (!WANO_CONNECTION_MANAGER_UPLINK_UPDATE(
                        ifname,
                        .if_type = iftype,
                        .has_L2 = WANO_TRI_TRUE,
                        .has_L3 = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplink table (has_L3 = true).",
                        self->wpl_ifname);
            }

            /* Notify upper layers */
            if (self->wpl_status_fn != NULL)
            {
                self->wpl_status_fn(self, WANO_PPLINE_OK);
            }
            return;

        case WANP_SKIP:
            LOG(INFO, "wano: %s: Plug-in requested skip: %s", self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));
            wano_ppline_runq_del(self, wpp);
            free(wpp);
            wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
            break;

        case WANP_BUSY:
            LOG(INFO, "wano: %s: Plug-in %s is busy, stopping timeout timer.", self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            /* Stop the timeout timer */
            ev_timer_stop(EV_DEFAULT, &wpp->wpp_timeout);
            break;

        case WANP_ERROR:
            LOG(ERR, "wano: %s: Error detected running plug-in: %s. Skipping.",
                    self->wpl_ifname, wano_plugin_name(wpp->wpp_handle));

            wano_ppline_runq_del(self, wpp);
            free(wpp);
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

    wano_ppline_runq_del(self, wpp);
    free(wpp);

    wano_ppline_state_do(&self->wpl_state, wano_ppline_do_PLUGIN_UPDATE, NULL);
}

void wano_ppline_reset_ipv6(wano_ppline_t *self)
{
    ovsdb_table_t table_IP_Interface;
    ovsdb_table_t table_DHCPv6_Client;
    ovsdb_table_t table_DHCPv6_Server;
    ovsdb_table_t table_IPv6_RouteAdv;

    struct schema_IP_Interface ip_interface;

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
    ovsdb_table_delete_where(&table_IP_Interface, ovsdb_where_uuid("_uuid", ip_interface._uuid.uuid));
}

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */

/*
 * Move to the IF_L2_RESET state if WANO_PLUGIN_MASK_L2 is not set
 */
enum wano_ppline_state wano_ppline_state_INIT(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    (void)action;
    (void)data;

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    /* Remove any entries in the Connection_Manager_Uplink table */
    if (wano_connection_manager_uplink_delete(self->wpl_ifname))
    {
        LOG(INFO, "wano: %s: Stale Connection_Manager_Uplink entry was removed.", self->wpl_ifname);
    }

    if (!WANO_CONNECTION_MANAGER_UPLINK_UPDATE(
                self->wpl_ifname,
                .if_type = self->wpl_iftype,
                .has_L2 = WANO_TRI_FALSE))
    {
        LOG(WARN, "wano: %s: Error updating Connection_Manager_Uplink (iftype = %s, has_L2 = false)",
                self->wpl_ifname, self->wpl_iftype);
    }

    wano_ppline_start_queues(self);

    /*
     * Do not reset the interface L2 layer
     */
    if (self->wpl_plugin_emask & WANO_PLUGIN_MASK_L2)
    {
        return wano_ppline_IF_ENABLE;
    }

    return wano_ppline_IF_L2_RESET;
}

/*
 * The IF_L2_RESET state simply sets the Wifi_Inet_Config:enabled,network fields
 * to false and waits until Wifi_Inet_State reflects the change.
 */
enum wano_ppline_state wano_ppline_state_IF_L2_RESET(
        wano_ppline_state_t *state,
        enum wano_ppline_action action,
        void *data)
{
    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);
    struct wano_inet_state *is = data;

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            LOG(INFO, "wano: %s: Resetting interface L2.", self->wpl_ifname);
            if (!WANO_INET_CONFIG_UPDATE(
                        self->wpl_ifname,
                        .if_type = self->wpl_iftype,
                        .enabled = WANO_TRI_FALSE,
                        .network = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error writing Wifi_Inet_Config in IF_L2_RESET.", self->wpl_ifname);
            }

            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            return 0;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (is->is_enabled) break;
            if (is->is_network) break;

            return wano_ppline_IF_ENABLE;

        default:
            break;
    }

    return 0;
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
                        .nat = WANO_TRI_TRUE))
            {
                LOG(WARN, "wano: %s: Error writing Wifi_Inet_Config in IF_ENABLE.", self->wpl_ifname);
            }
            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            return 0;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (!is->is_enabled) break;
            if (!is->is_network) break;
            if (!is->is_nat) break;
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

            self->wpl_carrier_exception = true;

            if (!WANO_CONNECTION_MANAGER_UPLINK_UPDATE(self->wpl_ifname, .has_L2 = WANO_TRI_TRUE))
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

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            if (!(self->wpl_plugin_imask & WANO_PLUGIN_MASK_IPV4))
            {
                return wano_ppline_IF_IPV6_RESET;
            }

            LOG(INFO, "wano: %s: An IPv4 plugin is scheduled to run, resetting IPv4 settings.",
                    self->wpl_ifname);

            self->wpl_plugin_imask &= ~WANO_PLUGIN_MASK_IPV4;

            WANO_INET_CONFIG_UPDATE(self->wpl_ifname, .ip_assign_scheme = "none");
            wano_inet_state_event_refresh(&self->wpl_inet_state_event);
            break;

        case wano_ppline_do_INET_STATE_UPDATE:
            if (strcmp(is->is_ip_assign_scheme, "none") != 0) break;
            return wano_ppline_IF_IPV6_RESET;

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

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            if (!(self->wpl_plugin_imask & WANO_PLUGIN_MASK_IPV6))
            {
                return wano_ppline_PLUGIN_RUN;
            }

            LOG(INFO, "wano: %s: An IPv6 plugin is scheduled to run, resetting IPv6 settings.",
                    self->wpl_ifname);

            wano_ppline_reset_ipv6(self);

            self->wpl_plugin_imask &= ~WANO_PLUGIN_MASK_IPV6;
            return wano_ppline_PLUGIN_RUN;

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
            if (wano_ppline_runq_start(self))
            {
                break;
            }

            LOG(NOTICE, "wano: %s: Pipeline has no active plug-ins.", self->wpl_ifname);
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

    wano_ppline_t *self = CONTAINER_OF(state, wano_ppline_t, wpl_state);

    switch (action)
    {
        case wano_ppline_do_STATE_INIT:
            /*
             * Update connection manager table
             */
            if (!WANO_CONNECTION_MANAGER_UPLINK_UPDATE(self->wpl_ifname, .has_L3 = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error updating the Connection_Manager_Uplink table (has_L3 = false)",
                        self->wpl_ifname);
            }

            if (self->wpl_status_fn != NULL)
            {
                self->wpl_status_fn(self, WANO_PPLINE_IDLE);
            }

            /*
             * Disable NAT
             */
            if (!WANO_INET_CONFIG_UPDATE(
                        self->wpl_ifname,
                        .nat = WANO_TRI_FALSE))
            {
                LOG(WARN, "wano: %s: Error disabling NAT.", self->wpl_ifname);
            }

            break;

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

    /*
     * Tear-down the wait and run queues and any plug-ins on them
     */
    wano_ppline_stop_queues(self);

    switch (action)
    {
        case wano_ppline_exception_PPLINE_RESTART:
            /* Return to the init state */
            if (self->wpl_status_fn != NULL)
            {
                self->wpl_status_fn(self, WANO_PPLINE_RESTART);
            }

            return wano_ppline_INIT;

        case wano_ppline_exception_PPLINE_ABORT:
            return wano_ppline_IDLE;

        default:
            break;
    }

    return 0;
}
