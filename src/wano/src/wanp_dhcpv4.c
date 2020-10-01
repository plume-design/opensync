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
#include "schema.h"
#include "wano.h"
#include "osa_assert.h"

#include "wanp_dhcpv4_stam.h"

struct wanp_dhcpv4_handle
{
    wano_plugin_handle_t        wd4_handle;
    wanp_dhcpv4_state_t         wd4_state;
    wano_plugin_status_fn_t    *wd4_status_fn;
    wano_inet_state_event_t     wd4_inet_state_watcher;
    osn_ip_addr_t               wd4_ipaddr;
};

static void wanp_dhcpv4_module_start(void);
static void wanp_dhcpv4_module_stop(void);
static wano_plugin_ops_init_fn_t wanp_dhcpv4_init;
static wano_plugin_ops_run_fn_t wanp_dhcpv4_run;
static wano_plugin_ops_fini_fn_t wanp_dhcpv4_fini;
static wano_inet_state_event_fn_t wanp_dhcpv4_inet_state_event_fn;

static struct wano_plugin wanp_dhcpv4 = WANO_PLUGIN_INIT(
        "dhcpv4",
        100,
        WANO_PLUGIN_MASK_IPV4,
        wanp_dhcpv4_init,
        wanp_dhcpv4_run,
        wanp_dhcpv4_fini);

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */
enum wanp_dhcpv4_state wanp_dhcpv4_state_INIT(
        wanp_dhcpv4_state_t *state,
        enum wanp_dhcpv4_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    return wanp_dhcpv4_ENABLE_DHCP;
}

enum wanp_dhcpv4_state wanp_dhcpv4_state_ENABLE_DHCP(
        wanp_dhcpv4_state_t *state,
        enum wanp_dhcpv4_action action,
        void *data)
{
    struct wanp_dhcpv4_handle *wd4;
    struct wano_inet_state *is;

    wd4 = CONTAINER_OF(state, struct wanp_dhcpv4_handle, wd4_state);
    is = data;

    switch (action)
    {
        case wanp_dhcpv4_do_STATE_INIT:
            LOG(INFO, "wanp_dhcpv4: %s: Enabling DHCP on interface.", wd4->wd4_handle.wh_ifname);
            WANO_INET_CONFIG_UPDATE(
                    wd4->wd4_handle.wh_ifname,
                    .enabled = WANO_TRI_TRUE,
                    .network = WANO_TRI_TRUE,
                    .ip_assign_scheme = "dhcp");
            break;

        case wanp_dhcpv4_do_INET_STATE_UPDATE:
            if (is->is_enabled != true) break;
            if (is->is_network != true) break;
            if (strcmp(is->is_ip_assign_scheme, "dhcp") != 0) break;
            if (is->is_port_state != true) break;

            return wanp_dhcpv4_WAIT_IP;

        default:
            return 0;
    }

    return 0;
}

enum wanp_dhcpv4_state wanp_dhcpv4_state_WAIT_IP(
        wanp_dhcpv4_state_t *state,
        enum wanp_dhcpv4_action action,
        void *data)
{
    struct wanp_dhcpv4_handle *self;

    (void)action;
    (void)data;

    self = CONTAINER_OF(state, struct wanp_dhcpv4_handle, wd4_state);

    if (memcmp(&self->wd4_ipaddr, &OSN_IP_ADDR_INIT, sizeof(OSN_IP_ADDR_INIT)) == 0)
    {
        return 0;
    }

    LOG(INFO, "wanp_dhcpv4: Acquired DHCPv4 address: "PRI_osn_ip_addr, FMT_osn_ip_addr(self->wd4_ipaddr));

    wano_inet_state_event_fini(&self->wd4_inet_state_watcher);

    return wanp_dhcpv4_RUNNING;
}

enum wanp_dhcpv4_state wanp_dhcpv4_state_RUNNING(
        wanp_dhcpv4_state_t *state,
        enum wanp_dhcpv4_action action,
        void *data)
{
    (void)action;
    (void)data;

    struct wanp_dhcpv4_handle *wd4 = CONTAINER_OF(state, struct wanp_dhcpv4_handle, wd4_state);

    if (action == wanp_dhcpv4_do_STATE_INIT)
    {
        struct wano_plugin_status ws = WANO_PLUGIN_STATUS(WANP_OK);
        wd4->wd4_status_fn(&wd4->wd4_handle, &ws);
    }

    LOG(INFO, "DHCPV4_RUNNING: %s", wanp_dhcpv4_action_str(action));

    return 0;
}

enum wanp_dhcpv4_state wanp_dhcpv4_state_EXCEPTION(
        wanp_dhcpv4_state_t *state,
        enum wanp_dhcpv4_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    LOG(INFO, "DHCPV4_EXCEPTION: %s", wanp_dhcpv4_action_str(action));

    return 0;
}

/*
 * ===========================================================================
 *  Plugin implementation
 * ===========================================================================
 */
wano_plugin_handle_t *wanp_dhcpv4_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    struct wanp_dhcpv4_handle *wd4;

    wd4 = calloc(1, sizeof(struct wanp_dhcpv4_handle));
    ASSERT(wd4 != NULL, "Error allocating DHCPv4 object")

    wd4->wd4_handle.wh_plugin = wp;
    STRSCPY(wd4->wd4_handle.wh_ifname, ifname);
    wd4->wd4_status_fn = status_fn;

    return &wd4->wd4_handle;
}

void wanp_dhcpv4_run(wano_plugin_handle_t *wh)
{
    struct wanp_dhcpv4_handle *wdh = CONTAINER_OF(wh, struct wanp_dhcpv4_handle, wd4_handle);

    (void)wdh;
    (void)wanp_dhcpv4_inet_state_event_fn;

    /* Register to Wifi_Inet_State events, this will also kick-off the state machine */
    wano_inet_state_event_init(
            &wdh->wd4_inet_state_watcher,
            wdh->wd4_handle.wh_ifname,
            wanp_dhcpv4_inet_state_event_fn);
}

void wanp_dhcpv4_fini(wano_plugin_handle_t *wh)
{
    struct wanp_dhcpv4_handle *wdh = CONTAINER_OF(wh, struct wanp_dhcpv4_handle, wd4_handle);

    wano_inet_state_event_fini(&wdh->wd4_inet_state_watcher);

    free(wh);
}

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */
void wanp_dhcpv4_inet_state_event_fn(
        wano_inet_state_event_t *ise,
        struct wano_inet_state *is)
{
    struct wanp_dhcpv4_handle *wd4 = CONTAINER_OF(ise, struct wanp_dhcpv4_handle, wd4_inet_state_watcher);

    wd4->wd4_ipaddr = is->is_ipaddr;

    if (wanp_dhcpv4_state_do(&wd4->wd4_state, wanp_dhcpv4_do_INET_STATE_UPDATE, is) < 0)
    {
        LOG(ERR, "wanp_dhcpv4: Error sending action INET_STATE_UPDATE to state machine.");
    }
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_dhcpv4_module_start(void)
{
    wano_plugin_register(&wanp_dhcpv4);
}

void wanp_dhcpv4_module_stop(void)
{
    wano_plugin_unregister(&wanp_dhcpv4);
}

MODULE(wanp_dhcpv4, wanp_dhcpv4_module_start, wanp_dhcpv4_module_stop)

