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

#include "log.h"
#include "module.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "wano.h"

#include "wano_wan.h"
#include "wanp_cmts_stam.h"

typedef struct
{
    wano_plugin_handle_t      handle;
    wanp_cmts_state_t         state;
    wano_plugin_status_fn_t  *status_fn;
    wano_inet_state_event_t   inet_state_watcher;
    osn_ip_addr_t             ipaddr;
    bool                      is_ipv4;
    bool                      is_ipv6;

    /** True if this plug-in detected a working WAN configuration */
    bool plugin_has_wan;
    ds_tree_node_t            tnode;
} wanp_cmts_handle_t;


static void wanp_cmts_module_start(void *data);
static void wanp_cmts_module_stop(void *data);
static wano_plugin_ops_init_fn_t wanp_cmts_init;
static wano_plugin_ops_run_fn_t wanp_cmts_run;
static wano_plugin_ops_fini_fn_t wanp_cmts_fini;
static wano_inet_state_event_fn_t wanp_cmts_inet_state_event_fn;
static void wanp_cmts_ip_interface_reset(wanp_cmts_handle_t *self);
static void wanp_cmts_ipv6_enable(wanp_cmts_handle_t *self);

void callback_IP_Interface(
        ovsdb_update_monitor_t *self,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new);

static ovsdb_table_t table_IP_Interface;

/* List of handles subscribed to OVSDB events */
static ds_tree_t wanp_cmts_ovsdb_list = DS_TREE_INIT(ds_str_cmp, wanp_cmts_handle_t, tnode);

static struct wano_plugin wanp_cmts = WANO_PLUGIN_INIT(
        "cmts",
        100,
        WANO_PLUGIN_MASK_ALL,
        wanp_cmts_init,
        wanp_cmts_run,
        wanp_cmts_fini);


void wanp_cmts_ip_interface_reset(wanp_cmts_handle_t *self)
{
    ovsdb_table_t table_IPv6_Address;
    struct schema_IP_Interface ip_interface;
    int i;

    OVSDB_TABLE_INIT(IPv6_Address, _uuid);

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", self->handle.wh_ifname, &ip_interface))
    {
        /* No entry in IP_Interface -- we're good */
        LOG(DEBUG, "wanp_cmts: %s: IP_Interface no entry for if_name.", self->handle.wh_ifname);
        return;
    }

    if (ovsdb_table_delete_where(&table_IP_Interface, ovsdb_where_uuid("_uuid", ip_interface._uuid.uuid)) < 0)
    {
        LOG(WARN, "wanp_cmts: %s: Error deleting IP_Interface row.",
                self->handle.wh_ifname);
    }

    for (i = 0; i < ip_interface.ipv6_addr_len; i++)
    {
        if (ovsdb_table_delete_where(&table_IPv6_Address, ovsdb_where_uuid("_uuid", ip_interface.ipv6_addr[i].uuid)) < 0)
        {
            LOG(WARN, "wanp_cmts: %s: Error deleting IPv6_Address row.",
                    self->handle.wh_ifname);
        }
    }
}

void wanp_cmts_ipv6_enable(wanp_cmts_handle_t *self)
{
    struct schema_IP_Interface ip_interface;

    memset(&ip_interface, 0, sizeof(ip_interface));
    ip_interface._partial_update = true;
    SCHEMA_SET_STR(ip_interface.name, self->handle.wh_ifname);
    SCHEMA_SET_STR(ip_interface.if_name, self->handle.wh_ifname);
    SCHEMA_SET_STR(ip_interface.status, "up");
    SCHEMA_SET_INT(ip_interface.enable, true);

    if (!ovsdb_table_upsert_simple(
            &table_IP_Interface,
            "name",
            self->handle.wh_ifname,
            &ip_interface,
            true))
    {
        LOG(ERR, "wanp_cmts: %s: Error upserting IP_Interface.",
                self->handle.wh_ifname);
    }
}

/*
 * ===========================================================================
 *  OVSDB Interface
 * ===========================================================================
 */

void callback_IP_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new)
{
    wanp_cmts_handle_t *self;
    const char *ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    self = ds_tree_find(&wanp_cmts_ovsdb_list, (void *)ifname);
    if (self == NULL)
    {
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL && strcmp(ifname, self->handle.wh_ifname) == 0)
    {
        self->is_ipv6 = false;
    }
    else if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        return;
    }

    if (new->ipv6_addr_len == 0)
    {
        self->is_ipv6 = false;
    }
    else
    {
        self->is_ipv6 = true;
    }

    wanp_cmts_state_do(&self->state, wanp_cmts_do_IP_INTERFACE_UPDATE, NULL);
}

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */

enum wanp_cmts_state wanp_cmts_state_INIT(
        wanp_cmts_state_t *state,
        enum wanp_cmts_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    return wanp_cmts_ENABLE_IPV6;
}

enum wanp_cmts_state wanp_cmts_state_ENABLE_IPV6(
        wanp_cmts_state_t *state,
        enum wanp_cmts_action action,
        void *data)
{
    wanp_cmts_handle_t *h = CONTAINER_OF(state, wanp_cmts_handle_t, state);

    if (action != wanp_cmts_do_STATE_INIT)
    {
        return 0;
    }

    /* Subscribe to OVSDB events */
    ds_tree_insert(&wanp_cmts_ovsdb_list, h, h->handle.wh_ifname);
    wanp_cmts_ip_interface_reset(h);
    wanp_cmts_ipv6_enable(h);

    return wanp_cmts_WAIT_IP;
}

enum wanp_cmts_state wanp_cmts_state_WAIT_IP(
        wanp_cmts_state_t *state,
        enum wanp_cmts_action action,
        void *data)
{
    wanp_cmts_handle_t *h = CONTAINER_OF(state, wanp_cmts_handle_t, state);
    struct wano_inet_state *is = data;

    switch (action)
    {
        case wanp_cmts_do_STATE_INIT:
            wano_inet_state_event_refresh(&h->inet_state_watcher);
            break;

        case wanp_cmts_do_INET_STATE_UPDATE:
            if (osn_ip_addr_cmp(&is->is_ipaddr, &OSN_IP_ADDR_INIT) != 0)
            {
                h->is_ipv4 = true;
                return wanp_cmts_RUNNING;
            }
            else
            {
                h->is_ipv4 = false;
            }
            break;

        case wanp_cmts_do_IP_INTERFACE_UPDATE:
            if (h->is_ipv6) return wanp_cmts_RUNNING;
            break;

        default:
            break;
    }

    return 0;
}

enum wanp_cmts_state wanp_cmts_state_RUNNING(
        wanp_cmts_state_t *state,
        enum wanp_cmts_action action,
        void *data)
{
    wanp_cmts_handle_t *h = CONTAINER_OF(state, wanp_cmts_handle_t, state);
    struct wano_inet_state *is = data;
    struct wano_plugin_status ws;


    switch (action)
    {
        case wanp_cmts_do_STATE_INIT:
            ws = WANO_PLUGIN_STATUS(WANP_OK);

            h->plugin_has_wan = true;
            h->status_fn(&h->handle, &ws);
            break;

        case wanp_cmts_do_INET_STATE_UPDATE:
            if (osn_ip_addr_cmp(&is->is_ipaddr, &OSN_IP_ADDR_INIT) == 0)
            {
                h->is_ipv4 = false;
            }

            if (!h->is_ipv6 && !h->is_ipv4)
            {
                ws = WANO_PLUGIN_STATUS(WANP_RESTART);
                h->plugin_has_wan = true;
                h->status_fn(&h->handle, &ws);
                return wanp_cmts_WAIT_IP;
            }
            break;

        case wanp_cmts_do_IP_INTERFACE_UPDATE:
            if (!h->is_ipv6 && !h->is_ipv4)
            {
                ws = WANO_PLUGIN_STATUS(WANP_RESTART);
                h->plugin_has_wan = true;
                h->status_fn(&h->handle, &ws);
                return wanp_cmts_WAIT_IP;
            }

            break;

        default:
            break;
    }

    return 0;
}

/*
 * ===========================================================================
 *  Plugin implementation
 * ===========================================================================
 */
wano_plugin_handle_t *wanp_cmts_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    wanp_cmts_handle_t *h;

    h = CALLOC(1, sizeof(wanp_cmts_handle_t));

    h->handle.wh_plugin = wp;
    STRSCPY(h->handle.wh_ifname, ifname);
    h->status_fn = status_fn;

    return &h->handle;
}

void wanp_cmts_run(wano_plugin_handle_t *wh)
{
    wanp_cmts_handle_t *wsh = CONTAINER_OF(wh, wanp_cmts_handle_t, handle);

    /* Register to Wifi_Inet_State events, this will also kick-off the state machine */
    wano_inet_state_event_init(
            &wsh->inet_state_watcher,
            wsh->handle.wh_ifname,
            wanp_cmts_inet_state_event_fn);
}

void wanp_cmts_fini(wano_plugin_handle_t *wh)
{
    wanp_cmts_handle_t *wsh = CONTAINER_OF(wh, wanp_cmts_handle_t, handle);

    ds_tree_remove(&wanp_cmts_ovsdb_list, wsh);

    wanp_cmts_ip_interface_reset(wsh);
    wano_inet_state_event_fini(&wsh->inet_state_watcher);
    FREE(wsh);
}

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */
void wanp_cmts_inet_state_event_fn(
        wano_inet_state_event_t *ise,
        struct wano_inet_state *is)
{
    wanp_cmts_handle_t *h = CONTAINER_OF(ise, wanp_cmts_handle_t, inet_state_watcher);

    h->ipaddr = is->is_ipaddr;

    if (wanp_cmts_state_do(&h->state, wanp_cmts_do_INET_STATE_UPDATE, is) < 0)
    {
        LOG(ERR, "wanp_cmts: Error sending action INET_STATE_UPDATE to state machine.");
    }
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_cmts_module_start(void *data)
{
    (void)data;

    OVSDB_TABLE_INIT(IP_Interface, _uuid);

    if (!OVSDB_TABLE_MONITOR(IP_Interface, true))
    {
        LOG(WARN, "wanp_cmts: Error monitoring IP_Interface");
    }

    wano_plugin_register(&wanp_cmts);
}

void wanp_cmts_module_stop(void *data)
{
    (void)data;
    wano_plugin_unregister(&wanp_cmts);
}

MODULE(wanp_cmts, wanp_cmts_module_start, wanp_cmts_module_stop)
