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
#include "osp_ps.h"
#include "osa_assert.h"
#include "json_util.h"

#include "wano_localconfig.h"

#include "wanp_pppoe_stam.h"

struct wanp_pppoe_handle
{
    wano_plugin_handle_t            wpoe_handle;
    wanp_pppoe_state_t              wpoe_state;
    wano_plugin_status_fn_t        *wpoe_status_fn;
    wano_inet_state_event_t         wpoe_inet_state_watcher;
    osn_ip_addr_t                   wpoe_ipaddr;
    char                            wpoe_ppp_ifname[C_IFNAME_LEN];
    struct wano_localconfig_pppoe   wpoe_cred;
    struct wano_plugin_status       wpoe_status;
};

static void wanp_pppoe_module_start(void);
static void wanp_pppoe_module_stop(void);
static wano_plugin_ops_init_fn_t wanp_pppoe_init;
static wano_plugin_ops_run_fn_t wanp_pppoe_run;
static wano_plugin_ops_fini_fn_t wanp_pppoe_fini;
static wano_inet_state_event_fn_t wanp_pppoe_inet_state_event_fn;
static bool wanp_pppoe_get_credentials(struct wano_localconfig_pppoe *cred);

static struct wano_plugin wanp_pppoe = WANO_PLUGIN_INIT(
        "pppoe",
        50,
        WANO_PLUGIN_MASK_IPV4 | WANO_PLUGIN_MASK_IPV6,
        wanp_pppoe_init,
        wanp_pppoe_run,
        wanp_pppoe_fini);

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */
enum wanp_pppoe_state wanp_pppoe_state_INIT(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    (void)action;
    (void)data;

    struct wanp_pppoe_handle *wpoe;

    wpoe = CONTAINER_OF(state, struct wanp_pppoe_handle, wpoe_state);

    switch (action)
    {
        case wanp_pppoe_do_STATE_INIT:
            break;

        case wanp_pppoe_do_INIT:
            LOG(DEBUG, "wanp_pppoe: Reading credentials from persistent storage");
            if (wanp_pppoe_get_credentials(&wpoe->wpoe_cred) == false)
            {
                LOG(NOTICE, "wanp_pppoe: No PPPoE configuration is present. Skipping plug-in.");
                wpoe->wpoe_status_fn(
                        &wpoe->wpoe_handle,
                        &WANO_PLUGIN_STATUS(WANP_SKIP));
                break;
            }
            return wanp_pppoe_ENABLE_PPPOE;

        default:
            break;
    }

    return 0;
}

enum wanp_pppoe_state wanp_pppoe_state_ENABLE_PPPOE(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    struct wanp_pppoe_handle *wpoe;
    struct wano_inet_state *is;

    struct schema_Wifi_Inet_Config inet_config;
    ovsdb_table_t table_Wifi_Inet_Config;

    wpoe = CONTAINER_OF(state, struct wanp_pppoe_handle, wpoe_state);
    is = data;

    switch (action)
    {
        case wanp_pppoe_do_STATE_INIT:
            LOG(INFO, "wanp_pppoe: Enabling PPPOE interface %s with parent if: %s.",
                      wpoe->wpoe_ppp_ifname,
                      wpoe->wpoe_handle.wh_ifname);

            // Insert new pppoe interface in Wifi_Inet_Config table
            OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
            memset(&inet_config, 0, sizeof(inet_config));
            inet_config._partial_update = true;

            SCHEMA_SET_INT(inet_config.enabled, false);
            SCHEMA_SET_INT(inet_config.network, false);
            SCHEMA_SET_STR(inet_config.parent_ifname, wpoe->wpoe_handle.wh_ifname);
            SCHEMA_SET_STR(inet_config.if_type, "pppoe");
            SCHEMA_KEY_VAL_APPEND(inet_config.ppp_options, "username", wpoe->wpoe_cred.username);
            SCHEMA_KEY_VAL_APPEND(inet_config.ppp_options, "password", wpoe->wpoe_cred.password);
            SCHEMA_SET_STR(inet_config.if_name, wpoe->wpoe_ppp_ifname);

            ovsdb_table_upsert_simple(
                    &table_Wifi_Inet_Config,
                    "if_name",
                    wpoe->wpoe_ppp_ifname,
                    &inet_config,
                    false);

            /* Start monitoring Wifi_Inet_State events */
            wano_inet_state_event_init(
                    &wpoe->wpoe_inet_state_watcher,
                    wpoe->wpoe_ppp_ifname,
                    wanp_pppoe_inet_state_event_fn);
            break;

        case wanp_pppoe_do_INET_STATE_UPDATE:
            if (is->is_enabled) break;
            if (is->is_network) break;
            return wanp_pppoe_WAIT_IP;

        default:
            return 0;
    }

    return 0;
}

enum wanp_pppoe_state wanp_pppoe_state_WAIT_IP(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    struct wanp_pppoe_handle *wpoe;
    struct wano_inet_state *is = data;

    wpoe = CONTAINER_OF(state, struct wanp_pppoe_handle, wpoe_state);

    switch (action)
    {
        case wanp_pppoe_do_STATE_INIT:
            LOG(INFO, "wanp_pppoe: %s: Waiting for PPPOE address.", wpoe->wpoe_ppp_ifname);
            if (!WANO_INET_CONFIG_UPDATE(
                    wpoe->wpoe_ppp_ifname,
                    .enabled = WANO_TRI_TRUE,
                    .network = WANO_TRI_TRUE))
            {
                LOG(WARN, "wanp_pppoe: %s: Error re-eanbling PPPoE interface %s.",
                        wpoe->wpoe_handle.wh_ifname,
                        wpoe->wpoe_ppp_ifname);
                return 0;
            }

            break;

        case wanp_pppoe_do_INET_STATE_UPDATE:
            if (!is->is_enabled) break;
            if (!is->is_network) break;
            if (memcmp(&is->is_ipaddr, &OSN_IP_ADDR_INIT, sizeof(OSN_IP_ADDR_INIT)) == 0) break;

            LOG(INFO, "wanp_pppoe: Acquired PPPOE address: "PRI_osn_ip_addr, FMT_osn_ip_addr(wpoe->wpoe_ipaddr));
            // Stop listening to inet state events
            wano_inet_state_event_fini(&wpoe->wpoe_inet_state_watcher);
            return wanp_pppoe_RUNNING;

        default:
            break;
    }

    return 0;
}

enum wanp_pppoe_state wanp_pppoe_state_RUNNING(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    (void)action;
    (void)data;

    struct wanp_pppoe_handle *wpoe = CONTAINER_OF(state, struct wanp_pppoe_handle, wpoe_state);

    if (action == wanp_pppoe_do_STATE_INIT)
    {
        struct wano_plugin_status ws = WANO_PLUGIN_STATUS(WANP_OK);

        STRSCPY(ws.ws_ifname, wpoe->wpoe_ppp_ifname);
        STRSCPY(ws.ws_iftype, "pppoe");

        wpoe->wpoe_status_fn( &wpoe->wpoe_handle, &ws);
    }

    LOG(INFO, "wanp_pppoe: PPPOE_RUNNING: %s", wanp_pppoe_action_str(action));

    return 0;
}

enum wanp_pppoe_state wanp_pppoe_state_DONE(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    (void)action;
    (void)data;
    struct wanp_pppoe_handle *wpoe;

    LOG(INFO, "wanp_pppoe: PPPOE_DONE: %s", wanp_pppoe_action_str(action));

    wpoe = CONTAINER_OF(state, struct wanp_pppoe_handle, wpoe_state);

    if (action == wanp_pppoe_do_STATE_INIT)
    {
        wpoe->wpoe_status_fn(&wpoe->wpoe_handle, &WANO_PLUGIN_STATUS(WANP_OK));
    }

    return 0;
}

enum wanp_pppoe_state wanp_pppoe_state_EXCEPTION(
        wanp_pppoe_state_t *state,
        enum wanp_pppoe_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    LOG(INFO, "wanp_pppoe: PPPOE_EXCEPTION: %s", wanp_pppoe_action_str(action));

    return 0;
}

/*
 * ===========================================================================
 *  Plugin implementation
 * ===========================================================================
 */
wano_plugin_handle_t *wanp_pppoe_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    struct wanp_pppoe_handle *wpoe;

    wpoe = calloc(1, sizeof(struct wanp_pppoe_handle));
    ASSERT(wpoe != NULL, "Error allocating PPPOE object")

    wpoe->wpoe_handle.wh_plugin = wp;
    STRSCPY(wpoe->wpoe_handle.wh_ifname, ifname);
    // Concat name of new interface from "ppp-" and parent interface name
    STRSCPY_WARN(wpoe->wpoe_ppp_ifname, "ppp-");
    STRSCAT(wpoe->wpoe_ppp_ifname, wpoe->wpoe_handle.wh_ifname);
    wpoe->wpoe_status_fn = status_fn;

    return &wpoe->wpoe_handle;
}

void wanp_pppoe_run(wano_plugin_handle_t *wh)
{
    struct wanp_pppoe_handle *wph = CONTAINER_OF(wh, struct wanp_pppoe_handle, wpoe_handle);

    if (wanp_pppoe_state_do(&wph->wpoe_state, wanp_pppoe_do_INIT, NULL) < 0)
    {
        LOG(ERR, "wanp_pppoe: %s: Error initializing state machine.", wph->wpoe_ppp_ifname);
    }

}

void wanp_pppoe_fini(wano_plugin_handle_t *wh)
{
    struct wanp_pppoe_handle *wph = CONTAINER_OF(wh, struct wanp_pppoe_handle, wpoe_handle);

    wano_inet_state_event_fini(&wph->wpoe_inet_state_watcher);

    free(wph);
}

/*
 * ===========================================================================
 *  Misc
 * ===========================================================================
 */
void wanp_pppoe_inet_state_event_fn(
        wano_inet_state_event_t *ise,
        struct wano_inet_state *is)
{
    struct wanp_pppoe_handle *wpoe = CONTAINER_OF(ise, struct wanp_pppoe_handle, wpoe_inet_state_watcher);

    wpoe->wpoe_ipaddr = is->is_ipaddr;

    if (wanp_pppoe_state_do(&wpoe->wpoe_state, wanp_pppoe_do_INET_STATE_UPDATE, is) < 0)
    {
        LOG(ERR, "wanp_pppoe: Error sending action INET_STATE_UPDATE to state machine.");
    }
}

bool wanp_pppoe_get_credentials(struct wano_localconfig_pppoe *cred)
{
    struct wano_localconfig lc;

    if (!wano_localconfig_load(&lc))
    {
        LOG(DEBUG, "wanp_pppoe: No local configuration present.");
        return false;
    }

    if (!lc.PPPoE_exists)
    {
        LOG(DEBUG, "wanp_pppoe: No PPPoE configuration present in local config.");
        return false;
    }

    *cred = lc.PPPoE;

    return true;
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_pppoe_module_start(void)
{
    wano_plugin_register(&wanp_pppoe);
}

void wanp_pppoe_module_stop(void)
{
    wano_plugin_unregister(&wanp_pppoe);
}

MODULE(wanp_pppoe, wanp_pppoe_module_start, wanp_pppoe_module_stop)

